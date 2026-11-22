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

#include "mm_vf.h"
#include "mm_telem.h"
#include "mm_internal.h"
#include "board_desc.h"
#include "board_sensors.h"
#include "cali.h"
#include "boardconf.h"
#include "pout_reg.h"
#include "aging.h"
#include "autotune.h"
#include "mm_tune.h"
#include "power_cap.h"
#include "psu_ops.h"

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#define MODE_UNSET               0xFF
#define VF_SENSOR_FRESH_MS       6000u /* sensor reading age limit, ms */
#define ASIC_FREQ_FLOOR_MHZ      100u  /* lowest frequency sent to asic_miner, MHz */
#define AGING_FREQ_CEILING(base) ((uint16_t)((base) + 150u)) /* highest aging frequency, MHz */
#define MODE_FREQ_STEP           10                          /* work mode frequency step, MHz */
#define POLICY_TICK_MS           2000u                       /* V/F loop period when settled, ms */
#define MODE_STEP_TICK_MS        1000u                  /* V/F loop period while stepping, ms */
#define AGING_PERIOD_MS          300000u                /* aging step period, ms */
#define AGING_FIRST_RUN_MS       (2u * AGING_PERIOD_MS) /* delay before the first aging step, ms */
#define PRE_WORK_TICK_MS         500u                   /* V/F loop period before in-work, ms */
#define VOLT_REG_PERIOD_MS       5000u                  /* power regulation period, ms */
#define AUTOTUNE_PERIOD_MS       60000u                 /* mild frequency step period, ms */

static pthread_mutex_t  g_vf_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int      g_vf_run  = 1;
static pthread_t        g_vf_thread;
static int              g_vf_started;
static _Atomic uint64_t g_vf_beat_ms;             /* see mm_vf_beat_ms() */
static uint16_t         g_rail_mv;                /* hash rail voltage, DC-DC rail counts, mV */
static int              g_rail_known;             /* g_rail_mv set by a rail write flag */
static uint16_t         g_work_freq;              /* work frequency, MHz */
static uint8_t          g_aged_mode = MODE_UNSET; /* the mode whose aging finished this run */
static uint8_t          g_aging_bar;              /* aging progress, % */
static board_power      g_power;                  /* last PSU reading */
static uint64_t         g_power_at;               /* last PSU reading time, ms */
static int16_t          g_board_temp = ASIC_TEMP_NONE; /* board NTC temperature, degC */
static uint64_t         g_board_temp_at;               /* last board NTC reading time, ms */
static uint8_t          g_workmode       = MODE_UNSET; /* applied work mode */
static uint8_t          g_mode_announced = MODE_UNSET; /* work mode already logged */
static uint16_t         g_tgt_freq;                    /* target frequency, MHz */
static uint16_t         g_tgt_volt;                    /* target rail, rail counts */
static int              g_mode_settled = 1;
static _Atomic uint32_t g_ramp;         /* ramp base and at-target flag */
static uint8_t          g_ramp_at_sent; /* at-target flag sent to asic_miner */
static int              g_freq_held;
static uint16_t         g_freq_hold_base;   /* API frequency ramp base, MHz */
static uint16_t         g_switch_ramp_base; /* ramp base at the last work mode switch, MHz */
static _Atomic uint8_t  g_volt_tune = 1;
static uint8_t          g_revert_pending; /* work frequency restore pending flag */
static _Atomic int      g_aging_failed_req;
static _Atomic int      g_aging_rearm;
static _Atomic int      g_asic_respawned;
static int              g_seed_respawn; /* next seed follows an asic_miner spawn flag */
static _Atomic uint16_t g_volt_req;
static _Atomic int      g_aging_on;
static _Atomic uint16_t g_tuned_freq; /* mild autotune hold, MHz; 0 follows the ramp */

/* VOLT-REG servo state. MAX_LIMIT_ONLY is held once per process: a mode
 * switch takes the new mode's budget and settled rail and keeps the stage. */
static struct {
    int              inited;
    uint8_t          mode; /* work mode of cp */
    uint8_t          cali_aged;
    cali_param       cp; /* calibration of the work mode */
    pout_reg_state_t st;
} g_vreg = { .mode = MODE_UNSET };

