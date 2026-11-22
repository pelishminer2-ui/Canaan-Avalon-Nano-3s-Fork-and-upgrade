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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fan_pid.h"
#include "boardconf.h"

#if defined(BOARD_X3_H)
/*
 * Expected x3 descriptor fan PID values: gains, band in degC, initial duty in
 * percent, up-step limit in steps, then the err_quant/duty_whole_pct/instart_hold flags.
 */
#define BOARD_NAME     "x3"
#define WANT_P         50
#define WANT_I         2
#define WANT_D         1
#define WANT_TEMP_MIN  45
#define WANT_TEMP_MAX  95
#define WANT_DUTY_INIT 30
#define WANT_DELTA_UP  30
#define WANT_ERR_QUANT 1
#define WANT_WHOLE_PCT 1
#define WANT_INSTART   1
#elif defined(BOARD_NANO3S_H)
/*
 * Expected nano3s descriptor fan PID values: gains, band in degC, initial duty in
 * percent, up-step limit in steps, then the err_quant/duty_whole_pct/instart_hold flags.
 */
#define BOARD_NAME     "nano3s"
#define WANT_P         35
#define WANT_I         1
#define WANT_D         1
#define WANT_TEMP_MIN  50
#define WANT_TEMP_MAX  100
#define WANT_DUTY_INIT 25
#define WANT_DELTA_UP  50
#define WANT_ERR_QUANT 1
#define WANT_WHOLE_PCT 0
#define WANT_INSTART   0
#else
#error "no fan PID goldens for this board"
#endif

#define RIG2_N ((int)(sizeof(RIG2) / sizeof(RIG2[0])))

/* PID golden step: input degC; expected duty percent, duty 0.1 %, changed flag */
typedef struct {
    float t;
    int   duty_pct, duty_x10, changed;
} vec_t;

/* Fan curve sample: elapsed time, s; outlet temperature per board, degC */
struct rig_sample {
    short el;
    short o[3];
};
/*
 * Fan curve sample: elapsed time, s; highest chip temperature, degC; outlet
 * temperature per board, degC; fan duty, percent.
 */
struct rig2_sample {
    short el, tmax;
    short o[3];
    short fanr;
};

static int tst_duty_x10 = -1; /* last applied duty, 0.1 %; -1 none */

/* nano3s fan PID parameters as literals */
static const fan_pid_params_t NANO3S = {
    .p              = 35,
    .i              = 1,
    .d              = 1,
    .temp_max       = 100,
    .temp_min       = 50,
    .fan_min        = 154,
    .fan_max        = 0x3FF,
    .dlimit_up      = 50,
    .dlimit_down    = -100,
    .duty_init_pct  = 25,
    .duty_max_pct   = 100,
    .err_quant      = 1,
    .duty_whole_pct = 0,
    .instart_hold   = 0,
};

/* Fan PID parameters built from this build's board descriptor */
static const fan_pid_params_t FROM_DESC = {
    .p              = FAN_PID_P,
    .i              = FAN_PID_I,
    .d              = FAN_PID_D,
    .temp_max       = FAN_PID_TEMP_MAX,
    .temp_min       = FAN_PID_TEMP_MIN,
    .fan_min        = 154,
    .fan_max        = 0x3FF,
    .dlimit_up      = FAN_PID_DELTA_UP,
    .dlimit_down    = -100,
    .duty_init_pct  = FAN_DUTY_INIT_PCT,
    .duty_max_pct   = FAN_DUTY_MAX_PCT,
    .err_quant      = FAN_PID_ERR_QUANT,
    .duty_whole_pct = FAN_PID_DUTY_WHOLE_PCT,
    .instart_hold   = FAN_PID_INSTART_HOLD,
};

static const fan_pid_params_t g_bench = {
    .p              = 86,
    .i              = 2.5,
    .d              = 0,
    .temp_max       = 100,
    .temp_min       = 40,
    .fan_min        = 154,
    .fan_max        = 0x3FF,
    .dlimit_up      = 0,
    .dlimit_down    = -100,
    .duty_init_pct  = 15,
    .duty_max_pct   = 100,
    .err_quant      = 0,
    .duty_whole_pct = 1,
    .instart_hold   = 1,
};

