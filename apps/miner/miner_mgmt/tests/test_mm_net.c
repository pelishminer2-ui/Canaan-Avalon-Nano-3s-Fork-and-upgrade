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

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <net/if.h>

#include "boardconf.h"
#include "cali.h"
#include "ctrl_args.h"
#include "fan_dev.h"
#include "mc_paths.h"
#include "mm_telem.h"
#include "net_state.h"
#include "osif.h"
#include "wdt_dev.h"
#include "mm_net.h"
#include "net_linux_paths.h"
#include "tst_fan_dev.h"
#include "../../tests/include/tst_check.h"

#undef MM_CONSOLE_PATH
#define MM_CONSOLE_PATH "mm_net_console_" TST_BOARD ".txt" /* relative: run chdir()s to scratch */
#undef WIRED_DHCP_PIDF
#undef MM_SYS_NET_DIR
#undef MM_PROC_DIR
/*
 * Scratch stand-ins, relative to the cwd: wired and primary udhcpc PID files,
 * sysfs net and procfs directories.
 */
#define WIRED_DHCP_PIDF MM_CONSOLE_PATH ".udhcpc.pid"
#define TST_MAIN_PIDF   MM_CONSOLE_PATH ".udhcpc.main.pid"
#define MM_SYS_NET_DIR  MM_CONSOLE_PATH ".sysnet/"
#define MM_PROC_DIR     MM_CONSOLE_PATH ".proc/"

#define EV_MAX       64         /* recorded commands per log */
#define EV_LEN       256        /* recorded command length, bytes with NUL */
#define EXITST(n)    ((n) << 8) /* wait status of a child exiting with n */
#define TST_MAIN_PID 4343       /* PID of the fake primary-interface udhcpc */
#if IMPL_NET_IS_WIRED
/* udhcpc PID file of the primary interface; the interface that is not primary */
#define NET_INIT_PIDFILE "/var/run/udhcpc.eth1.pid"
#define NET_OTHER_IFACE  "wlan0"
#else /* IMPL_NET_IS_WIRED */
/* udhcpc PID file of the primary interface; the interface that is not primary */
#define NET_INIT_PIDFILE "/var/run/udhcpc.wlan0.pid"
#define NET_OTHER_IFACE  "eth0"
#endif /* IMPL_NET_IS_WIRED */
/*
 * Expected shell commands: start udhcpc, clear the address, drop the default
 * route, query wpa_supplicant.
 */
#define NET_UDHCPC_START \
    "udhcpc -i " MM_NET_IFACE " -b -T 1 -A 2 -s " NET_UDHCPC_SCRIPT " -p " NET_INIT_PIDFILE
#define NET_ADDR_ZERO    "ifconfig " MM_NET_IFACE " 0.0.0.0"
#define NET_ROUTE_DEL    "route del default 2>/dev/null"
#define NET_WPA_PROBE    "wpa_cli -i " MM_NET_IFACE " status 2>/dev/null"
/* Valid static IPv4 configuration and its "static,ip,mask,gw" setter argument */
#define GOOD_IP   "192.168.1.50"
#define GOOD_MASK "255.255.255.0"
#define GOOD_GW   "192.168.1.1"

int      tst_system(const char *cmd);
FILE    *tst_popen(const char *cmd, const char *mode);
int      tst_pclose(FILE *f);
int      tst_ioctl(int fd, unsigned long req, void *arg);
unsigned tst_sleep(unsigned s);
pid_t    tst_fork(void);
pid_t    tst_waitpid(pid_t, int *, int);
void     tst_log(osif_loglvl_t lvl, const char *fmt, ...);
int      tst_net_state_ip(char *ip, size_t cap);

/* Calls in the included sources, routed to the tst_* fakes below */
#define net_state_ip(i, c) tst_net_state_ip((i), (c))
#define system(c)          tst_system(c)
#define popen(c, m)        tst_popen((c), (m))
#define pclose(f)          tst_pclose(f)
#define sleep(s)           tst_sleep(s)
#define fork()             tst_fork()
#define waitpid            tst_waitpid
#define osif_log(...)      tst_log(__VA_ARGS__)

int  tst_kill(pid_t pid, int sig);
int  tst_execvp(const char *file, char *const argv[]);
void tst_exit(int st);

#include "mm_fan.c"
/* Process calls in mm_net.c and later sources, routed to the fakes below */
#define kill(p, s)   tst_kill((p), (s))
#define execvp(f, a) tst_execvp((f), (a))
#define _exit(s)     tst_exit(s)
#include "mm_net.c"
#include "../../hal/platform/linux/otp_dev.c"
#include "../../hal/platform/linux/net_ident.c"
FILE *tst_fopen(const char *path, const char *mode);
int   tst_remove(const char *path);
/* PID file access in net_wired_alt.c, with MAIN_DHCP_PIDF mapped to TST_MAIN_PIDF */
#define fopen(p, m) tst_fopen((p), (m))
#define remove(p)   tst_remove(p)
#include "../../hal/platform/linux/net_wired_alt.c"
#undef _exit
#undef execvp
#define ioctl(f, r, a) tst_ioctl((f), (r), (a)) /* interface address query fake */
#include "../../hal/platform/linux/net_linux_common.c"
#undef ioctl
#undef remove
#undef fopen
#include "../../hal/platform/linux/net_wifi.c"
#include "../../hal/platform/linux/net_wired.c"
#include "mm_tz.c"
#include "mm_logrot.c"
#include "mm_miner_tu.h"
#undef kill
#undef net_state_ip
#undef osif_log
#undef waitpid
#undef fork
#undef sleep
#undef pclose
#undef popen
#undef system

