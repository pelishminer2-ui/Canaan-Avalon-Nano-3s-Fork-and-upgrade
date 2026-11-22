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

#ifndef BOARD_ASSERT_H
#define BOARD_ASSERT_H

#ifndef CAP_HASHBOARD
#error "CAP_HASHBOARD undeclared: presence is never expressed by omission"
#endif
#ifndef CAP_FAN
#error "CAP_FAN undeclared: presence is never expressed by omission"
#endif
#ifndef FAN_COUNT
#error "FAN_COUNT undeclared: the fan topology is a per-board fact"
#endif
#ifndef CAP_FAN_TACH
#error "CAP_FAN_TACH undeclared: presence is never expressed by omission"
#endif
#ifndef CAP_HB_MCU
#error "CAP_HB_MCU undeclared: gates the thermal soft-off's hashboard-outlet arm"
#endif
#if CAP_HB_MCU && !defined(I2C_MUX_RESET_GPIO)
_Static_assert(0,
        "I2C_MUX_RESET_GPIO undeclared: a board with hashboard MCUs names its I2C mux reset_n "
        "GPIO, or -1 if it has none");
#endif
#ifndef I2C_MUX_RESET_GPIO
#define I2C_MUX_RESET_GPIO (-1) /* no I2C mux reset GPIO */
#endif
#ifndef CAP_LED_STRIP
#error "CAP_LED_STRIP undeclared: presence is never expressed by omission"
#endif
#ifndef CAP_LCD
#error "CAP_LCD undeclared: presence is never expressed by omission"
#endif
#ifndef CAP_BUTTON
#error "CAP_BUTTON undeclared: presence is never expressed by omission"
#endif
#ifndef CAP_BEEP
#error "CAP_BEEP undeclared: presence is never expressed by omission"
#endif
#ifndef NET_WIRED_ALT
#error "NET_WIRED_ALT undeclared: presence is never expressed by omission"
#endif
#if NET_WIRED_ALT && !defined(NET_WIRED_IFACE)
#error "NET_WIRED_ALT but NET_WIRED_IFACE is undeclared: the alternative has no interface to drive"
#endif
#if NET_WIRED_ALT && !defined(NET_USB_IFACE)
#error "NET_WIRED_ALT but NET_USB_IFACE is undeclared: CDC/RNDIS adapters appear as usb0"
#endif
#ifndef MM_NET_IFACE
_Static_assert(0,
        "MM_NET_IFACE undeclared: the product's one NIC, pasted into every "
        "ifconfig/udhcpc/wpa_cli command line and read for the MAC; omitting it is an "
        "undeclared identifier inside shared files that name neither the key nor the board");
#endif
#ifndef DEVNAME_PRE
_Static_assert(0,
        "DEVNAME_PRE undeclared: the box's LAN/BLE device-name prefix, which the phone app "
        "prefix-matches before taking split('_').last as the DNA; it ends in '_', and a default "
        "would announce a new product under nano3s' identity, so the app would match it as a "
        "nano3s and look up a nano3s by that DNA");
#endif

#ifndef WORK_MODE_DEFAULT_ID
#error "WORK_MODE_DEFAULT_ID undeclared: 0|1|2"
#endif
#ifndef WORK_LEVEL_COUNT
#error "WORK_LEVEL_COUNT undeclared: bounds every worklevel write"
#endif
#ifndef BOARD_WORKMODE_RANGES
#error "BOARD_WORKMODE_RANGES undeclared: the X(lo, hi) spans of work modes ascset accepts"
#endif
#ifndef PS_IOUT_PER_A
_Static_assert(0,
        "PS_IOUT_PER_A undeclared: ps_iout_ma units per amp; 1000 for a mA shunt, 1 for a PSU "
        "that reports whole amps");
#endif
#ifndef BOARD_CALI_HEADER
#error "BOARD_CALI_HEADER undeclared: the hal/board/ header with the V/F rows"
#endif

#ifndef VOL_MIN
_Static_assert(0,
        "VOL_MIN undeclared: the core-rail floor in the board's own rail counts, bound at every "
        "rail write");
#endif
#ifndef VOL_MAX
_Static_assert(0,
        "VOL_MAX undeclared: the core-rail ceiling in the board's own rail counts, bound at "
        "every rail write");