/* Bench fan curve sample */
static const struct rig_sample RIG[] = {
    { 53, { 26, 23, 22 } },
    { 58, { 25, 23, 22 } },
    { 63, { 25, 23, 22 } },
    { 68, { 26, 24, 23 } },
    { 73, { 27, 24, 23 } },
    { 79, { 29, 25, 24 } },
    { 84, { 30, 25, 24 } },
    { 89, { 31, 26, 25 } },
    { 94, { 33, 26, 25 } },
    { 99, { 34, 27, 26 } },
    { 104, { 36, 27, 26 } },
    { 109, { 37, 28, 27 } },
    { 114, { 39, 28, 27 } },
    { 119, { 41, 29, 28 } },
    { 124, { 42, 29, 28 } },
    { 129, { 44, 30, 29 } },
    { 134, { 46, 30, 29 } },
    { 140, { 47, 31, 30 } },
    { 146, { 49, 31, 30 } },
    { 151, { 51, 32, 31 } },
    { 157, { 53, 32, 31 } },
    { 162, { 54, 32, 32 } },
    { 167, { 56, 33, 32 } },
    { 172, { 58, 33, 33 } },
    { 177, { 60, 34, 33 } },
    { 182, { 61, 34, 33 } },
    { 187, { 63, 34, 34 } },
    { 192, { 64, 35, 34 } },
    { 197, { 66, 35, 34 } },
    { 202, { 67, 35, 35 } },
    { 207, { 69, 36, 35 } },
    { 212, { 70, 36, 35 } },
    { 218, { 68, 34, 34 } },
    { 223, { 66, 33, 33 } },
    { 228, { 64, 32, 32 } },
    { 233, { 60, 31, 30 } },
    { 238, { 58, 30, 29 } },
    { 243, { 55, 29, 28 } },
    { 248, { 52, 28, 27 } },
    { 253, { 50, 27, 26 } },
    { 258, { 48, 26, 25 } },
    { 263, { 46, 26, 25 } },
    { 268, { 45, 25, 24 } },
    { 273, { 43, 25, 24 } },
    { 279, { 42, 24, 24 } },
    { 284, { 40, 24, 23 } },
    { 289, { 39, 24, 23 } },
    { 294, { 38, 24, 23 } },
    { 299, { 37, 23, 23 } },
    { 304, { 36, 23, 22 } },
    { 309, { 36, 23, 22 } },
    { 314, { 35, 23, 22 } },
    { 319, { 35, 23, 22 } },
    { 324, { 34, 22, 22 } },
    { 329, { 33, 22, 21 } },
    { 334, { 33, 22, 21 } },
    { 340, { 32, 22, 21 } },
    { 345, { 32, 22, 21 } },
    { 350, { 32, 22, 21 } },
    { 355, { 31, 22, 21 } },
    { 360, { 31, 22, 21 } },
    { 365, { 31, 22, 21 } },
    { 370, { 31, 21, 20 } },
    { 375, { 31, 21, 20 } },
    { 380, { 30, 21, 20 } },
    { 385, { 30, 21, 20 } },
    { 390, { 30, 21, 20 } },
    { 395, { 30, 21, 20 } },
    { 401, { 30, 21, 20 } },
    { 406, { 30, 21, 20 } },
    { 411, { 30, 21, 20 } },
    { 416, { 30, 21, 20 } },
    { 421, { 30, 21, 20 } },
    { 426, { 29, 21, 20 } },
    { 431, { 29, 21, 20 } },
    { 436, { 29, 21, 20 } },
    { 441, { 29, 21, 20 } },
    { 446, { 29, 21, 20 } },
};

