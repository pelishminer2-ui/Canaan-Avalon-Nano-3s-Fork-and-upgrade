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
#include "boardconf.h"

#ifdef SUPPORT_BLE
#include "wifi_mgr.h"
#endif /* SUPPORT_BLE */

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

int net_iface_ipv4(const char *name, char *ip, size_t cap)
{
    struct ifreq r;
    int          fd, rc;

    if (!name || !ip || cap < INET_ADDRSTRLEN)
        return -1;
    fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    memset(&r, 0, sizeof r);
    snprintf(r.ifr_name, sizeof r.ifr_name, "%s", name);
    rc = ioctl(fd, SIOCGIFADDR, &r);
    close(fd);
    if (rc != 0)
        return -1;
    ip[0] = '\0';
    if (!inet_ntop(AF_INET, &((struct sockaddr_in *)&r.ifr_addr)->sin_addr, ip, (socklen_t)cap))
        return -1;
    return ip[0] ? 0 : -1;
}

static int net_wired_ipv4(char *ip, size_t cap)
{
#if NET_WIRED_ALT
    if (net_iface_ipv4(NET_WIRED_IFACE, ip, cap) == 0)
        return 0;
#ifdef NET_USB_IFACE
    if (strcmp(NET_USB_IFACE, NET_WIRED_IFACE) != 0 && net_iface_ipv4(NET_USB_IFACE, ip, cap) == 0)
        return 0;
#endif /* NET_USB_IFACE */
#else
    (void)ip;
    (void)cap;
#endif /* NET_WIRED_ALT */
    return -1;
}

int net_state_ip(char *ip, size_t cap)
{
    if (ip == NULL || cap < INET_ADDRSTRLEN)
        return -1;
    ip[0] = '\0';
    /* Wired USB Ethernet wins so LCD, discovery and the LAN UI show the preferred path. */
    if (net_wired_ipv4(ip, cap) == 0)
        return 0;
#ifdef SUPPORT_BLE
    if (wifi_get_state(ip, (int)cap) == NET_STATE_UP && ip[0])
        return 0;
    ip[0] = '\0';
#endif /* SUPPORT_BLE */
    if (net_iface_ipv4(MM_NET_IFACE, ip, cap) == 0)
        return 0;
    return -1;
}

int net_state_get(void)
{
    char ip[INET_ADDRSTRLEN];
    int  st = NET_STATE_DOWN;

    if (net_wired_ipv4(ip, sizeof ip) == 0)
        return NET_STATE_UP;
#ifdef SUPPORT_BLE
    st = wifi_get_state(NULL, 0);
    if (st == NET_STATE_UP)
        return NET_STATE_UP;
#endif /* SUPPORT_BLE */
#ifdef SUPPORT_BLE
    if (!wifi_carrier_up())
        return st;
#endif /* SUPPORT_BLE */
    if (net_iface_ipv4(MM_NET_IFACE, ip, sizeof ip) == 0)
        return NET_STATE_UP;
    return st;
}

net_wifi_repair_t net_wifi_repair_pick(int present, int carrier, int has_ip, int udhcpc_alive)
{
    if (!present)
        return NET_WIFI_REPAIR_STOP;
    /* Leave a link that is associating, addressed or already in DHCP. */
    if (carrier && !has_ip && !udhcpc_alive)
        return NET_WIFI_REPAIR_DHCP;
    return NET_WIFI_REPAIR_NONE;
}

unsigned net_wifi_daemons_pick(int started, int present, int supp_alive, int cli_alive)
{
    if (!started || !present)
        return 0;
    return (supp_alive ? 0u : NET_WIFI_START_SUPP) | (cli_alive ? 0u : NET_WIFI_START_CLI);
}
