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

#include "mm_tune.h"

#include "ini.h"
#include "mc_paths.h"
#include "osif.h"

#include <pthread.h>
#include <stdatomic.h>

#define TUNE_REREAD_MS 2000u /* systemcfg.ini refresh period, ms */

static pthread_mutex_t g_tune_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int     g_autotune;
static _Atomic int     g_aftermarket;
static uint64_t        g_tune_at;

static int flag_is_on(const char *s)
{
    return s && s[0] == '1' && s[1] == '\0';
}

static void tune_refresh(void)
{
    uint64_t now = osif_now_ms();

    pthread_mutex_lock(&g_tune_lock);
    if (g_tune_at && now - g_tune_at < TUNE_REREAD_MS) {
        pthread_mutex_unlock(&g_tune_lock);
        return;
    }
    g_tune_at = now ? now : 1;
    pthread_mutex_unlock(&g_tune_lock);

    int    autotune = 0, aftermarket = 0;
    ini_t *ini = ini_load(MC_SYSTEMCFG_DEFAULT);
    if (ini) {
        autotune    = flag_is_on(ini_get(ini, "syscfg", "autotune"));
        aftermarket = flag_is_on(ini_get(ini, "syscfg", "aftermarket_psu"));
        ini_free(ini);
    }
    atomic_store(&g_autotune, autotune);
    atomic_store(&g_aftermarket, aftermarket);
}

int mm_tune_autotune(void)
{
    tune_refresh();
    return atomic_load(&g_autotune);
}

int mm_tune_aftermarket(void)
{
    tune_refresh();
    return atomic_load(&g_aftermarket);
}
