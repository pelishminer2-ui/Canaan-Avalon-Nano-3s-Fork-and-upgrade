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

#ifndef MM_VF_HARNESS_H
#define MM_VF_HARNESS_H

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "osif.h"
#include "miner_ipc.h"
#include "board_sensors.h"
#include "mm_telem.h"
#include "psu_ops.h"
#include "mc_paths.h"
#include "cali.h"
#include "../../tests/include/tst_check.h"

#undef CALI_INI_PATH
/* Overridden to a per-board scratch calibration file in the build tree */
#define CALI_INI_PATH TST_BIN_DIR "/mm_vf_cali_" TST_BOARD ".ini"
/* Default-mode start frequency of the default calibration, MHz */
#define WORK_FREQ (cali_default_param(MAX_ASIC_ON_SINGLE_HASH, CALI_MODE_DEFAULT)->pll_start)
/* Middle of the VOL_MIN..VOL_MAX rail range */
#define RAIL_MID ((uint16_t)((VOL_MIN + VOL_MAX) / 2))

__attribute__((weak)) int mm_tune_autotune(void)
{
    return 0;
}
__attribute__((weak)) int mm_tune_aftermarket(void)
{
    return 0;
}

#include "mm_vf.c"

static int      g_vf_sends;                /* MSG_ASIC_VF sends seen */
static uint16_t g_vf_last;                 /* freq[0] of the last MSG_ASIC_VF, MHz */
static int      g_vf_zero_sends;           /* MSG_ASIC_VF sends with freq[0] == 0 */
static uint8_t  g_vf_at;                   /* at_target of the last MSG_ASIC_VF */
static uint16_t g_vf_pts[ASIC_PLL_POINTS]; /* PLL point frequencies of the last MSG_ASIC_VF, MHz */
static int      g_send_fail;               /* Non-zero makes MSG_ASIC_VF sends fail */
static uint64_t g_now_ms = 1000;           /* Simulated clock behind osif_now_ms(), ms */
static board_power   g_psu;                /* Reading board_power_read() returns */
static int           g_psu_rc;             /* Value board_power_read() returns */
static int           g_reads, g_writes;    /* board_power_read() and board_power_set_volt() calls */
static hal_psu_ops_t tst_psu;              /* PSU ops hal_psu() returns */
osif_ipc_t          *g_ipc_asic_mm;        /* asic IPC handle; NULL, osif_ipc_send() ignores it */
static mm_telem_t    g_tel;                /* Telemetry mm_telem_get() returns */
static char          g_ord[16]; /* rail writes (V) and MSG_ASIC_VF sends (F), in call order */
static int           g_ord_n;
static int           tst_idle;                     /* Value mm_asic_stopped() returns */
static uint8_t       tst_mode = CALI_MODE_DEFAULT; /* Work mode mm_workmode_running() returns */
static uint16_t      tst_pout;                     /* Value mm_power_budget() returns */

static void ord_add(char c)
{
    if (g_ord_n < (int)sizeof g_ord - 1)
        g_ord[g_ord_n++] = c;
    g_ord[g_ord_n] = '\0';
}

static void ord_reset(void)
{
    g_ord_n  = 0;
    g_ord[0] = '\0';
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
osif_status_t osif_ipc_send(osif_ipc_t *h, uint32_t type, const void *data, int len)
{
    (void)h;
    if (type == MSG_ASIC_VF && g_send_fail)
        return OSIF_ERR;
    if (type == MSG_ASIC_VF && data && len >= (int)sizeof(mc_asic_vf)) {
        mc_asic_vf vf;
        memcpy(&vf, data, sizeof vf);
        g_vf_last = vf.freq[0];
        if (vf.freq[0] == 0)
            g_vf_zero_sends++;
        g_vf_at = vf.at_target;
        memcpy(g_vf_pts, vf.freq, sizeof g_vf_pts);
        g_vf_sends++;
        ord_add('F');
    }
    return OSIF_OK;
}
void osif_log(osif_loglvl_t lvl, const char *fmt, ...)
{
    (void)lvl;
    (void)fmt;
}
void osif_sleep_ms(uint32_t ms)
{
    (void)ms;
}
uint64_t osif_now_ms(void)
{
    return g_now_ms;
}

int board_sensors_init(void)
{
    return 0;
}
int board_power_read(board_power *out)
{
    g_reads++;
    if (out)
        *out = g_psu;
    return g_psu_rc;
}
int board_power_set_volt(uint16_t mv)
{
    (void)mv;
    g_writes++;
    ord_add('V');
    return 0;
}
int board_ntc_read(int *degc)
{
    if (degc)
        *degc = 30;
    return 0;
}

static int tst_set_volt(uint16_t units)
{
    (void)units;
    return 0;
}
const hal_psu_ops_t *hal_psu(void)
{
    return &tst_psu;
}

void mm_telem_get(mm_telem_t *o)
{
    if (o)
        *o = g_tel;
}

int mm_asic_stopped(void)
{
    return tst_idle;
}
uint8_t mm_workmode_running(void)
{
    return tst_mode;
}
uint16_t mm_power_budget(void)
{
    return tst_pout;
}

static void psu_set(uint16_t pout_w, uint16_t voutcmd)
{
    memset(&g_psu, 0, sizeof g_psu);
    g_psu.vout_mv    = RAIL_MID;
    g_psu.iout_ma    = 100;
    g_psu.pout_w     = pout_w;
    g_psu.voutcmd_mv = voutcmd;
    g_psu.err        = 0;
    g_psu_rc         = 0;
}

#endif /* MM_VF_HARNESS_H */