/* Aging sweep state; all-zero is a sweep not yet started. */
static struct {
    uint8_t       mode;   /* work mode of the sweep */
    int           inited; /* sweep started flag */
    int           finished;
    int           aborted; /* sweep stopped at the frequency ceiling flag */
    int           failed;  /* sweep found no usable point flag */
    int           primed;  /* first-run delay started flag */
    uint32_t      next_at; /* next aging step time, ms */
    aging_state_t st;
    cali_param    cp; /* calibration of the work mode */
} g_aging;

static uint16_t pout_budget(uint16_t row)
{
    return power_cap_servo(row, mm_power_budget(), mm_tune_aftermarket());
}

void mm_vf_set_ramp_target(uint16_t base, uint8_t at_target)
{
    atomic_store(&g_ramp, (uint32_t)base << 8 | (at_target ? 1u : 0u));
}

void mm_vf_asic_spawned(void)
{
    atomic_store(&g_asic_respawned, 1);
}

static void ramp_get(uint16_t *base, uint8_t *at_target)
{
    uint32_t r = atomic_load(&g_ramp);

    *base      = (uint16_t)(r >> 8);
    *at_target = (uint8_t)(r & 0xffu);
}

static int rail_controllable(void)
{
    const hal_psu_ops_t *psu = hal_psu();

    return psu && psu->set_volt;
}

void mm_vf_set_rail(uint16_t mv)
{
    if (!rail_controllable()) {
        pthread_mutex_lock(&g_vf_lock);
        g_rail_mv = mv;
        pthread_mutex_unlock(&g_vf_lock);
        return;
    }
    if (mv < VOL_MIN || mv > VOL_MAX) {
        uint16_t was = mv;
        mv           = mv < VOL_MIN ? (uint16_t)VOL_MIN : (uint16_t)VOL_MAX;
        osif_log(OSIF_LOG_WARN, "volt %u outside [%u %u] using %u", was, (unsigned)VOL_MIN,
                (unsigned)VOL_MAX, mv);
    }
    pthread_mutex_lock(&g_vf_lock);
    if (board_power_set_volt(mv) == 0) {
        g_rail_mv    = mv;
        g_rail_known = 1;
    }
    pthread_mutex_unlock(&g_vf_lock);
}

void mm_vf_rail_unknown(void)
{
    atomic_store(&g_volt_req, 0);
    pthread_mutex_lock(&g_vf_lock);
    g_rail_known = 0;
    pthread_mutex_unlock(&g_vf_lock);
}

int mm_vf_set_volt(uint16_t v)
{
    if (!rail_controllable() || v < VOL_MIN || v > VOL_MAX)
        return -1;
    if (atomic_load(&g_aging_on))
        return -2;
    atomic_store(&g_volt_req, v);
    return 0;
}

static int aging_running(void)
{
    return g_aging.inited && !g_aging.finished && !g_aging.aborted && g_aging.mode == g_workmode;
}

static void volt_req_apply(void)
{
    uint16_t v = atomic_exchange(&g_volt_req, 0);

    if (!v || aging_running())
        return;
    g_tgt_volt = v;
    mm_vf_set_rail(v);
}

static uint16_t rail_get(void)
{
    pthread_mutex_lock(&g_vf_lock);
    uint16_t mv = g_rail_mv;
    pthread_mutex_unlock(&g_vf_lock);
    return mv;
}

static void rail_set_volt(uint16_t mv)
{
    if (rail_controllable())
        mm_vf_set_rail(mv);
}

static void set_aged_mode(uint8_t mode)
{
    pthread_mutex_lock(&g_vf_lock);
    g_aged_mode = mode;
    pthread_mutex_unlock(&g_vf_lock);
}

static void set_aging_bar(uint8_t bar)
{
    pthread_mutex_lock(&g_vf_lock);
    g_aging_bar = bar;
    pthread_mutex_unlock(&g_vf_lock);
}

static uint16_t asic_freq_floor(uint16_t f)
{
    return f && f < ASIC_FREQ_FLOOR_MHZ ? (uint16_t)ASIC_FREQ_FLOOR_MHZ : f;
}

static int asic_vf_send(mc_asic_vf *vf)
{
    for (int i = 0; i < ASIC_PLL_POINTS; i++)
        vf->freq[i] = asic_freq_floor(vf->freq[i]);
    return osif_ipc_send(g_ipc_asic_mm, MSG_ASIC_VF, vf, (int)sizeof *vf) == OSIF_OK;
}

