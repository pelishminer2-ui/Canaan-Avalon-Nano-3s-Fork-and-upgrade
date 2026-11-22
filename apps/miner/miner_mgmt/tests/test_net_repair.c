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

#include "net_state.h"
#include <stdio.h>

#define CHECK(c, ...) \
    do { \
        if (c) { \
            printf("  ok: "); \
        } else { \
            fails++; \
            printf("  FAIL: "); \
        } \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } while (0)

static int fails;

int main(void)
{
    CHECK(net_wifi_repair_pick(1, 1, 0, 0) == NET_WIFI_REPAIR_DHCP,
            "associated, no lease, no client running: the lost-event case, re-DHCP");

    CHECK(net_wifi_repair_pick(1, 1, 1, 0) == NET_WIFI_REPAIR_NONE,
            "associated with a lease is healthy: left alone");
    CHECK(net_wifi_repair_pick(1, 1, 0, 1) == NET_WIFI_REPAIR_NONE,
            "no lease yet but a udhcpc is already fetching one: do not double-start");
    CHECK(net_wifi_repair_pick(1, 0, 0, 0) == NET_WIFI_REPAIR_NONE,
            "present but not yet associated: nothing to DHCP against");
    CHECK(net_wifi_repair_pick(1, 0, 1, 0) == NET_WIFI_REPAIR_NONE,
            "carrier down while a stale address lingers is still no-op, not DHCP");

    CHECK(net_wifi_repair_pick(0, 0, 0, 0) == NET_WIFI_REPAIR_STOP,
            "dongle gone: drop the stale client so the next replug starts clean");
    CHECK(net_wifi_repair_pick(0, 1, 1, 1) == NET_WIFI_REPAIR_STOP,
            "absence wins over every other input");

    CHECK(net_wifi_daemons_pick(1, 1, 1, 1) == 0, "both daemons running: nothing to start");
    CHECK(net_wifi_daemons_pick(1, 1, 0, 1) == NET_WIFI_START_SUPP,
            "wpa_supplicant stopped: restart it alone");
    CHECK(net_wifi_daemons_pick(1, 1, 1, 0) == NET_WIFI_START_CLI,
            "wpa_cli stopped: restart it alone");
    CHECK(net_wifi_daemons_pick(1, 1, 0, 0) == (NET_WIFI_START_SUPP | NET_WIFI_START_CLI),
            "both stopped: restart both");
    CHECK(net_wifi_daemons_pick(0, 1, 0, 0) == 0,
            "never started yet: provisioning starts them, not the repair");
    CHECK(net_wifi_daemons_pick(1, 0, 0, 0) == 0, "dongle gone: no daemon can bind wlan0");

    CHECK(net_wifi_eth_act(0, 0, 0) == NET_WIFI_ETH_NONE,
            "no Ethernet: leave Wi-Fi running");
    CHECK(net_wifi_eth_act(1, 0, 0) == NET_WIFI_ETH_HOLD,
            "USB Ethernet has IPv4: take Wi-Fi down");
    CHECK(net_wifi_eth_act(1, 0, 1) == NET_WIFI_ETH_NONE,
            "already held while Ethernet is up: do not toggle");
    CHECK(net_wifi_eth_act(0, 0, 1) == NET_WIFI_ETH_RELEASE,
            "Ethernet gone: bring Wi-Fi back");
    CHECK(net_wifi_eth_act(1, 1, 0) == NET_WIFI_ETH_NONE,
            "keep-wifi checkbox: stay on Ethernet and Wi-Fi together");
    CHECK(net_wifi_eth_act(1, 1, 1) == NET_WIFI_ETH_RELEASE,
            "keep-wifi while held: restore Wi-Fi");

    printf(fails ? "FAILED (%d)\n" : "OK\n", fails);
    return fails ? 1 : 0;
}
