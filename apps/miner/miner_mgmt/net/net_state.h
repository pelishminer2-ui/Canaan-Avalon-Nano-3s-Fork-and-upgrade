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

#ifndef NET_STATE_H
#define NET_STATE_H

#include <stddef.h>

#define NET_WIFI_START_SUPP (1u << 0) /* start wpa_supplicant */
#define NET_WIFI_START_CLI  (1u << 1) /* start the wpa_cli action monitor */

/*
 * Network state for every consumer (LCD, UDP discovery, summary screen), so
 * all of them agree on whether the box is online. The USB host may carry a
 * WiFi dongle, a USB Ethernet adapter, or both on a hub; IPv4 prefers
 * eth0/usb0 then falls back to wlan0.
 */

int net_state_ip(char *ip, size_t cap);

enum {
    NET_STATE_DOWN       = 0,
    NET_STATE_CONNECTING = 1,
    NET_STATE_UP         = 2, /* connected with an IPv4 address */
};

int net_state_get(void);
int net_iface_ipv4(const char *name, char *ip, size_t cap);

/*
 * Whether wlan0's DHCP client needs attention after a dongle replug. Pure
 * decision, kept apart from the syscalls so it is host-testable.
 */
typedef enum {
    NET_WIFI_REPAIR_NONE = 0, /* leave it alone */
    NET_WIFI_REPAIR_DHCP,     /* associated but no lease -- (re)start udhcpc */
    NET_WIFI_REPAIR_STOP,     /* dongle gone -- drop the stale client */
} net_wifi_repair_t;

net_wifi_repair_t net_wifi_repair_pick(int present, int carrier, int has_ip, int udhcpc_alive);

unsigned net_wifi_daemons_pick(int started, int present, int supp_alive, int cli_alive);

#endif /* NET_STATE_H */