static int asic_send_vf(uint16_t freq, uint8_t at_target)
{
    mc_asic_vf vf = { .at_target = at_target };

    freq = asic_freq_floor(freq);
    for (int i = 0; i < ASIC_PLL_POINTS; i++)
        vf.freq[i] = (uint16_t)(freq + i * g_board.freq_interval);
    return asic_vf_send(&vf);
}

static int asic_send_freq(uint16_t freq)
{
    uint16_t base;
    uint8_t  at;

    ramp_get(&base, &at);
    return asic_send_vf(freq, at && freq == base);
}

static int set_work_freq(uint16_t freq)
{
    if (!asic_send_freq(freq))
        return 0;
    g_work_freq = freq;
    return 1;
}

void mm_vf_set_volt_tune(uint8_t on)
{
    atomic_store(&g_volt_tune, on ? 1u : 0u);
}

int mm_vf_hold_freq(uint16_t pll[ASIC_PLL_POINTS])
{
    mc_asic_vf vf = { .at_target = 0 };
    uint16_t   base;

    memcpy(vf.freq, pll, sizeof vf.freq);
    ramp_get(&base, &vf.at_target);
    if (!asic_vf_send(&vf))
        return 0;
    memcpy(pll, vf.freq, sizeof vf.freq);

    g_work_freq      = vf.freq[0];
    g_tgt_freq       = vf.freq[0];
    g_mode_settled   = 1;
    g_freq_held      = 1;
    g_freq_hold_base = base;
    return 1;
}

static int vf_step_toward(uint16_t tgt_freq, uint16_t tgt_volt)
{
    int volt_ctl = rail_controllable();

    if (tgt_volt < VOL_MIN)
        tgt_volt = VOL_MIN;
    if (tgt_volt > VOL_MAX)
        tgt_volt = VOL_MAX;

    uint16_t cur_v = rail_get();
    uint16_t cur_f = g_work_freq;

    if (volt_ctl && tgt_volt > cur_v) {
        uint16_t nv = cur_v + RAIL_VOLT_STEP;

        if (nv > tgt_volt)
            nv = tgt_volt;
        rail_set_volt(nv);
        return 0;
    }
    if (tgt_freq > cur_f) {
        uint16_t nf = cur_f + MODE_FREQ_STEP;

        if (nf > tgt_freq)
            nf = tgt_freq;
        (void)set_work_freq(nf);
        return 0;
    }
    if (tgt_freq < cur_f) {
        uint16_t nf = cur_f > (uint16_t)(MODE_FREQ_STEP + tgt_freq) ?
                (uint16_t)(cur_f - MODE_FREQ_STEP) :
                tgt_freq;
        (void)set_work_freq(nf);
        return 0;
    }
    if (volt_ctl && tgt_volt < cur_v) {
        uint16_t nv = cur_v > (uint16_t)(tgt_volt + RAIL_VOLT_STEP) ?
                (uint16_t)(cur_v - RAIL_VOLT_STEP) :
                tgt_volt;
        rail_set_volt(nv);
        return 0;
    }
    return 1;
}

static int mode_needs_aging(uint8_t mode)
{
    cali_param cp;
    uint8_t    aged = 0;

    cali_resolve(CALI_INI_PATH, g_board.max_asic, mode, 0, &cp, &aged);
    return !aged;
}

static int mode_tick(void)
{
    uint16_t req;
    uint8_t  at;

    ramp_get(&req, &at);
    if (g_freq_held && req != g_freq_hold_base)
        g_freq_held = 0;
    if (g_switch_ramp_base && req != g_switch_ramp_base)
        g_switch_ramp_base = 0;
    if (req && req != g_tgt_freq && !g_freq_held && !g_switch_ramp_base) {
        uint16_t tuned = atomic_load(&g_tuned_freq);
        /* The ramp names the cali start. A mild tune above that start stays. */
        if (!(tuned && req < g_tgt_freq && mm_tune_autotune() == AUTOTUNE_MILD)) {
            g_tgt_freq     = req;
            g_mode_settled = 0;
            osif_log(OSIF_LOG_DBG, "freq tgt %u MHz, now at %u MHz", (unsigned)req,
                    (unsigned)g_work_freq);
        }
    }

    uint8_t want = mm_workmode_running();
    if (want != g_workmode) {
        atomic_store(&g_tuned_freq, 0);
        int keep_held = g_workmode == MODE_UNSET && g_freq_held;
        int announced = g_workmode == MODE_UNSET && !keep_held && want == g_mode_announced;
        g_workmode    = want;
        cali_param cp;
        uint8_t    cali_aged = 0;
        cali_resolve(CALI_INI_PATH, g_board.max_asic, want, 0, &cp, &cali_aged);
        g_tgt_volt = cp.volt;
        set_aging_bar(!cali_aged && g_aged_mode != want ? 0 : 100);
        if (!keep_held) {
            g_freq_held        = 0;
            g_switch_ramp_base = req;
            g_tgt_freq         = cp.pll_start;
            g_mode_settled     = 0;
        }
        if (!announced)
            osif_log(OSIF_LOG_OP, "work mode %u: tgt %u MHz, volt %u", (unsigned)want, g_tgt_freq,
                    g_tgt_volt);
    }
    if (!g_mode_settled) {
        if (vf_step_toward(g_tgt_freq, g_tgt_volt)) {
            g_mode_settled = 1;
            osif_log(OSIF_LOG_INFO, "work mode %u reached %u MHz", (unsigned)g_workmode,
                    (unsigned)g_work_freq);
        }
        return 1;
    }
    return 0;
}

