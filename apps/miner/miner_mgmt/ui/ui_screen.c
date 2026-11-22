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

#include "ui_screen.h"
#include "ui_types.h"
#include "ui_dispatch.h"
#include "mc_paths.h"

#include "lvgl/lvgl.h"
#include "lvgl/src/drivers/display/fb/lv_linux_fbdev.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define LCD_HOR_RES 240 /* screen width, pixels */
#define LCD_VER_RES 240 /* screen height, pixels */
#define BG_COLOR    0x0 /* screen background, RGB */

/* LVGL path of the LCD assets */
#define ROOT_PATH        "S:/mntapp/release/linux/resource/assets_lcd/"
#define PIC_PATH         ROOT_PATH "screen_pic/" /* full-screen images */
#define IMG_LOGO         PIC_PATH "logo_b.png"
#define IMG_HI           PIC_PATH "hi.png"  /* boot splash image */
#define IMG_BYE          PIC_PATH "bye.png" /* shutdown image */
#define IMG_CONFIG       PIC_PATH "2.png"   /* network provisioning image */
#define IMG_CHECK_POOL   PIC_PATH "3.png"   /* pool inactive image */
#define IMG_CHECK_WIFI   PIC_PATH "4.png"   /* network lost image */
#define IMG_OVER_HOT     PIC_PATH "5.png"
#define IMG_ADAPTER      PIC_PATH "6.png" /* power adapter error image */
#define IMG_NIGHT        PIC_PATH "7.png"
#define IMG_START_MINING PIC_PATH "9.png"
#define IMG_FILTER       PIC_PATH "filter.png" /* dirty filter image */
#define IMG_NO_USB       PIC_PATH "no_usb.png" /* no network dongle image */
/* Circular Pelish Crypto & More coin only — never the Avalon chassis product photo */
#define IMG_PELISH       PIC_PATH "pelish.png"

#define WATCH_PATH       ROOT_PATH "variant/watch/"       /* clock screen images */
#define IMG_WATCH_BG     WATCH_PATH "watch_bg.png"        /* clock face */
#define IMG_WATCH_CENTER WATCH_PATH "1x/watch_center.png" /* hand hub */
#define IMG_WATCH_HOUR   WATCH_PATH "1x/hour.png"
#define IMG_WATCH_MINUTE WATCH_PATH "1x/minute.png"
#define IMG_WATCH_SECOND WATCH_PATH "1x/second.png"

#define INFO_PATH      ROOT_PATH "variant/info/"     /* summary screen images */
#define IMG_INFO_BG    INFO_PATH "info_bg_3.png"     /* summary background */
#define IMG_INFO_MASK  INFO_PATH "qie/mask_3.png"    /* overlay drawn above the arcs */
#define IMG_ERR_WIFI   INFO_PATH "qie/wifi.png"      /* network lost icon */
#define IMG_ERR_BLUE   INFO_PATH "qie/bluetooth.png" /* BLE provisioning icon */
#define IMG_ERR_FILTER INFO_PATH "qie/filter.png"    /* dirty filter icon */

#define WALLPAPER_LV_DIR "S:" MM_CFGWR_WP_DIR "/" /* LVGL path of the wallpaper directory */

LV_FONT_DECLARE(clock_font_40);
#define FONT_SMALL (&lv_font_montserrat_12)
#define FONT_BOLD  (&lv_font_montserrat_16) /* hash rate font */
#define FONT_BIG   (&clock_font_40)         /* summary clock font */
#define FONT_LABEL (&lv_font_montserrat_32) /* error code font */

#define ARC_BUILT_VAL 55   /* initial arc value, 0..100 */
#define HASH_ARC_MAX  7000 /* hash rate at a full hash arc, GH/s */
#define POWER_ARC_MIN 10   /* power at an empty power arc, W */
#define POWER_ARC_MAX 140  /* power at a full power arc, W */

#define HOUR_PIC_OFFSET 900  /* hour hand image angle at 12 o'clock, 0.1 degree */
#define MIN_PIC_OFFSET  2700 /* minute hand image angle at 0 min, 0.1 degree */
#define SEC_PIC_OFFSET  1800 /* second hand image angle at 0 s, 0.1 degree */
#define MAX_ANGLE       3600 /* full turn, 0.1 degree */

