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

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "boardconf.h"
#include "osif.h"
#include "cali.h"
#include BOARD_CALI_HEADER
#include "asic_temp_pub.h"
#include "board_sensors.h"
#include "eeprom_dev.h"
#include "mm_telem.h"
#include "hashmcu.h"
#include "mm_led.h"
#include "mm_system.h"
#include "mm_ota.h"
#include "mc_api_bootby.h"
#include "mm_cfgstore.h"
#include "mm_factory.h"
#include "board_desc.h"
#include "psu_ops.h"
#include "temp_ops.h"
#include "net_ops.h"
#include "mm_model.h"
#include "tst_board_expect.h"

/* Weak mc_set_result setter taking a text argument; always MC_SET_ERR_UNSUP */
#define MM_APPLY_STUB_TEXT(name) \
    __attribute__((weak)) mc_set_result name(const char *a) \
    { \
        (void)a; \
        return MC_SET_ERR_UNSUP; \
    }

/* Board descriptor seeded from the compiled boardconf constants */
board_desc_t g_board = {
    .hboard_count  = HBOARD_COUNT,
    .max_asic      = MAX_ASIC_ON_SINGLE_HASH,
    .fan_count     = FAN_COUNT,
    .freq_interval = FREQ_INTERVAL,
};
uint16_t tst_rail_mv; /* last mm_vf_set_rail() value after VOL_MIN..VOL_MAX clamp, rail counts */
int      stub_vf_asic_spawned_n;
uint64_t tst_vf_beat_ms; /* mm_vf_beat_ms() value, 0: V/F thread not running */
int      stub_telem_asic_reset_n;
unsigned stub_telem_asic_reset_conn; /* last mm_telem_asic_reset() argument */
unsigned stub_ipc_conn_seq;          /* osif_ipc_conn_seq() result */
int      stub_reboot_n;              /* mm_system_reboot() call count */
uint8_t  stub_reboot_reason;         /* last mm_system_reboot() reason, MC_BOOTBY_* */
uint32_t stub_reboot_sub;            /* last mm_system_reboot() sub-code */
int      stub_poweroff_n;            /* mm_system_poweroff() call count */
__attribute__((weak)) atomic_int g_fan_dev_ok;

void board_desc_init(const board_desc_t *seed)
{
    if (seed)
        g_board = *seed;
}
static const cali_param *tst_cali_pout(uint8_t mode)
{
    static const struct {
        uint16_t   hash_asics;
        cali_param param[CALI_MODE_MAX];
    } rows[] = { BOARD_CALI_ROWS };
    static cali_param pout[CALI_MODE_MAX];
    uint8_t           m = mode < CALI_MODE_MAX ? mode : 0;

    pout[m].max_pout = rows[0].param[m].max_pout;
    return &pout[m];
}
static int stub_pout_limit(uint16_t *watts)
{
    unsigned top = tst_cali_pout(CALI_MODE_MAX - 1)->max_pout + PD_POWER_RESERVE_W;

    *watts = (uint16_t)(top > 140 ? top : 140);
    return 0;
}
__attribute__((weak)) const hal_psu_ops_t *hal_psu(void)
{
    static const hal_psu_ops_t psu = {
        .pout_limit = stub_pout_limit,
        .vout_fault = tst_psu_vout_fault,
        .flags      = TST_PSU_FLAGS,
    };
    return &psu;
}
__attribute__((weak)) const hal_temp_ops_t *hal_temp(void)
{
    return NULL;
}
__attribute__((weak)) const hal_net_ops_t *hal_net(void)
{
    return NULL;
}

int mm_model_check(const char *hardware, const char *software, char *env_hw, char *env_sw,
        size_t cap)
{
    (void)hardware;
    (void)software;
    if (cap) {
        env_hw[0] = 0;
        env_sw[0] = 0;
    }
    return -1;
}
int mc_api_identity_mac(char out[18])
{
    strcpy(out, "001122334455");
    return 0;
}

__attribute__((weak)) void mm_cfgwr_webpass_set(const char *hex)
{
    (void)hex;
}
__attribute__((weak)) void mm_cfgwr_webpass_get(char *out, size_t cap)
{
    if (out && cap)
        out[0] = 0;
}

__attribute__((weak)) void mm_cfgwr_hwcfg_set(const char *prod, const char *model)
{
    (void)prod;
    (void)model;
}
__attribute__((weak)) void mm_cfgwr_hwcfg_get(char *prod, size_t prodcap, char *model,
        size_t modelcap)
{
    if (prod && prodcap)
        prod[0] = 0;
    if (model && modelcap)
        model[0] = 0;
}
__attribute__((weak)) int mm_cfgwr_activation_get(void)
{
    return 0;
}
__attribute__((weak)) int mm_cfgwr_pool_configured(void)
{
    return 0;
}
__attribute__((weak)) void mm_cfgwr_seed(const char *systemcfg_path)
{
    (void)systemcfg_path;
}

