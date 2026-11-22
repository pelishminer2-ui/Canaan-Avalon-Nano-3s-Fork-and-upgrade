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

#include "ui_types.h"
#include "ui_screen.h"
#include <stddef.h>
#include "ui_dispatch.h"
#include <stdlib.h>

/* clear policy of every screen id, indexed by id */
static const ui_show ui_const_table[UI_SHOW_MAX] = {
    [UI_OFF]            = { UI_OFF, UI_CLEAR_INVALID },
    [UI_SUMMARY]        = { UI_SUMMARY, UI_CLEAR_INVALID },
    [UI_TIME]           = { UI_TIME, UI_CLEAR_INVALID },
    [UI_WALLPAPER]      = { UI_WALLPAPER, UI_CLEAR_INVALID },
    [UI_PELISH]         = { UI_PELISH, UI_CLEAR_INVALID },
    [UI_CONFIG_NETWORK] = { UI_CONFIG_NETWORK, UI_CLEAR_TRANSFER },
    [UI_LOST_CONNECT]   = { UI_LOST_CONNECT, UI_CLEAR_TRANSFER },
    [UI_POWER_ERR]      = { UI_POWER_ERR, UI_CLEAR_NO },
    [UI_FAN_ERR]        = { UI_FAN_ERR, UI_CLEAR_NO },
    [UI_OVERHOT]        = { UI_OVERHOT, UI_CLEAR_NO },
    [UI_FILTER_DIRTY]   = { UI_FILTER_DIRTY, UI_CLEAR_TRANSFER },
    [UI_USB_DEVICE]     = { UI_USB_DEVICE, UI_CLEAR_NO },
    [UI_MINING]         = { UI_MINING, UI_CLEAR_YES },
    [UI_HI]             = { UI_HI, UI_CLEAR_YES },
    [UI_BYE]            = { UI_BYE, UI_CLEAR_YES },
    [UI_BLACK]          = { UI_BLACK, UI_CLEAR_NO },
    [UI_NIGHT]          = { UI_NIGHT, UI_CLEAR_YES },
    [UI_HASH_ERR]       = { UI_HASH_ERR, UI_CLEAR_NO },
    [UI_FAN_STALL]      = { UI_FAN_STALL, UI_CLEAR_NO },
    [UI_AGING_ERR]      = { UI_AGING_ERR, UI_CLEAR_NO },
    [UI_POWER_FAULT]    = { UI_POWER_FAULT, UI_CLEAR_NO },
    [UI_CHECK_POOL]     = { UI_CHECK_POOL, UI_CLEAR_TRANSFER },
};

/* text of the error-code screens; desc NULL shows the code alone */
static const struct {
    uint16_t    id;
    const char *code;
    const char *desc;
} ui_errcode_table[] = {
    { UI_FAN_ERR, "E-04", NULL },
    { UI_HASH_ERR, "E-06", "Hashboard Fault" },
    { UI_FAN_STALL, "E-07", "Fan Stopped" },
    { UI_AGING_ERR, "E-08", "Aging Failed" },
    { UI_POWER_FAULT, "E-09", "Power Fault" },
};

static int g_pelish_rotate; /* 1: home cycle includes UI_PELISH */

void ui_set_pelish_rotate(int on)
{
    g_pelish_rotate = on ? 1 : 0;
}

int ui_get_pelish_rotate(void)
{
    return g_pelish_rotate;
}

static uint16_t cycle_next(uint16_t id)
{
    uint16_t nid = (uint16_t)(id + 1);

    if (nid >= UI_N_MAX)
        nid = UI_SUMMARY;
    if (!g_pelish_rotate && nid == UI_PELISH) {
        nid = (uint16_t)(nid + 1);
        if (nid >= UI_N_MAX)
            nid = UI_SUMMARY;
    }
    return nid;
}

