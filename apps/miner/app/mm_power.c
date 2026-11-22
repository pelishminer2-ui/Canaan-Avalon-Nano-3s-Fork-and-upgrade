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

#include <pthread.h>
#include <string.h>

#include "mm_internal.h"
#include "board_desc.h"
#include "board_sensors.h"
#include "cali.h"
#include "mc_errcode.h"
#include "mm_cfgstore.h"
#include "mm_factory.h"
#include "mm_fan.h"
#include "mm_telem.h"
#include "mm_vf.h"
#include "mm_tune.h"
#include "power_cap.h"
#include "psu_ops.h"
#include "osif.h"

/* consecutive bad rail readings (mm_power_vout_bad()) that latch the fault */
#define VOUT_BELOW_LOW_CNT 10
#define POUT_OVER_HIGH_CNT 3 /* readings in a row over the limit that latch the fault */

static const uint16_t g_ramp_step = 10; /* frequency step per freq_ramp_rule() run, MHz */
uint16_t g_ramp_freq; /* current ramp frequency, MHz, 0 before the first step; main loop only */
uint8_t  g_ramp_announced; /* ramp target reached and sent flag; main loop only */
static pthread_mutex_t g_rail_lock = PTHREAD_MUTEX_INITIALIZER; /* rail-on vs reboot rail-off */

/* Written by mm_power_prepare() on rail power-on; read on any thread. */
static _Atomic unsigned g_power_modes = 1u << 0; /* modes the adapter powers, bit n = mode n */
static _Atomic uint16_t g_power_watts; /* capped budget at the last admission, W */
static _Atomic uint16_t g_power_raw;   /* PD advertisement before the software cap, W */

/* Set by rail_power_off() on any thread, including the reboot paths; cleared by a
 * confirmed rail-off or a successful rail-on. */
static _Atomic int g_rail_off_unconfirmed;

static const cali_wm_store g_wm_store = { mm_cfgstore_get, mm_cfgstore_set };

static int mm_power_limit_w(void)
{
    const hal_psu_ops_t *psu = hal_psu();
    return (psu && psu->pout_limit) ? (int)g_power_watts : -1;
}

static void mm_power_modes_from(unsigned *allowed, uint16_t watts)
{
    *allowed = 0;
    for (uint8_t mode = 0; mode < CALI_MODE_MAX; mode++) {
        const cali_param *params = cali_default_param(g_board.max_asic, mode);
        if (params->max_pout && (unsigned)params->max_pout + PD_POWER_RESERVE_W <= watts)
            *allowed |= 1u << mode;
    }
    *allowed |= 1u << 0;
}

/* Re-apply the software cap to the last PD advertisement. Does not renegotiate PD. */
static void mm_power_cap_sync(void)
{
    uint16_t raw = g_power_raw;

    if (!raw)
        return;
    int      after  = mm_tune_aftermarket();
    uint16_t capped = power_cap_apply(raw, after);
    if (capped == g_power_watts)
        return;
    unsigned allowed;
    mm_power_modes_from(&allowed, capped);
    g_power_modes = allowed;
    g_power_watts = capped;
    osif_log(OSIF_LOG_OP, "software power cap %u W (PD advertised %u W, aftermarket %s)",
            (unsigned)capped, (unsigned)raw, after ? "on" : "off");
}

static int mm_power_modes(unsigned *allowed, uint16_t *watts)
{
    *allowed                 = 0;
    *watts                   = 0;
    const hal_psu_ops_t *psu = hal_psu();
    if (!psu || !psu->pout_limit || psu->pout_limit(watts) != 0)
        return -1;
    g_power_raw = *watts;
    *watts      = power_cap_apply(*watts, mm_tune_aftermarket());
    mm_power_modes_from(allowed, *watts);
    return 0;
}

static unsigned mm_power_modes_allowed(void)
{
    return mm_power_limit_w() < 0 ? ~0u : g_power_modes;
}

static int mm_power_prepare(void)
{
    if (mm_power_limit_w() < 0)
        return HAL_PSU_RAIL_OK;
    unsigned allowed;
    uint16_t watts;
    if (mm_power_modes(&allowed, &watts) != 0)
        return HAL_PSU_RAIL_E_UNCONFIRMED;
    g_power_modes     = allowed;
    g_power_watts     = watts;
    uint8_t requested = cali_workmode_get(&g_wm_store);
    uint8_t selected  = mm_workmode_running();
    if (selected != requested) {
        osif_log(OSIF_LOG_OP,
                "power adapter allows only %u W — running mode %u "
                "as mode %u (the selection is kept)",
                (unsigned)watts, (unsigned)requested, (unsigned)selected);
        mm_fan_target_apply(selected);
    }
    return HAL_PSU_RAIL_OK;
}

static unsigned psu_flag(unsigned f)
{
    const hal_psu_ops_t *psu = hal_psu();
    return psu ? psu->flags & f : 0;
}

static void mm_rail_off_record(int rc)
{
    if (psu_flag(HAL_PSU_F_OFF_CONFIRM))
        g_rail_off_unconfirmed = rc != HAL_PSU_RAIL_OK;
}

