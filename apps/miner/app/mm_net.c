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

#include "mm_net.h"
#include "fault_report.h"
#include "net_select.h"
#include "net_state.h"
#include "net_ident.h"
#include "net_wired_alt.h"
#include "net_linux_paths.h"
#include "otp_dev.h"
#include "boardconf.h"
#include "osif.h"
#include "hex.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DNA_LEN 8 /* chip DNA read from OTP, bytes */

#if NET_WIRED_ALT
#define WIRED_IFF_UP 0x1u /* IFF_UP bit of the wired interface's sysfs flags */
#endif                    /* NET_WIRED_ALT */

#if NET_WIRED_ALT
/*
 * Wired link and udhcpc state seen by the last mm_net_wired_rule (main loop);
 * read by mm_net_devtype without a lock. g_wired is eth0 (host tests read it);
 * CDC/RNDIS usb0 is tracked separately so both can hold leases at once.
 */
static net_select_t g_wired;
#ifdef NET_USB_IFACE
static net_select_t g_usb_wired;
#endif /* NET_USB_IFACE */
#endif /* NET_WIRED_ALT */

void mm_dev_name(char *out, size_t n)
{
    uint8_t dna[DNA_LEN]             = { 0 };
    char    dna_str[DNA_LEN * 2 + 1] = { 0 };

    int rc = mm_dna_read(dna, sizeof dna);
    if (rc == -ENOENT)
        osif_log(OSIF_LOG_ERR, "Device ID not readable (open dna device error)");
    else if (rc != 0)
        osif_log(OSIF_LOG_ERR, "Device ID not readable (read dna error)");
    hex_encode(dna_str, dna, DNA_LEN);
    if (rc == 0) {
        osif_log(OSIF_LOG_INFO, "Device ID (DNA): %s", dna_str);
    } else {
        static const char *const ifs[] = {
            MM_NET_IFACE,
#if NET_WIRED_ALT
            NET_WIRED_IFACE,
#ifdef NET_USB_IFACE
            NET_USB_IFACE,
#endif /* NET_USB_IFACE */
#endif /* NET_WIRED_ALT */
        };
        char mac[5];

        for (size_t i = 0; i < sizeof ifs / sizeof ifs[0]; i++) {
            if (mm_mac_tail(ifs[i], mac, sizeof mac) != 0)
                continue;
            osif_log(OSIF_LOG_WARN,
                    "Device name taken from the %s MAC address "
                    "(device ID unreadable)",
                    ifs[i]);
            snprintf(out, n, DEVNAME_PRE "%s", mac);
            return;
        }
    }
    snprintf(out, n, DEVNAME_PRE "%s", &dna_str[12]);
}

static void mm_console_write(const char *line)
{
    int fd = open(MM_CONSOLE_PATH, O_WRONLY | O_APPEND | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (fd < 0)
        return;
    ssize_t w = write(fd, line, strlen(line));
    (void)w;
    close(fd);
}

void mm_net_ip_show_rule(void *ctx, uint64_t now_ms)
{
    static fault_report_t st;
    char                  ip[INET_ADDRSTRLEN];
    char                  line[64];
    struct in_addr        a;
    uint32_t              code = 0;

    (void)ctx;
    (void)now_ms;
    if (net_state_ip(ip, sizeof ip) != 0)
        ip[0] = '\0';
    if (ip[0] && inet_pton(AF_INET, ip, &a) == 1)
        code = (uint32_t)a.s_addr;

    if (fault_report_step(&st, code) != FAULT_REPORT_CHANGED)
        return;

    snprintf(line, sizeof line, "mm_miner: IP %s\n", code ? ip : "none");
    mm_console_write(line);
    osif_log(OSIF_LOG_OP, "IP %s", code ? ip : "none");
}

#if NET_WIRED_ALT
static void wired_link_ensure_up_if(const char *iface)
{
    static fault_report_t st_eth;
#ifdef NET_USB_IFACE
    static fault_report_t st_usb;
#endif
    unsigned              flags = 0;
    fault_report_t       *st    = &st_eth;

#ifdef NET_USB_IFACE
    if (!strcmp(iface, NET_USB_IFACE))
        st = &st_usb;
#endif
    if (wired_iface_flags(iface, &flags) != 0)
        return;
    uint32_t             down = !(flags & WIRED_IFF_UP);
    fault_report_verdict v    = fault_report_step(st, down);
    if (!down)
        return;
    wired_iface_link_set_up(iface);
    if (v != FAULT_REPORT_NONE)
        osif_log(OSIF_LOG_INFO, "Wired port %s was down; brought it up", iface);
}

static void wired_poll_if(const char *iface, net_select_t *st)
{
    int was_up = st->link;

    wired_link_ensure_up_if(iface);
    switch (net_select_eval(st, wired_iface_link_up(iface), wired_iface_dhcp_pid(iface) > 0)) {
    case NET_SEL_DHCP_START:
        wired_iface_dhcp_start(iface);
        break;
    case NET_SEL_DHCP_STOP:
        wired_iface_dhcp_stop(iface, NULL);
        break;
    case NET_SEL_NONE:
        if (was_up && !st->link)
            wired_iface_dhcp_stop(iface, NULL);
        break;
    }
}

void mm_net_wired_rule(void *ctx, uint64_t now_ms)
{
    (void)ctx;
    (void)now_ms;
    wired_poll_if(NET_WIRED_IFACE, &g_wired);
#ifdef NET_USB_IFACE
    if (strcmp(NET_USB_IFACE, NET_WIRED_IFACE) != 0)
        wired_poll_if(NET_USB_IFACE, &g_usb_wired);
#endif
}

int mm_net_devtype(void)
{
    if (net_select_wired_up(&g_wired))
        return MM_NET_DEV_WIRED;
#ifdef NET_USB_IFACE
    if (net_select_wired_up(&g_usb_wired))
        return MM_NET_DEV_WIRED;
#endif
    return IMPL_NET_IS_WIFI ? MM_NET_DEV_WIFI : MM_NET_DEV_WIRED;
}
#else  /* NET_WIRED_ALT */
void mm_net_wired_rule(void *ctx, uint64_t now_ms)
{
    (void)ctx;
    (void)now_ms;
}
int mm_net_devtype(void)
{
    return IMPL_NET_IS_WIFI ? MM_NET_DEV_WIFI : MM_NET_DEV_WIRED;
}
#endif /* NET_WIRED_ALT */

int mm_net_present(void)
{
    if (access(MM_SYS_NET_DIR MM_NET_IFACE, F_OK) == 0)
        return 1;
#if NET_WIRED_ALT
    if (access(MM_SYS_NET_DIR NET_WIRED_IFACE, F_OK) == 0)
        return 1;
#ifdef NET_USB_IFACE
    if (access(MM_SYS_NET_DIR NET_USB_IFACE, F_OK) == 0)
        return 1;
#endif /* NET_USB_IFACE */
#endif /* NET_WIRED_ALT */
    return 0;
}

int mm_net_wired_present(void)
{
#if NET_WIRED_ALT
    if (access(MM_SYS_NET_DIR NET_WIRED_IFACE, F_OK) == 0)
        return 1;
#ifdef NET_USB_IFACE
    if (access(MM_SYS_NET_DIR NET_USB_IFACE, F_OK) == 0)
        return 1;
#endif /* NET_USB_IFACE */
    return 0;
#else  /* NET_WIRED_ALT */
    return 0;
#endif /* NET_WIRED_ALT */
}