enum {
    SC_INFO = 0, /* summary */
    SC_TIME,     /* clock */
    SC_HI,       /* boot splash */
    SC_BYE,      /* shutdown */
    SC_WALLPAPER,
    SC_BLACK,
    SC_CONFIG,     /* network provisioning */
    SC_CHECK_WIFI, /* network lost */
    SC_OVER_HOT,
    SC_ADAPTER, /* power adapter error */
    SC_NIGHT,
    SC_START_MINING,
    SC_FILTER, /* dirty filter */
    SC_ERRORCODE,
    SC_NO_USB,     /* no network dongle */
    SC_CHECK_POOL, /* pool inactive */
    SC_FAULT,      /* fault stop: icon, error code, description */
    SC_PELISH,     /* Pelish coin logo */
    SC_MAX,
};

typedef struct {
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *second;
    lv_obj_t *month;
    lv_obj_t *day; /* day-of-month label */
    lv_obj_t *week;
} watch_t;

static lv_obj_t *g_screens[SC_MAX]; /* screen objects, by SC_* slot */

static struct {
    lv_obj_t *hash_arc;   /* hashrate gauge arc, 0..100 */
    lv_obj_t *power_arc;  /* power gauge arc, 0..100 */
    lv_obj_t *time_label; /* 12-hour clock label, "hh:mm:ss" */
    lv_obj_t *ampm_label; /* "AM" or "PM" label */
    lv_obj_t *week_label;
    lv_obj_t *month_label;
    lv_obj_t *day_label; /* day-of-month label */
    lv_obj_t *hash_label;
    lv_obj_t *power_label;
    lv_obj_t *ip_label;
    lv_obj_t *mode_label;
    lv_obj_t *bluetooth; /* BLE provisioning icon */
    lv_obj_t *filter;    /* dirty filter icon */
    lv_obj_t *wifi;      /* network lost icon */
} g_info;

static lv_obj_t *g_errcode_label;
static lv_obj_t *g_fault_code; /* error code label of SC_FAULT */
static lv_obj_t *g_fault_desc; /* description label of SC_FAULT */

static watch_t g_watch;

