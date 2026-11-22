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

#ifndef POWER_CAP_H
#define POWER_CAP_H

#include <stdint.h>

/*
 * Software power ceiling for a Nano 3s PD advertisement.
 * Off: min(reported, PD_STOCK_CAP_W) when that cap exists, else the advertisement.
 * On: PD_AFTERMARKET_CAP_W (Mean Well LRS-150 class), still under the fault trip.
 * Boards without those macros keep the advertisement.
 */
static inline uint16_t power_cap_apply(uint16_t reported_w, int aftermarket)
{
#if defined(PD_STOCK_CAP_W) && defined(PD_AFTERMARKET_CAP_W)
    if (aftermarket)
        return (uint16_t)PD_AFTERMARKET_CAP_W;
    if (reported_w > (uint16_t)PD_STOCK_CAP_W)
        return (uint16_t)PD_STOCK_CAP_W;
#else
    (void)aftermarket;
#endif
    return reported_w;
}

/*
 * Servo ceiling. Stock keeps the cali row, clamped by the adapter budget minus
 * the PD reserve. Aftermarket may use the raised budget, and only while that
 * budget is above the stock cap and still under the abnormal-power fault.
 */
static inline uint16_t power_cap_servo(uint16_t row_w, uint16_t budget_w, int aftermarket)
{
    uint16_t lim = budget_w;

    if (!lim)
        return row_w;
#if defined(PD_POWER_RESERVE_W)
    lim = lim > (uint16_t)PD_POWER_RESERVE_W ? (uint16_t)(lim - (uint16_t)PD_POWER_RESERVE_W) : 0;
#endif
#if defined(PD_STOCK_CAP_W) && defined(MAX_POUT_LIMIT_ABNORMAL)
    if (aftermarket && budget_w > (uint16_t)PD_STOCK_CAP_W && lim > row_w &&
            lim < (uint16_t)MAX_POUT_LIMIT_ABNORMAL)
        return lim;
#else
    (void)aftermarket;
#endif
    return lim < row_w ? lim : row_w;
}

#endif /* POWER_CAP_H */
