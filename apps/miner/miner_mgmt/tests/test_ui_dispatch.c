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

#include "ui_dispatch.h"
#include <stdio.h>
#include <string.h>
#include "../../tests/include/tst_check.h"

static int popup_depth(ui_dispatch *d)
{
    int n = 0;
    for (ui_node *p = d->popup; p; p = p->next)
        n++;
    return n;
}
static int popup_head(ui_dispatch *d)
{
    return d->popup ? d->popup->show.id : -1;
}

int main(void)
{
    ui_dispatch d;
    ui_dispatch_init(&d, UI_SUMMARY);
    CHECK(d.regular.id == UI_SUMMARY, "init regular = SUMMARY");
    CHECK(d.popup == NULL && d.attach == NULL, "init lists empty");
    CHECK(d.onoff == 1, "init on");

    ui_show_add(&d, UI_HI);
    CHECK(popup_head(&d) == UI_HI && popup_depth(&d) == 1, "add HI -> popup head");
    ui_show_add(&d, UI_POWER_ERR);
    CHECK(popup_head(&d) == UI_POWER_ERR && popup_depth(&d) == 2, "add POWER_ERR -> new head");
    ui_show_add(&d, UI_HI);
    CHECK(popup_head(&d) == UI_HI && popup_depth(&d) == 2, "re-add HI -> head, no dup");

    ui_show_delete(&d, UI_HI);
    CHECK(popup_head(&d) == UI_POWER_ERR && popup_depth(&d) == 1, "delete HI");
    ui_show_delete(&d, UI_POWER_ERR);
    CHECK(popup_depth(&d) == 0, "delete last popup");
    ui_show_delete(&d, UI_MINING);
    CHECK(popup_depth(&d) == 0, "delete absent -> no-op");

    ui_dispatch d2;
    ui_dispatch_init(&d2, UI_SUMMARY);
    CHECK(ui_pick_show(&d2).id == UI_SUMMARY, "pick: regular when no popup");
    ui_show_add(&d2, UI_MINING);
    CHECK(ui_pick_show(&d2).id == UI_MINING, "pick: popup over regular");
    CHECK(ui_pick_show(&d2).id == UI_SUMMARY, "pick: CLEAR_YES popped after one show");
    ui_show_add(&d2, UI_POWER_ERR);
    CHECK(ui_pick_show(&d2).id == UI_POWER_ERR, "pick: sticky popup shows");
    CHECK(ui_pick_show(&d2).id == UI_POWER_ERR, "pick: CLEAR_NO stays (ring)");
    d2.onoff = 0;
    CHECK(ui_pick_show(&d2).id == UI_OFF, "pick: off -> OFF");
    d2.onoff = 1;

    ui_dispatch d3;
    ui_dispatch_init(&d3, UI_SUMMARY);
    ui_show_switch(&d3, 0);
    CHECK(d3.regular.id == UI_TIME, "switch cycle -> TIME");
    ui_show_switch(&d3, 0);
    CHECK(d3.regular.id == UI_WALLPAPER, "switch cycle -> WALLPAPER");
    ui_show_switch(&d3, 0);
    CHECK(d3.regular.id == UI_SUMMARY, "switch cycle wraps -> SUMMARY");
    ui_show_switch(&d3, UI_PELISH);
    CHECK(d3.regular.id == UI_SUMMARY, "PELISH explicit id is skipped when rotation is off");
    ui_set_pelish_rotate(1);
    ui_show_switch(&d3, 0);
    CHECK(d3.regular.id == UI_TIME, "pelish on: SUMMARY -> TIME");
    ui_show_switch(&d3, 0);
    CHECK(d3.regular.id == UI_WALLPAPER, "pelish on: TIME -> WALLPAPER");
    ui_show_switch(&d3, 0);
    CHECK(d3.regular.id == UI_PELISH, "pelish on: WALLPAPER -> PELISH coin");
    ui_show_switch(&d3, 0);
    CHECK(d3.regular.id == UI_SUMMARY, "pelish on: PELISH wraps -> SUMMARY");
    ui_set_pelish_rotate(0);
    ui_show_switch(&d3, UI_TIME);
    CHECK(d3.regular.id == UI_TIME, "switch explicit id");

    ui_dispatch d4;
    ui_dispatch_init(&d4, UI_SUMMARY);
    ui_show_add(&d4, UI_CONFIG_NETWORK);
    ui_show_switch(&d4, 0);
    CHECK(d4.popup == NULL, "switch: TRANSFER popup left popup");
    CHECK(d4.attach && d4.attach->show.id == UI_CONFIG_NETWORK, "switch: moved to attach");
    CHECK(d4.regular.id == UI_SUMMARY, "switch: regular unchanged while transferring");
    ui_dispatch_free(&d2);
    ui_dispatch_free(&d3);
    ui_dispatch_free(&d4);

    ui_dispatch_free(&d);
    ui_dispatch_init(&d, UI_TIME);
    ui_show_add(&d, UI_NIGHT);
    CHECK(ui_pick_show(&d).id == UI_NIGHT, "night notice precedes blanking");
    CHECK(d.popup == NULL && d.regular.id == UI_TIME,
            "night notice clears without changing saved page");
    d.onoff = 0;
    CHECK(ui_pick_show(&d).id == UI_OFF, "night completion selects the off screen");
    ui_dispatch_free(&d);

    static const struct {
        uint16_t    id;
        const char *code;
    } fault_ids[] = {
        { UI_HASH_ERR, "E-06" },
        { UI_FAN_STALL, "E-07" },
        { UI_AGING_ERR, "E-08" },
        { UI_POWER_FAULT, "E-09" },
    };
    for (unsigned i = 0; i < sizeof fault_ids / sizeof fault_ids[0]; i++) {
        const char *code = NULL, *desc = NULL;
        ui_dispatch f;
        ui_dispatch_init(&f, UI_SUMMARY);
        ui_show_add(&f, fault_ids[i].id);
        ui_show_switch(&f, 0);
        CHECK(popup_head(&f) == fault_ids[i].id, "stop fault popup survives a short press");
        CHECK(ui_pick_show(&f).id == fault_ids[i].id && popup_depth(&f) == 1,
                "stop fault popup stays in the ring");
        CHECK(ui_errcode(fault_ids[i].id, &code, &desc) == 0 && !strcmp(code, fault_ids[i].code) &&
                        desc && desc[0],
                "stop fault popup has its code and a description");
        ui_dispatch_free(&f);
    }
    const char *code = NULL, *desc = NULL;
    CHECK(ui_errcode(UI_FAN_ERR, &code, &desc) == 0 && !strcmp(code, "E-04") && desc == NULL,
            "E-04 keeps its code-only screen");
    CHECK(ui_errcode(UI_OVERHOT, &code, &desc) < 0, "image popups have no text code");

    ui_dispatch p;
    ui_dispatch_init(&p, UI_SUMMARY);
    ui_show_add(&p, UI_CHECK_POOL);
    CHECK(ui_pick_show(&p).id == UI_CHECK_POOL, "pool popup shows");
    ui_show_switch(&p, 0);
    CHECK(p.popup == NULL && p.attach && p.attach->show.id == UI_CHECK_POOL,
            "a short press moves the pool popup off the screen");
    ui_show_delete(&p, UI_CHECK_POOL);
    CHECK(p.attach == NULL, "pool recovery clears the transferred popup");
    ui_dispatch_free(&p);
    printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
    return fails ? 1 : 0;
}