static void volt_reg_tick(const board_power *pw)
{
    static uint32_t last_ms = 0;

    if (!rail_controllable())
        return;

    uint32_t now = (uint32_t)osif_now_ms();
    if (last_ms && (uint32_t)(now - last_ms) < VOLT_REG_PERIOD_MS)
        return;
    last_ms = now;

    static const pout_reg_params_t pp = {
        .up_limit           = POUT_UP_LIMIT,
        .down_limit         = POUT_DOWN_LIMIT,
        .pout_min           = 50,
        .vol_min            = VOL_MIN,
        .vol_max            = VOL_MAX,
        .volt_step          = RAIL_VOLT_STEP,
        .abnormal_pout      = MAX_POUT_LIMIT_ABNORMAL,
        .stab_thresh_normal = 10,
        .stab_thresh_aged   = 120,
    };
    if (!pw)
        return;

    uint16_t cur_rail = rail_get();

    if (!g_vreg.inited) {
        memset(&g_vreg.st, 0, sizeof g_vreg.st);
        g_vreg.st.stage = POUT_STAGE_MAX_LIMIT_ONLY;
        g_vreg.inited   = 1;
    }
    if (g_workmode != g_vreg.mode) {
        g_vreg.mode = g_workmode;
        cali_resolve(CALI_INI_PATH, g_board.max_asic, g_workmode, 0, &g_vreg.cp, &g_vreg.cali_aged);
        /* pout_reg clamps max_pout to [pout_min, max(budget, pout_min)]. */
        g_vreg.st.max_pout   = pout_budget(g_vreg.cp.max_pout);
        g_vreg.st.start_volt = (short)cur_rail;
        osif_log(OSIF_LOG_INFO, "power regulation for mode %u: %u W, volt %u", (unsigned)g_workmode,
                g_vreg.st.max_pout, cur_rail);
    }

    pout_reg_telem_t t = {
        .tuning         = atomic_load(&g_volt_tune),
        .aging_finished = g_vreg.cali_aged || g_aged_mode == g_workmode,
        .pout           = pw->pout_w,
        .vout           = pw->vout_mv,
        .voutcmd        = (short)cur_rail,
        .poutlimit      = pout_budget(g_vreg.cp.max_pout),
        .volt_cap       = (short)g_vreg.cp.volt,
    };
    short volt_out = 0;
    if (pout_reg_step(&g_vreg.st, &pp, &t, &volt_out) != 1)
        return;

    if (volt_out < 0)
        return;
    if (volt_out < VOL_MIN)
        volt_out = VOL_MIN;
    if (volt_out > VOL_MAX)
        volt_out = VOL_MAX;
    if ((uint16_t)volt_out == cur_rail)
        return;
    osif_log(OSIF_LOG_INFO, "volt %u->%d (%u W, tgt %u W)", cur_rail, (int)volt_out, pw->pout_w,
            g_vreg.st.max_pout);
    rail_set_volt((uint16_t)volt_out);
}

int mm_vf_take_aging_failed(void)
{
    return atomic_exchange(&g_aging_failed_req, 0);
}

void mm_vf_aging_rearm(void)
{
    atomic_store(&g_aging_rearm, 1);
}

static void aging_revert_retry(void)
{
    if (g_revert_pending && asic_send_freq(g_work_freq))
        g_revert_pending = 0;
}

static void aging_revert(void)
{
    g_revert_pending = 1;
    aging_revert_retry();
}