__attribute__((weak)) void mm_factory_boot_seed(void)
{
}
__attribute__((weak)) void mm_factory_boot_fill(void)
{
}
__attribute__((weak)) int mm_tune_autotune(void)
{
    return 0;
}
__attribute__((weak)) int mm_tune_aftermarket(void)
{
    return 0;
}
__attribute__((weak)) int mm_cfgstore_get(const char *section, const char *key, char *buf,
        size_t cap)
{
    (void)section;
    (void)key;
    (void)buf;
    (void)cap;
    return -1;
}
__attribute__((weak)) int mm_cfgstore_set(const char *section, const char *key, const char *value)
{
    (void)section;
    (void)key;
    (void)value;
    return 0;
}
__attribute__((weak)) int mm_cfgstore_put(const char *section, const char *key, const char *value)
{
    (void)section;
    (void)key;
    (void)value;
    return 0;
}
__attribute__((weak)) int mm_cfgstore_flush(void)
{
    return 0;
}
__attribute__((weak)) int mm_cfgstore_sync(void)
{
    return 0;
}
__attribute__((weak)) int mm_cfgstore_frozen(void)
{
    return 0;
}
__attribute__((weak)) int mm_factory_in_progress(void)
{
    return 0;
}

osif_status_t osif_ipc_init(osif_ipc_t **h, osif_ipc_recv_cb cb, const char *path, int is_server)
{
    (void)cb;
    (void)path;
    (void)is_server;
    if (h)
        *h = NULL;
    return OSIF_OK;
}
void osif_ipc_deinit(osif_ipc_t *h)
{
    (void)h;
}
osif_status_t osif_ipc_send(osif_ipc_t *h, uint32_t type, const void *data, int len)
{
    (void)h;
    (void)type;
    (void)data;
    (void)len;
    return OSIF_OK;
}
__attribute__((weak)) void osif_log(osif_loglvl_t lvl, const char *fmt, ...)
{
    (void)lvl;
    (void)fmt;
}
int osif_log_op_sink_open(void)
{
    return -1;
}

#ifndef TEST_RUNS_SUPERVISOR_LOOP
uint64_t osif_now_ms(void)
{
    return 0;
}
void osif_sleep_ms(uint32_t ms)
{
    (void)ms;
}
#endif /* !TEST_RUNS_SUPERVISOR_LOOP */

__attribute__((weak)) int tst_children_stop(const pid_t *pids, int n, unsigned term_ms,
        unsigned kill_ms)
{
    (void)pids;
    (void)n;
    (void)term_ms;
    (void)kill_ms;
    return 0;
}
int mm_children_stop(const pid_t *pids, int n, unsigned term_ms, unsigned kill_ms)
{
    return tst_children_stop(pids, n, term_ms, kill_ms);
}

#ifndef TEST_RUNS_SUPERVISOR_LOOP
void mm_telem_poll(void)
{
}
void mm_telem_ingest(const void *d, int l, unsigned c)
{
    (void)d;
    (void)l;
    (void)c;
}
void mm_telem_ingest_core(const void *d, int l)
{
    (void)d;
    (void)l;
}
#endif /* !TEST_RUNS_SUPERVISOR_LOOP */

int mm_hashboard_bringup(void)
{
    return 0;
}

void beep_volume(int pct)
{
    (void)pct;
}
int board_power_set_volt(uint16_t mv)
{
    (void)mv;
    return 0;
}
int i2c_init(uint8_t id)
{
    (void)id;
    return 0;
}
__attribute__((weak)) int eeprom_init(void)
{
    return -1;
}
__attribute__((weak)) int eeprom_read(uint8_t a, uint8_t *b, uint8_t n, uint8_t id)
{
    (void)a;
    (void)b;
    (void)n;
    (void)id;
    return -1;
}
__attribute__((weak)) int board_power_read(board_power *o)
{
    if (o)
        memset(o, 0, sizeof *o);
    return -1;
}
__attribute__((weak)) int board_power_rail_on(uint16_t counts)
{
    (void)counts;
    return HAL_PSU_RAIL_OK;
}
__attribute__((weak)) int board_power_rail_off(void)
{
    return HAL_PSU_RAIL_OK;
}
__attribute__((weak)) const char *hal_psu_rail_strerror(int rc)
{
    (void)rc;
    return "stub";
}
__attribute__((weak)) int board_ntc_read(int *degc)
{
    (void)degc;
    return -1;
}
__attribute__((weak)) int fan_dev_get_rpm(int fan)
{
    (void)fan;
    return -1;
}
__attribute__((weak)) int osif_ipc_connected(osif_ipc_t *h)
{
    (void)h;
    return 1;
}
__attribute__((weak)) unsigned osif_ipc_conn_seq(osif_ipc_t *h)
{
    (void)h;
    return stub_ipc_conn_seq;
}
__attribute__((weak)) void mm_vf_set_rail(uint16_t mv)
{
    if (mv < VOL_MIN)
        mv = VOL_MIN;
    if (mv > VOL_MAX)
        mv = VOL_MAX;
    tst_rail_mv = mv;
}