static void mm_rail_on_settle(int rc)
{
    if (!psu_flag(HAL_PSU_F_OFF_CONFIRM))
        return;
    if (rc != HAL_PSU_RAIL_OK)
        (void)rail_power_off();
    else {
        g_rail_off_unconfirmed = 0;
        g_startup_errors &= ~ERR_MM_POWER;
    }
}

static void rail_shadow_reconcile(void)
{
    const hal_psu_ops_t *psu = hal_psu();
    board_power          pw;

    if (psu && psu->set_volt)
        return;
    if (board_power_read(&pw) == 0 && pw.voutcmd_mv)
        mm_vf_set_rail(pw.voutcmd_mv);
}

static uint16_t rail_enable_counts(void)
{
    return (uint16_t)VOL_ENUM;
}

uint16_t mm_power_budget(void)
{
    int watts = mm_power_limit_w();
    return watts < 0 ? 0 : (uint16_t)watts;
}

int mm_rail_off_unconfirmed(void)
{
    return psu_flag(HAL_PSU_F_OFF_CONFIRM) ? g_rail_off_unconfirmed : 0;
}

int mm_rail_off_duty(int rc, int duty)
{
    return psu_flag(HAL_PSU_F_OFF_CONFIRM) && rc != HAL_PSU_RAIL_OK ? FAN_DUTY_MAX_PCT : duty;
}

int mm_power_vout_bad(int vout_mv, int pout_w, int vout12)
{
    const hal_psu_ops_t *psu = hal_psu();
    if (psu && psu->vout_fault)
        return psu->vout_fault(vout_mv, pout_w, vout12);
    return vout_mv < POWER_VALID_VOLT_MIN;
}

int mm_rail_on_fault_latch(void)
{
    if (!psu_flag(HAL_PSU_F_LATCH_ON_FAIL))
        return 0;
    g_softoff_rsn = MC_SOFTOFF_POWER_FAULT;
    g_fault_off   = 1;
    return 1;
}

int mm_power_fault_stop(void)
{
    return psu_flag(HAL_PSU_F_LATCH_ON_FAIL) && g_fault_off &&
            g_softoff_rsn == MC_SOFTOFF_POWER_FAULT;
}

void mm_power_watch(const mm_telem_t *tel, pid_t asic)
{
    static int pf_cnt;
    int        power_watch = !g_fault_off && !g_softonoff.api_softoff && !g_softonoff.thermal_until;
    int power_bad = power_watch && mm_power_vout_bad(tel->ps_vout_mv, tel->pout_w, tel->ps_vout12);
#if IMPL_PSU_IS_DCDC_PD
    static int po_cnt;

    if (power_watch && tel->pout_w > MAX_POUT_LIMIT_ABNORMAL) {
        if (++po_cnt >= POUT_OVER_HIGH_CNT) {
            po_cnt    = 0;
            power_bad = 0;
            osif_log(OSIF_LOG_OP,
                    "POWER FAULT: output power %d W above "
                    "the %d W limit for %d checks in a row — mining "
                    "stopped, hashboard power off",
                    tel->pout_w, MAX_POUT_LIMIT_ABNORMAL, POUT_OVER_HIGH_CNT);
            mm_fault_stop(MC_SOFTOFF_POWER_FAULT, 0, asic);
        }
    } else {
        po_cnt = 0;
    }
#endif /* IMPL_PSU_IS_DCDC_PD */
    if (power_bad) {
        if (++pf_cnt >= VOUT_BELOW_LOW_CNT) {
            pf_cnt = 0;
            osif_log(OSIF_LOG_OP,
                    "POWER FAULT: power supply output %d "
                    "below the %d minimum for %d checks in a row "
                    "(output power %d W, 12 V reading %d) — "
                    "mining stopped, hashboard power off",
                    tel->ps_vout_mv, POWER_VALID_VOLT_MIN, VOUT_BELOW_LOW_CNT, tel->pout_w,
                    tel->ps_vout12);
            if (tel->ps_vout_mv == MM_TELEM_VOUT_NONE)
                osif_log(OSIF_LOG_ERR,
                        "power supply output could not "
                        "be read for %d checks in a row",
                        VOUT_BELOW_LOW_CNT);
            mm_fault_stop(MC_SOFTOFF_POWER_FAULT, 0, asic);
        }
    } else {
        pf_cnt = 0;
    }
}

uint8_t mm_workmode_running(void)
{
    uint8_t mode = cali_workmode_get(&g_wm_store);
    if (!(CALI_MODE_BOARD_MASK & (1u << mode)))
        mode = CALI_MODE_DEFAULT;
    if (mm_power_budget()) {
        unsigned allowed = mm_power_modes_allowed();
        while (mode && !(allowed & (1u << mode)))
            mode--;
    }
    return mode;
}

uint8_t mm_workmode_max(void)
{
    return cali_mode_top(mm_power_modes_allowed() & CALI_MODE_BOARD_MASK);
}