static void aging_tick(double ghs, const board_power *pw)
{
    static aging_params_t pp = {
        .up_stages     = ADG_STAGE_COUNT_UP,
        .down_stages   = ADG_STAGE_COUNT_DOWN,
        .freq_step     = ADJ_FREQ_INTERVAL,
        .pout_up_limit = POUT_UP_LIMIT,
        .pll_offset    = 0,
        .pll_interval  = FREQ_INTERVAL,
    };
    aging_state_t    *st = &g_aging.st;
    const cali_param *cp = &g_aging.cp;

    if (g_workmode != g_aging.mode) {
        g_aging.mode   = g_workmode;
        g_aging.inited = g_aging.finished = g_aging.aborted = g_aging.failed = 0;
    }
    if (atomic_exchange(&g_aging_rearm, 0) && g_aging.failed)
        g_aging.inited = g_aging.finished = g_aging.failed = 0;
    if (g_aging.finished || g_aging.aborted)
        return;
    if (!g_aging.inited) {
        cali_resolve(CALI_INI_PATH, g_board.max_asic, g_workmode, 0, &g_aging.cp, NULL);
        pp.pll_interval = cp->pll_interval ? cp->pll_interval : FREQ_INTERVAL;
        aging_reset(st, (uint16_t)INIT_FREQ);
        aging_pll_fill(st->current_pll, &pp, cp->pll_start);
        asic_send_freq(cp->pll_start);
        g_aging.inited = 1;
        osif_log(OSIF_LOG_INFO, "aging mode %u started [%u %u] MHz", (unsigned)g_workmode,
                (unsigned)cp->pll_start, (unsigned)AGING_FREQ_CEILING(g_work_freq));
    }

    int pok = rail_controllable() && pw;

    mm_telem_t tel;
    mm_telem_get(&tel);
    aging_telem_t t = {
        .hashrate  = (int)(ghs * 1000.0),
        .pout      = (uint16_t)(pok ? pw->pout_w : 0),
        .voutcmd   = (short)(pok ? rail_get() : 0),
        .temp      = (short)tel.tavg,
        .power_ok  = pok,
        .max_pout  = pout_budget(cp->max_pout),
        .pll_start = cp->pll_start,
    };

    uint16_t       freq_out = 0;
    short          volt_out = 0;
    aging_action_t act      = aging_rule_tick(st, &pp, &t, &freq_out, &volt_out);
    set_aging_bar(st->progress);
    if (st->measured)
        osif_log(OSIF_LOG_OP, "aging %u: %.2f TH/s, %d W, pe %.2f J/TH, %u MHz, volt %d, t %d C",
                (unsigned)st->measured, st->last.hashrate_Mhz / 1e6, st->last.pout,
                st->last.pe / 1000.0, (unsigned)st->last.freq[pp.pll_offset], st->last.volt,
                st->last.temp);
    if (act == AGING_ACT_NONE)
        return;

    uint16_t hi = 0;
    for (int i = 0; i < AGING_PLL_COUNT; i++) {
        if (st->current_pll[i] > hi)
            hi = st->current_pll[i];
    }
    if (hi > AGING_FREQ_CEILING(g_work_freq)) {
        g_aging.aborted = 1;
        osif_log(OSIF_LOG_INFO,
                "aging stopped: %u MHz is above the %u MHz "
                "limit — back to the work frequency",
                (unsigned)hi, (unsigned)AGING_FREQ_CEILING(g_work_freq));
        aging_revert();
        return;
    }

    if (act == AGING_ACT_SET_PLL_BEST) {
        uint16_t best = freq_out;
        osif_log(OSIF_LOG_INFO,
                "aging best hash: %u MHz, volt %d, %d GH/s, "
                "%d W, pe %.2f J/TH, t %d C",
                (unsigned)st->best_rate.freq[pp.pll_offset], st->best_rate.volt,
                st->best_rate.hashrate_Mhz / 1000, st->best_rate.pout, st->best_rate.pe / 1000.0,
                st->best_rate.temp);
        osif_log(OSIF_LOG_INFO,
                "aging best pe: %u MHz, volt %d, %d GH/s, "
                "%d W, pe %.2f J/TH, t %d C",
                (unsigned)st->best_pe.freq[pp.pll_offset], st->best_pe.volt,
                st->best_pe.hashrate_Mhz / 1000, st->best_pe.pout, st->best_pe.pe / 1000.0,
                st->best_pe.temp);
        int volt_ok = volt_out > 0;
        int volt_up = volt_ok && (uint16_t)volt_out > rail_get();

        if (volt_up || (volt_ok && !best))
            rail_set_volt((uint16_t)volt_out);
        if (!best) {
            aging_revert();
            return;
        }
        if (!set_work_freq(best)) {
            osif_log(OSIF_LOG_ERR,
                    "aging best frequency %u MHz could not be "
                    "applied — chips stay at %u MHz",
                    (unsigned)best, (unsigned)g_work_freq);
            volt_ok = volt_up;
        } else if (volt_ok && !volt_up) {
            rail_set_volt((uint16_t)volt_out);
        }
        osif_log(OSIF_LOG_OP, "aging best point: %u MHz, volt %d%s", (unsigned)best, (int)volt_out,
                volt_ok ? "" : " (not applied)");
        return;
    }

    if (act == AGING_ACT_DONE) {
        g_aging.finished = 1;
        uint16_t best    = st->best_rate.freq[pp.pll_offset];
        if (!best) {
            g_aging.failed = 1;
            atomic_store(&g_aging_failed_req, 1);
            return;
        }
        set_aged_mode(g_workmode);
        set_aging_bar(100);
        if (best >= INIT_FREQ && best <= AGING_FREQ_CEILING(g_work_freq)) {
            cali_param done = {
                .max_pout     = cp->max_pout,
                .temp         = cp->temp,
                .volt         = (uint16_t)(st->best_rate.volt ? st->best_rate.volt : cp->volt),
                .pll_start    = best,
                .pll_interval = pp.pll_interval,
            };
            char buf[CALI_STR_MAX];
            if (cali_format(&done, buf, sizeof buf) > 0 &&
                    cali_set(CALI_INI_PATH, g_workmode, 0, buf) == 0)
                osif_log(OSIF_LOG_INFO, "aging mode %u finished — result saved (%s)",
                        (unsigned)g_workmode, buf);
            else
                osif_log(OSIF_LOG_ERR, "aging finished but the result could not be saved");
        }
        return;
    }

    asic_send_freq(st->current_pll[pp.pll_offset]);
    osif_log(OSIF_LOG_DBG, "aging mode %u: testing %u/%u/%u/%u MHz, %u%% done",
            (unsigned)g_workmode, st->current_pll[0], st->current_pll[1], st->current_pll[2],
            st->current_pll[3], st->progress);
}