#endif
#ifndef VOL_ENUM
#error "VOL_ENUM undeclared: the rail the board enumerates at, in its own rail counts"
#endif
#ifndef INIT_FREQ
_Static_assert(0,
        "INIT_FREQ undeclared: the PLL park frequency in MHz that correct_freq() floors at and "
        "the chain is programmed to before any ramp; an unstated value is another board's park "
        "point");
#endif
#ifndef SCAN_CADENCE_MS
_Static_assert(0,
        "SCAN_CADENCE_MS undeclared: the asic_miner nonce-scan sleep in ms; nano3s runs 30 and "
        "x3 8, so an unstated value spins another board's UART poll rate");
#endif
#ifndef POWER_VALID_VOLT_MIN
_Static_assert(0,
        "POWER_VALID_VOLT_MIN undeclared: compared against the DC-DC PD input voltage measured "
        "by the INA226 in mV, so a shared literal latches a power fault at boot");
#endif
#ifndef FAN_PWM_ACTIVE_LOW
_Static_assert(0,
        "FAN_PWM_ACTIVE_LOW undeclared: 0|1, the carrier's PWM sense; get it wrong on an "
        "active-low carrier and the thermal soft-off max-airflow pin becomes ZERO airflow");
#endif

#ifndef FAN_DUTY_MIN_PCT
_Static_assert(0,
        "FAN_DUTY_MIN_PCT undeclared: this board's airflow floor in percent; nano3s runs 10 and "
        "x3 20");
#endif
#ifndef FAN_DUTY_MAX_PCT
#error "FAN_DUTY_MAX_PCT undeclared: this board's airflow ceiling in percent"
#endif
#ifndef FAN_DUTY_INIT_PCT
_Static_assert(0,
        "FAN_DUTY_INIT_PCT undeclared: the PID's below-band duty in percent; nano3s runs 25 and "
        "x3 30");
#endif
#ifndef FAN_DUTY_DEFAULT_PCT
_Static_assert(0,
        "FAN_DUTY_DEFAULT_PCT undeclared: the startup duty in percent, a different role from "
        "FAN_DUTY_INIT_PCT; nano3s runs 25 and x3 60");
#endif
#ifndef FAN_DUTY_API_SOFTOFF_PCT
_Static_assert(0,
        "FAN_DUTY_API_SOFTOFF_PCT undeclared: the API soft-off duty in percent, a different "
        "role from FAN_DUTY_MIN_PCT");
#endif
#ifndef FAN_DUTY_SOFTOFF_PCT
_Static_assert(0,
        "FAN_DUTY_SOFTOFF_PCT undeclared: the duty the thermal soft-off pins the fan to while "
        "the rail is down; nano3s runs 25 and x3 100");
#endif
#ifndef FAN_DUTY_ENUM_PCT
_Static_assert(0,
        "FAN_DUTY_ENUM_PCT undeclared: the duty pinned across enumeration, 0 = no pin; nano3s "
        "runs 0 and x3 80");
#endif
#ifndef FAN_DUTY_HASH_FAULT_PCT
_Static_assert(0,
        "FAN_DUTY_HASH_FAULT_PCT undeclared: the duty pinned while a hash fault holds the rail "
        "down; nano3s runs 0 and x3 20");
#endif

#ifndef FAN_TARGET_TEMP
#error "FAN_TARGET_TEMP undeclared: the boot fan target before cali resolves, per board"
#endif
#ifndef PVT_TEMP_OVER_HR_MAX
_Static_assert(0,
        "PVT_TEMP_OVER_HR_MAX undeclared: the thermal soft-off's chip-PVT trip point in degC, "
        "mandatory on every product");
#endif
#ifndef HASHOUT_OVER_HR_MAX
_Static_assert(0,
        "HASHOUT_OVER_HR_MAX undeclared: the thermal soft-off's hashboard-outlet trip point in "
        "degC, mandatory on every product");
#endif
#ifndef HASHOUT_OVERHOT_CNT_MAX
_Static_assert(0,
        "HASHOUT_OVERHOT_CNT_MAX undeclared: consecutive hot outlet samples before the thermal "
        "soft-off trips; 0 is the way to disarm that arm, omission is not");
#endif
/* consecutive stalled fan samples that latch a fan fault, 0 = no stall check */
#define FAN_BELOW_STALL_CNT ((CAP_FAN && CAP_FAN_TACH) ? 5 : 0)

