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

#ifndef AUTOTUNE_H
#define AUTOTUNE_H

#include <stdint.h>

/*
 * Nano 3s board tune. One core rail, one hashboard: every A3197S die is
 * already programmed together by asic_miner. Multi-chip here means those
 * A3197S dies, not a BM1370 chain. A step is one Canaan PLL interval from
 * the aged or factory cali row. The PLL sequence timing is not part of this
 * decision, and no foreign V/F table is used.
 */
#ifndef AUTOTUNE_OFF
#define AUTOTUNE_OFF 0
#endif
#ifndef AUTOTUNE_MILD
#define AUTOTUNE_MILD 1
#endif
#ifndef AUTOTUNE_TEMP_MARGIN_C
#define AUTOTUNE_TEMP_MARGIN_C 6
#endif
#ifndef AUTOTUNE_POWER_MARGIN_W
#define AUTOTUNE_POWER_MARGIN_W 5
#endif
#ifndef AUTOTUNE_MILD_STEPS
#define AUTOTUNE_MILD_STEPS 2
#endif

typedef struct autotune_in {
    int      mode;          /* AUTOTUNE_OFF or AUTOTUNE_MILD */
    int      settled;       /* 1 after the mode ramp and a cali or aging result */
    uint16_t pll_start;     /* aged or factory start frequency, MHz */
    uint16_t pll_interval;  /* Canaan cali PLL interval, MHz; 0 holds */
    uint16_t freq;          /* frequency the chips are at, MHz */
    int      temp_known;    /* 1 when temp_c is a real chip reading */
    int      temp_c;        /* hottest chip, degC */
    int      temp_target;   /* cali fan target, degC */
    int      temp_fan_max;  /* stock fan-band top, degC */
    int      temp_trip;     /* stock chip thermal trip, degC; not raised */
    int      power_known;   /* 1 when pout_w is a real reading */
    int      pout_w;        /* output power, W */
    int      pout_cap_w;    /* active servo ceiling, W */
} autotune_in;

typedef struct autotune_out {
    uint16_t freq; /* next frequency, MHz */
    int      step; /* -1, 0, or +1 interval */
} autotune_out;

void autotune_step(const autotune_in *in, autotune_out *out);

#endif /* AUTOTUNE_H */