static void policy_seed(const mm_telem_t *tel)
{
    uint8_t    mode = mm_workmode_running();
    cali_param cp;
    cali_resolve(CALI_INI_PATH, g_board.max_asic, mode, 0, &cp, NULL);
    int base    = tel->pll_mhz[0];
    int chip_hi = 0;
    for (int k = 0; k < MM_PLL_POINTS; k++) {
        if (tel->pll_mhz[k] > chip_hi)
            chip_hi = tel->pll_mhz[k];
    }
    int mode_hi        = cp.pll_start + (ASIC_PLL_POINTS - 1) * g_board.freq_interval;
    g_work_freq        = base > 0 ? (uint16_t)base : (uint16_t)INIT_FREQ;
    g_ramp_at_sent     = 0;
    uint16_t seed_volt = cp.volt;
    if (chip_hi > mode_hi) {
        for (uint8_t m = 0; m < CALI_MODE_MAX; m++) {
            cali_param mp;

            if (!(CALI_MODE_BOARD_MASK & (1u << m)))
                continue;
            cali_resolve(CALI_INI_PATH, g_board.max_asic, m, 0, &mp, NULL);
            if (mp.volt > seed_volt)
                seed_volt = mp.volt;
        }
    }
    pthread_mutex_lock(&g_vf_lock);
    if (!g_seed_respawn && g_rail_known && g_rail_mv > seed_volt)
        seed_volt = g_rail_mv;
    pthread_mutex_unlock(&g_vf_lock);
    rail_set_volt(seed_volt);
    g_workmode     = MODE_UNSET;
    g_mode_settled = 1;
    if (g_seed_respawn)
        g_freq_held = 0;
    g_seed_respawn       = 0;
    g_switch_ramp_base   = 0;
    uint8_t aborted_mode = g_aging.aborted ? g_aging.mode : MODE_UNSET;
    memset(&g_aging, 0, sizeof g_aging);
    if (aborted_mode != MODE_UNSET) {
        g_aging.mode    = aborted_mode;
        g_aging.aborted = 1;
    }
    g_mode_announced = mode;
    osif_log(OSIF_LOG_OP, "work mode %u: need %u->%u MHz, volt %u", (unsigned)mode, g_work_freq,
            cp.pll_start, seed_volt);
}