/* Second bench fan curve sample */
static const struct rig2_sample RIG2[] = {
    { 53, 17, { 19, 19, 19 }, 15 },
    { 58, 24, { 20, 19, 19 }, 15 },
    { 63, 24, { 21, 20, 20 }, 15 },
    { 68, 27, { 22, 20, 20 }, 15 },
    { 73, 28, { 23, 21, 21 }, 15 },
    { 78, 31, { 25, 21, 21 }, 15 },
    { 83, 38, { 26, 22, 22 }, 15 },
    { 88, 38, { 28, 22, 22 }, 15 },
    { 93, 40, { 30, 23, 23 }, 15 },
    { 98, 43, { 31, 23, 23 }, 15 },
    { 103, 46, { 32, 24, 24 }, 15 },
    { 108, 46, { 34, 24, 24 }, 15 },
    { 114, 48, { 36, 25, 25 }, 15 },
    { 119, 52, { 38, 25, 26 }, 15 },
    { 124, 52, { 39, 26, 26 }, 15 },
    { 129, 54, { 41, 26, 27 }, 15 },
    { 134, 57, { 43, 27, 27 }, 15 },
    { 139, 57, { 45, 27, 28 }, 19 },
    { 144, 60, { 46, 28, 28 }, 17 },
    { 149, 60, { 48, 28, 29 }, 18 },
    { 154, 62, { 50, 29, 29 }, 24 },
    { 159, 63, { 51, 29, 30 }, 19 },
    { 164, 63, { 53, 29, 30 }, 27 },
    { 169, 65, { 55, 30, 30 }, 32 },
    { 174, 65, { 56, 30, 31 }, 25 },
    { 179, 65, { 56, 30, 31 }, 26 },
    { 185, 65, { 57, 30, 31 }, 25 },
    { 190, 65, { 58, 30, 31 }, 28 },
    { 195, 65, { 59, 30, 31 }, 28 },
    { 200, 65, { 60, 31, 31 }, 31 },
    { 205, 66, { 61, 31, 31 }, 32 },
    { 210, 66, { 61, 31, 32 }, 28 },
    { 215, 70, { 62, 31, 32 }, 30 },
    { 220, 70, { 62, 31, 32 }, 26 },
    { 225, 70, { 63, 31, 32 }, 31 },
    { 230, 71, { 63, 31, 32 }, 25 },
    { 235, 71, { 64, 31, 32 }, 30 },
    { 240, 71, { 64, 31, 32 }, 26 },
    { 245, 71, { 65, 31, 32 }, 32 },
    { 251, 71, { 65, 31, 32 }, 28 },
    { 256, 71, { 65, 31, 32 }, 26 },
    { 261, 71, { 66, 31, 32 }, 32 },
    { 266, 71, { 66, 31, 32 }, 29 },
    { 271, 71, { 66, 31, 32 }, 27 },
    { 276, 71, { 66, 31, 32 }, 24 },
    { 281, 71, { 67, 31, 32 }, 31 },
    { 286, 71, { 67, 31, 32 }, 28 },
    { 291, 71, { 67, 31, 32 }, 27 },
    { 296, 71, { 67, 31, 32 }, 25 },
    { 301, 71, { 68, 31, 32 }, 32 },
    { 306, 74, { 68, 31, 32 }, 31 },
    { 311, 74, { 68, 31, 32 }, 30 },
    { 316, 74, { 68, 30, 31 }, 29 },
    { 321, 74, { 68, 30, 31 }, 27 },
    { 327, 76, { 68, 30, 31 }, 26 },
    { 332, 76, { 68, 30, 31 }, 25 },
    { 337, 76, { 68, 30, 31 }, 24 },
    { 342, 76, { 69, 30, 31 }, 32 },
    { 347, 76, { 69, 30, 31 }, 31 },
    { 352, 76, { 69, 30, 31 }, 30 },
    { 357, 76, { 69, 30, 31 }, 29 },
    { 362, 76, { 69, 30, 31 }, 29 },
    { 367, 76, { 69, 30, 31 }, 28 },
    { 372, 76, { 69, 30, 31 }, 27 },
    { 377, 76, { 69, 30, 31 }, 27 },
    { 382, 76, { 69, 30, 31 }, 26 },
    { 387, 76, { 69, 30, 31 }, 25 },
    { 392, 76, { 70, 30, 31 }, 33 },
    { 398, 76, { 70, 30, 31 }, 33 },
    { 403, 77, { 70, 30, 31 }, 33 },
};

static uint16_t step_and_actuate(fan_pid_state_t *st, const fan_pid_params_t *pp, float temp,
        uint16_t target_temp, int *duty_x10, int *changed)
{
    uint16_t pct = fan_pid_step(st, pp, temp, target_temp, tst_duty_x10, duty_x10, changed);
    if (*changed)
        tst_duty_x10 = *duty_x10;
    return pct;
}
#define fan_pid_step step_and_actuate /* PID step that also applies the duty */

