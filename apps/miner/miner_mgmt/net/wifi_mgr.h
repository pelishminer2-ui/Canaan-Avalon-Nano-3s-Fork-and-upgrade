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

#ifndef WIFI_MGR_H
#define WIFI_MGR_H

#include <stdint.h>

#define WIFI_DEV_STA_NAME    "wlan0"
#define WIFI_SSID_OCTETS_MAX 32 /* IEEE 802.11 SSID maximum, raw octets */
/* SSID maximum in wpa_supplicant escaped form, chars */
#define WIFI_SSID_ESC_CHARS_MAX 128
/* escaped SSID maximum as ASCII hex, chars */
#define WIFI_SSID_HEX_CHARS_MAX 256
/* SSID/credential buffer size, bytes, including NUL */
#define WIFI_SSID_LEN_MAX 129
#define WIFI_ACT_PROV     (1 << 10)

typedef struct {
    unsigned int auth;                        /* security type, WIFI_AUTH_* */
    char         name[WIFI_SSID_LEN_MAX];     /* SSID, wpa_supplicant escaped form */
    char         passwd[WIFI_SSID_LEN_MAX];   /* passphrase or EAP password */
    char         identity[WIFI_SSID_LEN_MAX]; /* EAP identity */
} wifi_ssid_t;

int  wifi_init(char *name);
void wifi_startup(void);
void wifi_exception(void);
void wifi_action_set(unsigned int);
int  wifi_update_ssid_info(char *ssid, int *rssi);
void wifi_rssi_refresh(void *ctx, uint64_t now_ms);
int  wifi_get_state(char *ip, int ip_len);
int  wifi_carrier_up(void);
int  wifi_has_ssid(void);
void wifi_link_repair_tick(void *ctx, uint64_t now_ms);
void wifi_cfg_freeze(int on);
void wifi_eth_hold_set(int hold);
int  wifi_eth_held(void);

#endif /* WIFI_MGR_H */
