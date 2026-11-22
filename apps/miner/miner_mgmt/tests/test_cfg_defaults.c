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

#include <stdlib.h>
#include <string.h>

#include "boardconf.h"
#include "cali.h"
#include "ctrl_cmds.h"
#include "ini.h"
#include "mc_api_bootby.h"
#include "mm_factory.h"
#include "mm_led_cfg.h"
#include "ui_types.h"

#include "../../tests/include/tst_check.h"

#define SYSTEMCFG  FACTORY_SEED_DIR "/systemcfg.ini"
#define ACTIVATION FACTORY_SEED_DIR "/activation.ini"
#define CALI_INI   FACTORY_SEED_DIR "/../factory/hashrate_cali.ini"

/* One systemcfg.ini key the miner reads */
struct cfg_key {
    const char *sect, *key;
    int         may_be_empty; /* 1: an empty factory value is allowed */
    int         feature;      /* FEAT_*: the board feature the key belongs to */
};

enum { FEAT_BASE, FEAT_LED, FEAT_FILTER };

/* systemcfg.ini keys the miner reads on this board */
static const struct cfg_key g_keys[] = {
    { "syscfg", "work_mode", 0, FEAT_BASE },
    { "syscfg", "webuser", 0, FEAT_BASE },
    { "syscfg", "webpass", 0, FEAT_BASE },
    { "syscfg", "reason", 0, FEAT_BASE },
    { "syscfg", "subreason", 0, FEAT_BASE },
    { "syscfg", "ledmode", 0, FEAT_LED },
    { "syscfg", "timezone", 0, FEAT_BASE },
    { "hwcfg", "prod", 0, FEAT_BASE },
    { "hwcfg", "model", 0, FEAT_BASE },
    { "led", "effect", 0, FEAT_LED },
    { "led", "bright", 0, FEAT_LED },
    { "led", "temper", 0, FEAT_LED },
    { "led", "rgb", 0, FEAT_LED },
    { "misc", "filtermesh", 0, FEAT_FILTER },
#if CAP_LCD
    { "lcd", "id", 0, FEAT_BASE },
    { "lcd", "onoff", 0, FEAT_BASE },
    { "audio", "volume", 0, FEAT_BASE },
#endif
#if NET_WIRED_ALT
    { "net", "wifi_eth", 0, FEAT_BASE },
#endif
};

static ini_t *g_sys; /* parsed board factory systemcfg.ini */

static int board_has_op(uint16_t op)
{
    static const uint16_t ops[] = { BOARD_CTRL_OPS };

    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++)
        if (ops[i] == op)
            return 1;
    return 0;
}

static int feature_on(int feature)
{
    if (feature == FEAT_LED)
        return CAP_LED_STRIP;
    if (feature == FEAT_FILTER)
        return board_has_op(CTRL_OP_FILTER_CLEAN);
    return 1;
}

static int num_is(const char *sect, const char *key, unsigned long want)
{
    const char *v = ini_get(g_sys, sect, key);
    char       *end;

    return v && *v && strtoul(v, &end, 10) == want && !*end;
}

static int str_is(const char *sect, const char *key, const char *want)
{
    const char *v = ini_get(g_sys, sect, key);

    return v && !strcmp(v, want);
}

int main(void)
{
    g_sys = ini_load(SYSTEMCFG);
    CHECK(g_sys != NULL, "%s loads", SYSTEMCFG);
    if (!g_sys)
        return tst_done();

    for (size_t i = 0; i < sizeof g_keys / sizeof g_keys[0]; i++) {
        const char *v = ini_get(g_sys, g_keys[i].sect, g_keys[i].key);

        if (!feature_on(g_keys[i].feature))
            CHECK(!v, "[%s] %s is absent", g_keys[i].sect, g_keys[i].key);
        else
            CHECK(v && (v[0] || g_keys[i].may_be_empty), "[%s] %s has a value (\"%s\")",
                    g_keys[i].sect, g_keys[i].key, v ? v : "(missing)");
    }

    CHECK(num_is("syscfg", "work_mode", WORK_MODE_DEFAULT_ID),
            "work_mode is WORK_MODE_DEFAULT_ID %u", (unsigned)WORK_MODE_DEFAULT_ID);
    CHECK(num_is("syscfg", "reason", MC_BOOTBY_POWRON) && num_is("syscfg", "subreason", 0),
            "reason and subreason are the power-on boot reason");
    CHECK(str_is("syscfg", "timezone", FACTORY_TIMEZONE), "timezone is FACTORY_TIMEZONE %s",
            FACTORY_TIMEZONE);
    if (feature_on(FEAT_LED))
        CHECK(num_is("syscfg", "ledmode", mm_led_cfg_default.ledmode) &&
                        num_is("led", "effect", mm_led_cfg_default.effect) &&
                        num_is("led", "bright", mm_led_cfg_default.bright) &&
                        num_is("led", "temper", mm_led_cfg_default.temper) &&
                        num_is("led", "rgb", mm_led_cfg_default.rgb),
                "ledmode and [led] are mm_led_cfg_default");
    if (feature_on(FEAT_FILTER))
        CHECK(num_is("misc", "filtermesh", 0), "filtermesh is 0");
    CHECK(str_is("hwcfg", "prod", PROD_NAME) && str_is("hwcfg", "model", MODEL),
            "prod and model are PROD_NAME %s and MODEL %s", PROD_NAME, MODEL);
#if CAP_LCD
    CHECK(num_is("lcd", "id", UI_SUMMARY) && num_is("lcd", "onoff", 1) &&
                    num_is("audio", "volume", 100)
#if NET_WIRED_ALT
                            && num_is("net", "wifi_eth", 0)
#endif
                    ,
            "[lcd] and [audio] are the display defaults");
#endif
    ini_free(g_sys);

    ini_t *cali = ini_load(CALI_INI);
    CHECK(cali != NULL, "%s loads", CALI_INI);
    for (unsigned m = 0; cali && m < CALI_MODE_MAX; m++) {
        if (!(CALI_MODE_BOARD_MASK & (1u << m)))
            continue;
        for (unsigned l = 0; l < CALI_LEVEL_COUNT; l++) {
            char sect[16], key[16];
            snprintf(sect, sizeof sect, "mode%u", m);
            snprintf(key, sizeof key, "cali_param%u", l);
            CHECK(ini_get(cali, sect, key) != NULL, "[%s] %s is present", sect, key);
        }
    }
    ini_free(cali);

#ifdef CAP_ACTIVATION
    ini_t      *act = ini_load(ACTIVATION);
    const char *a   = act ? ini_get(act, "dev_attr", "activation") : NULL;
    CHECK(a && !strcmp(a, "0"), "activation.ini [dev_attr] activation is 0");
    ini_free(act);
#endif
    return tst_done();
}
