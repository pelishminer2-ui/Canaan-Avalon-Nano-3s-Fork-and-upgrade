/* SPDX-License-Identifier: MIT */
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

#include "boardconf.h"
#include "autotune.h"

/*
 * One step, up or down, by the caller's PLL interval. The caller sends that
 * frequency through the existing V/F path, which programs the A3197S dies
 * with the Canaan PLL sequence. This file does not time that sequence.
 */
void autotune_step(const autotune_in *in, autotune_out *out)
{
    if (!out)
        return;
    out->freq = in ? in->freq : 0;
    out->step = 0;
    if (!in || in->mode != AUTOTUNE_MILD || !in->settled || !in->pll_interval)
        return;

    uint16_t step  = in->pll_interval;
    uint32_t ceil_f = (uint32_t)in->pll_start + (uint32_t)AUTOTUNE_MILD_STEPS * step;
    if (ceil_f > 65535u)
        ceil_f = 65535u;
    uint16_t floor_f = in->pll_start;
    int      hot     = 0;
    int      cool    = 0;

    if (in->temp_known && in->temp_target > 0 && in->temp_fan_max > 0 && in->temp_trip > 0) {
        int backoff = in->temp_target;
        if (in->temp_fan_max < backoff)
            backoff = in->temp_fan_max;
        int trip_guard = in->temp_trip - AUTOTUNE_TEMP_MARGIN_C;
        if (trip_guard < backoff)
            backoff = trip_guard;
        hot  = in->temp_c >= backoff;
        cool = in->temp_c + AUTOTUNE_TEMP_MARGIN_C <= in->temp_target &&
                in->temp_c < in->temp_fan_max &&
                in->temp_c + AUTOTUNE_TEMP_MARGIN_C < in->temp_trip;
    }
    int power_hot = in->power_known && in->pout_cap_w > 0 && in->pout_w >= in->pout_cap_w;
    int power_room = in->power_known && in->pout_cap_w > 0 &&
            in->pout_w + AUTOTUNE_POWER_MARGIN_W <= in->pout_cap_w;

    if ((hot || power_hot) && in->freq > floor_f) {
        uint16_t nf = in->freq > (uint16_t)(floor_f + step) ? (uint16_t)(in->freq - step) : floor_f;
        out->freq   = nf;
        out->step   = -1;
        return;
    }
    if (hot || power_hot || !cool || !power_room)
        return;
    if (in->freq >= floor_f && (uint32_t)in->freq + step <= ceil_f) {
        out->freq = (uint16_t)(in->freq + step);
        out->step = 1;
    }
}
