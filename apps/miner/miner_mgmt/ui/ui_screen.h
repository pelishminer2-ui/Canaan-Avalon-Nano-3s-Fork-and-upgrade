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

#ifndef UI_SCREEN_H
#define UI_SCREEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* values shown on the summary screen */
typedef struct {
    char ip[16];       /* IPv4 address text */
    char hashrate[24]; /* hash rate text, TH/s */
    char level[16];    /* work mode text */
    char power[16];    /* output power text, W */
    int  ghs;          /* hash rate for the hash arc, GH/s */
    int  watt;         /* output power for the power arc, W */
    int  ready;        /* UI_RDY_* bits of the arc values that are valid */
    int  err_flag;     /* error icons shown, bit n = UI_ERR_* value n */
} ui_summary;

/* bit positions of the summary screen error icons */
enum {
    UI_ERR_BLUETOOTH = 0, /* BLE provisioning pending */
    UI_ERR_WIFI,          /* network connection lost */
    UI_ERR_FILTER,        /* filter dirty */
};

/* ui_summary.ready bits */
enum {
    UI_RDY_HASH  = 0x1, /* ghs is valid */
    UI_RDY_POWER = 0x2, /* watt is valid */
};

typedef int (*ui_summary_cb)(ui_summary *out, void *arg);

enum {
    UI_ACT_SWITCH = 0, /* set regular (0 cycles) / transfer popups   */
    UI_ACT_ADD,        /* push a popup                               */
    UI_ACT_DELETE,     /* remove a popup/attach flag                 */
};

int      ui_start(ui_summary_cb cb, void *cb_arg);
void     ui_stop(void);
void     ui_show_update(int action, uint16_t id);
int      ui_switch_regular(uint16_t id, uint16_t *settled);
uint16_t ui_get_regular(void);
void     ui_seed_regular(uint16_t id);
void     ui_set_pelish_rotate(int on);
int      ui_get_pelish_rotate(void);
void     ui_show_onoff(int onoff);
void     ui_set_brightness(int pct);
void     ui_show_wallpaper_reload(void);
int      ui_screen_init(void);
void     ui_screen_deinit(void);
void     ui_screen_render(uint16_t id);
void     ui_screen_refresh(const ui_summary *s);
void     ui_screen_reload_wallpaper(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_H */
