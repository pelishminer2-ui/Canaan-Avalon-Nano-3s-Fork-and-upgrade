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

#include "mm_ui_cfg.h"
#include "cfg_u32.h"
#include "mm_cfgstore.h"
#include "ui_types.h"
#include "osif.h"

#include <stdio.h>

#define UI_CFG_TEXT_MAX 32 /* stored LCD value incl. NUL, bytes */

void mm_ui_cfg_load(uint16_t *show, int *onoff, int *pelish)
{
    const char *const keys[] = { "id", "onoff", "pelish" };
    const uint32_t    max[]  = { UINT32_MAX, 1, 1 };
    uint32_t          val[3];
    int               ok[3];

    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        char v[UI_CFG_TEXT_MAX];
        ok[i] = 0;
        if (mm_cfgstore_get("lcd", keys[i], v, sizeof v) != 0)
            continue;
        ok[i] = cfg_u32_parse(v, max[i], &val[i]) == 0;
        if (!ok[i])
            osif_log(OSIF_LOG_WARN,
                    "LCD setting [lcd] %s = \"%s\" is not a valid number; "
                    "keeping the default",
                    keys[i], v);
    }
    if (ok[2] && pelish)
        *pelish = (int)val[2];
    if (ok[0] && val[0] >= UI_SUMMARY && val[0] <= UI_WALLPAPER)
        *show = (uint16_t)val[0];
    else if (ok[0] && val[0] == UI_PELISH && pelish && *pelish)
        *show = UI_PELISH;
    if (ok[1])
        *onoff = (int)val[1];
}

int mm_ui_cfg_save(uint16_t show, int onoff, int pelish)
{
    char              id[UI_CFG_TEXT_MAX], on[UI_CFG_TEXT_MAX], pe[UI_CFG_TEXT_MAX];
    const char *const sects[] = { "lcd", "lcd", "lcd" };
    const char *const keys[]  = { "id", "onoff", "pelish" };
    const char *const vals[]  = { id, on, pe };

    snprintf(id, sizeof id, "%u", (unsigned)show);
    snprintf(on, sizeof on, "%d", onoff ? 1 : 0);
    snprintf(pe, sizeof pe, "%d", pelish ? 1 : 0);
    return mm_cfgstore_set_n(sects, keys, vals, 3);
}
