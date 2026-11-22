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

#ifndef FAN_PID_H
#define FAN_PID_H

#include <stdint.h>

#include "miner_ipc.h"

typedef struct fan_pid_params {
    float p, i, d;            /* proportional, integral and derivative gains */
    int   temp_max, temp_min; /* PID temperature band top and bottom, degC */
    int   fan_min, fan_max;   /* PID output floor and full scale, steps */
    /* largest output increase (0 = unlimited) and decrease (negative) per step, steps */
    int dlimit_up, dlimit_down;
    /* duty below the band; duty above it and during the start hold, percent */
    int duty_init_pct, duty_max_pct;
    int err_quant;      /* 1 truncates the PID error toward zero to 0.5 degC */
    int duty_whole_pct; /* 1 truncates the PID duty to whole percent */
    /* 1 holds duty_max_pct until the first chip temperature arrives */
    int instart_hold;
} fan_pid_params_t;

typedef struct fan_pid_state {
    float    e[3];         /* PID errors, newest first, degC */
    int      fan_val;      /* PID output, steps */
    uint16_t last_target;  /* target temperature the errors belong to, degC */
    int      fixed_duty;   /* fixed duty, percent; -1 automatic */
    int      instart_done; /* first chip temperature seen flag */
} fan_pid_state_t;

uint16_t fan_pid_step(fan_pid_state_t *st, const fan_pid_params_t *pp, float temp,
        uint16_t target_temp, int last_duty_x10, int *duty_x10, int *changed);
void     fan_pid_set_fixed(fan_pid_state_t *st, int duty_pct);
int      fan_pid_pick_input(int tavg, int tmax, const int *outlet, int n);
/* Pull the temperature band back inside [band_min, band_max]. Returns 1 if a
 * limit was outside that band. Gains and thermal trip points are untouched. */
int fan_pid_clamp_band(fan_pid_params_t *pp, int band_min, int band_max);

#endif /* FAN_PID_H */
