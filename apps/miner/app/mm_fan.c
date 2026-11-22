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

#include "mm_fan.h"
#include "fan_dev.h"
#include "fan_pid.h"
#include "mm_telem.h"
#include "thermal.h"
#include "cali.h"
#include "board_desc.h"
#include "autotune.h"
#include "mm_tune.h"
#include "osif.h"

#include <pthread.h>

#define FAN_PID_OUT_MIN    154    /* PID output floor, steps on the 0..0x3ff scale */
#define FAN_PID_OUT_MAX    0x3ff  /* PID output full scale, steps */
#define FAN_PID_DELTA_DOWN (-100) /* largest PID output decrease per step, steps */
#define FAN_DEV_REOPEN_MS  60000u /* fan controller reopen interval, ms */

atomic_int             g_fan_dev_ok;
static pthread_mutex_t g_fan_lock = PTHREAD_MUTEX_INITIALIZER; /* g_fan_st and fan target lock */
static fan_pid_state_t g_fan_st = { .fixed_duty = -1 }; /* fan PID state, fixed_duty -1 automatic */
static uint16_t g_fan_target_temp = FAN_TARGET_TEMP;    /* fan control target temperature, degC */
static int      g_fan_target_manual;                    /* target set by API flag */
static uint64_t g_fan_retry_ms;                         /* last fan controller reopen attempt, ms */
static thermal_telem_state_t g_fan_tel;                 /* chip temperature freshness state */

/* fan PID parameters; fan_min/fan_max on the 0..0x3ff PID output scale */
static fan_pid_params_t g_fan_pp = {
    .p              = FAN_PID_P,
    .i              = FAN_PID_I,
    .d              = FAN_PID_D,
    .temp_max       = FAN_PID_TEMP_MAX,
    .temp_min       = FAN_PID_TEMP_MIN,
    .fan_min        = FAN_PID_OUT_MIN,
    .fan_max        = FAN_PID_OUT_MAX,
    .dlimit_up      = FAN_PID_DELTA_UP,
    .dlimit_down    = FAN_PID_DELTA_DOWN,
    .duty_init_pct  = FAN_DUTY_INIT_PCT,
    .duty_max_pct   = FAN_DUTY_MAX_PCT,
    .err_quant      = FAN_PID_ERR_QUANT,
    .duty_whole_pct = FAN_PID_DUTY_WHOLE_PCT,
    .instart_hold   = FAN_PID_INSTART_HOLD,
};

void mm_fan_pid_fixed(int duty_pct)
{
    pthread_mutex_lock(&g_fan_lock);
    fan_pid_set_fixed(&g_fan_st, duty_pct);
    pthread_mutex_unlock(&g_fan_lock);
}

void mm_fan_target_set_manual(uint16_t temp)
{
    pthread_mutex_lock(&g_fan_lock);
    g_fan_target_temp   = temp;
    g_fan_target_manual = 1;
    pthread_mutex_unlock(&g_fan_lock);
}

void mm_fan_set_fixed(int duty)
{
    mm_fan_pid_fixed(duty);
    fan_dev_set_duty_x10(duty * 10);
}

void mm_fan_target_apply(uint8_t mode)
{
    cali_param cp;
    uint8_t    aged = 0;
    cali_resolve(CALI_INI_PATH, g_board.max_asic, mode, 0, &cp, &aged);
    pthread_mutex_lock(&g_fan_lock);
    int manual = g_fan_target_manual;
    if (!manual)
        g_fan_target_temp = cp.temp;
    uint16_t cur = g_fan_target_temp;
    pthread_mutex_unlock(&g_fan_lock);
    if (manual)
        osif_log(OSIF_LOG_INFO,
                "fan target stays at %u degC (set by API) "
                "in mode %u",
                cur, (unsigned)mode);
    else
        osif_log(OSIF_LOG_INFO, "fan target %u degC for mode %u (%s)", cp.temp, (unsigned)mode,
                aged ? "calibrated" : "factory default");
}