static void check_descriptor(void)
{
    printf("board: %s (P/I/D %g/%g/%g, band %d..%d)\n", BOARD_NAME, (double)FROM_DESC.p,
            (double)FROM_DESC.i, (double)FROM_DESC.d, FROM_DESC.temp_min, FROM_DESC.temp_max);
    if (FROM_DESC.p != (float)WANT_P || FROM_DESC.i != (float)WANT_I ||
            FROM_DESC.d != (float)WANT_D)
        fprintf(stderr, "  FAIL: %s gains %g/%g/%g, want %g/%g/%g\n", BOARD_NAME,
                (double)FROM_DESC.p, (double)FROM_DESC.i, (double)FROM_DESC.d, (double)WANT_P,
                (double)WANT_I, (double)WANT_D);
    assert(FROM_DESC.p == (float)WANT_P);
    assert(FROM_DESC.i == (float)WANT_I);
    assert(FROM_DESC.d == (float)WANT_D);
    if (FROM_DESC.temp_min != WANT_TEMP_MIN || FROM_DESC.temp_max != WANT_TEMP_MAX)
        fprintf(stderr, "  FAIL: %s band %d..%d, want %d..%d\n", BOARD_NAME, FROM_DESC.temp_min,
                FROM_DESC.temp_max, WANT_TEMP_MIN, WANT_TEMP_MAX);
    assert(FROM_DESC.temp_min == WANT_TEMP_MIN);
    assert(FROM_DESC.temp_max == WANT_TEMP_MAX);
    if (FROM_DESC.duty_init_pct != WANT_DUTY_INIT)
        fprintf(stderr, "  FAIL: %s below-band duty %d%%, want %d%%\n", BOARD_NAME,
                FROM_DESC.duty_init_pct, WANT_DUTY_INIT);
    assert(FROM_DESC.duty_init_pct == WANT_DUTY_INIT);
    if (FROM_DESC.dlimit_up != WANT_DELTA_UP || FROM_DESC.err_quant != WANT_ERR_QUANT ||
            FROM_DESC.duty_whole_pct != WANT_WHOLE_PCT || FROM_DESC.instart_hold != WANT_INSTART)
        fprintf(stderr,
                "  FAIL: %s shape up=%d quant=%d whole=%d instart=%d, "
                "want %d/%d/%d/%d\n",
                BOARD_NAME, FROM_DESC.dlimit_up, FROM_DESC.err_quant, FROM_DESC.duty_whole_pct,
                FROM_DESC.instart_hold, WANT_DELTA_UP, WANT_ERR_QUANT, WANT_WHOLE_PCT,
                WANT_INSTART);
    assert(FROM_DESC.dlimit_up == WANT_DELTA_UP);
    assert(FROM_DESC.err_quant == WANT_ERR_QUANT);
    assert(FROM_DESC.duty_whole_pct == WANT_WHOLE_PCT);
    assert(FROM_DESC.instart_hold == WANT_INSTART);
}

static void goldens(const fan_pid_params_t *pp, const vec_t *v, unsigned n, int want_fan_val,
        const char *what)
{
    fan_pid_state_t st;
    memset(&st, 0, sizeof(st));
    tst_duty_x10  = -1;
    st.fixed_duty = -1;
    for (unsigned k = 0; k < n; k++) {
        int      x10 = -1, ch = -1;
        uint16_t pct = fan_pid_step(&st, pp, v[k].t, 70, &x10, &ch);
        if (pct != (uint16_t)v[k].duty_pct || x10 != v[k].duty_x10 || ch != v[k].changed)
            fprintf(stderr,
                    "  FAIL: %s step %u (t=%g): pct=%u x10=%d ch=%d, "
                    "want %d/%d/%d\n",
                    what, k, (double)v[k].t, pct, x10, ch, v[k].duty_pct, v[k].duty_x10,
                    v[k].changed);
        assert(pct == (uint16_t)v[k].duty_pct);
        assert(x10 == v[k].duty_x10);
        assert(ch == v[k].changed);
    }
    assert(st.fan_val == want_fan_val);
}

static fan_pid_params_t shape_of(int quant, int whole, int dup)
{
    fan_pid_params_t pp = {
        .p              = 86,
        .i              = 2.5,
        .d              = 0,
        .temp_max       = 100,
        .temp_min       = 40,
        .fan_min        = 154,
        .fan_max        = 0x3FF,
        .dlimit_up      = dup,
        .dlimit_down    = -100,
        .duty_init_pct  = 15,
        .duty_max_pct   = 100,
        .err_quant      = quant,
        .duty_whole_pct = whole,
        .instart_hold   = 0,
    };
    return pp;
}

