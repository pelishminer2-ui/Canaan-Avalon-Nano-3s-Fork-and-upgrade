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
#include "ui_screen.h"
#include "ui_types.h"
#include "ini.h"
#include "osif.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static uint16_t regular = UI_SUMMARY; /* current regular screen ID */
/*
 * Backlight on flag; brightness and beep volume, percent; wallpaper reloads,
 * UI_NIGHT adds, screen cycle switches and UI_HI adds, counts.
 */
static int     backlight, brightness, volume, reloads, night, cycles, hi;
static uint8_t led_dark; /* mm_led_state() ledmode */
static char    last_log[512];

void ui_seed_regular(uint16_t id)
{
    regular = id;
}
uint16_t ui_get_regular(void)
{
    return regular;
}
int ui_switch_regular(uint16_t id, uint16_t *settled)
{
    *settled = regular = id;
    return 0;
}
void ui_show_update(int action, uint16_t id)
{
    if (action == UI_ACT_ADD && id == UI_HI)
        hi++;
    else if (action == UI_ACT_ADD) {
        assert(id == UI_NIGHT);
        night++;
    } else {
        assert(action == UI_ACT_SWITCH);
        if (!id)
            cycles++;
        else
            regular = id;
    }
}
void ui_show_onoff(int onoff)
{
    backlight = onoff;
}
void ui_set_brightness(int pct)
{
    brightness = pct < 0 ? 0 : pct > 100 ? 100 : pct;
}
void ui_show_wallpaper_reload(void)
{
    reloads++;
}
void ui_set_pelish_rotate(int on)
{
    (void)on;
}
void beep_volume(int pct)
{
    volume = pct;
}

void mm_led_state(uint8_t *ledmode, uint8_t *runeffect)
{
    *ledmode   = led_dark;
    *runeffect = 0;
}

void osif_log(osif_loglvl_t lvl, const char *fmt, ...)
{
    va_list ap;

    (void)lvl;
    va_start(ap, fmt);
    vsnprintf(last_log, sizeof last_log, fmt, ap);
    va_end(ap);
}

