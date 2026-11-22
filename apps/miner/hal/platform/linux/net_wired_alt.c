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

#include "net_wired_alt.h"
#include "boardconf.h"

#if NET_WIRED_ALT

#include "net_linux_common.h"
#include "osif.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAIN_SYS_CARRIER MM_SYS_NET_DIR MM_NET_IFACE "/carrier"

static void wired_pidf(const char *iface, char *buf, size_t cap)
{
    if (iface && !strcmp(iface, NET_WIRED_IFACE)) {
        snprintf(buf, cap, "%s", WIRED_DHCP_PIDF);
        return;
    }
#ifdef NET_USB_IFACE
    if (iface && !strcmp(iface, NET_USB_IFACE)) {
        snprintf(buf, cap, "%s", USB_DHCP_PIDF);
        return;
    }
#endif
    snprintf(buf, cap, "/var/run/udhcpc.%s.pid", iface ? iface : "eth0");
}

static void wired_spawn(char *const argv[], int detach)
{
    pid_t pid = fork();
    if (pid < 0)
        return;
    if (pid == 0) {
        if (detach && fork() != 0)
            _exit(0);
        if (detach)
            setsid();
        execvp(argv[0], argv);
        _exit(127);
    }
    (void)waitpid(pid, NULL, 0);
}

int wired_iface_link_up(const char *iface)
{
    char path[128];

    snprintf(path, sizeof path, MM_SYS_NET_DIR "%s/carrier", iface);
    return net_linux_carrier_up(path);
}

pid_t wired_iface_dhcp_pid(const char *iface)
{
    char pidf[128];

    wired_pidf(iface, pidf, sizeof pidf);
    return net_linux_udhcpc_pid(pidf);
}

int wired_iface_flags(const char *iface, unsigned *out)
{
    char  path[128];
    FILE *f;
    unsigned v   = 0;
    int      got;

    snprintf(path, sizeof path, MM_SYS_NET_DIR "%s/flags", iface);
    f = fopen(path, "re");
    if (!f)
        return -1;
    got = fscanf(f, "%x", &v) == 1;
    fclose(f);
    if (!got)
        return -1;
    *out = v;
    return 0;
}

void wired_iface_link_set_up(const char *iface)
{
    char *const up[] = { "ip", "link", "set", (char *)iface, "up", NULL };
    wired_spawn(up, 0);
}

void wired_iface_dhcp_start(const char *iface)
{
    char        pidf[128];
    char *const dhcp[] = { "udhcpc", "-i", (char *)iface, "-b", "-T", "1", "-A", "2", "-s",
        (char *)NET_UDHCPC_SCRIPT, "-p", pidf, NULL };

    wired_pidf(iface, pidf, sizeof pidf);
    wired_spawn(dhcp, 1);
    osif_log(OSIF_LOG_OP, "Wired network %s: link up, requesting an IP address", iface);
}

static void wired_dns_handback(const char *static_dns)
{
    pid_t p;

    if (!net_linux_carrier_up(MAIN_SYS_CARRIER))
        return;
    p = net_linux_udhcpc_pid(MAIN_DHCP_PIDF);
    if (p > 0) {
        if (kill(p, SIGUSR1) == 0)
            osif_log(OSIF_LOG_INFO,
                    "%s: renewing its IP address and switching DNS back "
                    "to it",
                    MM_NET_IFACE);
    } else if (static_dns) {
        char ndns[INET_ADDRSTRLEN];

        net_linux_write_resolv(static_dns);
        osif_log(OSIF_LOG_INFO, "DNS switched back to %s static server %s", MM_NET_IFACE,
                net_linux_ipv4_norm(static_dns, ndns) == 0 ? ndns : static_dns);
    }
}

void wired_iface_dhcp_stop(const char *iface, const char *static_dns)
{
    char        pidf[128];
    char *const flush[] = { "ip", "-4", "addr", "flush", "dev", (char *)iface, NULL };

    wired_pidf(iface, pidf, sizeof pidf);
    net_linux_udhcpc_stop(pidf);
    wired_spawn(flush, 0);
    osif_log(OSIF_LOG_OP, "Wired network %s: link down, IP address released", iface);
    wired_dns_handback(static_dns);
}

int wired_link_up(void)
{
    return wired_iface_link_up(NET_WIRED_IFACE);
}

pid_t wired_dhcp_pid(void)
{
    return wired_iface_dhcp_pid(NET_WIRED_IFACE);
}

int wired_flags(unsigned *out)
{
    return wired_iface_flags(NET_WIRED_IFACE, out);
}

void wired_link_set_up(void)
{
    wired_iface_link_set_up(NET_WIRED_IFACE);
}

void wired_dhcp_start(void)
{
    wired_iface_dhcp_start(NET_WIRED_IFACE);
}

void wired_dhcp_stop(const char *static_dns)
{
    wired_iface_dhcp_stop(NET_WIRED_IFACE, static_dns);
}

#endif /* NET_WIRED_ALT */