static void aging_cadence(double ghs, const board_power *pw)
{
    pthread_mutex_lock(&g_vf_lock);
    int aged = g_aged_mode == g_workmode;
    pthread_mutex_unlock(&g_vf_lock);

    if (aged || !(g_mode_settled && mode_needs_aging(g_workmode))) {
        g_aging.primed = 0;
        return;
    }
    uint32_t now = (uint32_t)osif_now_ms();
    if (!g_aging.primed) {
        g_aging.next_at = now + AGING_FIRST_RUN_MS;
        g_aging.primed  = 1;
        return;
    }
    if ((int32_t)(now - g_aging.next_at) >= 0) {
        g_aging.next_at = now + AGING_PERIOD_MS;
        if (ghs > 0.0)
            aging_tick(ghs, pw);
    }
}

/* One Canaan PLL-interval step for the whole A3197S board, after aging or cali. */
static void autotune_cadence(const board_power *pw)
{
    static uint32_t next_at;
    int             mode = mm_tune_autotune();

    if (mode != AUTOTUNE_MILD) {
        atomic_store(&g_tuned_freq, 0);
        next_at = 0;
        return;
    }
    if (g_freq_held || !g_mode_settled || aging_running() || !g_work_freq)
        return;

    uint32_t now = (uint32_t)osif_now_ms();
    if (next_at && (int32_t)(now - next_at) < 0)
        return;
    next_at = now + AUTOTUNE_PERIOD_MS;

    cali_param cp;
    uint8_t    aged = 0;
    cali_resolve(CALI_INI_PATH, g_board.max_asic, g_workmode, 0, &cp, &aged);
    mm_telem_t tel;
    mm_telem_get(&tel);
    uint16_t interval = cp.pll_interval ? cp.pll_interval : g_board.freq_interval;
    autotune_in in = {
        .mode         = mode,
        .settled      = aged || g_aged_mode == g_workmode,
        .pll_start    = cp.pll_start,
        .pll_interval = interval,
        .freq         = g_work_freq,
        .temp_known   = tel.tmax > ASIC_TEMP_NONE,
        .temp_c       = tel.tmax,
        .temp_target  = cp.temp,
        .temp_fan_max = FAN_PID_TEMP_MAX,
        .temp_trip    = PVT_TEMP_OVER_HR_MAX,
        .power_known  = pw != NULL,
        .pout_w       = pw ? pw->pout_w : 0,
        .pout_cap_w   = pout_budget(cp.max_pout),
    };
    autotune_out out;
    autotune_step(&in, &out);
    if (!out.step || out.freq == g_work_freq)
        return;
    if (!set_work_freq(out.freq))
        return;
    g_tgt_freq = out.freq;
    atomic_store(&g_tuned_freq, out.freq);
    osif_log(OSIF_LOG_OP,
            "autotune %s to %u MHz on the A3197S board (Canaan PLL interval %u)",
            out.step > 0 ? "up" : "down", (unsigned)out.freq, (unsigned)interval);
}

static int power_poll(board_power *pw)
{
    if (board_power_read(pw) != 0)
        return -1;
    pthread_mutex_lock(&g_vf_lock);
    g_power    = *pw;
    g_power_at = osif_now_ms();
    pthread_mutex_unlock(&g_vf_lock);
    return 0;
}

