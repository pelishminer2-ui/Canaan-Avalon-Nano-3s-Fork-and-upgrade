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

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

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

/* Interface that SIOCGIFADDR reports an address for; NULL = none. */
static const char *addr_iface;

int __wrap_ioctl(int fd, unsigned long req, ...)
{
    struct ifreq *r;
    va_list       ap;

    (void)fd;
    va_start(ap, req);
    r = va_arg(ap, struct ifreq *);
    va_end(ap);
    if (req != SIOCGIFADDR || addr_iface == NULL || strcmp(r->ifr_name, addr_iface) != 0)
        return -1;
    ((struct sockaddr_in *)&r->ifr_addr)->sin_family      = AF_INET;
    ((struct sockaddr_in *)&r->ifr_addr)->sin_addr.s_addr = htonl(0x0a000002);
    return 0;
}

int main(void)
{
    char ip[INET_ADDRSTRLEN];

    addr_iface = NULL;
    CHECK(net_state_get() == NET_STATE_DOWN, "no interface addressed: down");
    CHECK(net_state_ip(ip, sizeof ip) == -1, "no interface addressed: no ip");

    addr_iface = MM_NET_IFACE;
    CHECK(net_state_get() == NET_STATE_UP, "primary " MM_NET_IFACE " addressed: up");
    CHECK(net_state_ip(ip, sizeof ip) == 0 && !strcmp(ip, "10.0.0.2"),
            "primary " MM_NET_IFACE " addressed: ip %s", ip);

#if NET_WIRED_ALT
    addr_iface = NET_WIRED_IFACE;
    CHECK(net_state_get() == NET_STATE_UP, "wired alt " NET_WIRED_IFACE " addressed: up");
    CHECK(net_state_ip(ip, sizeof ip) == 0 && !strcmp(ip, "10.0.0.2"),
            "wired alt " NET_WIRED_IFACE " addressed: ip %s", ip);
#ifdef NET_USB_IFACE
    addr_iface = NET_USB_IFACE;
    CHECK(net_state_get() == NET_STATE_UP, "CDC/RNDIS " NET_USB_IFACE " addressed: up");
    CHECK(net_state_ip(ip, sizeof ip) == 0 && !strcmp(ip, "10.0.0.2"),
            "CDC/RNDIS " NET_USB_IFACE " addressed: ip %s", ip);
#endif /* NET_USB_IFACE */
#endif /* NET_WIRED_ALT */

    addr_iface = "nosuch0";
    CHECK(net_state_get() == NET_STATE_DOWN, "only an unrelated interface addressed: down");

    printf(fails ? "FAILED (%d)\n" : "OK\n", fails);
    return fails ? 1 : 0;
}