static char tst_cmd[EV_MAX][EV_LEN];      /* system() and exec command lines */
static int  tst_cmd_n, tst_cmd_over;      /* entries in tst_cmd; overflow flag */
static char tst_pipe_cmd[EV_MAX][EV_LEN]; /* popen() command lines */
static int  tst_pipe_n;                   /* popen() call count */
/* Target alive to kill(pid, 0); it was alive at an exec; kill() signal count */
static int tst_alive, tst_exec_alive, tst_kill_n;
/*
 * SIGTERM leaves the target alive; kill(pid, 0) probes it survives after
 * SIGKILL; probes left before it is gone.
 */
static int   tst_term_ignored, tst_kill_lag, tst_dying;
static pid_t tst_live_pid;    /* PID kill(pid, 0) reports alive; 0 none */
static pid_t tst_kill_pid[8]; /* PIDs of the first signals sent */
static int   tst_kill_sig[8]; /* signal numbers of the first signals sent */
static int   tst_fork_child;  /* nonzero: fork() returns 0, as in the child */
/* wpa_cli probe that first reports COMPLETED, -1 never; probes made */
static int tst_wpa_ok_at = -1, tst_wpa_calls;
/* ioctl() address query that first succeeds, -1 never; queries made */
static int           tst_addr_ok_at = -1, tst_addr_calls;
static int           tst_addr_n;              /* ioctl() call count */
static unsigned long tst_addr_req;            /* last ioctl() request */
static char          tst_addr_if[IFNAMSIZ];   /* last ioctl() interface name */
static int           tst_popen_null;          /* nonzero fails popen() */
static char          tst_pipe_buf[128];       /* output of the last popen() */
static unsigned      tst_slept_s;             /* total sleep() time, s */
static char          tst_logbuf[2048];        /* log lines, newline separated */
static char          tst_ip[INET_ADDRSTRLEN]; /* net_state_ip() address; empty fails */
static int           tst_ip_calls;            /* net_state_ip() call count */

int tst_system(const char *cmd)
{
    if (tst_cmd_n < EV_MAX)
        snprintf(tst_cmd[tst_cmd_n++], EV_LEN, "%s", cmd);
    else
        tst_cmd_over = 1;
    return EXITST(0);
}

int tst_kill(pid_t pid, int sig)
{
    if (sig == 0) {
        if (tst_dying > 0) {
            tst_dying--;
            return 0;
        }
        if (tst_alive || (tst_live_pid && pid == tst_live_pid))
            return 0;
        errno = ESRCH;
        return -1;
    }
    if (tst_kill_n < 8) {
        tst_kill_pid[tst_kill_n] = pid;
        tst_kill_sig[tst_kill_n] = sig;
    }
    tst_kill_n++;
    if (sig == SIGKILL)
        tst_dying = tst_kill_lag;
    if (sig != SIGTERM || !tst_term_ignored)
        tst_alive = 0;
    return 0;
}

int tst_execvp(const char *file, char *const argv[])
{
    char   line[EV_LEN];
    size_t n = 0;

    (void)file;
    line[0] = '\0';
    for (int k = 0; argv[k] && n + 1 < sizeof line; k++) {
        int w = snprintf(line + n, sizeof line - n, "%s%s", k ? " " : "", argv[k]);
        if (w < 0)
            break;
        n = (n + (size_t)w >= sizeof line) ? sizeof line - 1 : n + (size_t)w;
    }
    if (tst_cmd_n < EV_MAX)
        snprintf(tst_cmd[tst_cmd_n++], EV_LEN, "%s", line);
    else
        tst_cmd_over = 1;
    tst_exec_alive |= tst_alive || tst_dying > 0;
    return -1;
}

void tst_exit(int st)
{
    (void)st;
}

static const char *tst_pidf_path(const char *path)
{
    return strcmp(path, MAIN_DHCP_PIDF) ? path : TST_MAIN_PIDF;
}
FILE *tst_fopen(const char *path, const char *mode)
{
    return fopen(tst_pidf_path(path), mode);
}
int tst_remove(const char *path)
{
    return remove(tst_pidf_path(path));
}
static void tst_pidf_write(const char *path, int pid)
{
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%d\n", pid);
        fclose(f);
    }
}

pid_t tst_waitpid(pid_t pid, int *status, int options)
{
    (void)status;
    (void)options;
    CHECK(tst_fork_child && pid == 0, "wait only for the faked wired child");
    return 0;
}

pid_t tst_fork(void)
{
    if (tst_fork_child)
        return 0;
    fprintf(stderr,
            "  FAIL: fork() reached — this suite must never create a "
            "process\n");
    abort();
}

FILE *tst_popen(const char *cmd, const char *mode)
{
    (void)mode;
    if (tst_pipe_n < EV_MAX)
        snprintf(tst_pipe_cmd[tst_pipe_n], EV_LEN, "%s", cmd);
    tst_pipe_n++;
    if (tst_popen_null)
        return NULL;

    const char *out = "";
    if (strstr(cmd, "wpa_cli")) {
        out = (tst_wpa_ok_at >= 0 && tst_wpa_calls++ >= tst_wpa_ok_at) ?
                "bssid=02:00:00:00:00:00\nwpa_state=COMPLETED\nid=0\n" :
                "bssid=02:00:00:00:00:00\nwpa_state=SCANNING\nid=0\n";
    }
    snprintf(tst_pipe_buf, sizeof tst_pipe_buf, "%s", out);
    return fmemopen(tst_pipe_buf, strlen(tst_pipe_buf), "r");
}
int tst_pclose(FILE *f)
{
    return f ? fclose(f) : -1;
}

