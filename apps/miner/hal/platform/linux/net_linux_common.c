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

#include "net_ops.h"
#include "boardconf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "net_linux_common.h"

static int net_linux_sh(const char *cmd)
{
    int rc = system(cmd);
    return (rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0);
}

int net_linux_dhcp(void)
{
    net_linux_udhcpc_stop(MAIN_DHCP_PIDF);
    net_linux_sh("ifconfig " MM_NET_IFACE " 0.0.0.0");
    net_linux_sh("udhcpc -i " MM_NET_IFACE " -b -T 1 -A 2 -s " NET_UDHCPC_SCRIPT " -p " MAIN_DHCP_PIDF);
    return 0;
}

int net_linux_set_static(const char *ip, const char *mask, const char *gw, const char *dns)
{
    char cmd[128];
    char nip[INET_ADDRSTRLEN], nmask[INET_ADDRSTRLEN];
    char ngw[INET_ADDRSTRLEN], ndns[INET_ADDRSTRLEN];

    if (net_linux_ipv4_norm(ip, nip) != 0 || net_linux_ipv4_norm(mask, nmask) != 0 ||
            net_linux_ipv4_norm(gw, ngw) != 0 ||
            (dns && dns[0] && net_linux_ipv4_norm(dns, ndns) != 0))
        return -1;
    net_linux_udhcpc_stop(MAIN_DHCP_PIDF);

    snprintf(cmd, sizeof cmd, "ifconfig " MM_NET_IFACE " %s netmask %s", nip, nmask);
    net_linux_sh(cmd);

    net_linux_sh("route del default 2>/dev/null");
    snprintf(cmd, sizeof cmd, "route add default gw %s", ngw);
    net_linux_sh(cmd);

    net_linux_write_resolv((dns && dns[0]) ? ndns : ngw);
    return 0;
}

int net_linux_get_addr(char *buf, unsigned cap)
{
    struct ifreq ifr;
    int          fd, rc;

    if (!buf || cap == 0)
        return -1;
    buf[0] = 0;

    fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    memset(&ifr, 0, sizeof ifr);
    snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", MM_NET_IFACE);
    rc = ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);
    if (rc != 0 ||
            !inet_ntop(AF_INET, &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr, buf, cap)) {
        buf[0] = 0;
        return -1;
    }
    return 0;
}
