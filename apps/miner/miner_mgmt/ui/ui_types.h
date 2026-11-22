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

#ifndef UI_TYPES_H
#define UI_TYPES_H

#include <stdint.h>

/* how a popup leaves the popup list */
typedef enum {
    UI_CLEAR_INVALID = 0, /* not a popup: regular screen or off */
    UI_CLEAR_NO,          /* stays in the popup ring until deleted */
    UI_CLEAR_YES,         /* removed after being shown once */
    UI_CLEAR_TRANSFER     /* moved to the attach list on a screen switch */
} ui_clear;

/* screen ids. Regular (home-cycle) ids are contiguous [UI_SUMMARY..UI_N_MAX). */
enum {
    UI_OFF = 0,                   /* display off (black screen)       */
    UI_SUMMARY,                   /* regular: hash/power/ip dashboard */
    UI_TIME,                      /* regular: clock                   */
    UI_WALLPAPER,                 /* regular: user image              */
    UI_PELISH,                    /* regular: Pelish coin logo        */
    UI_N_MAX,                     /* one past the last regular id     */
    UI_CONFIG_NETWORK = UI_N_MAX, /* popup: BLE provisioning  */
    UI_LOST_CONNECT,              /* popup: network connection lost   */
    UI_POWER_ERR,                 /* popup: PSU                       */
    UI_FAN_ERR,                   /* popup: fan (E-04)                */
    UI_OVERHOT,                   /* popup: overheat                  */
    UI_FILTER_DIRTY,              /* popup/attach: filter             */
    UI_USB_DEVICE,                /* popup: no net dongle             */
    UI_MINING,                    /* popup: mining started            */
    UI_HI,                        /* popup: boot splash               */
    UI_BYE,                       /* popup: shutdown                  */
    UI_BLACK,                     /* popup: blank                     */
    UI_NIGHT,                     /* popup: night mode notice         */
    UI_HASH_ERR,                  /* popup: hash board fault (E-06)   */
    UI_FAN_STALL,                 /* popup: fan stalled (E-07)        */
    UI_AGING_ERR,                 /* popup: aging failed (E-08)       */
    UI_POWER_FAULT,               /* popup: runtime power fault (E-09) */
    UI_CHECK_POOL,                /* popup: pool inactive             */
    UI_SHOW_MAX
};

/* one screen id with its popup clear policy */
typedef struct {
    uint16_t id; /* screen id, UI_* */
    ui_clear clear;
} ui_show;

#endif /* UI_TYPES_H */