#ifndef ADG_STAGE_COUNT_UP
_Static_assert(0,
        "ADG_STAGE_COUNT_UP undeclared: aging up-sweep stages, per board; nano3s runs 6 and x3 "
        "9, so an unstated value is another board's sweep length");
#endif
#ifndef ADG_STAGE_COUNT_DOWN
#error "ADG_STAGE_COUNT_DOWN undeclared: aging down-sweep stages, per board"
#endif
#ifndef POUT_UP_LIMIT
_Static_assert(0,
        "POUT_UP_LIMIT undeclared: watts above the row's max_pout the pout servo will accept, "
        "in this board's own PSU units; nano3s runs 1 and x3 7");
#endif
#ifndef POUT_DOWN_LIMIT
_Static_assert(0,
        "POUT_DOWN_LIMIT undeclared: watts below the row's max_pout the pout servo will accept, "
        "negative, in this board's own PSU units; nano3s runs -2 and x3 -7");
#endif
#ifndef MAX_POUT_LIMIT_ABNORMAL
#error "MAX_POUT_LIMIT_ABNORMAL undeclared: output power limit"
#endif

#if CAP_HB_MCU && !defined(I2C_MCU_DEV)
#error "CAP_HB_MCU is 1 but I2C_MCU_DEV is undeclared: the MCU i2c slot would carry no node"
#endif
#ifndef LED_PIXEL_COUNT
#error "LED_PIXEL_COUNT undeclared: pixels on the LED strip; a board without a strip declares 1"
#endif
#if IMPL_BOARD_TEMP_IS_ADC_NTC && !defined(BOARD_NTC_ADC_CH)
_Static_assert(0,
        "IMPL_BOARD_TEMP_IS_ADC_NTC but BOARD_NTC_ADC_CH is undeclared: the wrong iio channel "
        "reads a different sensor and still decodes to a plausible degC");
#endif
#if IMPL_BOARD_TEMP_IS_ADC_NTC && !defined(BOARD_NTC_R_PULLUP)
_Static_assert(0,
        "IMPL_BOARD_TEMP_IS_ADC_NTC but BOARD_NTC_R_PULLUP is undeclared: the divider's "
        "pull-up, in ohms");
#endif
#if IMPL_BOARD_TEMP_IS_ADC_NTC && !defined(BOARD_NTC_R_25)
_Static_assert(0,
        "IMPL_BOARD_TEMP_IS_ADC_NTC but BOARD_NTC_R_25 is undeclared: the thermistor's 25 degC "
        "resistance, in ohms");
#endif
#if IMPL_BOARD_TEMP_IS_ADC_NTC && !defined(BOARD_NTC_B_CONST)
_Static_assert(0,
        "IMPL_BOARD_TEMP_IS_ADC_NTC but BOARD_NTC_B_CONST is undeclared: the thermistor's B "
        "constant, in kelvin");
#endif
#if IMPL_BACKLIGHT_IS_GPIO && !defined(BACKLIGHT_GPIO)
_Static_assert(0,
        "IMPL_BACKLIGHT_IS_GPIO but BACKLIGHT_GPIO is undeclared: the GPIO the panel's "
        "backlight enable is wired to");
#endif
#ifndef PD_POWER_RESERVE_W
#error "PD_POWER_RESERVE_W undeclared: watts held back from the PSU's pout_limit in mode admission"
#endif

#ifndef BOARD_FAN_ROWS
#error "BOARD_FAN_ROWS undeclared: one row per fan, see hal/board/board_tables.h"
#endif
#ifndef BOARD_HBOARD_ROWS
#error "BOARD_HBOARD_ROWS undeclared: one row per hashboard, see board_tables.h"
#endif
#if CAP_HB_MCU && !defined(BOARD_HB_MCU_ROWS)
#error "CAP_HB_MCU is 1 but BOARD_HB_MCU_ROWS is undeclared: one row per hashboard monitor MCU"
#endif
#ifndef BOARD_CTRL_OPS
#error "BOARD_CTRL_OPS undeclared: the CTRL_OP_* this product exposes on :4028, or CTRL_OP_NONE"
#endif

