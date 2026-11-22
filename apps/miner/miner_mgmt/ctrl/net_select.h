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

/*
 * Wired USB Ethernet discovery. A hub may present eth0 and/or usb0 beside
 * wlan0; each wired client follows its own interface and carrier at runtime.
 * No I/O: the caller observes the interface and performs the action.
 */
#ifndef NET_SELECT_H
#define NET_SELECT_H

#include <stdint.h>

typedef enum {
    NET_SEL_NONE = 0,   /* state matches intent; do nothing */
    NET_SEL_DHCP_START, /* link is up with no client: start one */
    NET_SEL_DHCP_STOP,  /* link is gone and a client is still up: stop it */
} net_select_action;

typedef struct net_select {
    uint8_t link;   /* last call: wired iface exists and has carrier */
    uint8_t client; /* last call: its udhcpc is running */
} net_select_t;

net_select_action net_select_eval(net_select_t *st, int link, int client);
int               net_select_wired_up(const net_select_t *st);

#endif /* NET_SELECT_H */
