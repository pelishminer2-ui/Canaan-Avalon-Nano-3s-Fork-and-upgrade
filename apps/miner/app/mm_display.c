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

#include "mm_display.h"
#include "mm_cfgstore.h"
#include "mm_led.h"
#include "mm_ui_cfg.h"
#include "ui_screen.h"
#include "ui_types.h"
#include "beep_dev.h"
#include "osif.h"

#include <pthread.h>
#include <stdlib.h>

#define DISPLAY_CFG_TEXT_MAX 32 /* stored display value incl. NUL, bytes */

static pthread_mutex_t  display_lock = PTHREAD_MUTEX_INITIALIZER;
static mm_display_state display      = { .show = UI_SUMMARY, .onoff = 1, .volume = 100 };
static unsigned         rotate_s; /* seconds on the current regular screen, Pelish cycle */

static int save_lcd(void)
{
    int result = mm_ui_cfg_save(display.show, display.onoff, display.pelish);
    if (result != 0 && !mm_cfgstore_frozen())
        osif_log(OSIF_LOG_ERR, "LCD settings NOT persisted");
    return result;
}

int mm_display_start(void)
{
    char    value[DISPLAY_CFG_TEXT_MAX];
    uint8_t ledmode, runeffect;
    mm_led_state(&ledmode, &runeffect);
    pthread_mutex_lock(&display_lock);
    display             = (mm_display_state){ .show = UI_SUMMARY, .onoff = 1, .volume = 100 };
    uint16_t file_show  = UI_OFF;
    int      file_onoff = -1;
    int      file_pelish = -1;
    mm_ui_cfg_load(&file_show, &file_onoff, &file_pelish);
    if (file_pelish >= 0)
        display.pelish = (uint8_t)file_pelish;
    if (file_show != UI_OFF)
        display.show = file_show;
    if (file_onoff >= 0)
        display.onoff = (uint8_t)file_onoff;
    if (ledmode)
        display.onoff = 0;
    if (mm_cfgstore_get("audio", "volume", value, sizeof value) == 0) {
        int volume     = atoi(value);
        display.volume = (uint8_t)(volume < 0 ? 0 : volume > 100 ? 100 : volume);
    }
    ui_set_pelish_rotate(display.pelish);
    ui_seed_regular(display.show);
    ui_show_update(UI_ACT_SWITCH, display.show);
    ui_set_brightness(100);
    ui_show_onoff(display.onoff);
    if (display.onoff)
        ui_show_update(UI_ACT_ADD, UI_HI);
    beep_volume(display.volume);
    int result = 0;
    if (display.show != file_show || display.onoff != file_onoff ||
            (file_pelish >= 0 && display.pelish != (uint8_t)file_pelish))
        result = save_lcd();
    pthread_mutex_unlock(&display_lock);
    return result;
}

int mm_display_lcd(int id, int value)
{
    int result = 0;
    pthread_mutex_lock(&display_lock);
    if (id == MM_LCD_ONOFF && display.onoff != value) {
        display.onoff = !!value;
        display.show  = ui_get_regular();
        if (value == 1)
            ui_set_brightness(100);
        ui_show_onoff(display.onoff);
        save_lcd();
    } else if (id == MM_LCD_BRIGHT) {
        ui_set_brightness(value);
        display.bright = (uint16_t)value;
    } else if (id == MM_LCD_SHOW && value >= UI_SUMMARY && value <= UI_WALLPAPER) {
        uint16_t settled;
        result = ui_switch_regular((uint16_t)value, &settled);
        if (result == 0) {
            display.show = settled;
            rotate_s     = 0;
            save_lcd();
        }
    } else if (id == MM_LCD_WALLPAPER && value == 1) {
        ui_show_wallpaper_reload();
    } else if (id == MM_LCD_PELISH && (value == 0 || value == 1)) {
        display.pelish = (uint8_t)value;
        ui_set_pelish_rotate(display.pelish);
        rotate_s       = 0;
        if (!display.pelish && ui_get_regular() == UI_PELISH) {
            uint16_t settled;
            if (ui_switch_regular(UI_SUMMARY, &settled) == 0)
                display.show = settled;
        }
        save_lcd();
    }
    pthread_mutex_unlock(&display_lock);
    return result;
}

void mm_display_night(int dark)
{
    pthread_mutex_lock(&display_lock);
    display.onoff = !dark;
    display.show  = ui_get_regular();
    if (dark) {
        ui_show_update(UI_ACT_ADD, UI_NIGHT);
    } else {
        ui_set_brightness(100);
        ui_show_onoff(1);
    }
    save_lcd();
    pthread_mutex_unlock(&display_lock);
}

void mm_display_cycle(void)
{
    pthread_mutex_lock(&display_lock);
    if (display.onoff) {
        rotate_s = 0;
        ui_show_update(UI_ACT_SWITCH, 0);
    }
    pthread_mutex_unlock(&display_lock);
}

void mm_display_tick(void)
{
    pthread_mutex_lock(&display_lock);
    uint16_t show = ui_get_regular();
    if (show != display.show) {
        display.show = show;
        save_lcd();
    }
    if (display.onoff && display.pelish) {
        unsigned n = (unsigned)(UI_WALLPAPER - UI_SUMMARY + 1) + 1u;
        unsigned step = MM_LCD_ROTATE_S / n;

        if (step == 0)
            step = 1;
        if (++rotate_s >= step) {
            rotate_s = 0;
            ui_show_update(UI_ACT_SWITCH, 0);
        }
    } else {
        rotate_s = 0;
    }
    pthread_mutex_unlock(&display_lock);
}

void mm_display_snapshot(mm_display_state *out)
{
    pthread_mutex_lock(&display_lock);
    *out      = display;
    out->show = ui_get_regular();
    pthread_mutex_unlock(&display_lock);
}
