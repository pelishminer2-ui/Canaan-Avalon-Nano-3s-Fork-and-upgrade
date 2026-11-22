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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "boardconf.h"
#include "autotune.h"
#include "power_cap.h"

static autotune_in mild_ready(void)
{
    autotune_in in;
    memset(&in, 0, sizeof in);
    in.mode         = AUTOTUNE_MILD;
    in.settled      = 1;
    in.pll_start    = 396;
    in.pll_interval = 20;
    in.freq         = 396;
    in.temp_known   = 1;
    in.temp_c       = 70;
    in.temp_target  = 90;
    in.temp_fan_max = 100;
    in.temp_trip    = 113;
    in.power_known  = 1;
    in.pout_w       = 100;
    in.pout_cap_w   = 133;
    return in;
}

static void step(const autotune_in *in, autotune_out *out)
{
    autotune_step(in, out);
}

int main(void)
{
    autotune_out out;
    autotune_in  in = mild_ready();

    in.mode = AUTOTUNE_OFF;
    step(&in, &out);
    assert(out.step == 0 && out.freq == 396);

    in = mild_ready();
    in.settled = 0;
    step(&in, &out);
    assert(out.step == 0 && out.freq == 396);

    in = mild_ready();
    in.pll_interval = 0;
    step(&in, &out);
    assert(out.step == 0);

    in = mild_ready();
    step(&in, &out);
    assert(out.step == 1 && out.freq == 416);

    in.freq = 416;
    step(&in, &out);
    assert(out.step == 1 && out.freq == 436);

    in.freq = 436;
    step(&in, &out);
    assert(out.step == 0 && out.freq == 436);

    in = mild_ready();
    in.freq   = 416;
    in.temp_c = 90;
    step(&in, &out);
    assert(out.step == -1 && out.freq == 396);

    in.freq   = 396;
    in.temp_c = 90;
    step(&in, &out);
    assert(out.step == 0 && out.freq == 396);

    in = mild_ready();
    in.freq   = 436;
    in.temp_c = 100;
    step(&in, &out);
    assert(out.step == -1 && out.freq == 416);

    in = mild_ready();
    in.freq   = 436;
    in.temp_c = 113;
    step(&in, &out);
    assert(out.step == -1 && out.freq == 416);

    in = mild_ready();
    in.temp_known = 0;
    step(&in, &out);
    assert(out.step == 0 && out.freq == 396);

    in = mild_ready();
    in.pout_w = 130;
    step(&in, &out);
    assert(out.step == 0 && out.freq == 396);

    in.freq   = 416;
    in.pout_w = 133;
    step(&in, &out);
    assert(out.step == -1 && out.freq == 396);

    in = mild_ready();
    in.freq = 390;
    step(&in, &out);
    assert(out.step == 0 && out.freq == 390);

#if defined(PD_STOCK_CAP_W)
    assert(PD_STOCK_CAP_W == 140);
    assert(PD_AFTERMARKET_CAP_W == 150);
    assert(PD_AFTERMARKET_CAP_W < MAX_POUT_LIMIT_ABNORMAL);
    assert(PD_AFTERMARKET_CAP_W + POUT_UP_LIMIT < MAX_POUT_LIMIT_ABNORMAL);
    assert(AUTOTUNE_DEFAULT == AUTOTUNE_OFF);
    assert(PVT_TEMP_OVER_HR_MAX == 113);
    assert(HASHOUT_OVER_HR_MAX == 95);
    assert(FAN_PID_TEMP_MAX == 100);
    assert(power_cap_apply(140, 0) == 140);
    assert(power_cap_apply(100, 0) == 100);
    assert(power_cap_apply(40, 0) == 40);
    assert(power_cap_apply(200, 0) == 140);
    assert(power_cap_apply(140, 1) == 150);
    assert(power_cap_apply(40, 1) == 150);
    assert(power_cap_apply(200, 1) == 150);
    assert(power_cap_servo(133, 140, 0) == 133);
    assert(power_cap_servo(133, 100, 0) == 97);
    assert(power_cap_servo(95, 100, 0) == 95);
    assert(power_cap_servo(62, 0, 0) == 62);
    assert(power_cap_servo(133, 140, 1) == 133);
    assert(power_cap_servo(133, 150, 1) == 147);
    assert(147 < MAX_POUT_LIMIT_ABNORMAL);
    printf("autotune nano3s caps: stock %u W, aftermarket %u W, fault %d W\n",
            (unsigned)PD_STOCK_CAP_W, (unsigned)PD_AFTERMARKET_CAP_W, MAX_POUT_LIMIT_ABNORMAL);
#else
    assert(power_cap_apply(350, 0) == 350);
    assert(power_cap_apply(350, 1) == 350);
    assert(power_cap_servo(300, 350, 1) == 300);
    printf("autotune caps passthrough on this board\n");
#endif
    printf("autotune flags OK\n");
    return 0;
}
