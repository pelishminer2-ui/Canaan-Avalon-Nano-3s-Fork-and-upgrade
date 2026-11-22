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

#ifndef NET_WIRED_ALT_H
#define NET_WIRED_ALT_H

#include <sys/types.h>

int   wired_link_up(void);
pid_t wired_dhcp_pid(void);
int   wired_flags(unsigned *out);
void  wired_link_set_up(void);
void  wired_dhcp_start(void);
void  wired_dhcp_stop(const char *static_dns);

int   wired_iface_link_up(const char *iface);
pid_t wired_iface_dhcp_pid(const char *iface);
int   wired_iface_flags(const char *iface, unsigned *out);
void  wired_iface_link_set_up(const char *iface);
void  wired_iface_dhcp_start(const char *iface);
void  wired_iface_dhcp_stop(const char *iface, const char *static_dns);

#endif /* NET_WIRED_ALT_H */