int tst_ioctl(int fd, unsigned long req, void *arg)
{
    struct ifreq       *ifr = arg;
    struct sockaddr_in *sin = (struct sockaddr_in *)&ifr->ifr_addr;

    (void)fd;
    tst_addr_n++;
    tst_addr_req = req;
    snprintf(tst_addr_if, sizeof tst_addr_if, "%s", ifr->ifr_name);
    if (!(tst_addr_ok_at >= 0 && tst_addr_calls++ >= tst_addr_ok_at)) {
        errno = EADDRNOTAVAIL;
        return -1;
    }
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "192.168.1.77", &sin->sin_addr);
    return 0;
}

unsigned tst_sleep(unsigned s)
{
    tst_slept_s += s;
    return 0;
}

void tst_log(osif_loglvl_t lvl, const char *fmt, ...)
{
    char    one[512];
    va_list ap;
    (void)lvl;
    va_start(ap, fmt);
    vsnprintf(one, sizeof one, fmt, ap);
    va_end(ap);
    strncat(tst_logbuf, one, sizeof tst_logbuf - strlen(tst_logbuf) - 1);
    strncat(tst_logbuf, "\n", sizeof tst_logbuf - strlen(tst_logbuf) - 1);
}

int tst_net_state_ip(char *ip, size_t cap)
{
    tst_ip_calls++;
    if (!tst_ip[0]) {
        if (cap)
            ip[0] = '\0';
        return -1;
    }
    snprintf(ip, cap, "%s", tst_ip);
    return 0;
}

static void tst_console_reset(void)
{
    FILE *f = fopen(MM_CONSOLE_PATH, "w");
    if (f)
        fclose(f);
}
static const char *tst_console(void)
{
    static char buf[2048];
    size_t      n = 0;
    FILE       *f = fopen(MM_CONSOLE_PATH, "r");
    buf[0]        = '\0';
    if (f) {
        n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
    }
    buf[n] = '\0';
    return buf;
}
static int tst_console_lines(void)
{
    const char *s = tst_console();
    int         n = 0;
    for (; *s; s++)
        if (*s == '\n')
            n++;
    return n;
}

void mm_telem_get(mm_telem_t *out)
{
    memset(out, 0, sizeof *out);
}
int wdt_dev_init(void)
{
    return 0;
}
int wdt_dev_bootstatus(void)
{
    return 0;
}
int wdt_dev_timeout_s(void)
{
    return 2 * WDT_TIMEOUT_S;
}
int wdt_dev_deadline_s(void)
{
    return WDT_TIMEOUT_S;
}
int wdt_dev_feed_period_ms(void)
{
    return WDT_FEED_PERIOD_MS_MAX;
}
void wdt_dev_disarm(void)
{
}
void wdt_dev_feed(void)
{
}

static void reset(void)
{
    tst_cmd_n        = 0;
    tst_cmd_over     = 0;
    tst_pipe_n       = 0;
    tst_wpa_calls    = 0;
    tst_addr_calls   = 0;
    tst_addr_n       = 0;
    tst_addr_if[0]   = '\0';
    tst_wpa_ok_at    = -1;
    tst_addr_ok_at   = -1;
    tst_popen_null   = 0;
    tst_slept_s      = 0;
    tst_logbuf[0]    = '\0';
    tst_kill_n       = 0;
    tst_alive        = 0;
    tst_live_pid     = 0;
    tst_term_ignored = 0;
    tst_kill_lag     = 0;
    tst_dying        = 0;
    remove(TST_MAIN_PIDF);
}

static const char *cmd(int i)
{
    return (i >= 0 && i < tst_cmd_n) ? tst_cmd[i] : "";
}

static int cmd_has(const char *s)
{
    int n = 0;
    for (int k = 0; k < tst_cmd_n; k++)
        if (strstr(tst_cmd[k], s))
            n++;
    return n;
}
static const char *cmd_join(void)
{
    static char j[2048];
    size_t      n = 0;
    j[0]          = '\0';
    for (int k = 0; k < tst_cmd_n && n + 1 < sizeof j; k++) {
        int w = snprintf(j + n, sizeof j - n, "%s%s", k ? " | " : "", tst_cmd[k]);
        if (w < 0)
            break;
        n = (n + (size_t)w >= sizeof j) ? sizeof j - 1 : n + (size_t)w;
    }
    return j;
}

static void tst_mac_set(const char *ifname, const char *mac)
{
    char  path[512];
    FILE *f;

    mkdir(MM_SYS_NET_DIR, 0755);
    snprintf(path, sizeof path, MM_SYS_NET_DIR "%s", ifname);
    mkdir(path, 0755);
    snprintf(path, sizeof path, MM_SYS_NET_DIR "%s/address", ifname);
    f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "%s\n", mac);
    fclose(f);
}

static void tst_mac_clear(const char *ifname)
{
    char path[512];

    snprintf(path, sizeof path, MM_SYS_NET_DIR "%s/address", ifname);
    remove(path);
    snprintf(path, sizeof path, MM_SYS_NET_DIR "%s", ifname);
    rmdir(path);
}

static void tst_comm_set(int pid, const char *comm)
{
    char  path[512];
    FILE *f;

    mkdir(MM_PROC_DIR, 0755);
    snprintf(path, sizeof path, MM_PROC_DIR "%d", pid);
    mkdir(path, 0755);
    snprintf(path, sizeof path, MM_PROC_DIR "%d/comm", pid);
    f = fopen(path, "w");
    if (!f)
        return;
    fprintf(f, "%s\n", comm);
    fclose(f);
}