static void step_from(fan_pid_params_t pp, int fan_val, float temp, int *pct, int *x10,
        int *out_fan_val)
{
    fan_pid_state_t st;
    int             ch;
    memset(&st, 0, sizeof(st));
    st.fixed_duty = -1;
    st.fan_val    = fan_val;
    tst_duty_x10  = -1;
    *pct          = fan_pid_step(&st, &pp, temp, 70, x10, &ch);
    *out_fan_val  = st.fan_val;
}

static void shape_switches(void)
{
    int pct, x10, fv;

    step_from(shape_of(0, 1, 0), 500, 72, &pct, &x10, &fv);
    assert(fv == 677 && pct == 66 && x10 == 660);
    step_from(shape_of(1, 0, 50), 500, 72, &pct, &x10, &fv);
    assert(fv == 550 && pct == 53 && x10 == 537);

    step_from(shape_of(0, 0, 0), 500, 72, &pct, &x10, &fv);
    assert(fv == 677 && x10 == 661);

    step_from(shape_of(0, 1, 0), 500, 70.3f, &pct, &x10, &fv);
    assert(fv == 527 && pct == 51);
    step_from(shape_of(1, 1, 0), 500, 70.3f, &pct, &x10, &fv);
    assert(fv == 500 && pct == 48);
}

static void instart_arm(void)
{
    fan_pid_params_t on  = shape_of(0, 1, 0);
    fan_pid_params_t off = shape_of(0, 1, 0);
    fan_pid_state_t  st;
    int              x10, ch;
    on.instart_hold = 1;

    memset(&st, 0, sizeof(st));
    tst_duty_x10  = -1;
    st.fixed_duty = -1;
    assert(fan_pid_step(&st, &on, ASIC_TEMP_NONE, 70, &x10, &ch) == 100);
    assert(x10 == 1000 && ch == 1 && st.fan_val == 0x3FF && !st.instart_done);
    assert(fan_pid_step(&st, &on, 44, 70, &x10, &ch) == 100);
    assert(x10 == 1000 && ch == 0 && st.fan_val == 0x3FF && st.instart_done);
    assert(fan_pid_step(&st, &on, 44, 70, &x10, &ch) == 90);
    assert(st.fan_val == 0x3FF - 100);
    assert(fan_pid_step(&st, &on, ASIC_TEMP_NONE, 70, &x10, &ch) == 15);

    memset(&st, 0, sizeof(st));
    tst_duty_x10  = -1;
    st.fixed_duty = -1;
    assert(fan_pid_step(&st, &off, ASIC_TEMP_NONE, 70, &x10, &ch) == 15);
    assert(st.fan_val == 154);

    memset(&st, 0, sizeof(st));
    tst_duty_x10  = -1;
    st.fixed_duty = -1;
    assert(fan_pid_step(&st, &on, 44, 70, &x10, &ch) == 100);
    assert(x10 == 1000 && ch == 1 && st.instart_done && st.fan_val == 0);
    assert(fan_pid_step(&st, &on, 44, 70, &x10, &ch) == 15);
    assert(st.fan_val == 154);
}

static void input_chain(void)
{
    static const int ok[3]   = { 40, 41, 42 };
    static const int one[3]  = { -273, 42, 44 };
    static const int dead[3] = { -273, -273, -273 };
    static const int rig[3]  = { 53, 32, 31 };

    assert(fan_pid_pick_input(50, 91, ok, 3) == 91);
    assert(fan_pid_pick_input(-273, 2, ok, 3) == 2);
    assert(fan_pid_pick_input(65, -273, ok, 3) == 65);
    assert(fan_pid_pick_input(65, 1, ok, 3) == 65);
    assert(fan_pid_pick_input(-273, -273, ok, 3) == 42);
    assert(fan_pid_pick_input(-273, 1, ok, 3) == 42);
    assert(fan_pid_pick_input(1, -273, ok, 3) == 42);
    assert(fan_pid_pick_input(-273, -273, one, 3) == 44);
    assert(fan_pid_pick_input(-273, -273, rig, 3) != 38);
    assert(fan_pid_pick_input(-273, -273, rig, 3) == 53);
    assert(fan_pid_pick_input(-273, -273, rig, 3) >= 40);
    assert(fan_pid_pick_input(-273, 76, (const int[]){ 70, 30, 31 }, 3) != 70);
    assert(fan_pid_pick_input(-273, 76, (const int[]){ 70, 30, 31 }, 3) == 76);
    assert(fan_pid_pick_input(-273, -273, dead, 3) == -273);
    assert(fan_pid_pick_input(-273, -273, ok, 0) == -273);
    assert(fan_pid_pick_input(45, 91, ok, 0) == 45);
    assert(fan_pid_pick_input(-273, 91, ok, 0) == -273);
    assert(fan_pid_pick_input(-273, 91, dead, 0) == -273);
}