void fan_rule(void *ctx, uint64_t now_ms)
{
    const mm_fan_ctx_t      *fc = ctx;
    const softonoff_state_t *pk = fc->softonoff;
    mm_telem_t               t;
    mm_telem_get(&t);

    if (!atomic_load(&g_fan_dev_ok)) {
        if (now_ms - g_fan_retry_ms >= FAN_DEV_REOPEN_MS) {
            g_fan_retry_ms = now_ms;
            if (fan_dev_init() == 0) {
                atomic_store(&g_fan_dev_ok, 1);
                osif_log(OSIF_LOG_INFO, "fan controller available again — fan control resumed");
            } else {
                osif_log(OSIF_LOG_WARN,
                        "fan controller still unavailable — retrying "
                        "every 60 s");
            }
        }
        if (!atomic_load(&g_fan_dev_ok))
            return;
    }

    if (softonoff_fan_hold(pk, fc->fault_off && atomic_load(fc->fault_off))) {
        g_fan_tel.ok_ms    = now_ms;
        g_fan_tel.failsafe = 0;
        return;
    }

    int cur_temp = fan_pid_pick_input(t.tavg, t.tmax, t.hb_outlet, t.hb_count);

    int                   was_failsafe = g_fan_tel.failsafe;
    thermal_telem_verdict v            = thermal_telem_check(&g_fan_tel, t.asic_live,
                       cur_temp > ASIC_TEMP_NONE, now_ms, TELEM_STALE_FAILSAFE_MS);
    if (v == THERMAL_TELEM_FAILSAFE) {
        if (!was_failsafe)
            osif_log(OSIF_LOG_WARN,
                    "no usable chip temperature for over %u s "
                    "— fan set to full speed for safety (chips reporting %s, "
                    "average %d degC)",
                    TELEM_STALE_FAILSAFE_MS / 1000u, t.asic_live ? "yes" : "no", t.tavg);
        fan_dev_set_duty_x10(FAN_DUTY_MAX_PCT * 10);
        return;
    }
    if (v == THERMAL_TELEM_HOLD) {
        if (!enum_hold_active(fc->enum_hold, pk->thermal_until || pk->api_softoff))
            return;
        pthread_mutex_lock(&g_fan_lock);
        int pin_pct = g_fan_st.fixed_duty >= 0 ? g_fan_st.fixed_duty : FAN_DUTY_ENUM_PCT;
        pthread_mutex_unlock(&g_fan_lock);
        fan_dev_set_duty_x10(pin_pct * 10);
        return;
    }
    if (was_failsafe)
        osif_log(OSIF_LOG_INFO,
                "chip temperature available again — fan back "
                "to automatic control");

    int duty_x10 = 0, changed = 0;
    int mild     = 0;
#ifdef FAN_PID_P_MILD
    mild = mm_tune_autotune() == AUTOTUNE_MILD;
#endif
    pthread_mutex_lock(&g_fan_lock);
#ifdef FAN_PID_P_MILD
    g_fan_pp.p = mild ? (float)FAN_PID_P_MILD : (float)FAN_PID_P;
#else
    (void)mild;
    g_fan_pp.p = FAN_PID_P;
#endif
    g_fan_pp.temp_min = FAN_PID_TEMP_MIN;
    g_fan_pp.temp_max = FAN_PID_TEMP_MAX;
    (void)fan_pid_clamp_band(&g_fan_pp, FAN_PID_TEMP_MIN, FAN_PID_TEMP_MAX);
    fan_pid_step(&g_fan_st, &g_fan_pp, (float)cur_temp, g_fan_target_temp, fan_dev_last_duty_x10(),
            &duty_x10, &changed);
    pthread_mutex_unlock(&g_fan_lock);
    if (changed)
        fan_dev_set_duty_x10(duty_x10);
}