int ui_errcode(uint16_t id, const char **code, const char **desc)
{
    for (size_t i = 0; i < sizeof ui_errcode_table / sizeof ui_errcode_table[0]; i++) {
        if (ui_errcode_table[i].id == id) {
            *code = ui_errcode_table[i].code;
            *desc = ui_errcode_table[i].desc;
            return 0;
        }
    }
    return -1;
}

static const ui_show *ui_show_desc(uint16_t id)
{
    if (id >= UI_SHOW_MAX)
        return &ui_const_table[UI_OFF];
    return &ui_const_table[id];
}

void ui_dispatch_init(ui_dispatch *d, uint16_t start_id)
{
    d->regular = *ui_show_desc(start_id);
    d->popup   = NULL;
    d->attach  = NULL;
    d->onoff   = 1;
}

static void list_free(ui_node *n)
{
    while (n) {
        ui_node *x = n;
        n          = n->next;
        free(x);
    }
}
void ui_dispatch_free(ui_dispatch *d)
{
    list_free(d->popup);
    list_free(d->attach);
    d->popup = d->attach = NULL;
}

static ui_node *list_remove(ui_node **head, uint16_t id)
{
    for (ui_node **pp = head; *pp; pp = &(*pp)->next)
        if ((*pp)->show.id == id) {
            ui_node *n = *pp;
            *pp        = n->next;
            n->next    = NULL;
            return n;
        }
    return NULL;
}

void ui_show_add(ui_dispatch *d, uint16_t id)
{
    if (id == 0)
        return;
    ui_node *n = list_remove(&d->attach, id);
    if (!n)
        n = list_remove(&d->popup, id);
    if (!n) {
        n = calloc(1, sizeof *n);
        if (!n)
            return;
        n->show = *ui_show_desc(id);
    }
    n->next  = d->popup;
    d->popup = n;
}

void ui_show_delete(ui_dispatch *d, uint16_t id)
{
    if (id == 0)
        return;
    ui_node *n = list_remove(&d->popup, id);
    if (!n)
        n = list_remove(&d->attach, id);
    free(n);
}

static void popup_transfer(ui_dispatch *d)
{
    for (ui_node **pp = &d->popup; *pp;) {
        if ((*pp)->show.clear == UI_CLEAR_TRANSFER) {
            ui_node *n = *pp;
            *pp        = n->next;
            n->next    = d->attach;
            d->attach  = n;
        } else
            pp = &(*pp)->next;
    }
}

void ui_show_switch(ui_dispatch *d, int16_t id)
{
    if (d->popup) {
        popup_transfer(d);
        return;
    }
    uint16_t nid;
    if (id > 0)
        nid = (uint16_t)id;
    else
        nid = cycle_next(d->regular.id);
    if (!g_pelish_rotate && nid == UI_PELISH)
        nid = UI_SUMMARY;
    d->regular = *ui_show_desc(nid);
}

ui_show ui_pick_show(ui_dispatch *d)
{
    if (d->onoff == 0)
        return *ui_show_desc(UI_OFF);
    if (d->popup) {
        ui_node *head = d->popup;
        ui_show  s    = head->show;
        if (head->show.clear == UI_CLEAR_YES) {
            d->popup = head->next;
            free(head);
        } else {
            if (head->next) {
                d->popup   = head->next;
                ui_node *t = d->popup;
                while (t->next)
                    t = t->next;
                t->next    = head;
                head->next = NULL;
            }
        }
        return s;
    }
    return d->regular;
}

int ui_attach_err_flag(const ui_dispatch *d)
{
    int flag = 0;
    for (const ui_node *n = d->attach; n; n = n->next) {
        if (n->show.id == UI_CONFIG_NETWORK)
            flag |= 0x1 << UI_ERR_BLUETOOTH;
        else if (n->show.id == UI_LOST_CONNECT)
            flag |= 0x1 << UI_ERR_WIFI;
        else if (n->show.id == UI_FILTER_DIRTY)
            flag |= 0x1 << UI_ERR_FILTER;
    }
    return flag;
}