static lv_obj_t *make_screen(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LCD_HOR_RES, LCD_VER_RES);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(scr, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scr, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(scr, lv_color_hex(BG_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
    return scr;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, const char *text,
        uint32_t color)
{
    lv_obj_t *lab = lv_label_create(parent);
    lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_scrollbar_mode(lab, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(lab, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(lab, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lab, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lab, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lab, text);
    return lab;
}

static lv_obj_t *make_img(lv_obj_t *parent, const char *path)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, path);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    return img;
}

static lv_obj_t *screen_img(const char *path)
{
    lv_obj_t *scr = make_screen();
    make_img(scr, path);
    return scr;
}

static lv_obj_t *screen_pelish(void)
{
    lv_obj_t *scr = make_screen();
    lv_obj_t *img = make_img(scr, IMG_PELISH);

    lv_obj_set_style_radius(img, LCD_HOR_RES / 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(img, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    return scr;
}

static lv_obj_t *screen_gif(const char *path)
{
    lv_obj_t *scr = make_screen();
    lv_obj_t *gif = lv_gif_create(scr);
    lv_gif_set_src(gif, path);
    if (lv_gif_is_loaded(gif)) {
        lv_obj_align(gif, LV_ALIGN_CENTER, 0, 0);
    } else {
        lv_obj_delete(gif);
        make_img(scr, IMG_LOGO);
    }
    return scr;
}

static lv_obj_t *screen_errorcode(const char *text)
{
    lv_obj_t *scr = make_screen();
    lv_obj_t *lab = make_label(scr, FONT_LABEL, text, 0xffffff);
    lv_obj_align(lab, LV_ALIGN_CENTER, 0, 0);
    g_errcode_label = lab;
    return scr;
}

static lv_obj_t *screen_fault(void)
{
    lv_obj_t *scr  = make_screen();
    lv_obj_t *icon = make_label(scr, FONT_LABEL, LV_SYMBOL_WARNING, 0xffffff);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -50);
    g_fault_code = make_label(scr, FONT_LABEL, "", 0xffffff);
    lv_obj_align(g_fault_code, LV_ALIGN_CENTER, 0, 0);
    g_fault_desc = make_label(scr, FONT_BOLD, "", 0xffffff);
    lv_obj_align(g_fault_desc, LV_ALIGN_CENTER, 0, 40);
    return scr;
}

static lv_obj_t *make_arc(lv_obj_t *parent, lv_color_t color, int size)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(arc, 0, LV_PART_KNOB);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_style_arc_opa(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_center(arc);
    lv_arc_set_change_rate(arc, 360);
    lv_arc_set_value(arc, ARC_BUILT_VAL);
    return arc;
}

static void arc_anim_exec(void *obj, int32_t v)
{
    lv_arc_set_value(obj, v);
}

static void arc_animate(lv_obj_t *arc, int *last, int val)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, arc_anim_exec);
    lv_anim_set_time(&a, 500);
    lv_anim_set_values(&a, *last, val);
    lv_anim_start(&a);
    *last = val;
}

static void info_time_refresh(lv_event_t *event)
{
    static const char *month_str[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug",
        "Sep", "Oct", "Nov", "Dec" };
    static const char *week_str[]  = { "SU", "MO", "TU", "WE", "TH", "FR", "SA" };
    (void)event;

    char       systime[32] = { 0 };
    char       mday[4]     = { 0 };
    time_t     rawtime;
    struct tm *ti;
    time(&rawtime);
    ti = localtime(&rawtime);
    if (!ti)
        return;

    strftime(systime, sizeof(systime), "%I:%M:%S", ti);
    lv_label_set_text(g_info.time_label, systime);
    lv_label_set_text(g_info.month_label, month_str[ti->tm_mon]);
    snprintf(mday, sizeof(mday), "%02d", ti->tm_mday);
    lv_label_set_text(g_info.day_label, mday);
    lv_label_set_text(g_info.week_label, week_str[ti->tm_wday]);
    lv_label_set_text(g_info.ampm_label, ti->tm_hour < 12 ? "AM" : "PM");
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *scr = lv_timer_get_user_data(timer);
    lv_obj_send_event(scr, LV_EVENT_REFRESH, NULL);
}

static lv_obj_t *build_info(void)
{
    lv_obj_t *scr       = screen_img(IMG_INFO_BG);
    lv_obj_t *hash_arc  = make_arc(scr, lv_color_hex(0xe2b020), 217);
    lv_obj_t *power_arc = make_arc(scr, lv_color_hex(0x24b7b1), 219);
    make_img(scr, IMG_INFO_MASK);

    lv_arc_set_mode(power_arc, LV_ARC_MODE_REVERSE);
    lv_arc_set_rotation(hash_arc, 200);
    lv_arc_set_bg_angles(hash_arc, 0, 130);
    lv_arc_set_rotation(power_arc, 30);
    lv_arc_set_bg_angles(power_arc, 0, 130);

    lv_obj_t *mode_label = make_label(scr, FONT_SMALL, "--", 0xdcdddd);
    lv_obj_align(mode_label, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *time_label = make_label(scr, FONT_BIG, "00:00:00", 0xffffff);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -2);

    lv_obj_t *ampm_label = make_label(scr, FONT_SMALL, "PM", 0xb4b5b5);
    lv_obj_align(ampm_label, LV_ALIGN_CENTER, -91, -10);

    lv_obj_t *week_label = make_label(scr, FONT_SMALL, "TU", 0xb4b5b5);
    lv_obj_align(week_label, LV_ALIGN_CENTER, -91, 10);

    lv_obj_t *day_label = make_label(scr, FONT_SMALL, "01", 0xb4b5b5);
    lv_obj_align(day_label, LV_ALIGN_CENTER, 90, -8);

    lv_obj_t *month_label = make_label(scr, FONT_SMALL, "Jan", 0xb4b5b5);
    lv_obj_align(month_label, LV_ALIGN_CENTER, 91, 8);

    lv_obj_t *wifi_img = make_img(scr, IMG_ERR_WIFI);
    lv_obj_align(wifi_img, LV_ALIGN_CENTER, 0, 33);
    lv_obj_add_flag(wifi_img, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *blue_img = make_img(scr, IMG_ERR_BLUE);
    lv_obj_align(blue_img, LV_ALIGN_CENTER, -20, 33);
    lv_obj_add_flag(blue_img, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *filter_img = make_img(scr, IMG_ERR_FILTER);
    lv_obj_align(filter_img, LV_ALIGN_CENTER, 25, 33);
    lv_obj_add_flag(filter_img, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *hash_label = make_label(scr, FONT_BOLD, "0.00", 0xdaa603);
    lv_obj_align(hash_label, LV_ALIGN_CENTER, 0, -65);

    lv_obj_t *power_label = make_label(scr, FONT_SMALL, "0", 0xffffff);
    lv_obj_align(power_label, LV_ALIGN_CENTER, -2, 83);

    lv_obj_t *ip_label = make_label(scr, FONT_SMALL, "0.0.0.0", 0x26b8b1);
    lv_obj_align(ip_label, LV_ALIGN_CENTER, 10, 65);

    g_info.hash_arc    = hash_arc;
    g_info.power_arc   = power_arc;
    g_info.hash_label  = hash_label;
    g_info.power_label = power_label;
    g_info.ip_label    = ip_label;
    g_info.mode_label  = mode_label;
    g_info.time_label  = time_label;
    g_info.month_label = month_label;
    g_info.day_label   = day_label;
    g_info.ampm_label  = ampm_label;
    g_info.week_label  = week_label;
    g_info.bluetooth   = blue_img;
    g_info.filter      = filter_img;
    g_info.wifi        = wifi_img;

    lv_obj_add_event_cb(scr, info_time_refresh, LV_EVENT_REFRESH, NULL);
    lv_obj_send_event(scr, LV_EVENT_REFRESH, NULL);
    lv_timer_create(refresh_timer_cb, 1000, scr);
    return scr;
}

static void watch_refresh(lv_event_t *event)
{
    static const char *month_str[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG",
        "SEP", "OCT", "NOV", "DEC" };
    static const char *week_str[]  = { "SUN", "MO", "TU", "WE", "TH", "FR", "SAT" };
    (void)event;

    time_t     rawtime;
    struct tm *ti;
    char       mday[16] = { 0 };
    time(&rawtime);
    ti = localtime(&rawtime);
    if (!ti)
        return;

    int32_t hour_angle = ((ti->tm_hour % 12) * 300 + HOUR_PIC_OFFSET + ti->tm_min * 5) % MAX_ANGLE;
    int32_t min_angle  = (ti->tm_min * 60 + MIN_PIC_OFFSET) % MAX_ANGLE;
    int32_t sec_angle  = (ti->tm_sec * 60 + SEC_PIC_OFFSET) % MAX_ANGLE;
    lv_image_set_rotation(g_watch.second, sec_angle);
    lv_image_set_rotation(g_watch.minute, min_angle);
    lv_image_set_rotation(g_watch.hour, hour_angle);
    lv_label_set_text(g_watch.month, month_str[ti->tm_mon]);
    snprintf(mday, sizeof(mday), "%d", ti->tm_mday);
    lv_label_set_text(g_watch.day, mday);
    lv_label_set_text(g_watch.week, week_str[ti->tm_wday]);
}

static lv_obj_t *build_watch(void)
{
    lv_obj_t *scr = screen_img(IMG_WATCH_BG);
    make_img(scr, IMG_WATCH_CENTER);
    lv_obj_t *hour = make_img(scr, IMG_WATCH_HOUR);
    lv_obj_align(hour, LV_ALIGN_CENTER, -18, 0);
    lv_obj_t *minute = make_img(scr, IMG_WATCH_MINUTE);
    lv_obj_align(minute, LV_ALIGN_CENTER, 30, 0);
    lv_obj_t *second = make_img(scr, IMG_WATCH_SECOND);
    lv_obj_align(second, LV_ALIGN_CENTER, -1, 30);

    lv_obj_t *month = make_label(scr, FONT_SMALL, "OCT", 0xb5b5b5);
    lv_obj_align(month, LV_ALIGN_CENTER, 60, -15);
    lv_obj_t *day = make_label(scr, FONT_SMALL, "01", 0xb5b5b5);
    lv_obj_align(day, LV_ALIGN_CENTER, 60, 0);
    lv_obj_t *week = make_label(scr, FONT_SMALL, "TU", 0xb5b5b5);
    lv_obj_align(week, LV_ALIGN_CENTER, 60, 15);

    lv_image_set_pivot(second, 1, 20);
    lv_image_set_pivot(hour, 35, 1);
    lv_image_set_pivot(minute, 0, 2);

    g_watch.hour   = hour;
    g_watch.minute = minute;
    g_watch.second = second;
    g_watch.month  = month;
    g_watch.day    = day;
    g_watch.week   = week;

    lv_obj_add_event_cb(scr, watch_refresh, LV_EVENT_REFRESH, NULL);
    lv_obj_send_event(scr, LV_EVENT_REFRESH, NULL);
    lv_timer_create(refresh_timer_cb, 1000, scr);
    return scr;
}

static const char *wallpaper_src(int *is_gif)
{
    static char path[sizeof WALLPAPER_LV_DIR + NAME_MAX];
    *is_gif = 0;
    DIR *d  = opendir(MM_CFGWR_WP_DIR);
    if (!d)
        return NULL;
    const char    *found = NULL;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot)
            continue;
        int gif = strcasecmp(dot, ".gif") == 0;
        if (!gif && strcasecmp(dot, ".png") != 0)
            continue;
        snprintf(path, sizeof path, "%s%s", WALLPAPER_LV_DIR, e->d_name);
        found   = path;
        *is_gif = gif;
        break;
    }
    closedir(d);
    return found;
}

static lv_obj_t *screen_wallpaper(void)
{
    int         gif = 0;
    const char *wp  = wallpaper_src(&gif);
    if (!wp)
        return screen_img(IMG_LOGO);
    return gif ? screen_gif(wp) : screen_img(wp);
}

int ui_screen_init(void)
{
    memset(g_screens, 0, sizeof(g_screens));
    memset(&g_info, 0, sizeof(g_info));

    lv_init();

    lv_display_t *disp = lv_linux_fbdev_create();
    if (!disp)
        return -1;
    lv_linux_fbdev_set_file(disp, "/dev/fb0");

    g_screens[SC_INFO]         = build_info();
    g_screens[SC_TIME]         = build_watch();
    g_screens[SC_HI]           = screen_img(IMG_HI);
    g_screens[SC_BYE]          = screen_img(IMG_BYE);
    g_screens[SC_WALLPAPER]    = screen_wallpaper();
    g_screens[SC_BLACK]        = make_screen();
    g_screens[SC_CONFIG]       = screen_img(IMG_CONFIG);
    g_screens[SC_CHECK_WIFI]   = screen_img(IMG_CHECK_WIFI);
    g_screens[SC_OVER_HOT]     = screen_img(IMG_OVER_HOT);
    g_screens[SC_ADAPTER]      = screen_img(IMG_ADAPTER);
    g_screens[SC_NIGHT]        = screen_img(IMG_NIGHT);
    g_screens[SC_START_MINING] = screen_img(IMG_START_MINING);
    g_screens[SC_FILTER]       = screen_img(IMG_FILTER);
    g_screens[SC_ERRORCODE]    = screen_errorcode("");
    g_screens[SC_NO_USB]       = screen_img(IMG_NO_USB);
    g_screens[SC_CHECK_POOL]   = screen_img(IMG_CHECK_POOL);
    g_screens[SC_FAULT]        = screen_fault();
    g_screens[SC_PELISH]       = screen_pelish();

    lv_screen_load_anim(g_screens[SC_BLACK], LV_SCR_LOAD_ANIM_FADE_IN, 0, 0, false);
    return 0;
}

void ui_screen_deinit(void)
{
    lv_deinit();
    memset(g_screens, 0, sizeof(g_screens));
}

static int id_to_slot(uint16_t id)
{
    switch (id) {
    case UI_OFF:
        return SC_BLACK;
    case UI_SUMMARY:
        return SC_INFO;
    case UI_TIME:
        return SC_TIME;
    case UI_WALLPAPER:
        return SC_WALLPAPER;
    case UI_PELISH:
        return SC_PELISH;
    case UI_CONFIG_NETWORK:
        return SC_CONFIG;
    case UI_LOST_CONNECT:
        return SC_CHECK_WIFI;
    case UI_POWER_ERR:
        return SC_ADAPTER;
    case UI_FAN_ERR:
        return SC_ERRORCODE;
    case UI_OVERHOT:
        return SC_OVER_HOT;
    case UI_FILTER_DIRTY:
        return SC_FILTER;
    case UI_USB_DEVICE:
        return SC_NO_USB;
    case UI_MINING:
        return SC_START_MINING;
    case UI_HI:
        return SC_HI;
    case UI_BYE:
        return SC_BYE;
    case UI_BLACK:
        return SC_BLACK;
    case UI_NIGHT:
        return SC_NIGHT;
    case UI_HASH_ERR:
    case UI_FAN_STALL:
    case UI_AGING_ERR:
    case UI_POWER_FAULT:
        return SC_FAULT;
    case UI_CHECK_POOL:
        return SC_CHECK_POOL;
    default:
        return -1;
    }
}

void ui_screen_render(uint16_t id)
{
    int slot = id_to_slot(id);
    if (slot < 0 || !g_screens[slot])
        return;

    const char *code, *desc;
    if (ui_errcode(id, &code, &desc) == 0) {
        if (slot == SC_ERRORCODE && g_errcode_label) {
            lv_label_set_text(g_errcode_label, code);
        } else if (slot == SC_FAULT && g_fault_code && g_fault_desc) {
            lv_label_set_text(g_fault_code, code);
            lv_label_set_text(g_fault_desc, desc);
        }
    }

    uint32_t              t = 1000, delay = 100;
    lv_screen_load_anim_t anim = LV_SCR_LOAD_ANIM_FADE_IN;
    if (slot == SC_BLACK) {
        t     = 0;
        delay = 0;
        anim  = LV_SCR_LOAD_ANIM_NONE;
    }
    lv_screen_load_anim(g_screens[slot], anim, t, delay, false);
}

void ui_screen_reload_wallpaper(void)
{
    lv_obj_t *fresh = screen_wallpaper();
    if (!fresh)
        return;
    lv_obj_t *old           = g_screens[SC_WALLPAPER];
    g_screens[SC_WALLPAPER] = fresh;

    if (old && lv_screen_active() == old)
        lv_screen_load_anim(fresh, LV_SCR_LOAD_ANIM_FADE_IN, 1000, 100, true);
    else if (old)
        lv_obj_delete(old);
}

static void set_flag(lv_obj_t *obj, int shown)
{
    if (!obj)
        return;
    if (shown)
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void ui_screen_refresh(const ui_summary *s)
{
    if (!s)
        return;

    static int hash_arc_last  = ARC_BUILT_VAL;
    static int power_arc_last = ARC_BUILT_VAL;

    if (g_info.ip_label)
        lv_label_set_text(g_info.ip_label, s->ip);
    if (g_info.hash_label)
        lv_label_set_text(g_info.hash_label, s->hashrate);
    if (g_info.mode_label)
        lv_label_set_text(g_info.mode_label, s->level);
    if (g_info.power_label)
        lv_label_set_text(g_info.power_label, s->power);

    if (g_info.hash_arc)
        arc_animate(g_info.hash_arc, &hash_arc_last,
                (s->ready & UI_RDY_HASH) ? lv_map(s->ghs, 0, HASH_ARC_MAX, 0, 100) : 0);
    if (g_info.power_arc)
        arc_animate(g_info.power_arc, &power_arc_last,
                (s->ready & UI_RDY_POWER) ? lv_map(s->watt, POWER_ARC_MIN, POWER_ARC_MAX, 0, 100) :
                                            0);

    set_flag(g_info.bluetooth, s->err_flag & (0x1 << UI_ERR_BLUETOOTH));
    set_flag(g_info.wifi, s->err_flag & (0x1 << UI_ERR_WIFI));
    set_flag(g_info.filter, s->err_flag & (0x1 << UI_ERR_FILTER));
}
