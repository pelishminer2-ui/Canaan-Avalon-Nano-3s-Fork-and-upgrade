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

#ifndef BOARD_NANO3S_H
#define BOARD_NANO3S_H

#define CAP_HASHBOARD  1
#define CAP_FAN        1
#define CAP_FAN_TACH   1
#define CAP_HB_MCU     0 /* 1 when each hashboard slot has a monitor MCU */
#define CAP_LED_STRIP  1
#define CAP_LCD        1
#define CAP_BUTTON     1
#define CAP_BEEP       1
#define CAP_ACTIVATION 1
#define NET_WIRED_ALT  1 /* 1 when a USB hub may carry WiFi and USB Ethernet together */
/* 1 when the power supply is a PD input with a DC-DC core rail */
#define IMPL_PSU_IS_DCDC_PD        1
#define PD_POWER_RESERVE_W         3u /* power held back from the PD input limit, W */
#define IMPL_BOARD_TEMP_IS_ADC_NTC 1
#define IMPL_NET_IS_WIRED          0
#define IMPL_NET_IS_WIFI           1

#define IMPL_BACKLIGHT_IS_GPIO   1
#define POWER_VALID_VOLT_MIN     8000 /* INA226 PD input voltage below which supply is absent, mV */
#define BOARD_CALI_HEADER        "board_cali_nano3s.h" /* header defining BOARD_CALI_ROWS */
#define PS_IOUT_PER_A            1000                  /* PSU output current units per A */
#define LED_PIXEL_COUNT          (9)
#define BACKLIGHT_GPIO           (25)
#define BOARD_NTC_ADC_CH         (1)
#define BOARD_NTC_R_PULLUP       (10000) /* NTC divider pull-up resistance, ohms */
#define BOARD_NTC_R_25           (10000) /* NTC resistance at 25 degC, ohms */
#define BOARD_NTC_B_CONST        (3950)  /* NTC B constant, K */
#define FAN_PWM_ACTIVE_LOW       (0)
#define FAN_DUTY_MIN_PCT         (10)
#define FAN_DUTY_INIT_PCT        (25) /* fan duty below the PID temperature band, percent */
#define FAN_DUTY_MAX_PCT         (100)
#define FAN_DUTY_DEFAULT_PCT     (25) /* fan duty at startup, percent */
#define FAN_DUTY_API_SOFTOFF_PCT (10)
#define FAN_DUTY_SOFTOFF_PCT     (25) /* fan duty during a thermal or fan-fault soft-off, percent */
#define FAN_DUTY_ENUM_PCT        (0)  /* fan duty during enumeration, 0 = not pinned, percent */
/* fan duty during a hash fault, percent; 0 = clamped to FAN_DUTY_MIN_PCT */
#define FAN_DUTY_HASH_FAULT_PCT (0)
#define FAN_TACH_PULSES_PER_REV (3)
#define FAN_TARGET_TEMP         (90) /* fan target temperature before calibration loads, degC */
#define FAN_PID_P               (35)
#define FAN_PID_I               (1)
#define FAN_PID_D               (1)
#define FAN_PID_TEMP_MIN        (50)  /* lowest temperature of the fan PID band, degC */
#define FAN_PID_TEMP_MAX        (100) /* highest temperature of the fan PID band, degC */
/* largest PID output increase per step, 0 = unlimited, steps */
#define FAN_PID_DELTA_UP       (50)
#define FAN_PID_ERR_QUANT      (1) /* 1 truncates the PID error toward zero to 0.5 degC */
#define FAN_PID_DUTY_WHOLE_PCT (0) /* 1 truncates the PID duty to whole percent */
#define FAN_PID_INSTART_HOLD   (0) /* 1 holds full duty until the first chip temperature arrives */
#define PVT_TEMP_OVER_HR_MAX   (113) /* chip temperature that trips the thermal soft-off, degC */
/* hashboard outlet temperature that trips the thermal soft-off, degC */
#define HASHOUT_OVER_HR_MAX (95)
/* consecutive hot outlet samples that trip the thermal soft-off, 0 = disarmed */
#define HASHOUT_OVERHOT_CNT_MAX (0)
#define ADG_STAGE_COUNT_UP      (6) /* aging up-sweep stages */
#define ADG_STAGE_COUNT_DOWN    (3) /* aging down-sweep stages */
#define POUT_UP_LIMIT           (1) /* output power the servo accepts above the row max_pout, W */
/* output power the servo accepts below the row max_pout, negative, W */
#define POUT_DOWN_LIMIT         (-2)
#define MAX_POUT_LIMIT_ABNORMAL (155) /* invalid reading above this; a run latches a fault, W */
/*
 * Stock USB-PD software ceiling. Mode 2 cali is 133 W; 133 + PD_POWER_RESERVE_W
 * fits this adapter. A larger PD advertisement is clamped here unless the
 * aftermarket opt-in is on.
 */
