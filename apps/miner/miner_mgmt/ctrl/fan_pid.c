// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2026, Canaan Creative Co., Ltd.
 *
 * For the licensing terms, see the LICENSE file (MIT License).
 *
 * NO WARRANTY. The Software is provided "AS IS", without warranty of any kind, express or
 * implied, including but not limited to the warranties of merchantability, fitness for a
 * particular purpose and non-infringement. In no event shall the authors or copyright
 * holders be liable for any claim, damages or other liability arising from, out of or in
 * connection with the Software or the use or other dealings in the Software.
 */

#include <math.h>
#include "fan_pid.h"

#define ASIC_TEMP_DOWNLIMIT 2 /* lowest chip temperature reading taken as valid, degC */

uint16_t fan_pid_step(fan_pid_state_t *st, const fan_pid_params_t *pp, float temp,
        uint16_t target_temp, int last_duty_x10, int *duty_x10, int *changed)
{
    int     fan_duty_x10 = 0;
    int32_t delta_u      = 0;
    float   delta_p = 0.0, delta_d = 0.0, delta_i = 0.0;

    *changed = 0;

    if (st->last_target != target_temp) {
        st->last_target = target_temp;
        st->e[0]        = 0.0;
        st->e[1]        = 0.0;
        st->e[2]        = 0.0;
    }
    if (st->fixed_duty >= 0) {
        st->fan_val = 0;
        *duty_x10   = st->fixed_duty * 10;
        *changed    = 1;
        return (uint16_t)st->fixed_duty;
    }
    if (pp->instart_hold && !st->instart_done) {
        fan_duty_x10 = pp->duty_max_pct * 10;
        if (temp == (float)ASIC_TEMP_NONE)
            st->fan_val = pp->fan_max;
        else
            st->instart_done = 1;
    } else if (temp > pp->temp_max) {
        fan_duty_x10 = pp->duty_max_pct * 10;
        st->fan_val  = pp->fan_max;
    } else if (temp < pp->temp_min) {
        fan_duty_x10 = pp->duty_init_pct * 10;
        st->fan_val  = pp->fan_min;
    } else {
        st->e[2] = st->e[1];
        st->e[1] = st->e[0];
        st->e[0] = pp->err_quant ? (int)((temp - target_temp) * 2) * 0.5f : (temp - target_temp);

        delta_p = pp->p * (st->e[0] - st->e[1]);
        delta_i = pp->i * st->e[0];
        delta_d = pp->d * (st->e[0] - 2 * st->e[1] + st->e[2]);

        delta_u = round(delta_p + delta_i + delta_d);
        delta_u = (delta_u < pp->dlimit_down) ? (pp->dlimit_down) : delta_u;
        if (pp->dlimit_up)
            delta_u = (delta_u > pp->dlimit_up) ? (pp->dlimit_up) : delta_u;
        st->fan_val += delta_u;

        if (st->fan_val > pp->fan_max)
            st->fan_val = pp->fan_max;

        if (st->fan_val < pp->fan_min)
            st->fan_val = pp->fan_min;

        fan_duty_x10 = pp->duty_whole_pct ? ((100 * st->fan_val) / pp->fan_max) * 10 :
                                            (1000 * st->fan_val) / pp->fan_max;
    }

    if (last_duty_x10 != fan_duty_x10)
        *changed = 1;
    *duty_x10 = fan_duty_x10;
    return fan_duty_x10 / 10;
}

void fan_pid_set_fixed(fan_pid_state_t *st, int duty_pct)
{
    st->e[0]       = 0.0;
    st->e[1]       = 0.0;
    st->e[2]       = 0.0;
    st->fixed_duty = duty_pct;
}

int fan_pid_clamp_band(fan_pid_params_t *pp, int band_min, int band_max)
{
    int clamped = 0;

    if (!pp)
        return 0;
    if (band_min > band_max) {
        int swap = band_min;
        band_min = band_max;
        band_max = swap;
    }
    if (pp->temp_min < band_min) {
        pp->temp_min = band_min;
        clamped      = 1;
    }
    if (pp->temp_max > band_max) {
        pp->temp_max = band_max;
        clamped      = 1;
    }
    if (pp->temp_min > pp->temp_max) {
        pp->temp_min = band_min;
        pp->temp_max = band_max;
        clamped      = 1;
    }
    return clamped;
}

int fan_pid_pick_input(int tavg, int tmax, const int *outlet, int n)
{
    int max = ASIC_TEMP_NONE;

    if (n <= 0)
        return tavg >= ASIC_TEMP_DOWNLIMIT ? tavg : ASIC_TEMP_NONE;
    if (tmax >= ASIC_TEMP_DOWNLIMIT)
        return tmax;
    if (tavg >= ASIC_TEMP_DOWNLIMIT)
        return tavg;
    for (int i = 0; i < n; i++) {
        if (outlet[i] == ASIC_TEMP_NONE)
            continue;
        if (outlet[i] > max)
            max = outlet[i];
    }
    return max;
}