static int outlet_mean(int tavg, const int *outlet, int n)
{
    int sum = 0, cnt = 0;
    for (int i = 0; i < n; i++) {
        if (outlet[i] == ASIC_TEMP_NONE)
            continue;
        sum += outlet[i];
        cnt++;
    }
    if (tavg >= 2)
        return tavg;
    return cnt ? sum / cnt : ASIC_TEMP_NONE;
}

static void rig_replay(int use_max, int upto_s, int *duty, int *peak_pct, int *left_init_s)
{
    static const int handover[3] = { 26, 23, 22 };
    fan_pid_state_t  st;
    int              x10 = 0, ch = 0, in0;

    memset(&st, 0, sizeof(st));
    tst_duty_x10  = -1;
    st.fixed_duty = -1;
    *peak_pct     = 0;
    *left_init_s  = -1;

    in0 = use_max ? fan_pid_pick_input(ASIC_TEMP_NONE, ASIC_TEMP_NONE, handover, 3) :
                    outlet_mean(ASIC_TEMP_NONE, handover, 3);
    assert(fan_pid_step(&st, &g_bench, (float)in0, 70, &x10, &ch) == 100);
    assert(st.instart_done && st.fan_val == 0);

    for (unsigned k = 0; k < sizeof(RIG) / sizeof(RIG[0]); k++) {
        int o[3] = { RIG[k].o[0], RIG[k].o[1], RIG[k].o[2] };
        int in, pct;
        in      = use_max ? fan_pid_pick_input(ASIC_TEMP_NONE, ASIC_TEMP_NONE, o, 3) :
                            outlet_mean(ASIC_TEMP_NONE, o, 3);
        pct     = fan_pid_step(&st, &g_bench, (float)in, 70, &x10, &ch);
        duty[k] = pct;
        if (RIG[k].el > upto_s)
            continue;
        if (pct > *peak_pct)
            *peak_pct = pct;
        if (*left_init_s < 0 && pct > 15)
            *left_init_s = RIG[k].el;
    }
}

static void rig_bringup_replay(void)
{
    int duty[sizeof(RIG) / sizeof(RIG[0])];
    int peak, left;

    rig_replay(0, 202, duty, &peak, &left);
    printf("replay mean 49..202s: peak %d%%, left 15%% at %ds\n", peak, left);
    assert(peak == 21);
    assert(left == 172);

    rig_replay(1, 202, duty, &peak, &left);
    printf("replay max  49..202s: peak %d%%, left 15%% at %ds\n", peak, left);
    assert(peak == 100);
    assert(left == 124);

    static const struct {
        short el, pct;
    } want[] = {
        { 119, 15 },
        { 124, 16 },
        { 129, 27 },
        { 134, 38 },
        { 140, 40 },
        { 146, 52 },
        { 151, 64 },
        { 157, 77 },
        { 162, 82 },
        { 167, 95 },
        { 172, 100 },
        { 202, 100 },
    };
    for (unsigned k = 0, j = 0; k < sizeof(want) / sizeof(want[0]); k++) {
        while (RIG[j].el != want[k].el)
            j++;
        if (duty[j] != want[k].pct)
            fprintf(stderr, "  FAIL: replay max at %ds: %d%%, want %d%%\n", want[k].el, duty[j],
                    want[k].pct);
        assert(duty[j] == want[k].pct);
    }

    rig_replay(0, 450, duty, &peak, &left);
    assert(peak == 21);
    rig_replay(1, 450, duty, &peak, &left);
    assert(peak == 100);
}