__attribute__((weak)) void mm_vf_rail_unknown(void)
{
}
__attribute__((weak)) void mm_vf_start(void)
{
}
__attribute__((weak)) void mm_vf_stop_request(void)
{
}
__attribute__((weak)) void mm_vf_stop(void)
{
}
__attribute__((weak)) uint64_t mm_vf_beat_ms(void)
{
    return tst_vf_beat_ms;
}
__attribute__((weak)) void mm_vf_set_ramp_target(uint16_t b, uint8_t t)
{
    (void)b;
    (void)t;
}
__attribute__((weak)) void mm_vf_asic_spawned(void)
{
    stub_vf_asic_spawned_n++;
}
__attribute__((weak)) void mm_telem_asic_reset(unsigned c)
{
    stub_telem_asic_reset_conn = c;
    stub_telem_asic_reset_n++;
}
__attribute__((weak)) void mm_vf_snapshot(mm_periph *p)
{
    (void)p;
}
__attribute__((weak)) int mm_vf_take_aging_failed(void)
{
    return 0;
}
__attribute__((weak)) void mm_vf_aging_rearm(void)
{
}

void mm_led_start(void)
{
}
void mm_led_stop(void)
{
}
mc_set_result mm_led_apply(uint16_t op, const char *args)
{
    (void)op;
    (void)args;
    return MC_SET_ERR_UNSUP;
}
void mm_led_state(uint8_t *ledmode, uint8_t *runeffect)
{
    if (ledmode)
        *ledmode = 0;
    if (runeffect)
        *runeffect = 0;
}
void mm_led_snapshot(mm_led_cfg_t *cfg, uint8_t *effect)
{
    *cfg    = mm_led_cfg_default;
    *effect = 1;
}
int mm_led_wake(void)
{
    return 0;
}

__attribute__((weak)) void mm_bootby_recover(int wdt_bootstatus, uint8_t *out_reason,
        uint32_t *out_sub)
{
    (void)wdt_bootstatus;
    if (out_reason)
        *out_reason = MC_BOOTBY_POWRON;
    if (out_sub)
        *out_sub = 0;
}

__attribute__((weak)) int mm_bootby_stage(uint8_t reason, uint32_t sub)
{
    (void)reason;
    (void)sub;
    return 0;
}

const char *mc_bootby_string(uint8_t reason)
{
    (void)reason;
    return "";
}

__attribute__((weak)) int mm_system_reboot(uint8_t reason, uint32_t sub)
{
    stub_reboot_n++;
    stub_reboot_reason = reason;
    stub_reboot_sub    = sub;
    return 0;
}
__attribute__((weak)) int mm_system_poweroff(void)
{
    stub_poweroff_n++;
    return 0;
}

__attribute__((weak)) int cali_get(const char *path, uint8_t mode, int8_t level, char *out,
        size_t cap)
{
    (void)path;
    (void)mode;
    (void)level;
    if (out && cap)
        out[0] = '\0';
    return -1;
}
__attribute__((weak)) int cali_parse(const char *str, cali_param *out)
{
    (void)str;
    if (out)
        memset(out, 0, sizeof *out);
    return -1;
}
__attribute__((weak)) uint8_t cali_workmode_get(const cali_wm_store *s)
{
    (void)s;
    return 0;
}
__attribute__((weak)) int cali_workmode_set(const cali_wm_store *s, uint8_t mode)
{
    (void)s;
    (void)mode;
    return 0;
}
int cali_resolve(const char *path, uint16_t asic_cnt, uint8_t mode, int8_t level, cali_param *out,
        uint8_t *aged)
{
    (void)path;
    (void)asic_cnt;
    (void)level;
    if (out)
        *out = *tst_cali_pout(mode);
    if (aged)
        *aged = 0;
    return 0;
}
__attribute__((weak)) const cali_param *cali_default_param(uint16_t asic_cnt, uint8_t mode)
{
    (void)asic_cnt;
    return tst_cali_pout(mode);
}

__attribute__((weak)) void fan_rule(void *ctx, uint64_t now_ms)
{
    (void)ctx;
    (void)now_ms;
}
__attribute__((weak)) void logrot_rule(void *ctx, uint64_t now_ms)
{
    (void)ctx;
    (void)now_ms;
}
__attribute__((weak)) void mm_tz_restore(void)
{
}
__attribute__((weak)) void mm_dev_name(char *o, size_t n)
{
    if (o && n)
        o[0] = 0;
}
__attribute__((weak)) void net_discover_start(const char *d)
{
    (void)d;
}

MM_APPLY_STUB_TEXT(mm_apply_settz)
__attribute__((weak)) void mm_fan_target_apply(uint8_t m)
{
    (void)m;
}

__attribute__((weak)) int mm_ota_start(void)
{
    return 0;
}

__attribute__((weak)) int mm_cfgwr_start(const char *pools_path, const char *systemcfg_path)
{
    (void)pools_path;
    (void)systemcfg_path;
    return 0;
}

__attribute__((weak)) void mm_efuse_asic_spawned(void)
{
}
__attribute__((weak)) void mm_efuse_ingest(const void *data, int len)
{
    (void)data;
    (void)len;
}
__attribute__((weak)) void mm_efuse_rule(void *ctx, uint64_t now_ms)
{
    (void)ctx;
    (void)now_ms;
}