static void tst_comm_clear(int pid)
{
    char path[512];

    snprintf(path, sizeof path, MM_PROC_DIR "%d/comm", pid);
    remove(path);
    snprintf(path, sizeof path, MM_PROC_DIR "%d", pid);
    rmdir(path);
}

#if NET_WIRED_ALT
static void tst_sys_write(const char *ifname, const char *leaf, const char *v)
{
    char  path[512];
    FILE *f;

    mkdir(MM_SYS_NET_DIR, 0755);
    snprintf(path, sizeof path, MM_SYS_NET_DIR "%s", ifname);
    mkdir(path, 0755);
    snprintf(path, sizeof path, MM_SYS_NET_DIR "%s/%s", ifname, leaf);
    f = fopen(path, "w");
    if (!f)
        return;
    fputs(v, f);
    fclose(f);
}

static void tst_sys_clear(const char *ifname, const char *leaf)
{
    char path[512];

    snprintf(path, sizeof path, MM_SYS_NET_DIR "%s/%s", ifname, leaf);
    remove(path);
    snprintf(path, sizeof path, MM_SYS_NET_DIR "%s", ifname);
    rmdir(path);
}
#endif /* NET_WIRED_ALT */

static void rm_rf(const char *path)
{
    DIR *d = opendir(path);
    if (!d) {
        remove(path);
        return;
    }
    struct dirent *e;
    char           child[512];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        snprintf(child, sizeof child, "%s/%s", path, e->d_name);
        rm_rf(child);
    }
    closedir(d);
    rmdir(path);
}

