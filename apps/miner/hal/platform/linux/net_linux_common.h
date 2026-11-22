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

#ifndef NET_LINUX_COMMON_H
#define NET_LINUX_COMMON_H

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/types.h>

#include "net_linux_paths.h"

int net_linux_dhcp(void);
int net_linux_set_static(const char *ip, const char *mask, const char *gw, const char *dns);
int net_linux_get_addr(char *buf, unsigned cap);

static inline int net_linux_carrier_up(const char *path)
{
    FILE *f = fopen(path, "re");
    int   c;

    if (!f)
        return 0;
    c = fgetc(f);
    fclose(f);
    return c == '1';
}

static inline int net_linux_is_comm(pid_t p, const char *name)
{
    char   path[256];
    char   comm[32];
    size_t n = strlen(name);
    FILE  *f;
    int    ok;

    snprintf(path, sizeof path, "%s%ld/comm", MM_PROC_DIR, (long)p);
    f = fopen(path, "r");
    if (!f)
        return 0;
    ok = fgets(comm, sizeof comm, f) && !strncmp(comm, name, n) && !strcmp(comm + n, "\n");
    fclose(f);
    return ok;
}

static inline pid_t net_linux_pidfile_pid(const char *pidf, const char *name)
{
    FILE *f = fopen(pidf, "r");
    long  p = 0;
    int   got;

    if (!f)
        return 0;
    got = fscanf(f, "%ld", &p) == 1;
    fclose(f);
    if (!got || p <= 0 || kill((pid_t)p, 0) != 0 || !net_linux_is_comm((pid_t)p, name))
        return 0;
    return (pid_t)p;
}

static inline pid_t net_linux_udhcpc_pid(const char *pidf)
{
    return net_linux_pidfile_pid(pidf, "udhcpc");
}

static inline int net_linux_wait_exit(pid_t p, int ticks)
{
    const struct timespec tick = { 0, 100 * 1000000L };
    int                   i;

    for (i = 0; i < ticks && kill(p, 0) == 0; i++)
        nanosleep(&tick, NULL);
    return i < ticks;
}

static inline void net_linux_udhcpc_stop(const char *pidf)
{
    pid_t p = net_linux_udhcpc_pid(pidf);

    if (p > 0 && kill(p, SIGTERM) == 0 && !net_linux_wait_exit(p, 20) && kill(p, SIGKILL) == 0)
        (void)net_linux_wait_exit(p, 10);
    remove(pidf);
}

static inline int net_linux_ipv4_norm(const char *in, char out[INET_ADDRSTRLEN])
{
    const char   *p = in;
    unsigned char o[4];
    unsigned      v;
    int           i, n;

    if (!p)
        return -1;
    for (i = 0; i < 4; i++) {
        if (i > 0 && *p++ != '.')
            return -1;
        for (n = 0, v = 0; n < 3 && *p >= '0' && *p <= '9'; n++)
            v = v * 10 + (unsigned)(*p++ - '0');
        if (n == 0 || v > 255)
            return -1;
        o[i] = (unsigned char)v;
    }
    if (*p)
        return -1;
    snprintf(out, INET_ADDRSTRLEN, "%u.%u.%u.%u", o[0], o[1], o[2], o[3]);
    return 0;
}

static inline void net_linux_write_resolv(const char *dns)
{
    char cmd[128];
    char ndns[INET_ADDRSTRLEN];
    int  rc;

    if (net_linux_ipv4_norm(dns, ndns) != 0)
        return;
    snprintf(cmd, sizeof cmd, "printf 'nameserver %s\\n' > /etc/resolv.conf", ndns);
    rc = system(cmd);
    (void)rc;
}

#endif /* NET_LINUX_COMMON_H */
