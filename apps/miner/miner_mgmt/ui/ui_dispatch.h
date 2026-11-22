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

#ifndef UI_DISPATCH_H
#define UI_DISPATCH_H

#include "ui_types.h"

/* one entry of a popup or attach list */
typedef struct ui_node {
    ui_show         show;
    struct ui_node *next; /* next entry, NULL at the tail */
} ui_node;

typedef struct {
    ui_show  regular; /* current regular (home-cycle) screen */
    ui_node *popup;   /* popup list, head is shown next */
    ui_node *attach;  /* transferred popups, shown as summary flags */
    int      onoff;   /* display on (1) or off (0) */
} ui_dispatch;

void    ui_dispatch_init(ui_dispatch *d, uint16_t start_id);
void    ui_dispatch_free(ui_dispatch *d);
void    ui_show_add(ui_dispatch *d, uint16_t id);
void    ui_show_delete(ui_dispatch *d, uint16_t id);
void    ui_show_switch(ui_dispatch *d, int16_t id);
void    ui_set_pelish_rotate(int on);
int     ui_get_pelish_rotate(void);
ui_show ui_pick_show(ui_dispatch *d);
int     ui_attach_err_flag(const ui_dispatch *d);
int     ui_errcode(uint16_t id, const char **code, const char **desc);

#endif /* UI_DISPATCH_H */