_Static_assert(CAP_HASHBOARD == 0 || CAP_HASHBOARD == 1, "CAP_HASHBOARD is 0 or 1");
_Static_assert(CAP_FAN == 0 || CAP_FAN == 1, "CAP_FAN is 0 or 1");
_Static_assert(CAP_FAN_TACH == 0 || CAP_FAN_TACH == 1, "CAP_FAN_TACH is 0 or 1");
_Static_assert(CAP_HB_MCU == 0 || CAP_HB_MCU == 1, "CAP_HB_MCU is 0 or 1");
_Static_assert(CAP_LED_STRIP == 0 || CAP_LED_STRIP == 1, "CAP_LED_STRIP is 0 or 1");
_Static_assert(CAP_LCD == 0 || CAP_LCD == 1, "CAP_LCD is 0 or 1");
_Static_assert(CAP_BUTTON == 0 || CAP_BUTTON == 1, "CAP_BUTTON is 0 or 1");
_Static_assert(CAP_BEEP == 0 || CAP_BEEP == 1, "CAP_BEEP is 0 or 1");
#ifdef CAP_ACTIVATION
_Static_assert(CAP_ACTIVATION == 1, "CAP_ACTIVATION is 1 or undefined");
#endif
_Static_assert(NET_WIRED_ALT == 0 || NET_WIRED_ALT == 1, "NET_WIRED_ALT is 0 or 1");
_Static_assert(NET_WIRED_ALT == 0 || IMPL_NET_IS_WIFI,
        "NET_WIRED_ALT says the USB host may carry a WiFi dongle and a USB "
        "Ethernet adapter at once (hub), with wifi as the primary NIC "
        "(IMPL_NET_IS_WIFI); a board whose wired NIC is its only NIC "
        "declares that through IMPL_NET_IS_*, not here");

_Static_assert(IMPL_PSU_IS_DCDC_PD == 1,
        "IMPL_PSU_IS_DCDC_PD is 1: hal_psu() is defined for that kind only "
        "(hal/platform/linux/psu_dcdc_pd.c), so any other kind is an unresolved symbol at link "
        "time rather than a diagnosed descriptor");
_Static_assert(IMPL_BOARD_TEMP_IS_ADC_NTC == 1,
        "IMPL_BOARD_TEMP_IS_ADC_NTC is 1: hal_temp() is defined for that kind only "
        "(hal/platform/linux/temp_adc_ntc.c), so any other kind is an unresolved symbol at link "
        "time rather than a diagnosed descriptor");
_Static_assert(IMPL_NET_IS_WIRED + IMPL_NET_IS_WIFI == 1, "exactly one IMPL_NET_IS_* is 1");
_Static_assert(IMPL_BACKLIGHT_IS_GPIO == 1,
        "IMPL_BACKLIGHT_IS_GPIO is 1: hal_ui_backlight_set() drives a GPIO-switched backlight "
        "only (hal/ui/ui_backlight.c)");

_Static_assert(WORK_MODE_DEFAULT_ID >= 0 && WORK_MODE_DEFAULT_ID < 3,
        "WORK_MODE_DEFAULT_ID is a mode number 0..2");
/* BOARD_WORKMODE_RANGES term: WORK_MODE_DEFAULT_ID lies in [lo, hi] */
#define WORK_MODE_DEFAULT_IN_SPAN(lo, hi) \
    (WORK_MODE_DEFAULT_ID >= (lo) && WORK_MODE_DEFAULT_ID <= (hi)) ||
_Static_assert(BOARD_WORKMODE_RANGES(WORK_MODE_DEFAULT_IN_SPAN) 0,
        "WORK_MODE_DEFAULT_ID lies outside BOARD_WORKMODE_RANGES");
#undef WORK_MODE_DEFAULT_IN_SPAN
_Static_assert(WORK_LEVEL_COUNT >= 1, "WORK_LEVEL_COUNT is at least 1");
_Static_assert(FAN_COUNT >= 1, "FAN_COUNT is at least 1");
_Static_assert(HBOARD_COUNT >= 1, "HBOARD_COUNT is at least 1");
_Static_assert(FAN_PWM_ACTIVE_LOW == 0 || FAN_PWM_ACTIVE_LOW == 1, "FAN_PWM_ACTIVE_LOW is 0 or 1");
_Static_assert(MAX_ASIC_ON_SINGLE_HASH >= 1, "MAX_ASIC_ON_SINGLE_HASH is at least 1");

