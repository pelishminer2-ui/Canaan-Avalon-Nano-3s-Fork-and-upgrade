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

#ifndef NET_LINUX_PATHS_H
#define NET_LINUX_PATHS_H

#define MM_PROC_DIR           "/proc/"
#define MM_SYS_NET_DIR        "/sys/class/net/"
#define NET_DHCP_PIDF(ifname) "/var/run/udhcpc." ifname ".pid"
#define WIRED_DHCP_PIDF       NET_DHCP_PIDF(NET_WIRED_IFACE)
#ifdef NET_USB_IFACE
#define USB_DHCP_PIDF NET_DHCP_PIDF(NET_USB_IFACE)
#endif
#define MAIN_DHCP_PIDF NET_DHCP_PIDF(MM_NET_IFACE)
#define NET_UDHCPC_SCRIPT "/etc/network/udhcpc-metric.script"

#endif /* NET_LINUX_PATHS_H */