int main(void)
{
    char path[] = "/tmp/mm_displayXXXXXX";
    int  fd     = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd, "w");
    assert(file);
    fputs("[lcd]\nid=3\nonoff=1\nbrightness=17\n[syscfg]\nledmode=1\n[audio]\nvolume=42\n", file);
    fclose(file);
    regular     = 2;
    last_log[0] = '\0';
    mm_display_tick();
    assert(strstr(last_log, "LCD settings NOT persisted"));
    regular = UI_SUMMARY;
    assert(mm_cfgstore_open(path) == 0);
    led_dark = 1;
    assert(mm_display_start() == 0);
    mm_display_state state;
    mm_display_snapshot(&state);
    assert(state.show == 3 && state.onoff == 0 && state.bright == 0 && state.volume == 42);
    assert(backlight == 0 && volume == 42 && brightness == 100);
    assert(hi == 0);
    mm_display_night(0);
    assert(backlight == 1 && brightness == 100);
    assert(mm_display_lcd(1, 37) == 0);
    mm_display_snapshot(&state);
    assert(state.bright == 37 && brightness == 37);
    assert(mm_display_lcd(1, 65535) == 0);
    mm_display_snapshot(&state);
    assert(state.bright == 65535 && brightness == 100);
    assert(mm_display_lcd(1, 37) == 0);
    assert(mm_display_lcd(0, 0) == 0 && backlight == 0);
    mm_display_cycle();
    assert(cycles == 0);
    assert(mm_display_lcd(0, 1) == 0 && backlight == 1 && brightness == 100);
    mm_display_cycle();
    assert(cycles == 1);
    mm_display_snapshot(&state);
    assert(state.bright == 37);
    assert(mm_display_lcd(2, 2) == 0 && regular == 2);
    assert(mm_display_lcd(2, 99) == 0 && regular == 2);
    assert(mm_display_lcd(3, 1) == 0 && regular == 2);
    assert(mm_display_lcd(4, 1) == 0 && reloads == 1);
    assert(mm_display_lcd(5, 1) == 0);
    mm_display_snapshot(&state);
    assert(state.pelish == 1);
    assert(mm_display_lcd(5, 0) == 0);
    mm_display_snapshot(&state);
    assert(state.pelish == 0);
    mm_display_night(1);
    assert(night == 1);
    mm_display_snapshot(&state);
    assert(state.onoff == 0);
    regular = 1;
    mm_display_tick();
    assert(mm_cfgstore_flush() == 1);
    ini_t *ini = ini_load(path);
    assert(ini);
    assert(atoi(ini_get(ini, "lcd", "id")) == 1);
    assert(atoi(ini_get(ini, "audio", "volume")) == 42);
    assert(atoi(ini_get(ini, "lcd", "brightness")) == 17);
    assert(atoi(ini_get(ini, "syscfg", "ledmode")) == 1);
    ini_free(ini);
    mm_cfgstore_freeze(1);
    regular     = 2;
    last_log[0] = '\0';
    mm_display_tick();
    assert(last_log[0] == '\0');
    assert(mm_cfgstore_flush() < 0);
    mm_cfgstore_freeze(0);
    ini = ini_load(path);
    assert(ini);
    assert(atoi(ini_get(ini, "lcd", "id")) == 1);
    assert(atoi(ini_get(ini, "audio", "volume")) == 42);
    ini_free(ini);
    regular = 1;
    mm_display_tick();
    assert(mm_cfgstore_flush() == 0);
    assert(mm_display_start() == 0);
    mm_display_snapshot(&state);
    assert(state.show == 1 && state.onoff == 0 && state.bright == 0 && state.volume == 42);
    assert(hi == 0);
    unlink(path);
    assert(mm_cfgstore_reload() == 0);
    led_dark = 0;
    assert(mm_display_start() == 0);
    mm_display_snapshot(&state);
    assert(state.show == 1 && state.onoff == 1 && state.bright == 0 && state.volume == 100);
    assert(hi == 1);
    unlink(path);
    char lockpath[sizeof path + 5];
    snprintf(lockpath, sizeof lockpath, "%s.lock", path);
    unlink(lockpath);
    char absent[sizeof path + 16];
    snprintf(absent, sizeof absent, "%s/missing.ini", path);
    assert(mm_cfgstore_open(absent) == 0);
    assert(mm_display_start() == 0);
    assert(mm_display_lcd(0, 0) == 0);
    assert(mm_cfgstore_flush() < 0);
    assert(mkdir(path, 0700) == 0);
    assert(mm_cfgstore_flush() == 1);
    ini = ini_load(absent);
    assert(ini && !ini_get(ini, "audio", "volume"));
    assert(atoi(ini_get(ini, "lcd", "onoff")) == 0);
    ini_free(ini);
    unlink(absent);
    char absent_lock[sizeof absent + 5];
    snprintf(absent_lock, sizeof absent_lock, "%s.lock", absent);
    unlink(absent_lock);
    rmdir(path);

    char same[] = "/tmp/mm_display_sameXXXXXX";
    fd          = mkstemp(same);
    assert(fd >= 0);
    file = fdopen(fd, "w");
    assert(file);
    fputs("[lcd]\nid=2\nonoff=1\n", file);
    fclose(file);
    struct stat before, after;
    assert(stat(same, &before) == 0);
    assert(mm_cfgstore_open(same) == 0);
    assert(mm_display_start() == 0 && mm_cfgstore_flush() == 0);
    assert(stat(same, &after) == 0 && after.st_ino == before.st_ino);
    mm_display_snapshot(&state);
    assert(state.show == 2 && state.onoff == 1);

    file = fopen(same, "w");
    assert(file);
    fputs("[lcd]\nid=2\nonoff=1\n[syscfg]\nledmode=1\n", file);
    fclose(file);
    assert(mm_cfgstore_reload() == 0);
    led_dark = 1;
    assert(mm_display_start() == 0 && mm_cfgstore_flush() == 1);
    ini = ini_load(same);
    assert(ini && atoi(ini_get(ini, "lcd", "onoff")) == 0);
    assert(atoi(ini_get(ini, "lcd", "id")) == 2);
    ini_free(ini);
    unlink(same);
    char same_lock[sizeof same + 5];
    snprintf(same_lock, sizeof same_lock, "%s.lock", same);
    unlink(same_lock);
    puts("display legacy compatibility OK");
    return 0;
}