int main(void)
{
    printf("descriptor %s: MM_NET_IFACE=\"%s\"\n", BOARDCONF_SECTION, MM_NET_IFACE);

    /* Every scratch path (pidfiles, fake /proc and /sys) is derived from the
     * relative MM_CONSOLE_PATH. Run in a private directory so two concurrent
     * copies of this test -- one build tree can be run twice at once -- write
     * to separate files instead of racing on a shared fixed path. */
    char scratch[] = "mm_net_scratch_XXXXXX";
    if (mkdtemp(scratch) == NULL || chdir(scratch) != 0) {
        fprintf(stderr, "%s: cannot create a private scratch dir\n", BOARDCONF_SECTION);
        return 1;
    }

    tst_comm_set(TST_MAIN_PID, "udhcpc");
    tst_comm_set(4242, "udhcpc");

    reset();
    tst_pidf_write(TST_MAIN_PIDF, TST_MAIN_PID);
    tst_alive = 1;
    CHECK(hal_net()->set_static(GOOD_IP, GOOD_MASK, GOOD_GW, "") == 0, "a good static is accepted");

    CHECK(tst_kill_n == 1 && tst_kill_pid[0] == TST_MAIN_PID && tst_kill_sig[0] == SIGTERM &&
                    access(TST_MAIN_PIDF, F_OK) != 0 && cmd_has("udhcpc") == 0,
            "1.1 the udhcpc stop SIGTERMs only the pid in %s, waits for it to "
            "exit (escalating to SIGKILL after 20 x 0.1 s) and removes the "
            "pidfile, because a udhcpc still alive renews its lease over the "
            "address set next — no shell command is involved (%d signal(s), "
            "first pid %d sig %d)",
            MAIN_DHCP_PIDF, tst_kill_n, (int)tst_kill_pid[0], tst_kill_sig[0]);

    CHECK(!strcmp(cmd(0), "ifconfig " MM_NET_IFACE " " GOOD_IP " netmask " GOOD_MASK),
            "1.2 the address is set with `ifconfig <if> <ip> netmask <mask>` — "
            "L3 only, never ifup/ifdown, which would drop wpa_supplicant's "
            "association and lock the board out (got \"%s\")",
            cmd(0));

    CHECK(!strcmp(cmd(1), NET_ROUTE_DEL),
            "1.3 the old default route is deleted BEFORE the new one is added, "
            "with stderr swallowed so an absent route is not an error "
            "(got \"%s\")",
            cmd(1));

    CHECK(!strcmp(cmd(2), "route add default gw " GOOD_GW),
            "1.4 the gateway is installed as `route add default gw <gw>` "
            "(got \"%s\")",
            cmd(2));

    CHECK(!strcmp(cmd(3), "printf 'nameserver " GOOD_GW "\\n' > /etc/resolv.conf"),
            "1.5 the resolver is rewritten with a single-quoted busybox printf, "
            "the ONE interpolation on this path that is quoted at all "
            "(got \"%s\")",
            cmd(3));

    CHECK(tst_cmd_n == 4 && !tst_cmd_over,
            "1.7 a static costs exactly 4 commands, no more (got %d: %s)", tst_cmd_n, cmd_join());

    reset();
    tst_pidf_write(TST_MAIN_PIDF, TST_MAIN_PID);
    tst_alive = 1;
    CHECK(hal_net()->dhcp() == 0 && tst_cmd_n == 2 && tst_kill_n == 1 &&
                    tst_kill_pid[0] == TST_MAIN_PID && tst_kill_sig[0] == SIGTERM &&
                    access(TST_MAIN_PIDF, F_OK) != 0 && !strcmp(cmd(0), NET_ADDR_ZERO) &&
                    !strcmp(cmd(1), NET_UDHCPC_START),
            "1.8 `ip,dhcp` is exactly stop-udhcpc (pidfile SIGTERM), zero the "
            "address, restart "
            "udhcpc — and the restart is character-for-character wifi_mgr.c's "
            "UDHCPC_CMD minus its trailing \" &\", so the two never leave two "
            "clients with different timing and different pidfiles on the same "
            "interface (got %d: %s)",
            tst_cmd_n, cmd_join());

    CHECK(strstr(cmd(1), "-p " NET_INIT_PIDFILE) != NULL && strstr(cmd(1), NET_OTHER_IFACE) == NULL,
            "1.9 ...including the PIDFILE, which is what makes it the same client "
            "and not a second one. It is the pidfile THIS BOARD's own DHCP client "
            "writes (%s), spelled out here as that other process's literal — "
            "asserted on the string mm actually shelled out, not on this file's "
            "copy of it, and with the other product's interface name required to "
            "be absent (\"%s\")",
            NET_INIT_PIDFILE, cmd(1));

#if NET_WIRED_ALT
    reset();
    {
        FILE *pf = fopen(WIRED_DHCP_PIDF, "w");
        if (pf) {
            fprintf(pf, "4242\n");
            fclose(pf);
        }
        tst_kill_n     = 0;
        tst_alive      = 1;
        tst_exec_alive = 0;
        tst_fork_child = 1;
        wired_dhcp_stop(NULL);
        tst_fork_child = 0;
        CHECK(tst_kill_n == 1 && tst_kill_pid[0] == 4242 && tst_kill_sig[0] == SIGTERM,
                "2.9 losing the wired carrier SIGTERMs only the %s udhcpc "
                "(%d signal(s))",
                NET_WIRED_IFACE, tst_kill_n);
        CHECK(tst_cmd_n == 1 && !strcmp(cmd(0), "ip -4 addr flush dev " NET_WIRED_IFACE) &&
                        !tst_exec_alive,
                "2.10 ...and once it has exited flushes %s's IPv4 addresses, "
                "which drops every route through it (got %d: %s)",
                NET_WIRED_IFACE, tst_cmd_n, cmd_join());
        CHECK(cmd_has(MM_NET_IFACE) == 0 && access(WIRED_DHCP_PIDF, F_OK) != 0,
                "2.11 ...touching nothing on %s, and the pidfile is gone", MM_NET_IFACE);
    }
    {
        static const struct {
            const char *carrier;
            int         main_dhcp;
            int         renew;
        } c[] = {
            { "1\n", 1, 1 },
            { "0\n", 1, 0 },
            { "1\n", 0, 0 },
        };
        const char *path = MM_SYS_NET_DIR MM_NET_IFACE "/carrier";
        FILE       *f;

        mkdir(MM_SYS_NET_DIR, 0755);
        mkdir(MM_SYS_NET_DIR MM_NET_IFACE, 0755);
        for (size_t k = 0; k < sizeof c / sizeof c[0]; k++) {
            reset();
            f = fopen(path, "w");
            if (f) {
                fputs(c[k].carrier, f);
                fclose(f);
            }
            tst_pidf_write(WIRED_DHCP_PIDF, 4242);
            if (c[k].main_dhcp) {
                tst_pidf_write(TST_MAIN_PIDF, TST_MAIN_PID);
                tst_live_pid = TST_MAIN_PID;
            }
            tst_alive      = 1;
            tst_fork_child = 1;
            wired_dhcp_stop(NULL);
            tst_fork_child = 0;
            int renewed    = tst_kill_n == 2 && tst_kill_pid[1] == TST_MAIN_PID &&
                    tst_kill_sig[1] == SIGUSR1;
            CHECK(tst_kill_pid[0] == 4242 && tst_kill_sig[0] == SIGTERM && renewed == c[k].renew &&
                            tst_kill_n == 1 + c[k].renew && tst_cmd_n == 1,
                    "2.12.%zu losing %s with %s carrier %c and %s udhcpc %s "
                    "SIGUSR1 to that client, whose lease script rewrites "
                    "resolv.conf with its own DNS; a static %s config runs no "
                    "client, so its DNS is left alone (%d signal(s), %d "
                    "command(s))",
                    k, NET_WIRED_IFACE, MM_NET_IFACE, c[k].carrier[0], c[k].main_dhcp ? "a" : "no",
                    c[k].renew ? "sends" : "sends no", MM_NET_IFACE, tst_kill_n, tst_cmd_n);
            remove(path);
        }
        rmdir(MM_SYS_NET_DIR MM_NET_IFACE);
        reset();
    }

    reset();
    tst_pidf_write(WIRED_DHCP_PIDF, 4242);
    tst_alive        = 1;
    tst_term_ignored = 1;
    tst_kill_lag     = 3;
    tst_exec_alive   = 0;
    tst_fork_child   = 1;
    wired_dhcp_stop(NULL);
    tst_fork_child = 0;
    CHECK(tst_kill_n == 2 && tst_kill_pid[0] == 4242 && tst_kill_sig[0] == SIGTERM &&
                    tst_kill_pid[1] == 4242 && tst_kill_sig[1] == SIGKILL,
            "2.13 a wired udhcpc that outlives SIGTERM for 2 s is SIGKILLed "
            "(%d signal(s))",
            tst_kill_n);
    CHECK(tst_cmd_n == 1 && !tst_exec_alive && tst_dying == 0 && access(WIRED_DHCP_PIDF, F_OK) != 0,
            "2.14 ...and the flush waits until the SIGKILLed client has exited "
            "(got %d: %s)",
            tst_cmd_n, cmd_join());

    reset();
    tst_pidf_write(WIRED_DHCP_PIDF, 4242);
    tst_comm_set(4242, "sshd");
    tst_alive      = 1;
    tst_fork_child = 1;
    wired_dhcp_stop(NULL);
    tst_fork_child = 0;
    tst_comm_set(4242, "udhcpc");
    CHECK(tst_kill_n == 0 && access(WIRED_DHCP_PIDF, F_OK) != 0 && tst_cmd_n == 1,
            "2.15 a wired pidfile whose pid is no longer a udhcpc is removed "
            "without signalling that process (%d signal(s))",
            tst_kill_n);

    {
        static const int main_dhcp[] = { 1, 0 };

        for (size_t k = 0; k < sizeof main_dhcp / sizeof main_dhcp[0]; k++) {
            reset();
            memset(&g_wired, 0, sizeof g_wired);
            tst_sys_write(MM_NET_IFACE, "carrier", "1\n");
            tst_sys_write(NET_WIRED_IFACE, "carrier", "1\n");
            tst_pidf_write(WIRED_DHCP_PIDF, 4242);
            tst_alive = 1;
            mm_net_wired_rule(NULL, 0);
            CHECK(tst_cmd_n == 0 && tst_kill_n == 0 && g_wired.link == 1,
                    "2.16.%zu a running %s udhcpc with carrier in %s is adopted "
                    "(got %d: %s)",
                    k, NET_WIRED_IFACE, MM_SYS_NET_DIR, tst_cmd_n, cmd_join());

            remove(WIRED_DHCP_PIDF);
            tst_alive = 0;
            tst_sys_write(NET_WIRED_IFACE, "carrier", "0\n");
            if (main_dhcp[k]) {
                tst_pidf_write(TST_MAIN_PIDF, TST_MAIN_PID);
                tst_live_pid = TST_MAIN_PID;
            }
            tst_fork_child = 1;
            mm_net_wired_rule(NULL, 1000);
            tst_fork_child = 0;
            int renewed    = tst_kill_n == 1 && tst_kill_pid[0] == TST_MAIN_PID &&
                    tst_kill_sig[0] == SIGUSR1;
            CHECK(!strcmp(cmd(0), "ip -4 addr flush dev " NET_WIRED_IFACE) && tst_cmd_n == 1 &&
                            (main_dhcp[k] ? renewed : tst_kill_n == 0),
                    "2.17.%zu a %s udhcpc that died before %s lost carrier "
                    "still gets the flush, and the DNS goes back to %s (%s client): "
                    "%s (got %d: %s, %d signal(s))",
                    k, NET_WIRED_IFACE, NET_WIRED_IFACE, MM_NET_IFACE,
                    main_dhcp[k] ? "running" : "no",
                    main_dhcp[k] ? "the running client is asked to renew" :
                                   "resolv.conf is left untouched",
                    tst_cmd_n, cmd_join(), tst_kill_n);

            tst_cmd_n      = 0;
            tst_kill_n     = 0;
            tst_fork_child = 1;
            mm_net_wired_rule(NULL, 2000);
            tst_fork_child = 0;
            CHECK(tst_cmd_n == 0 && tst_kill_n == 0,
                    "2.18.%zu a wired link that stays down is not flushed again "
                    "(got %d: %s)",
                    k, tst_cmd_n, cmd_join());

            tst_sys_clear(NET_WIRED_IFACE, "carrier");
            tst_sys_clear(MM_NET_IFACE, "carrier");
        }
        memset(&g_wired, 0, sizeof g_wired);
        reset();
    }
#endif /* NET_WIRED_ALT */

#if IMPL_NET_IS_WIFI
    reset();
    tst_wpa_ok_at = -1;
    int assoc     = hal_net()->wait_ready(120000);
    CHECK(assoc == -1 && tst_pipe_n == 120 && tst_slept_s == 120,
            "4.1 wait_assoc is bounded at exactly 120 probes and 120 s and then "
            "GIVES UP, returning 0 so the caller applies anyway (got rc %d, %d "
            "probe(s), %u s)",
            assoc, tst_pipe_n, tst_slept_s);

    reset();
    tst_wpa_ok_at = 0;
    assoc         = hal_net()->wait_ready(120000);
    CHECK(assoc == 0 && tst_pipe_n == 1 && tst_slept_s == 0,
            "4.2 ...and returns on the FIRST probe when wpa is already COMPLETED, "
            "without sleeping — the wait costs nothing on a warm boot (rc %d, %d "
            "probe(s), %u s)",
            assoc, tst_pipe_n, tst_slept_s);

    reset();
    tst_popen_null = 1;
    assoc          = hal_net()->wait_ready(120000);
    CHECK(assoc == -1 && tst_pipe_n == 120 && tst_slept_s == 120,
            "4.3 popen() itself failing every time does not break the bound "
            "either — the `if (f)` arm still counts the iteration (rc %d, %d "
            "probe(s), %u s)",
            assoc, tst_pipe_n, tst_slept_s);

#else  /* IMPL_NET_IS_WIFI */
    reset();
    CHECK(hal_net()->wait_ready(0) == -1 && tst_pipe_n == 0,
            "4.1 a WIRED wait_ready issues no wpa probe at all (%d probe(s))", tst_pipe_n);
#endif /* IMPL_NET_IS_WIFI */

    if (IMPL_NET_IS_WIRED)
        CHECK(!strcmp(MM_NET_IFACE, "eth1"),
                "6.1 x3's interface is eth1 — its ONLY NIC is wired; there is no "
                "wlan0 on this board (got \"%s\")",
                MM_NET_IFACE);
    else
        CHECK(!strcmp(MM_NET_IFACE, "wlan0"),
                "6.1 nano3s' primary interface is wlan0 — L2 owned by "
                "wpa_supplicant (wifi_mgr.c); USB Ethernet is NET_WIRED_ALT "
                "(got \"%s\")",
                MM_NET_IFACE);
#ifdef NET_USB_IFACE
        CHECK(!strcmp(NET_WIRED_IFACE, "eth0") && !strcmp(NET_USB_IFACE, "usb0"),
                "6.1a USB Ethernet names are eth0 (ASIX/RTL/SMSC) and usb0 "
                "(CDC/RNDIS) (got %s / %s)",
                NET_WIRED_IFACE, NET_USB_IFACE);
#endif

    {
        int named = 0, foreign = 0, total = 0;

        reset();
        hal_net()->set_static(GOOD_IP, GOOD_MASK, GOOD_GW, "");
        hal_net()->dhcp();
        for (int k = 0; k < tst_cmd_n; k++, total++) {
            if (strstr(tst_cmd[k], MM_NET_IFACE))
                named++;
            if (strstr(tst_cmd[k], NET_OTHER_IFACE))
                foreign++;
        }

        CHECK(total > 0 && named > 0 && foreign == 0,
                "6.2 across the WHOLE region — the static and DHCP sets — "
                "every string that names an interface names "
                "\"%s\", and NOT ONE mentions \"%s\", the interface this product "
                "does not have (%d string(s), %d named, %d foreign)",
                MM_NET_IFACE, NET_OTHER_IFACE, total, named, foreign);
    }

    reset();
    tst_pidf_write(TST_MAIN_PIDF, TST_MAIN_PID);
    tst_alive = 1;
    hal_net()->dhcp();
    CHECK(cmd_has("pidof") == 0 && tst_kill_n == 1 && tst_kill_pid[0] == TST_MAIN_PID &&
                    strstr(cmd(1), "-i " MM_NET_IFACE " ") != NULL &&
                    strstr(cmd(1), "-p " NET_INIT_PIDFILE) != NULL,
            "6.3 the stop is scoped to \"%s\": it signals only the pid in the "
            "pidfile the replacement client writes (%s), never `pidof udhcpc`, "
            "so a DHCP client on any other interface is left running (%d "
            "signal(s): %s)",
            MM_NET_IFACE, NET_INIT_PIDFILE, tst_kill_n, cmd_join());

    {
        char name[16];
        mm_dev_name(name, sizeof name);

        if (IMPL_NET_IS_WIRED) {
            CHECK(!strcmp(DEVNAME_PRE, "x3_"),
                    "7.1 x3 announces itself as x3, not as a nano3s: the "
                    "token comes from this product's identity (got \"%s\")",
                    DEVNAME_PRE);
        } else {
            CHECK(!strcmp(DEVNAME_PRE, "nan3s_"),
                    "7.1 nano3s SHIPS, and its name is the shipped firmware's byte for "
                    "byte — DEVNAME_PRE, fed to wifi_init and the :10001 reply — and "
                    "is one of the three prefixes the phone app knows. Changing it "
                    "breaks re-discovery on a shipped product (got \"%s\")",
                    DEVNAME_PRE);
            CHECK(!strcmp(name, "nan3s_0000"),
                    "7.2 nano3s' built name is byte-identical to the literal this "
                    "builder carried before the key existed (got \"%s\")",
                    name);
        }

        size_t      plen = strlen(DEVNAME_PRE);
        const char *tail = strrchr(name, '_');
        CHECK(plen >= 2 && DEVNAME_PRE[plen - 1] == '_',
                "7.3 the prefix ends in '_' — the app takes split('_').last, so "
                "without it the DNA is not a field at all (got \"%s\")",
                DEVNAME_PRE);
        CHECK(!strncmp(name, DEVNAME_PRE, plen) && tail && strlen(tail + 1) == 4,
                "7.4 the built name is DEVNAME_PRE followed by exactly 4 DNA hex "
                "chars, so the app's startsWith and split('_').last both resolve "
                "(got \"%s\")",
                name);
        CHECK(plen + 4 + 1 <= 16,
                "7.5 prefix + 4 DNA chars + NUL fits mm_miner_main.c's char "
                "dev_name[16] uncut — snprintf truncates in silence, and a "
                "truncated name is a device the app cannot find (needs %zu bytes)",
                plen + 5);
    }

    {
        char name[16];

        tst_mac_clear("eth0");
        tst_mac_clear("wlan0");

        reset();
        tst_mac_set(MM_NET_IFACE, "02:11:22:33:a4:5b");
        tst_mac_set(NET_OTHER_IFACE, "02:66:77:88:99:aa");
        mm_dev_name(name, sizeof name);
        CHECK(!strcmp(name, DEVNAME_PRE "a45b"),
                "7.6 DNA unreadable: the name takes the last 2 bytes of the "
                "MAC of %s, the product's own interface (got \"%s\")",
                MM_NET_IFACE, name);
        CHECK(strstr(tst_logbuf, "dna device error") != NULL ||
                        strstr(tst_logbuf, "read dna error") != NULL,
                "7.7 the DNA read failure is still logged as ERR (log \"%s\")", tst_logbuf);
        CHECK(strstr(tst_logbuf, "Device ID (DNA):") == NULL,
                "7.7a an unreadable DNA is not also logged as a zero device ID "
                "(log \"%s\")",
                tst_logbuf);

        reset();
        tst_mac_clear(MM_NET_IFACE);
        mm_dev_name(name, sizeof name);
#if NET_WIRED_ALT
        CHECK(!strcmp(name, DEVNAME_PRE "99aa"),
                "7.8 no %s MAC: the name falls back to the wired alternative "
                "%s's MAC (got \"%s\")",
                MM_NET_IFACE, NET_WIRED_IFACE, name);
#else  /* NET_WIRED_ALT */
        CHECK(!strcmp(name, DEVNAME_PRE "0000"),
                "7.8 no %s MAC: %s is not an interface of this product, so "
                "the name keeps the zero DNA (got \"%s\")",
                MM_NET_IFACE, NET_OTHER_IFACE, name);
#endif /* NET_WIRED_ALT */

        reset();
        tst_mac_clear(NET_OTHER_IFACE);
        tst_mac_set(MM_NET_IFACE, "00:00:00:00:00:00");
        mm_dev_name(name, sizeof name);
        CHECK(!strcmp(name, DEVNAME_PRE "0000"),
                "7.9 an all-zero MAC is not used; the name keeps the zero DNA "
                "(got \"%s\")",
                name);

        reset();
        tst_mac_set(MM_NET_IFACE, "garbage");
        mm_dev_name(name, sizeof name);
        CHECK(!strcmp(name, DEVNAME_PRE "0000"),
                "7.10 an unparsable MAC is not used; the name keeps the zero "
                "DNA (got \"%s\")",
                name);

        tst_mac_clear(MM_NET_IFACE);
        rmdir(MM_SYS_NET_DIR);
    }

    reset();
    tst_console_reset();
    tst_ip[0]    = '\0';
    tst_ip_calls = 0;

    mm_net_ip_show_rule(NULL, 0);
    CHECK(tst_console_lines() == 0, "8.1 a box with no address says nothing (got \"%s\")",
            tst_console());

    tst_console_reset();
    snprintf(tst_ip, sizeof tst_ip, "192.168.1.23");
    mm_net_ip_show_rule(NULL, 1000);
    CHECK(!strcmp(tst_console(), "mm_miner: IP 192.168.1.23\n"),
            "8.2 the FIRST address is announced at once and in full, newline "
            "included, on the console and not only in the log (got \"%s\")",
            tst_console());
    CHECK(!strncmp(tst_logbuf, "IP 192.168.1.23\n", sizeof "IP 192.168.1.23\n" - 1) ||
                    strstr(tst_logbuf, "\nIP 192.168.1.23\n") != NULL,
            "8.3 and it is in the log as well, so a box read over ssh after the "
            "fact is not worse off than one watched over serial (got \"%s\")",
            tst_logbuf);

    tst_console_reset();
    mm_net_ip_show_rule(NULL, 2000);
    mm_net_ip_show_rule(NULL, 3000);
    CHECK(tst_console_lines() == 0,
            "8.4 an UNCHANGED address is not news: the 1 Hz rule must not print "
            "per tick, or the console is 3600 lines an hour (got \"%s\")",
            tst_console());

    tst_console_reset();
    snprintf(tst_ip, sizeof tst_ip, "10.0.0.7");
    mm_net_ip_show_rule(NULL, 5000);
    CHECK(!strcmp(tst_console(), "mm_miner: IP 10.0.0.7\n"),
            "8.5 a changed address is announced on the very next evaluation, "
            "however soon after the previous change (got \"%s\")",
            tst_console());
    tst_console_reset();
    mm_net_ip_show_rule(NULL, 11000);
    CHECK(tst_console_lines() == 0,
            "8.6 ...and only once: the same address on the next evaluation is "
            "not news (got \"%s\")",
            tst_console());
    CHECK(tst_ip_calls == 6,
            "8.7 one net_state_ip() per evaluation and nothing else — the rule "
            "names no interface, which is why the same code serves this "
            "descriptor's \"%s\" and the other product's (got %d calls in 6 "
            "evaluations)",
            MM_NET_IFACE, tst_ip_calls);

    tst_console_reset();
    tst_ip[0] = '\0';
    mm_net_ip_show_rule(NULL, 12000);
    mm_net_ip_show_rule(NULL, 21000);
    CHECK(!strcmp(tst_console(), "mm_miner: IP none\n"),
            "8.8 LOSING the address is announced too, once: leaving the console "
            "naming an address the box no longer answers on is worse than saying "
            "nothing (got \"%s\")",
            tst_console());
    tst_console_reset();
    mm_net_ip_show_rule(NULL, 22000);
    mm_net_ip_show_rule(NULL, 25000);
    CHECK(tst_console_lines() == 0,
            "8.9 and then it stays quiet — an addressless box must not repeat "
            "(got \"%s\")",
            tst_console());

    tst_console_reset();
    for (int t = 0; t < 60; t++) {
        snprintf(tst_ip, sizeof tst_ip, "10.1.%d.%d", t / 256, t % 256);
        mm_net_ip_show_rule(NULL, (uint64_t)(32000 + t * 1000));
    }
    CHECK(tst_console_lines() == 60,
            "8.10 60 s of an address changing every tick costs 60 console lines: "
            "every change is announced, none is held or dropped (got %d)",
            tst_console_lines());
    CHECK(strstr(tst_console(), "mm_miner: IP 10.1.0.50\nmm_miner: IP 10.1.0.51\n") != NULL,
            "8.11 the lines name the addresses in the order the box held them "
            "(got \"%s\")",
            tst_console());
    tst_console_reset();
    mm_net_ip_show_rule(NULL, 92000);
    CHECK(tst_console_lines() == 0,
            "8.12 and once the address settles the console is quiet: the last "
            "line already named what the box has (got \"%s\")",
            tst_console());

    tst_comm_clear(TST_MAIN_PID);
    tst_comm_clear(4242);
    rmdir(MM_PROC_DIR);

    int rc = tst_done();
    if (chdir("..") == 0)
        rm_rf(scratch);
    return rc;
}