mc_set_result mm_apply_workmode(uint16_t op, const mc_ctrl_cmd *c)
{
    uint8_t mode  = c->v.workmode.mode;
    int32_t level = c->v.workmode.level;

    (void)op;
    if (mode >= CALI_MODE_MAX || level < 0 || level >= CALI_LEVEL_COUNT)
        return MC_SET_ERR_ARG;

    if (!(mm_power_modes_allowed() & CALI_MODE_BOARD_MASK & (1u << mode)))
        return MC_SET_ERR_ARG;

    if (mm_factory_in_progress()) {
        osif_log(OSIF_LOG_WARN, "work mode %u refused: factory recovery in progress",
                (unsigned)mode);
        return MC_SET_ERR_HW;
    }

    /* A mode without a calibration record is still aging, or failed it. */
    uint8_t    running = mm_workmode_running();
    cali_param cp;
    uint8_t    aged = 0;
    cali_resolve(CALI_INI_PATH, g_board.max_asic, running, 0, &cp, &aged);
    if (!aged) {
        osif_log(OSIF_LOG_INFO, "work mode %u refused: mode %u has not finished aging",
                (unsigned)mode, (unsigned)running);
        return MC_SET_ERR_ARG;
    }

    if (cali_workmode_set(&g_wm_store, mode) != 0) {
        osif_log(OSIF_LOG_ERR, "work mode %u could not be saved", (unsigned)mode);
        return MC_SET_ERR_HW;
    }
    mm_fan_target_apply(mode);
    return MC_SET_OK;
}

int mm_workmode_button_cycle(void)
{
    uint8_t     mode  = mm_workmode_running();
    uint8_t     modes = (uint8_t)(mm_workmode_max() + 1u);
    mc_ctrl_cmd c;

    /* Cycling to the one allowed mode would overwrite the saved selection. */
    if (modes < 2) {
        osif_log(OSIF_LOG_INFO, "work mode button ignored: only mode %u is allowed",
                (unsigned)mode);
        return -1;
    }
    memset(&c, 0, sizeof c);
    c.v.workmode.mode  = (uint8_t)((mode + 1) % modes);
    c.v.workmode.level = 0;
    if (mm_apply_workmode(CTRL_OP_WORKMODE, &c) != MC_SET_OK)
        return -1;
    return (int)c.v.workmode.mode;
}

int rail_power_off(void)
{
    int rc = board_power_rail_off();
    mm_vf_rail_unknown();
    mm_rail_off_record(rc);
    rail_shadow_reconcile();
    return rc;
}

int rail_power_off_for_reboot(void)
{
    pthread_mutex_lock(&g_rail_lock);
    int rc = rail_power_off();
    pthread_mutex_unlock(&g_rail_lock);
    return rc;
}

static int rail_power_on_locked(void)
{
    int admission = atomic_load(&g_rebooting) ? HAL_PSU_RAIL_OK : mm_power_prepare();
    if (admission != HAL_PSU_RAIL_OK) {
        (void)rail_power_off();
        return admission;
    }
    if (atomic_load(&g_rebooting)) {
        osif_log(OSIF_LOG_INFO, "hashboard power-on refused — reboot in progress");
        return HAL_PSU_RAIL_E_REFUSED;
    }
    uint16_t counts = rail_enable_counts();
    osif_log(OSIF_LOG_INFO, "hashboard power on (volt %u)", counts);
    int rc = board_power_rail_on(counts);
    mm_vf_rail_unknown();
    mm_rail_on_settle(rc);
    rail_shadow_reconcile();
    return rc;
}

int rail_power_on(void)
{
    pthread_mutex_lock(&g_rail_lock);
    int rc = rail_power_on_locked();
    pthread_mutex_unlock(&g_rail_lock);
    return rc;
}

void freq_ramp_rule(void *ctx, uint64_t now_ms)
{
    (void)ctx;
    (void)now_ms;

    mm_power_cap_sync();

    mm_telem_t t;
    mm_telem_get(&t);
    if (t.asic_chips <= 0)
        return;

    cali_param cp;
    uint8_t    mode = mm_workmode_running();
    cali_resolve(CALI_INI_PATH, g_board.max_asic, mode, 0, &cp, NULL);
    uint16_t target = cp.pll_start;
    if (target == 0)
        return;

    if (g_ramp_freq == 0)
        g_ramp_freq = (uint16_t)INIT_FREQ;
    if (g_ramp_freq == target) {
        if (!g_ramp_announced) {
            mm_vf_set_ramp_target(target, 1);
            g_ramp_announced = 1;
        }
        return;
    }

    uint16_t next;
    if (t.in_work)
        next = target;
    else if (target > g_ramp_freq)
        next = (uint16_t)(target - g_ramp_freq < g_ramp_step ? target : g_ramp_freq + g_ramp_step);
    else
        next = (uint16_t)(g_ramp_freq - target < g_ramp_step ? target : g_ramp_freq - g_ramp_step);

    mm_vf_set_ramp_target(next, next == target);
    g_ramp_freq = next;
    if (next == target)
        g_ramp_announced = 1;

    osif_log(OSIF_LOG_DBG,
            "frequency ramp step %u MHz (target %u MHz, chips at "
            "%d MHz, %d chip(s))",
            (unsigned)next, (unsigned)target, mm_telem_real_freq(&t), t.asic_chips);
}