static uint32_t vf_tick(int *was_live, int *pre_work)
{
    board_power        pw;
    const board_power *sample = power_poll(&pw) == 0 ? &pw : NULL;
    int                bt;
    if (board_ntc_read(&bt) == 0) {
        pthread_mutex_lock(&g_vf_lock);
        g_board_temp    = (int16_t)bt;
        g_board_temp_at = osif_now_ms();
        pthread_mutex_unlock(&g_vf_lock);
    }
    /* Taken before the telemetry read: spawn_asic() drops the previous
     * instance's telemetry before it raises the flag. */
    if (atomic_exchange(&g_asic_respawned, 0)) {
        *was_live      = 0;
        g_seed_respawn = 1;
        atomic_store(&g_volt_req, 0);
        CTRL_PLUGIN_VF_RESPAWN();
    }
    mm_telem_t tel;
    mm_telem_get(&tel);
    int    idle       = mm_asic_stopped();
    int    asic_ready = !idle && tel.asic_live && tel.asic_chips > 0;
    int    in_work    = tel.in_work;
    double ghs        = tel.ghs_spd;

    if (asic_ready) {
        if (!*was_live) {
            policy_seed(&tel);
            *was_live = 1;
        }
        aging_revert_retry();
        if (in_work) {
            volt_req_apply();
            CTRL_PLUGIN_VF_TICK();
            if (!mode_tick())
                volt_reg_tick(sample);
            aging_cadence(ghs, sample);
            autotune_cadence(sample);
        } else {
            uint16_t req;
            uint8_t  at;

            ramp_get(&req, &at);
            if (!at)
                g_ramp_at_sent = 0;
            if (req && (req != g_work_freq || (at && !g_ramp_at_sent)) && asic_send_vf(req, at)) {
                g_work_freq    = req;
                g_tgt_freq     = req;
                g_ramp_at_sent = at;
            }
        }
        *pre_work = !in_work;
    } else {
        *was_live = 0;
    }
    atomic_store(&g_aging_on, aging_running());
    if (*pre_work)
        return PRE_WORK_TICK_MS;
    return g_mode_settled ? POLICY_TICK_MS : MODE_STEP_TICK_MS;
}

static void *vf_thread(void *arg)
{
    (void)arg;
    int was_live = 0, pre_work = 0;
    while (atomic_load(&g_vf_run)) {
        atomic_store(&g_vf_beat_ms, osif_now_ms());
        osif_sleep_ms(vf_tick(&was_live, &pre_work));
    }
    return NULL;
}

uint64_t mm_vf_beat_ms(void)
{
    return atomic_load(&g_vf_beat_ms);
}

void mm_vf_snapshot(mm_periph *p)
{
    if (!p)
        return;
    uint8_t mode = mm_workmode_running();
    pthread_mutex_lock(&g_vf_lock);
    uint64_t now      = osif_now_ms();
    p->aging_bar      = g_aging_bar;
    p->aging_finished = (uint8_t)(g_aged_mode == mode);
    p->rail_mv        = g_rail_mv;
    p->board_temp     = (g_board_temp_at && now - g_board_temp_at <= VF_SENSOR_FRESH_MS) ?
                g_board_temp :
                ASIC_TEMP_NONE;
    p->ps_valid       = (uint8_t)(g_power_at && now - g_power_at <= VF_SENSOR_FRESH_MS);
    p->ps_err         = g_power.err;
    p->ps_vout_mv     = g_power.vout_mv;
    p->ps_iout_ma     = g_power.iout_ma;
    p->ps_pout_w      = g_power.pout_w;
    p->ps_voutcmd_mv  = g_power.voutcmd_mv;
    pthread_mutex_unlock(&g_vf_lock);
}

void mm_vf_start(void)
{
    for (uint8_t m = 0; m < CALI_MODE_MAX; m++) {
        if (!(CALI_MODE_BOARD_MASK & (1u << m)))
            continue;
        for (int8_t l = 0; l < CALI_LEVEL_COUNT; l++) {
            char raw[CALI_STR_MAX] = "";
            cali_get(CALI_INI_PATH, m, l, raw, sizeof raw);
            osif_log(OSIF_LOG_INFO, "cali M:%u L:%d %s", m, l, raw[0] ? raw : "none");
        }
    }
    board_power pw0;
    if (board_sensors_init() == 0 && board_power_read(&pw0) == 0 && pw0.voutcmd_mv)
        g_rail_mv = pw0.voutcmd_mv;
    if (!g_rail_mv)
        osif_log(OSIF_LOG_WARN, "volt could not be read at start");
    atomic_store(&g_vf_run, 1);
    if (pthread_create(&g_vf_thread, NULL, vf_thread, NULL) == 0)
        g_vf_started = 1;
    else
        osif_log(OSIF_LOG_ERR, "V/F ctrl did not start (thread creation failed)");
}

void mm_vf_stop_request(void)
{
    atomic_store(&g_vf_run, 0);
}

void mm_vf_stop(void)
{
    atomic_store(&g_vf_run, 0);
    if (g_vf_started) {
        pthread_join(g_vf_thread, NULL);
        g_vf_started = 0;
    }
    atomic_store(&g_vf_beat_ms, 0);
}