_Static_assert(FAN_DUTY_MIN_PCT >= 0 && FAN_DUTY_MIN_PCT <= 100,
        "FAN_DUTY_MIN_PCT is a percentage, 0..100");
_Static_assert(FAN_DUTY_MAX_PCT >= 0 && FAN_DUTY_MAX_PCT <= 100,
        "FAN_DUTY_MAX_PCT is a percentage, 0..100");
_Static_assert(FAN_DUTY_INIT_PCT >= 0 && FAN_DUTY_INIT_PCT <= 100,
        "FAN_DUTY_INIT_PCT is a percentage, 0..100");
_Static_assert(FAN_DUTY_DEFAULT_PCT >= 0 && FAN_DUTY_DEFAULT_PCT <= 100,
        "FAN_DUTY_DEFAULT_PCT is a percentage, 0..100");
_Static_assert(FAN_DUTY_API_SOFTOFF_PCT >= 0 && FAN_DUTY_API_SOFTOFF_PCT <= 100,
        "FAN_DUTY_API_SOFTOFF_PCT is a percentage, 0..100");
_Static_assert(FAN_DUTY_SOFTOFF_PCT >= 0 && FAN_DUTY_SOFTOFF_PCT <= 100,
        "FAN_DUTY_SOFTOFF_PCT is a percentage, 0..100");
_Static_assert(FAN_DUTY_ENUM_PCT >= 0 && FAN_DUTY_ENUM_PCT <= 100,
        "FAN_DUTY_ENUM_PCT is a percentage, 0..100");
_Static_assert(FAN_DUTY_HASH_FAULT_PCT >= 0 && FAN_DUTY_HASH_FAULT_PCT <= 100,
        "FAN_DUTY_HASH_FAULT_PCT is a percentage, 0..100");
_Static_assert(VOL_MIN <= VOL_MAX,
        "VOL_MIN is the core-rail floor and VOL_MAX the ceiling, in this board's own rail counts");
_Static_assert(VOL_ENUM >= VOL_MIN && VOL_ENUM <= VOL_MAX,
        "VOL_ENUM is the rail this board enumerates at and must sit inside its own "
        "[VOL_MIN..VOL_MAX] window, in the same DC-DC rail counts (mV): a seed outside the "
        "window is commanded before any clamp-bearing servo runs");

_Static_assert(FAN_DUTY_MIN_PCT <= FAN_DUTY_MAX_PCT,
        "FAN_DUTY_MIN_PCT is the airflow floor and FAN_DUTY_MAX_PCT the ceiling");

_Static_assert(LED_PIXEL_COUNT >= 1,
        "LED_PIXEL_COUNT sizes the frame buffers ws2812.c, led_fx.c and "
        "mm_led.c compile on every product, so it is at least 1 "
        "even where CAP_LED_STRIP is 0");

_Static_assert(HASHOUT_OVERHOT_CNT_MAX == 0 || CAP_HB_MCU,
        "the thermal soft-off's outlet arm is armed but CAP_HB_MCU is 0: with no monitor MCU "
        "every board reads the ASIC_TEMP_NONE sentinel, which resets the consecutive-hot run on "
        "every sample, so the trip never fires");

/*
 * The SUPPORT_LCD / SUPPORT_BLE descriptor keys and the board keys describe
 * the same hardware. Checked on the product build only: host tests pair boards
 * with options on purpose.
 */
#ifdef MINER_PRODUCT_BUILD
#if defined(SUPPORT_LCD) != CAP_LCD
#error "boardconf.ini SUPPORT_LCD must be 1 exactly when the board has a screen (CAP_LCD 1)"
#endif
#if defined(SUPPORT_BLE) != IMPL_NET_IS_WIFI
#error "boardconf.ini SUPPORT_BLE must be 1 exactly when the network is WiFi (IMPL_NET_IS_WIFI 1)"
#endif
#if defined(SUPPORT_LCD) && !defined(SUPPORT_BLE)
#error "boardconf.ini SUPPORT_LCD needs SUPPORT_BLE: the LCD UI assumes BLE provisioning"
#endif
#endif /* MINER_PRODUCT_BUILD */

#endif /* BOARD_ASSERT_H */
