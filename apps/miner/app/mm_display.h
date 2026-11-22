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

#ifndef MM_DISPLAY_H
#define MM_DISPLAY_H

#include <stdint.h>
#include <string.h>

#define MM_LCD_ONOFF     0 /* mm_display_lcd id: screen on 1 / off 0 */
#define MM_LCD_BRIGHT    1 /* mm_display_lcd id: brightness, percent */
#define MM_LCD_SHOW      2 /* mm_display_lcd id: regular screen, UI_SUMMARY..UI_WALLPAPER */
#define MM_LCD_WALLPAPER 4 /* mm_display_lcd id: 1 reloads the wallpaper image */
#define MM_LCD_PELISH    5 /* mm_display_lcd id: 1 rotates Pelish coin with stock screens */

#define MM_LCD_ROTATE_S 180 /* home-cycle period with Pelish rotation enabled, seconds */

/* screen state reported to core_miner */
typedef struct {
    uint16_t show;   /* regular screen shown, UI_* */
    uint8_t  onoff;  /* screen on 1, off 0 */
    uint16_t bright; /* last brightness set by MM_LCD_BRIGHT, percent */
    uint8_t  volume; /* beeper volume, percent */
    uint8_t  pelish; /* 1: 3-minute cycle includes the Pelish coin screen */
} mm_display_state;

#ifdef SUPPORT_LCD
int  mm_display_start(void);
int  mm_display_lcd(int id, int value);
void mm_display_night(int dark);
void mm_display_tick(void);
void mm_display_cycle(void);
void mm_display_snapshot(mm_display_state *out);
#else  /* SUPPORT_LCD */
static inline int mm_display_start(void)
{
    return 0;
}
static inline int mm_display_lcd(int id, int value)
{
    (void)id;
    (void)value;
    return -1;
}
static inline void mm_display_snapshot(mm_display_state *out)
{
    memset(out, 0, sizeof *out);
}
#endif /* SUPPORT_LCD */

#endif /* MM_DISPLAY_H */