static void rig2_replay(int use_tmax, int *duty)
{
    fan_pid_state_t st;
    int             x10 = 0, ch = 0, k = 0;

    memset(&st, 0, sizeof(st));
    tst_duty_x10  = -1;
    st.fixed_duty = -1;

    for (int t = RIG2[0].el; t <= RIG2[RIG2_N - 1].el; t += 2) {
        int o[3], in, pct;
        while (k + 1 < RIG2_N && RIG2[k + 1].el <= t)
            k++;
        o[0] = RIG2[k].o[0];
        o[1] = RIG2[k].o[1];
        o[2] = RIG2[k].o[2];
        in   = fan_pid_pick_input(ASIC_TEMP_NONE, use_tmax ? RIG2[k].tmax : ASIC_TEMP_NONE, o, 3);
        pct  = fan_pid_step(&st, &g_bench, (float)in, 70, &x10, &ch);
        duty[k] = pct;
    }
}

static void rig2_window(const int *duty, int lo, int hi, int *dmin, int *dmax)
{
    *dmin = 1000;
    *dmax = -1;
    for (int k = 0; k < RIG2_N; k++) {
        if (RIG2[k].el < lo || RIG2[k].el > hi)
            continue;
        if (duty[k] < *dmin)
            *dmin = duty[k];
        if (duty[k] > *dmax)
            *dmax = duty[k];
    }
}

static void rig2_bringup_replay(void)
{
    int duty[RIG2_N], dmin, dmax;

    for (int k = 1; k < RIG2_N; k++)
        assert(RIG2[k].tmax >= RIG2[k - 1].tmax);

    rig2_replay(0, duty);
    rig2_window(duty, 256, 392, &dmin, &dmax);
    printf("replay run2 outlet 256..392s: duty %d..%d%%\n", dmin, dmax);
    assert(dmin >= 19 && dmin <= 29);
    assert(dmax >= 28 && dmax <= 38);
    assert(duty[RIG2_N - 1] < 40);

    assert(RIG2[RIG2_N - 1].o[0] == 70);
    assert(RIG2[RIG2_N - 1].tmax == 77);

    rig2_replay(1, duty);
    rig2_window(duty, 256, 392, &dmin, &dmax);
    printf("replay run2 tmax   256..392s: duty %d..%d%%\n", dmin, dmax);
    assert(dmin == 100 && dmax == 100);

    assert(RIG2[8].el == 93 && RIG2[8].tmax == 40);
    assert(RIG2[15].el == 129 && RIG2[15].o[0] == 41);
    assert(RIG2[15].tmax == 54);
    rig2_window(duty, 53, 93, &dmin, &dmax);
    assert(dmin == 15);
    rig2_window(duty, 98, 403, &dmin, &dmax);
    assert(dmin > 15);

    int g1_outlet = 0, g1_tmax = 0;
    rig2_replay(0, duty);
    for (int k = 0; k < RIG2_N; k++) {
        int om = RIG2[k].o[0] > RIG2[k].o[1] ? RIG2[k].o[0] : RIG2[k].o[1];
        if (om < RIG2[k].o[2])
            om = RIG2[k].o[2];
        if (duty[k] <= 15 && om >= 40)
            g1_outlet++;
    }
    rig2_replay(1, duty);
    for (int k = 0; k < RIG2_N; k++) {
        int om = RIG2[k].o[0] > RIG2[k].o[1] ? RIG2[k].o[0] : RIG2[k].o[1];
        if (om < RIG2[k].o[2])
            om = RIG2[k].o[2];
        if (duty[k] <= 15 && om >= 40)
            g1_tmax++;
    }
    printf("replay run2 G1 rows: outlet %d, tmax %d\n", g1_outlet, g1_tmax);
    assert(g1_outlet > 0);
    assert(g1_tmax == 0);
}