#define PD_STOCK_CAP_W       (140u)
/*
 * Mean Well LRS-150-class software ceiling. Stays under MAX_POUT_LIMIT_ABNORMAL,
 * including the servo's POUT_UP_LIMIT: 150 + 1 < 155. Not a chip-damage change.
 */
#define PD_AFTERMARKET_CAP_W (150u)
/*
 * Mild board autotune. 0 leaves aging and the V/F search in charge.
 * Multi-chip on this board means every A3197S die asic_miner already drives,
 * not a BM1370 chain. Steps use the Canaan PLL interval only.
 */
#define AUTOTUNE_OFF            0
#define AUTOTUNE_MILD           1
#define AUTOTUNE_DEFAULT        AUTOTUNE_OFF
#define AUTOTUNE_TEMP_MARGIN_C  6 /* degC under the cali fan target before a step up */
#define AUTOTUNE_POWER_MARGIN_W 5 /* watts under the active ceiling before a step up */
#define AUTOTUNE_MILD_STEPS     2 /* PLL intervals the mild tune may sit above pll_start */
/* Fan P gain while mild cooling is on. The temperature band is not moved. */
#define FAN_PID_P_MILD ((FAN_PID_P) + 8)
#define WORK_MODE_DEFAULT_ID    2
#define WORK_LEVEL_COUNT        1 /* work levels per work mode */
/* Inclusive [lo, hi] spans of the work modes ascset workmode and work_mode_lvl accept. */
#define BOARD_WORKMODE_RANGES(X) X(0, 2)
#define I2C_PD_DEV               "/dev/i2c-2" /* power supply control I2C bus device path */
#define I2C_POWER_E2P_DEV        "/dev/i2c-3" /* DC-DC and hashboard EEPROM I2C bus device path */
#define SCAN_CADENCE_MS          (30)         /* nonce scan sleep, ms */
#define MM_NET_IFACE             "wlan0"
#define NET_WIRED_IFACE          "eth0" /* ASIX/RTL/SMSC USB Ethernet */
#define NET_USB_IFACE            "usb0" /* CDC-ECM/NCM/EEM and RNDIS */
#define DEVNAME_PRE              "nan3s_" /* LAN and BLE device name prefix */
/* clang-format off */
/* fan rows: BOARD_FAN_ROW(PWM pad, tachometer device path, pulses per revolution) */
#define BOARD_FAN_ROWS \
    BOARD_FAN_ROW(3, "/dev/timer5", FAN_TACH_PULSES_PER_REV),
/* hashboard slot rows: { ASIC UART device path or NULL, RI GPIO or 0 } */
#define BOARD_HBOARD_ROWS \
    { "/dev/ttyS3", 31 },
#define BOARD_CTRL_OPS \
    CTRL_OP_WORKMODE, CTRL_OP_LEDMODE, CTRL_OP_LEDSET, \
    CTRL_OP_NIGHTLAMP, \
    CTRL_OP_FILTER_CLEAN, CTRL_OP_LCD, CTRL_OP_SOFTON, \
    CTRL_OP_SOFTOFF, CTRL_OP_SETTZ, CTRL_OP_WP_RELOAD, CTRL_OP_REBOOT
/* clang-format on */

_Static_assert(PD_STOCK_CAP_W == 140u, "stock PD software cap is the 140 W adapter");
_Static_assert(PD_AFTERMARKET_CAP_W > PD_STOCK_CAP_W,
        "aftermarket cap is above the stock PD cap");
_Static_assert(PD_AFTERMARKET_CAP_W < MAX_POUT_LIMIT_ABNORMAL,
        "aftermarket cap stays under the power-fault trip");
_Static_assert(PD_AFTERMARKET_CAP_W + POUT_UP_LIMIT < MAX_POUT_LIMIT_ABNORMAL,
        "servo headroom above the aftermarket cap stays under the power-fault trip");
_Static_assert(AUTOTUNE_DEFAULT == AUTOTUNE_OFF, "autotune defaults off");
_Static_assert(FAN_PID_P_MILD > FAN_PID_P, "mild cooling does not reduce the P gain");
_Static_assert(PVT_TEMP_OVER_HR_MAX == 113, "chip thermal trip stays 113 C");
_Static_assert(HASHOUT_OVER_HR_MAX == 95, "outlet thermal trip stays 95 C");
_Static_assert(FAN_PID_TEMP_MAX == 100, "fan band top stays 100 C");

#endif /* BOARD_NANO3S_H */