int main(void)
{
    fan_pid_state_t st;
    memset(&st, 0, sizeof(st));
    tst_duty_x10  = -1;
    st.fixed_duty = -1;

    check_descriptor();

    static const vec_t v[] = {
        { 75, 15, 150, 1 },
        { 76, 18, 186, 1 },
        { 74, 15, 150, 1 },
        { 72, 15, 150, 0 },
        { 71, 15, 150, 0 },
        { 70, 15, 150, 0 },
        { 69, 15, 150, 0 },
        { 68, 15, 150, 0 },
        { 50, 15, 150, 0 },
        { 49, 25, 250, 1 },
        { 101, 100, 1000, 1 },
    };
    for (unsigned k = 0; k < sizeof(v) / sizeof(v[0]); k++) {
        int      x10 = -1, ch = -1;
        uint16_t pct = fan_pid_step(&st, &NANO3S, v[k].t, 70, &x10, &ch);
        assert(pct == (uint16_t)v[k].duty_pct);
        assert(x10 == v[k].duty_x10);
        assert(ch == v[k].changed);
    }
    assert(st.fan_val == 0x3FF);

#if defined(BOARD_X3_H)
    static const vec_t vd[] = {
        { -273, 100, 1000, 1 },
        { -273, 100, 1000, 0 },
        { 50, 100, 1000, 0 },
        { 50, 90, 900, 1 },
        { 50, 88, 880, 1 },
        { 80, 91, 910, 1 },
        { 96, 100, 1000, 1 },
        { 44, 30, 300, 1 },
    };
#elif defined(BOARD_NANO3S_H)
    static const vec_t vd[] = {
        { 75, 15, 150, 1 },
        { 76, 18, 186, 1 },
        { 74, 15, 150, 1 },
        { 72, 15, 150, 0 },
        { 71, 15, 150, 0 },
        { 70, 15, 150, 0 },
        { 69, 15, 150, 0 },
        { 68, 15, 150, 0 },
        { WANT_TEMP_MIN, 15, 150, 0 },
        { 101, 100, 1000, 1 },
        { WANT_TEMP_MIN - 1, WANT_DUTY_INIT, WANT_DUTY_INIT * 10, 1 },
    };
#else
#error "no fan PID duty goldens for this board"
#endif
    goldens(&FROM_DESC, vd, sizeof(vd) / sizeof(vd[0]), 154, BOARD_NAME " descriptor");

    int x10, ch;
    fan_pid_step(&st, &NANO3S, 75, 65, &x10, &ch);
    assert(st.last_target == 65 && st.e[1] == 0.0f && st.e[2] == 0.0f);

    fan_pid_set_fixed(&st, 40);
    uint16_t pct = fan_pid_step(&st, &NANO3S, 90, 65, &x10, &ch);
    assert(pct == 40 && x10 == 400 && ch == 1 && st.fan_val == 0);
    pct = fan_pid_step(&st, &NANO3S, 90, 65, &x10, &ch);
    assert(pct == 40 && ch == 1);

    fan_pid_set_fixed(&st, -1);
    pct = fan_pid_step(&st, &NANO3S, 90, 65, &x10, &ch);
    assert(pct == 15 && x10 == 150 && ch == 1);

    shape_switches();
    instart_arm();
    input_chain();
    rig_bringup_replay();
    rig2_bringup_replay();

    fan_pid_params_t band = { .temp_min = FAN_PID_TEMP_MIN - 10, .temp_max = FAN_PID_TEMP_MAX + 20,
        .p = FAN_PID_P };
    assert(fan_pid_clamp_band(&band, FAN_PID_TEMP_MIN, FAN_PID_TEMP_MAX) == 1);
    assert(band.temp_min == FAN_PID_TEMP_MIN && band.temp_max == FAN_PID_TEMP_MAX);
    assert(band.p == FAN_PID_P);
    assert(fan_pid_clamp_band(&band, FAN_PID_TEMP_MIN, FAN_PID_TEMP_MAX) == 0);
#if defined(BOARD_NANO3S_H)
    assert(PVT_TEMP_OVER_HR_MAX == 113);
    assert(HASHOUT_OVER_HR_MAX == 95);
    assert(FAN_PID_TEMP_MAX == 100);
    assert(FAN_PID_P_MILD > FAN_PID_P);
    band.temp_max = PVT_TEMP_OVER_HR_MAX + 5;
    assert(fan_pid_clamp_band(&band, FAN_PID_TEMP_MIN, FAN_PID_TEMP_MAX) == 1);
    assert(band.temp_max == FAN_PID_TEMP_MAX);
#endif

    printf("fan-pid rule goldens OK (%s)\n", BOARD_NAME);
    return 0;
}
