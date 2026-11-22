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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>

#include "osif.h"
#include "net_util.h"
#include "wifi_mgr.h"
#include "ble_prov.h"
#include "ini_file.h"
#include "../../tests/include/tst_check.h"

#define EV_MAX 16  /* Capacity of tst_cmd[], commands */
#define EV_LEN 256 /* Size of one tst_cmd[] entry, bytes */
/* DHCP client restart command wifi_mgr.c issues for wlan0 */
#define UDHCPC_RESTART \
    "udhcpc -i wlan0 -b -T 1 -A 2 -s /etc/network/udhcpc-metric.script -p /var/run/udhcpc.wlan0.pid &"
/* Three CJK characters in wpa_cli \x escapes: 9 octets, 36 characters */
#define SSID_CN  "\\xe4\\xb8\\xad\\xe6\\x96\\x87\\xe5\\xad\\x97"
#define SSID_CN3 SSID_CN SSID_CN SSID_CN /* SSID_CN three times: 27 octets */
#define MB1      "\\xe4\\xb8\\xad"       /* One 3-octet UTF-8 character in wpa_cli \x escapes */
#define MB5      MB1 MB1 MB1 MB1 MB1     /* MB1 five times: 15 octets */
/* SSID at the 32-octet maximum: ten 3-octet characters and one 2-octet one */
#define SSID_MB32 MB5 MB5 "\\xc2\\xa9"
/* 33-octet SSID: ten 3-octet characters, a lone 0xc2 octet and "ab" */
#define SSID_33OCT \
    MB5 MB5 "\\xc2" \
            "ab"
/* wpa_supplicant.conf with a saved network, SSID "lab" */
#define CONF_SAVED \
    "ctrl_interface=/var/run/wpa_supplicant\nnetwork={\n" \
    "\tssid=\"lab\"\n\tpsk=\"12345678\"\n}\n"
/* wpa_supplicant.conf with a saved network whose SSID is hex-encoded */
#define CONF_HEX \
    "network={\n\tssid=e4b8ade69687\n\tscan_ssid=1\n" \
    "\tpsk=\"12345678\"\n}\n"
/* wpa_supplicant.conf with no saved network */
#define CONF_EMPTY "ctrl_interface=/var/run/wpa_supplicant\nupdate_config=1\n"
/* wpa_cli status reply: associated to "lab" with an address */
#define ST_UP   "wpa_state=COMPLETED\nssid=lab\nip_address=192.168.1.9\n"
#define ST_DOWN "wpa_state=SCANNING\n" /* wpa_cli status reply: scanning, not associated */

/* pids the wpa_supplicant and wpa_cli pid files hold */
#define TST_SUPP_PID 4242
#define TST_CLI_PID  4343

int   tst_system(const char *cmd);
FILE *tst_fopen(const char *path, const char *mode);
int   tst_kill(pid_t pid, int sig);
int   tst_remove(const char *path);
int   tst_exec_argv(const char *const argv[], char *res, int res_size);
int   tst_exec_argv_timeout(const char *const argv[], char *res, int res_size, int timeout_ms);
void  tst_log(osif_loglvl_t lvl, const char *fmt, ...);
int   tst_access(const char *path, int mode);
int   tst_ini_file_seed(const char *path, const char *src);
int   tst_ini_file_head_fill(const char *path, const char *src);

#define system(c)                     tst_system(c)       /* wifi_mgr.c shell commands, recorded */
#define fopen(p, m)                   tst_fopen((p), (m)) /* wifi_mgr.c opens, served from memory */
#define kill(p, s)                    tst_kill((p), (s))  /* wifi_mgr.c signals; aborts the test */
#define remove(p)                     tst_remove(p)       /* wifi_mgr.c file removal, a no-op */
#define exec_argv(a, r, n)            tst_exec_argv((a), (r), (n)) /* wpa_cli, canned replies */
#define exec_argv_timeout(a, r, n, t) tst_exec_argv_timeout((a), (r), (n), (t))
#define osif_log(...)                 tst_log(__VA_ARGS__) /* wifi_mgr.c logging, into tst_logbuf */
#define access(p, m)                  tst_access((p), (m)) /* wifi_mgr.c path probes */
#define ini_file_seed(p, s)           tst_ini_file_seed((p), (s))
#define ini_file_head_fill(p, s)      tst_ini_file_head_fill((p), (s))

#undef _DEFAULT_SOURCE
#include "wifi_mgr.c"

#undef ini_file_head_fill
#undef ini_file_seed
#undef access
#undef osif_log
#undef exec_argv_timeout
#undef exec_argv
#undef kill
#undef fopen
#undef system

static char tst_cmd[EV_MAX][EV_LEN];         /* Shell commands passed to system(), in call order */
static int  tst_cmd_n;                       /* system() calls; may exceed EV_MAX */
static char tst_status[BLE_DATA_STATUS_LEN]; /* Last status pushed through ble_set_data_status() */
static int  tst_status_n;                    /* ble_set_data_status() calls */
static char tst_logbuf[4096];                /* Every log line, newline separated */
static int  tst_exec_n;                      /* exec_argv() calls */
/* wlan0 carrier state the carrier file reads: 1 up, 0 down, -1 file absent */
static int tst_carrier = -1;
/* wpa_supplicant.conf contents fopen() serves; NULL = file absent */
static const char *tst_wpa_conf;
/* Non-zero: wpa_supplicant is not running and its control socket is absent */
static int tst_ctrl_absent;
/* Non-zero: the wpa_cli action monitor is not running */
static int tst_cli_absent;
static int tst_cli_n; /* wpa_cli -a starts */
/* wpa_cli subcommand answered with tst_reply_out; NULL = none */
static const char *tst_reply_cmd;
static const char *tst_reply_out;            /* Reply returned for tst_reply_cmd */
static char tst_setnet_val[BLE_DATA_WR_LEN]; /* Value argument of the last answered set_network */
static int  tst_save_n;                      /* save_config calls */
static int  tst_ble_startup_n;
/* Last ble_dev_set_power() argument, -1 before any call */
static int  tst_ble_power = -1;
static int  tst_ble_run   = 1; /* Value ble_dev_is_run() returns */
static int  tst_seq;
static int  tst_seed_n;
static int  tst_seed_at;
static int  tst_head_n;
static int  tst_head_at;
static int  tst_supp_n;
static int  tst_supp_at;
static char tst_seed_args[2 * EV_LEN];
static char tst_head_args[2 * EV_LEN];
static char tst_seed_dir[] = "/tmp/test_wifi_prov.XXXXXX";
static char tst_seed_dst[EV_LEN];
static char tst_seed_src[EV_LEN];
static int  tst_last_timeout;
static int  tst_lock_held;
static int (*tst_exec_hook)(const char *sub, const char *const argv[], char *res, int res_size);
static int  tst_conn_status_n;
static int  tst_conn_injected;
static char tst_conn_ssid[BLE_DATA_WR_LEN];
static char tst_conn_psk[BLE_DATA_WR_LEN];

static const char *tst_addnet_reply;    /* add_network reply hook_addnet returns */
static int         tst_addnet_setnet_n; /* set_network calls under hook_addnet */
static char        tst_addnet_sel[16];  /* select_network id under hook_addnet */

static void tst_check_unlocked(void)
{
    if (pthread_mutex_trylock(&g_wifi_lock) == 0)
        pthread_mutex_unlock(&g_wifi_lock);
    else
        tst_lock_held++;
}

int tst_system(const char *cmd)
{
    tst_check_unlocked();
    if (tst_cmd_n < EV_MAX)
        snprintf(tst_cmd[tst_cmd_n], EV_LEN, "%s", cmd);
    tst_cmd_n++;
    return 0;
}

int tst_access(const char *path, int mode)
{
    (void)mode;
    if (tst_ctrl_absent && !strcmp(path, WPA_CTRL_FILE))
        return -1;
    return 0;
}

int tst_ini_file_seed(const char *path, const char *src)
{
    tst_seed_n++;
    tst_seed_at = ++tst_seq;
    snprintf(tst_seed_args, sizeof tst_seed_args, "%s %s", path, src);
    return ini_file_seed(tst_seed_dst, tst_seed_src);
}

int tst_ini_file_head_fill(const char *path, const char *src)
{
    tst_head_n++;
    tst_head_at = ++tst_seq;
    snprintf(tst_head_args, sizeof tst_head_args, "%s %s", path, src);
    return ini_file_head_fill(tst_seed_dst, tst_seed_src);
}

static FILE *tst_fmem(const char *text)
{
    static char buf[8][32];
    static int  i;
    char       *b = buf[i++ % 8];

    snprintf(b, sizeof buf[0], "%s", text);
    return fmemopen(b, strlen(b), "r");
}

FILE *tst_fopen(const char *path, const char *mode)
{
    (void)mode;
    if (path && !strcmp(path, WPA_SUPP_PID_FILE))
        return tst_ctrl_absent ? NULL : tst_fmem("4242\n");
    if (path && !strcmp(path, WPA_PID_FILE))
        return tst_cli_absent ? NULL : tst_fmem("4343\n");
    if (path && !strcmp(path, "/proc/4242/comm"))
        return tst_fmem("wpa_supplicant\n");
    if (path && !strcmp(path, "/proc/4343/comm"))
        return tst_fmem("wpa_cli\n");
    if (path && tst_wpa_conf && !strcmp(path, WPA_CONF_PATH)) {
        static char conf[1024];
        snprintf(conf, sizeof conf, "%s", tst_wpa_conf);
        return fmemopen(conf, strlen(conf), "r");
    }
    if (path && strstr(path, "/carrier")) {
        if (tst_carrier < 0)
            return NULL;
        static char buf[2];
        buf[0] = tst_carrier ? '1' : '0';
        buf[1] = '\n';
        return fmemopen(buf, sizeof buf, "r");
    }
    return NULL;
}

int tst_kill(pid_t pid, int sig)
{
    if (sig == 0 && (pid == TST_SUPP_PID || pid == TST_CLI_PID))
        return 0;
    fprintf(stderr,
            "  FAIL: kill() reached - this suite must never signal a "
            "process\n");
    abort();
}

int tst_remove(const char *path)
{
    (void)path;
    return 0;
}

static void canned_reply(const char *subcmd, const char *out)
{
    tst_reply_cmd     = subcmd;
    tst_reply_out     = out;
    tst_setnet_val[0] = '\0';
}

int tst_exec_argv(const char *const argv[], char *res, int res_size)
{
    const char *sub = (argv && argv[3]) ? argv[3] : "";

    tst_exec_n++;
    tst_last_timeout = -1;
    tst_check_unlocked();
    if (tst_exec_hook && tst_exec_hook(sub, argv, res, res_size))
        return 0;
    if (argv && argv[0] && !strcmp(argv[0], "wpa_cli") && argv[1] && !strcmp(argv[1], "-a")) {
        tst_cli_n++;
        res[0] = '\0';
        return 0;
    }
    if (argv && argv[0] && !strcmp(argv[0], "wpa_supplicant")) {
        tst_supp_n++;
        tst_supp_at = ++tst_seq;
        res[0]      = '\0';
        return 0;
    }
    if (tst_reply_cmd && !strcmp(sub, tst_reply_cmd)) {
        if (!strcmp(sub, "set_network") && argv[6])
            snprintf(tst_setnet_val, sizeof tst_setnet_val, "%s", argv[6]);
        snprintf(res, res_size, "%s", tst_reply_out);
        return 0;
    }
    if (tst_reply_cmd && !strcmp(sub, "scan")) {
        res[0] = '\0';
        return 0;
    }
    if (!strcmp(sub, "save_config")) {
        tst_save_n++;
        snprintf(res, res_size, "OK\n");
        return 0;
    }

    fprintf(stderr,
            "  FAIL: exec_argv(%s) reached - no path under test may "
            "drive wpa_cli\n",
            sub);
    abort();
}

int tst_exec_argv_timeout(const char *const argv[], char *res, int res_size, int timeout_ms)
{
    int rc = tst_exec_argv(argv, res, res_size);

    tst_last_timeout = timeout_ms;
    return rc;
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

void ble_set_data_status(char *sta)
{
    tst_check_unlocked();
    snprintf(tst_status, sizeof tst_status, "%s", sta);
    tst_status_n++;
}
void ble_set_data(char *data)
{
    (void)data;
    tst_check_unlocked();
}

void ble_dev_init(char *name, ble_data_process_func cb)
{
    (void)name;
    (void)cb;
}
int ble_dev_startup(void)
{
    tst_check_unlocked();
    tst_ble_startup_n++;
    return 0;
}
void ble_dev_set_power(int onoff)
{
    tst_check_unlocked();
    tst_ble_power = onoff;
}
int ble_dev_is_run(void)
{
    return tst_ble_run;
}

static void reset(void)
{
    tst_cmd_n     = 0;
    tst_status_n  = 0;
    tst_status[0] = '\0';
    tst_logbuf[0] = '\0';
    memset(&g_ssid_info, 0, sizeof g_ssid_info);
    memset(&g_ssid_info_config, 0, sizeof g_ssid_info_config);
    memset(&g_ssid_status, 0, sizeof g_ssid_status);
    memset(g_ssid_list, 0, sizeof g_ssid_list);
    g_ssid_list_cnt      = 0;
    g_conn_state         = CONN_INIT;
    g_dev_action         = 0;
    g_wifi_retry_backoff = 0;
    g_wifi_retry_next    = 0;
    tst_carrier          = -1;
    canned_reply(NULL, NULL);
    tst_wpa_conf    = NULL;
    tst_ctrl_absent = 0;
    tst_cli_absent  = 0;
    tst_cli_n       = 0;
    atomic_store(&g_svc_at, 0);
    tst_save_n        = 0;
    tst_ble_startup_n = 0;
    tst_ble_power     = -1;
    tst_ble_run       = 1;
    tst_seq           = 0;
    tst_seed_n        = 0;
    tst_seed_at       = 0;
    tst_head_n        = 0;
    tst_head_at       = 0;
    tst_supp_n        = 0;
    tst_supp_at       = 0;
    tst_seed_args[0]  = '\0';
    tst_exec_hook     = NULL;
    g_prov_quiet      = 0;
    g_wifi_is_prov    = WIFI_PROV_NONE;
    g_wifi_link       = 0;
    g_conn_time       = 0;
    sem_destroy(&g_prov_sem);
    sem_init(&g_prov_sem, 0, 0);
}

static int sem_count(void)
{
    int v = -1;
    sem_getvalue(&g_prov_sem, &v);
    return v;
}

static void hexify(const char *s, char *out)
{
    size_t i;
    for (i = 0; s[i]; i++)
        sprintf(out + 2 * i, "%02X", (unsigned char)s[i]);
    out[2 * i] = '\0';
}

static int setssid(const char *ssid, const char *pw, const char *ident, int auth)
{
    char hs[600], hp[600], hi[600], line[2048];
    hexify(ssid, hs);
    hexify(pw, hp);
    hexify(ident, hi);
    snprintf(line, sizeof line, "setssid,%s,%s,%s,%d", hs, hp, hi, auth);
    return ble_cmd_process(line);
}

static void put_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static int file_is(const char *path, const char *text)
{
    char   buf[256];
    size_t n = 0;
    FILE  *f = fopen(path, "r");
    if (f) {
        n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
    }
    buf[n] = '\0';
    return f && !strcmp(buf, text);
}

static int hook_connect(const char *sub, const char *const argv[], char *res, int res_size)
{
    if (!strcmp(sub, "status")) {
        snprintf(res, res_size, ++tst_conn_status_n == 1 ? ST_DOWN : "wpa_state=COMPLETED\n");
        return 1;
    }
    if (!strcmp(sub, "add_network")) {
        snprintf(res, res_size, "0\n");
        return 1;
    }
    if (!strcmp(sub, "set_network")) {
        if (!strcmp(argv[5], "ssid")) {
            snprintf(tst_conn_ssid, sizeof tst_conn_ssid, "%s", argv[6]);
            if (!tst_conn_injected && pthread_mutex_trylock(&g_wifi_lock) == 0) {
                pthread_mutex_unlock(&g_wifi_lock);
                tst_conn_injected = 1;
                setssid("B", "passwordB", "1", 7);
            }
        } else if (!strcmp(argv[5], "psk")) {
            snprintf(tst_conn_psk, sizeof tst_conn_psk, "%s", argv[6]);
        }
        snprintf(res, res_size, "OK\n");
        return 1;
    }
    if (!strcmp(sub, "remove_network") || !strcmp(sub, "select_network")) {
        snprintf(res, res_size, "OK\n");
        return 1;
    }
    return 0;
}

static const char *cmd(int i)
{
    return (i >= 0 && i < tst_cmd_n) ? tst_cmd[i] : "";
}

static void rep(char *buf, size_t cap, char c, size_t n)
{
    if (n >= cap)
        n = cap - 1;
    memset(buf, c, n);
    buf[n] = '\0';
}

static int hook_addnet(const char *sub, const char *const argv[], char *res, int res_size)
{
    if (!strcmp(sub, "status")) {
        snprintf(res, res_size, "wpa_state=COMPLETED\n");
        return 1;
    }
    if (!strcmp(sub, "add_network")) {
        snprintf(res, res_size, "%s", tst_addnet_reply);
        return 1;
    }
    if (!strcmp(sub, "set_network"))
        tst_addnet_setnet_n++;
    if (!strcmp(sub, "select_network"))
        snprintf(tst_addnet_sel, sizeof tst_addnet_sel, "%s", argv[4]);
    if (!strcmp(sub, "set_network") || !strcmp(sub, "remove_network") ||
            !strcmp(sub, "select_network")) {
        snprintf(res, res_size, "OK\n");
        return 1;
    }
    return 0;
}

static int addnet_try(const char *reply)
{
    reset();
    tst_addnet_reply    = reply;
    tst_addnet_setnet_n = 0;
    tst_addnet_sel[0]   = '\0';
    snprintf(g_ssid_info_config.name, sizeof g_ssid_info_config.name, "lab");
    snprintf(g_ssid_info_config.passwd, sizeof g_ssid_info_config.passwd, "12345678");
    g_ssid_info_config.auth = WIFI_AUTH_WPA2_PSK;
    tst_exec_hook           = hook_addnet;
    int rc                  = wifi_connect();
    tst_exec_hook           = NULL;
    return rc;
}

static int rejected(void)
{
    return g_conn_state == CONN_FAIL && tst_status_n == 1 &&
            strstr(tst_status, "\"conn_state\":3}") != NULL && !(g_dev_action & WIFI_ACT_CONNECT);
}

int main(void)
{
    char big[300];

    printf("wifi provisioning: SSID bound, rejection reporting, CONN_FAIL retry\n");

    CHECK(wpa_esc_decoded_len("lab") == 3,
            "1.1 an all-ASCII SSID is its own decoded length (got %d)", wpa_esc_decoded_len("lab"));

    CHECK(strlen(SSID_CN) == 36 && wpa_esc_decoded_len(SSID_CN) == 9,
            "1.2 THE DEFECT IN ONE LINE: the 3-character Chinese SSID arrives as "
            "wpa_supplicant's printf_encode form, %zu characters long, and is 9 "
            "OCTETS - measuring the escaped characters against the 32-octet IEEE "
            "limit refused it (got %d octets)",
            strlen(SSID_CN), wpa_esc_decoded_len(SSID_CN));

    CHECK(wpa_esc_decoded_len("\\n\\r\\t\\e\\\\\\\"") == 6,
            "1.3 the five two-character escapes and the escaped quote each decode "
            "to one octet, as printf_decode does (got %d)",
            wpa_esc_decoded_len("\\n\\r\\t\\e\\\\\\\""));

    CHECK(wpa_esc_decoded_len("\\101\\7") == 2,
            "1.4 an octal escape decodes to one octet whether it carries 3 digits "
            "or 1 (got %d)",
            wpa_esc_decoded_len("\\101\\7"));

    CHECK(wpa_esc_decoded_len("\\xZ") == 1 && wpa_esc_decoded_len("a\\q") == 2 &&
                    wpa_esc_decoded_len("abc\\") == 3,
            "1.5 the three malformed forms terminate and count exactly as "
            "printf_decode does: a bad \\x yields its literal text, an unknown "
            "escape yields the escaped character, a trailing backslash yields "
            "nothing (got %d / %d / %d)",
            wpa_esc_decoded_len("\\xZ"), wpa_esc_decoded_len("a\\q"), wpa_esc_decoded_len("abc\\"));

    reset();
    CHECK(setssid(SSID_CN, "12345678", "1", 7) == 0,
            "2.1 A CHINESE SSID IS ACCEPTED. The field is 36 characters and the "
            "bound is 32; checking the bound against the characters would drop "
            "the whole setssid with a WARN and nothing on the BLE status char");

    CHECK(!strcmp(g_ssid_info_config.name, SSID_CN),
            "2.2 ...and it is stored ESCAPED, byte for byte, because that is what "
            "wifi_connect hands to `set_network <id> ssid P\"%%s\"` - wpa's P-form "
            "is printf-decoded back to the 9 raw octets on the other side "
            "(got \"%s\")",
            g_ssid_info_config.name);

    CHECK((g_dev_action & WIFI_ACT_CONNECT) && g_conn_state == CONN_PEND && tst_status_n == 1 &&
                    strstr(tst_status, "\"conn_state\":1}") != NULL,
            "2.3 ...and it is dialled: WIFI_ACT_CONNECT armed, conn_state 1 pushed "
            "to the app on the BLE thread (%d push(es): \"%s\")",
            tst_status_n, tst_status);

    reset();
    CHECK(setssid(SSID_CN3, "12345678", "1", 7) == 0 && strlen(g_ssid_info_config.name) == 108 &&
                    wpa_esc_decoded_len(g_ssid_info_config.name) == 27,
            "2.4 nine Chinese characters - 27 octets carried as 108 escaped "
            "characters - is accepted too, so the limit really is being read as "
            "octets and not as a widened character count (got %zu chars, %d "
            "octets)",
            strlen(g_ssid_info_config.name), wpa_esc_decoded_len(g_ssid_info_config.name));

    reset();
    rep(big, sizeof big, 'a', 32);
    CHECK(setssid(big, "12345678", "1", 7) == 0 && g_conn_state == CONN_PEND,
            "2.5 a 32-octet plain SSID is accepted - the IEEE maximum, unchanged");

    reset();
    rep(big, sizeof big, 'a', 33);
    CHECK(setssid(big, "12345678", "1", 7) == 1 && !(g_dev_action & WIFI_ACT_CONNECT) &&
                    g_ssid_info_config.name[0] == '\0',
            "2.6 a 33-octet plain SSID is still REFUSED, and nothing is dialled - "
            "the bound was not simply raised, it was moved onto the decoded "
            "octets");

    CHECK(g_conn_state == CONN_FAIL && tst_status_n == 1 &&
                    strstr(tst_status, "\"conn_state\":3}") != NULL,
            "2.7 THE APP IS TOLD. A refused setssid reports failure the same "
            "way a 30 s association timeout does - conn_state 3 on the status "
            "char - not just a WARN in the journal and a status char that never "
            "changes (%d push(es): \"%s\")",
            tst_status_n, tst_status);

    reset();
    CHECK(ble_cmd_process("setssid,ZZ,3132333435363738,31,7") == 1 && g_conn_state == CONN_FAIL &&
                    tst_status_n == 1 && strstr(tst_status, "\"conn_state\":3}") != NULL,
            "2.8 ...and so does the other rejection arm, a field that is not hex "
            "at all (%d push(es): \"%s\")",
            tst_status_n, tst_status);

    reset();
    CHECK(ble_cmd_process("setssid,0,3132333435363738,31,7") == 1 && g_conn_state == CONN_FAIL,
            "2.9 an SSID field carrying no whole hex pair is zero octets and is "
            "refused - the minimum is unchanged");

    reset();
    rep(big, sizeof big, 'p', 63);
    CHECK(setssid("lab", big, "1", 7) == 0 && g_conn_state == CONN_PEND,
            "2.10 a 63-byte passphrase is accepted - the WPA maximum, and the "
            "password rule is unchanged");

    reset();
    rep(big, sizeof big, 'p', 64);
    CHECK(setssid("lab", big, "1", 7) == 1 && g_conn_state == CONN_FAIL,
            "2.11 ...and 64 bytes is refused, with a status push");

    reset();
    g_conn_state = CONN_FAIL;
    snprintf(g_ssid_info_config.name, sizeof g_ssid_info_config.name, "lab");
    snprintf(g_ssid_status.wpa_state, sizeof g_ssid_status.wpa_state, "COMPLETED");
    wifi_retry_recover();
    CHECK(tst_cmd_n == 1 && !strcmp(cmd(0), UDHCPC_RESTART),
            "3.1 THE RETRY EXISTS. Associated with no lease, the backoff slot "
            "restarts the DHCP client, the recovery for a wireless netdev fault "
            "(got %d command(s): \"%s\")",
            tst_cmd_n, cmd(0));

    reset();
    g_conn_state = CONN_FAIL;
    snprintf(g_ssid_info_config.name, sizeof g_ssid_info_config.name, "lab");
    snprintf(g_ssid_status.wpa_state, sizeof g_ssid_status.wpa_state, "SCANNING");
    wifi_retry_recover();
    CHECK(tst_cmd_n == 0,
            "3.2 ...and it does NOTHING while wpa is still trying to associate. "
            "That half belongs to wpa_supplicant: re-driving add_network/"
            "select_network mid-attempt tears its state down, and a scan wipes the "
            "driver's shared BSS queue (got %d command(s): \"%s\")",
            tst_cmd_n, cmd(0));

    reset();
    g_conn_state = CONN_FAIL;
    snprintf(g_ssid_info_config.name, sizeof g_ssid_info_config.name, "lab");
    snprintf(g_ssid_status.wpa_state, sizeof g_ssid_status.wpa_state, "COMPLETED");
    snprintf(g_ssid_status.ip, sizeof g_ssid_status.ip, "192.168.1.9");
    wifi_retry_recover();
    CHECK(tst_cmd_n == 0,
            "3.3 ...and nothing on a link that already has an address, so a "
            "CONN_FAIL pushed by a refused setssid cannot disturb a working "
            "connection (got %d command(s))",
            tst_cmd_n);

    reset();
    g_conn_state = CONN_PEND;
    snprintf(g_ssid_info_config.name, sizeof g_ssid_info_config.name, "lab");
    snprintf(g_ssid_status.wpa_state, sizeof g_ssid_status.wpa_state, "COMPLETED");
    wifi_retry_recover();
    CHECK(tst_cmd_n == 0,
            "3.4 ...and nothing during the first 30 s attempt, which wifi_connect "
            "has already started a fresh udhcpc for (got %d command(s))",
            tst_cmd_n);

    reset();
    snprintf(g_ssid_info_config.name, sizeof g_ssid_info_config.name, "lab");
    wifi_exception();
    CHECK(tst_cmd_n == 1 && !strcmp(cmd(0), UDHCPC_RESTART),
            "3.5 wifi_exception fires for the network being PROVISIONED, not only "
            "for one already connected: g_ssid_info is filled in on success, so "
            "its guard alone made the recovery a no-op on exactly the path that "
            "needs it (got %d command(s): \"%s\")",
            tst_cmd_n, cmd(0));

    reset();
    wifi_exception();
    CHECK(tst_cmd_n == 0,
            "3.6 ...and with no network known at all it still does nothing (got "
            "%d command(s))",
            tst_cmd_n);

    {
        char          scan[1024], status[512], list[BLE_DATA_RD_LEN];
        char          exp_hex[WIFI_SSID_HEX_CHARS_MAX + 1];
        char          hs[600], wire[BLE_DATA_WR_LEN * 2];
        wifi_status_t st;

        CHECK(strlen(SSID_MB32) == WIFI_SSID_ESC_CHARS_MAX &&
                        wpa_esc_decoded_len(SSID_MB32) == WIFI_SSID_OCTETS_MAX,
                "4.1 THE BOUNDING CASE: a 32-octet SSID - the IEEE "
                "maximum - in which no byte is printable ASCII reaches us "
                "printf-encoded as FOUR characters per octet, so it is %zu "
                "characters carrying %d octets. Every bound below is on the "
                "characters; only the last one is on the octets",
                strlen(SSID_MB32), wpa_esc_decoded_len(SSID_MB32));

        reset();
        snprintf(scan, sizeof scan,
                "bssid / frequency / signal level / flags / ssid\n"
                "aa:bb:cc:dd:ee:01\t2412\t-42\t[WPA2-PSK-CCMP][ESS]\t%s\n"
                "aa:bb:cc:dd:ee:02\t2437\t-70\t[WPA2-PSK-CCMP][ESS]\tlab\n",
                SSID_MB32);
        canned_reply("scan_results", scan);
        CHECK(wifi_scan() == 2 && !strcmp(g_ssid_list[0].ssid, SSID_MB32),
                "4.2 THE SCAN PARSE KEEPS ALL 128 CHARACTERS. A %%128 field plus "
                "a copy that starts at &ssid_str[1] to drop wpa_cli's tab "
                "would store at most 127, cutting the final \\xNN in half (got %zu "
                "chars: \"%.24s...\")",
                strlen(g_ssid_list[0].ssid), g_ssid_list[0].ssid);

        CHECK(wpa_esc_decoded_len(g_ssid_list[0].ssid) == WIFI_SSID_OCTETS_MAX,
                "4.3 ...and what it kept is still a WELL-FORMED escape sequence: "
                "it decodes to the 32 octets the AP broadcast, where a cut at 127 "
                "would leave a half escape that decodes to something else entirely "
                "(got %d octets)",
                wpa_esc_decoded_len(g_ssid_list[0].ssid));

        hexify(SSID_MB32, exp_hex);
        memset(list, 0, sizeof list);
        wifi_scan_ssid_results(list, sizeof list, WIFI_SSID_MAX_NUM);
        CHECK(strlen(exp_hex) == WIFI_SSID_HEX_CHARS_MAX &&
                        !strncmp(list, exp_hex, WIFI_SSID_HEX_CHARS_MAX) &&
                        !strncmp(list + WIFI_SSID_HEX_CHARS_MAX, ",7,-42,", 7),
                "4.4 ...and it REACHES THE PHONE. The BLE scan list is ASCII-hex, "
                "so this entry needs 256 hex digits and str_to_hex needs 2n+1 "
                "bytes of staging: one byte short and the entry would be dropped from "
                "the list entirely, by a bare `continue` with no log (%zu-char "
                "list: \"%.24s...\")",
                strlen(list), list);

        reset();
        CHECK(setssid(SSID_MB32, "12345678", "1", 7) == 0 &&
                        !strcmp(g_ssid_info_config.name, SSID_MB32) &&
                        wpa_esc_decoded_len(g_ssid_info_config.name) == WIFI_SSID_OCTETS_MAX &&
                        g_conn_state == CONN_PEND,
                "4.5 THE PROVISIONING LINE CARRIES IT BACK WHOLE. The phone "
                "returns the ssid field hex-encoded, so 128 characters are 256 "
                "hex digits, and a %%254 bound would drop the last two before "
                "anything validated - the field would then decode to 127 characters "
                "ending mid-escape (got %zu chars, %d octets)",
                strlen(g_ssid_info_config.name), wpa_esc_decoded_len(g_ssid_info_config.name));

        hexify(SSID_MB32, hs);
        snprintf(wire, sizeof wire, "setssid,%s,%s,%s,%d$$$$", hs, "3132333435363738", "31", 7);
        CHECK(strlen(wire) <= BLE_DATA_WR_LEN - 1,
                "4.6 ...and that line FITS THE BLE WRITE PATH: the phone writes in "
                "20-byte chunks that ble_prov_dbus reassembles into data_wr until "
                "the $$$$ terminator, refusing the whole line past %d bytes (this "
                "one is %zu)",
                BLE_DATA_WR_LEN - 1, strlen(wire));

        reset();
        canned_reply("set_network", "OK\n");
        CHECK(wpa_set_network(0, "ssid", "P\"%s\"", SSID_MB32) == 0 &&
                        !strcmp(tst_setnet_val, "P\"" SSID_MB32 "\""),
                "4.7 ...and it survives the LAST hop, into `wpa_cli set_network "
                "<id> ssid P\"...\"`, whose valbuf is WIFI_SSID_LEN_MAX + 8 - a "
                "silent snprintf truncation there would hand wpa a different "
                "network (got %zu chars)",
                strlen(tst_setnet_val));

        reset();
        snprintf(status, sizeof status,
                "bssid=aa:bb:cc:dd:ee:01\nfreq=2412\nssid=%s\nid=0\n"
                "mode=station\nwpa_state=COMPLETED\nip_address=192.168.1.9\n",
                SSID_MB32);
        canned_reply("status", status);
        memset(&st, 0, sizeof st);
        wifi_status_update(&st);
        CHECK(!strcmp(st.ssid, SSID_MB32) && !strcmp(st.wpa_state, "COMPLETED"),
                "4.8 ...and the READBACK agrees. `wpa_cli status` printf-encodes "
                "the SSID too, and the 128-byte line buffer cut it at 122 "
                "characters - which made wifi_connect's \"already on this "
                "network\" compare never match, so provisioning a long SSID tore "
                "a working link down and rebuilt it every time (got %zu chars)",
                strlen(st.ssid));

        reset();
        CHECK(setssid(SSID_33OCT, "12345678", "1", 7) == 1 && g_conn_state == CONN_FAIL &&
                        g_ssid_info_config.name[0] == '\0',
                "4.9 AND THE OCTET BOUND DID NOT MOVE. 33 octets carried as 126 "
                "characters fits every widened buffer, so only prov_ssid_ok's "
                "decoded-octet check can refuse it - and it does, with the same "
                "conn_state 3 the other rejection arms push");

        reset();
        rep(big, sizeof big, 'a', 32);
        CHECK(setssid(big, "12345678", "1", 7) == 0 && !strcmp(g_ssid_info_config.name, big),
                "4.10 ...and a 32-octet ASCII SSID, which is 32 characters and not "
                "128, is unaffected by any of it");
    }

    reset();
    g_conn_state = CONN_OK;
    tst_carrier  = 1;
    CHECK(wifi_get_state(NULL, 0) == 2,
            "5.1 CONN_OK with the kernel carrier UP is 'connected' - the state "
            "source the LCD reads");

    tst_carrier = 0;
    CHECK(wifi_get_state(NULL, 0) == 0,
            "5.2 THE DROP IS SEEN AT READ TIME. CONN_OK latches until re-provision "
            "and the monitor thread has parked, but a carrier that has gone down "
            "reads 0 here anyway, so the LCD can show 'network lost' on a link "
            "that dropped hours after it was provisioned");

    tst_carrier = -1;
    CHECK(wifi_get_state(NULL, 0) == 0,
            "5.3 ...and an absent carrier file is treated as down, not connected");

    g_conn_state = CONN_PEND;
    tst_carrier  = 0;
    CHECK(wifi_get_state(NULL, 0) == 1,
            "5.4 CONN_PEND is still 'connecting' regardless of carrier - the gate "
            "only qualifies CONN_OK");

    g_conn_state = CONN_INIT;
    CHECK(wifi_get_state(NULL, 0) == 0, "5.5 CONN_INIT is 'not configured', unchanged");

    reset();
    tst_wpa_conf = CONF_SAVED;
    wifi_startup();
    CHECK(g_prov_quiet == 1 && g_dev_action == WIFI_ACT_PROV && sem_count() == 1,
            "6.1 a boot with a saved SSID starts the provision thread QUIET: "
            "PROV without SCAN (quiet %d, actions 0x%x, sem %d)",
            g_prov_quiet, g_dev_action, sem_count());

    reset();
    tst_wpa_conf = CONF_HEX;
    wifi_startup();
    CHECK(g_prov_quiet == 1 && g_dev_action == WIFI_ACT_PROV,
            "6.2 ...and so does a saved SSID that wpa_supplicant wrote hex-encoded "
            "(quiet %d, actions 0x%x)",
            g_prov_quiet, g_dev_action);

    reset();
    tst_wpa_conf = CONF_EMPTY;
    wifi_startup();
    CHECK(g_prov_quiet == 0 && g_dev_action == (WIFI_ACT_SCAN | WIFI_ACT_PROV) && sem_count() == 1,
            "6.3 a boot without a saved SSID provisions over BLE as before: SCAN and "
            "PROV (quiet %d, actions 0x%x)",
            g_prov_quiet, g_dev_action);

    reset();
    g_prov_quiet = 1;
    g_dev_action = WIFI_ACT_PROV;
    tst_ble_run  = 0;
    canned_reply("status", ST_DOWN);
    ble_startup();
    CHECK(tst_ble_startup_n == 0 && tst_ble_power == -1 && g_dev_action == 0 &&
                    g_conn_state == CONN_INIT && g_conn_time == 0 &&
                    !strstr(tst_logbuf, "WiFi provisioning started"),
            "6.4 the quiet start neither advertises, scans nor logs a provisioning "
            "start (startup %d, power %d, actions 0x%x)",
            tst_ble_startup_n, tst_ble_power, g_dev_action);

    g_wifi_is_prov = WIFI_PROV_ING;
    wifi_action_set(WIFI_ACT_PROV);
    CHECK(g_prov_quiet == 0 && g_dev_action == (WIFI_ACT_SCAN | WIFI_ACT_PROV) && sem_count() == 0,
            "6.5 the config button during the quiet start queues a real BLE "
            "provisioning on the running thread (quiet %d, actions 0x%x, sem %d)",
            g_prov_quiet, g_dev_action, sem_count());

    ble_startup();
    CHECK(tst_ble_startup_n == 1 && strstr(tst_logbuf, "WiFi provisioning started"),
            "6.6 ...which starts BLE advertising (startup %d)", tst_ble_startup_n);

    reset();
    g_prov_quiet   = 1;
    g_wifi_is_prov = WIFI_PROV_ING;
    snprintf(g_ssid_status.wpa_state, sizeof g_ssid_status.wpa_state, "COMPLETED");
    snprintf(g_ssid_status.ssid, sizeof g_ssid_status.ssid, "lab");
    snprintf(g_ssid_status.ip, sizeof g_ssid_status.ip, "192.168.1.9");
    wifi_prov_connected(1000);
    CHECK(g_conn_state == CONN_OK && g_wifi_is_prov == WIFI_PROV_OK && g_conn_time == 1000 &&
                    tst_save_n == 1,
            "6.7 a quiet-start auto-connect records the connect time (state %d, "
            "conn_time %ld)",
            g_conn_state, (long)g_conn_time);

    CHECK(wifi_prov_finish(1000 + PROV_DELAY_SEC - 1) == 0 && g_wifi_is_prov == WIFI_PROV_OK,
            "6.8 ...and the provision thread keeps running until PROV_DELAY_SEC");

    CHECK(wifi_prov_finish(1000 + PROV_DELAY_SEC) == 1 && g_wifi_is_prov == WIFI_PROV_NONE &&
                    tst_ble_power == -1 && g_prov_quiet == 0 &&
                    !strstr(tst_logbuf, "Bluetooth turned off"),
            "6.9 ...then returns to WIFI_PROV_NONE without touching or logging the "
            "never-started BLE (prov %d, power %d)",
            g_wifi_is_prov, tst_ble_power);

    g_dev_action = 0;
    wifi_action_set(WIFI_ACT_PROV);
    CHECK(g_dev_action == (WIFI_ACT_SCAN | WIFI_ACT_PROV) && sem_count() == 1,
            "6.10 ...after which the config button wakes the thread for BLE "
            "provisioning (actions 0x%x, sem %d)",
            g_dev_action, sem_count());

    reset();
    g_wifi_is_prov = WIFI_PROV_ING;
    g_conn_state   = CONN_PEND;
    g_conn_time    = 1000;
    snprintf(g_ssid_status.wpa_state, sizeof g_ssid_status.wpa_state, "COMPLETED");
    snprintf(g_ssid_status.ip, sizeof g_ssid_status.ip, "192.168.1.9");
    wifi_prov_connected(1000);
    CHECK(wifi_prov_finish(1000 + PROV_DELAY_SEC) == 1 && g_wifi_is_prov == WIFI_PROV_NONE &&
                    tst_ble_power == 0 && strstr(tst_logbuf, "Bluetooth turned off"),
            "6.10a a join made through BLE powers BLE off and logs it at "
            "PROV_DELAY_SEC (prov %d, power %d)",
            g_wifi_is_prov, tst_ble_power);

    reset();
    g_prov_quiet = 1;
    g_conn_time  = 1000;
    g_conn_state = CONN_OK;
    g_dev_action = WIFI_ACT_SCAN | WIFI_ACT_PROV;
    CHECK(wifi_prov_finish(1000 + PROV_DELAY_SEC) == 0,
            "6.11 a pending BLE provisioning request is not cut off by the quiet "
            "start's stop");

    reset();
    g_prov_quiet = 1;
    g_dev_action = WIFI_ACT_PROV;
    canned_reply("status", ST_UP);
    ble_startup();
    CHECK(g_conn_state == CONN_OK && g_conn_time != 0 && tst_ble_startup_n == 0 &&
                    tst_ble_power == -1,
            "6.12 a quiet start on a link that is already up records the connect "
            "time and leaves BLE off (conn_time %ld)",
            (long)g_conn_time);

    reset();
    g_dev_action = WIFI_ACT_SCAN | WIFI_ACT_PROV;
    tst_ble_run  = 0;
    canned_reply("status", ST_DOWN);
    ble_startup();
    CHECK(tst_ble_startup_n == 1 && strstr(tst_logbuf, "WiFi provisioning started") &&
                    g_dev_action == WIFI_ACT_SCAN,
            "6.13 the no-SSID start advertises and logs as before, leaving the scan "
            "queued (startup %d, actions 0x%x)",
            tst_ble_startup_n, g_dev_action);

    reset();
    g_dev_action = WIFI_ACT_SCAN | WIFI_ACT_PROV;
    canned_reply("status", ST_UP);
    ble_startup();
    CHECK(tst_ble_power == 1 && g_conn_state == CONN_OK && g_conn_time == 0 &&
                    wifi_prov_finish(1000000) == 0 && g_wifi_is_prov == WIFI_PROV_OK,
            "6.14 BLE opened by the button on a live link keeps advertising until "
            "provisioning succeeds (power %d, conn_time %ld)",
            tst_ble_power, (long)g_conn_time);

    reset();
    g_wifi_is_prov = WIFI_PROV_ING;
    snprintf(g_ssid_status.wpa_state, sizeof g_ssid_status.wpa_state, "COMPLETED");
    snprintf(g_ssid_status.ip, sizeof g_ssid_status.ip, "192.168.1.9");
    wifi_prov_connected(1000);
    CHECK(g_conn_time == 0 && wifi_prov_finish(1000 + PROV_DELAY_SEC) == 0,
            "6.15 ...and so does BLE opened on a no-SSID boot when the link comes "
            "up without a setssid (conn_time %ld)",
            (long)g_conn_time);

    reset();
    tst_ctrl_absent = 1;
    CHECK(wifi_get_rssi() == 0,
            "6.16 the RSSI poll does not run wpa_cli before wpa_supplicant's "
            "control socket exists");

    if (mkdtemp(tst_seed_dir)) {
        uint64_t t0;

        snprintf(tst_seed_dst, sizeof tst_seed_dst, "%s/wpa_supplicant.conf", tst_seed_dir);
        snprintf(tst_seed_src, sizeof tst_seed_src, "%s/factory.conf", tst_seed_dir);
        put_file(tst_seed_src, CONF_EMPTY);

        reset();
        tst_ctrl_absent = 1;
        wifi_check_service();
        CHECK(tst_seed_n == 1 && tst_supp_n == 1 && tst_seed_at < tst_supp_at &&
                        !strcmp(tst_seed_args, WPA_CONF_PATH " " WPA_CONF_FACTORY),
                "6.17 a wpa_supplicant start first seeds %s from %s "
                "(%d seed(s), %d start(s): \"%s\")",
                WPA_CONF_PATH, WPA_CONF_FACTORY, tst_seed_n, tst_supp_n, tst_seed_args);
        CHECK(file_is(tst_seed_dst, CONF_EMPTY) &&
                        strstr(tst_logbuf, "missing — copied from") != NULL,
                "6.18 ...a missing wpa_supplicant.conf is created from the factory "
                "default and the copy is logged");

        reset();
        tst_ctrl_absent = 1;
        put_file(tst_seed_dst, CONF_SAVED);
        wifi_check_service();
        CHECK(tst_seed_n == 1 && tst_supp_n == 1 && tst_head_n == 1 && tst_head_at < tst_supp_at &&
                        !strcmp(tst_head_args, WPA_CONF_PATH " " WPA_CONF_FACTORY) &&
                        file_is(tst_seed_dst, "update_config=1\n" CONF_SAVED) &&
                        strstr(tst_logbuf, "1 missing header line(s) inserted from") != NULL,
                "6.19 ...an existing wpa_supplicant.conf is not overwritten; the factory "
                "header line it lacks is inserted at the top before the start and logged "
                "(%d fill(s), log \"%s\")",
                tst_head_n, tst_logbuf);

        reset();
        tst_ctrl_absent = 1;
        put_file(tst_seed_dst, "update_config=1\n" CONF_SAVED);
        wifi_check_service();
        CHECK(tst_head_n == 1 && tst_supp_n == 1 &&
                        file_is(tst_seed_dst, "update_config=1\n" CONF_SAVED) &&
                        tst_logbuf[0] == '\0',
                "6.19a ...a wpa_supplicant.conf with every factory header line is left "
                "alone and nothing is logged");

        reset();
        tst_ctrl_absent = 1;
        unlink(tst_seed_dst);
        unlink(tst_seed_src);
        wifi_check_service();
        CHECK(tst_supp_n == 1 && strstr(tst_logbuf, "not copied from") != NULL,
                "6.20 ...without a factory default the failure is logged and "
                "wpa_supplicant is still started");

        reset();
        wifi_check_service();
        CHECK(tst_seed_n == 0 && tst_head_n == 0 && tst_supp_n == 0,
                "6.21 ...and nothing is seeded or filled while wpa_supplicant runs");

        reset();
        tst_ctrl_absent = 1;
        wifi_daemon_repair(wifi_now_ms());
        CHECK(tst_supp_n == 0 && tst_cli_n == 0,
                "6.22 the repair starts no daemon before provisioning has started them");

        reset();
        wifi_check_service();
        t0              = wifi_now_ms();
        tst_ctrl_absent = 1;
        wifi_daemon_repair(t0 + WIFI_DAEMON_RETRY_MS - 1000);
        CHECK(tst_supp_n == 0,
                "6.23 a daemon just started is not checked for %u ms: wpa_supplicant "
                "writes its pid file only after -B returns",
                WIFI_DAEMON_RETRY_MS);
        wifi_daemon_repair(t0 + WIFI_DAEMON_RETRY_MS + 1000);
        CHECK(tst_supp_n == 1 && tst_cli_n == 0 &&
                        strstr(tst_logbuf, "wpa_supplicant not running; restarting") != NULL,
                "6.24 ...after that a wpa_supplicant that stopped is restarted and "
                "logged, its live wpa_cli left alone (log \"%s\")",
                tst_logbuf);
        t0 = wifi_now_ms();
        wifi_daemon_repair(t0 + WIFI_DAEMON_RETRY_MS - 1000);
        CHECK(tst_supp_n == 1, "6.25 ...a restart that did not take waits %u ms again",
                WIFI_DAEMON_RETRY_MS);

        reset();
        wifi_check_service();
        tst_cli_absent = 1;
        wifi_daemon_repair(wifi_now_ms() + WIFI_DAEMON_RETRY_MS + 1000);
        CHECK(tst_supp_n == 0 && tst_cli_n == 1,
                "6.26 a stopped wpa_cli action monitor is restarted on its own");

        unlink(tst_seed_dst);
        unlink(tst_seed_src);
        strncat(tst_seed_dst, ".lock", sizeof tst_seed_dst - strlen(tst_seed_dst) - 1);
        unlink(tst_seed_dst);
        rmdir(tst_seed_dir);
    } else {
        CHECK(0, "6.17 mkdtemp(%s) failed", tst_seed_dir);
    }

    {
        char ssid[WIFI_SSID_LEN_MAX] = "lab";
        int  rssi                    = 0;
        int  n;

        reset();
        snprintf(g_ssid_info.name, sizeof g_ssid_info.name, "lab");
        canned_reply("signal_poll", "RSSI=-55\nLINKSPEED=72\n");
        wifi_rssi_refresh(NULL, 0);
        canned_reply(NULL, NULL);
        n = tst_exec_n;
        CHECK(wifi_update_ssid_info(ssid, &rssi) == 0 && rssi == -55,
                "7.1 wifi_update_ssid_info reports the RSSI cached by "
                "wifi_rssi_refresh (got %d)",
                rssi);
        CHECK(tst_exec_n == n, "7.2 ...and runs no wpa_cli itself (%d call(s))", tst_exec_n - n);

        reset();
        canned_reply("signal_poll", "RSSI=-55\n");
        wifi_rssi_refresh(NULL, 0);
        CHECK(tst_last_timeout > 0 && tst_last_timeout <= 1000,
                "7.2a the RSSI poll on the control loop runs wpa_cli with a deadline "
                "of at most 1 s (got %d ms)",
                tst_last_timeout);
    }

    {
        struct timespec ts = { 0, 1000000 };
        pthread_t       th;

        reset();
        atomic_store(&g_rssi, -55);
        g_wifi_link    = 1;
        g_conn_state   = CONN_OK;
        g_wifi_is_prov = WIFI_PROV_ING;
        canned_reply("status", ST_DOWN);
        if (pthread_create(&th, NULL, wifi_provision, NULL) == 0) {
            for (int i = 0; i < 2000 && atomic_load(&g_rssi) != 0; i++)
                nanosleep(&ts, NULL);
            pthread_cancel(th);
            pthread_join(th, NULL);
        } else {
            fprintf(stderr, "  FAIL: cannot start the provision thread\n");
            fails++;
        }
        CHECK(atomic_load(&g_rssi) == 0 && g_wifi_link == 0 &&
                        strstr(tst_logbuf, "WiFi disconnected from") != NULL,
                "7.3 the provision loop clears the cached RSSI when the link drops "
                "(rssi %d, link %d)",
                atomic_load(&g_rssi), g_wifi_link);
    }

    {
        wifi_status_t st;

        reset();
        canned_reply("status",
                "bssid=aa:bb:cc:dd:ee:01\nfreq=2412\n"
                "ssid=ip_address=6.6.6.6\nid=0\nmode=station\n"
                "wpa_state=COMPLETED\nip_address=192.168.1.9\n");
        wifi_status_update(&st);
        CHECK(!strcmp(st.ssid, "ip_address=6.6.6.6") && !strcmp(st.ip, "192.168.1.9") &&
                        !strcmp(st.wpa_state, "COMPLETED"),
                "8.1 an SSID that reads \"ip_address=...\" stays the SSID and the "
                "real ip_address line still sets the IP (ssid \"%s\", ip \"%s\")",
                st.ssid, st.ip);

        canned_reply("status",
                "wpa_state=COMPLETED\nip_address=192.168.1.9\n"
                "ssid=wpa_state=DISCONNECTED\n");
        wifi_status_update(&st);
        CHECK(!strcmp(st.ssid, "wpa_state=DISCONNECTED") && !strcmp(st.wpa_state, "COMPLETED") &&
                        !strcmp(st.ip, "192.168.1.9"),
                "8.2 an SSID that reads \"wpa_state=...\", listed after the real "
                "wpa_state line, does not overwrite the state (state \"%s\")",
                st.wpa_state);

        canned_reply("status",
                "bssid=11:22:33:44:55:66\nssid=bssid=aa:bb\n"
                "wpa_state=COMPLETED\nip_address=10.0.0.2\n");
        wifi_status_update(&st);
        CHECK(!strcmp(st.ssid, "bssid=aa:bb") && !strcmp(st.ip, "10.0.0.2") &&
                        !strcmp(st.wpa_state, "COMPLETED"),
                "8.3 the bssid line is not read as an ssid line, and an SSID that "
                "reads \"bssid=...\" is kept whole (ssid \"%s\")",
                st.ssid);

        canned_reply("status",
                "ssid=a\\nip_address=6.6.6.6\\nwpa_state=X\n"
                "wpa_state=SCANNING\n");
        wifi_status_update(&st);
        CHECK(!strcmp(st.ssid, "a\\nip_address=6.6.6.6\\nwpa_state=X") && st.ip[0] == '\0' &&
                        !strcmp(st.wpa_state, "SCANNING"),
                "8.4 an SSID holding escaped newlines stays on its one line: no IP "
                "is invented and the state is the real one (ip \"%s\", state \"%s\")",
                st.ip, st.wpa_state);

        canned_reply("status", "ssid=wpa_state=COMPLETED ip_address=6.6.6.6\n");
        wifi_status_update(&st);
        CHECK(!strcmp(st.ssid, "wpa_state=COMPLETED ip_address=6.6.6.6") && st.ip[0] == '\0' &&
                        st.wpa_state[0] == '\0',
                "8.5 with only an ssid line, field names inside the SSID set "
                "neither the state nor the IP (state \"%s\", ip \"%s\")",
                st.wpa_state, st.ip);
    }

    {
        char hs[WIFI_SSID_LEN_MAX * 2], want[BLE_DATA_STATUS_LEN];

        reset();
        snprintf(g_dev_name, sizeof g_dev_name, "Avalon-ab12");
        canned_reply("status",
                "ssid=ip_address=6.6.6.6\nwpa_state=COMPLETED\n"
                "ip_address=192.168.1.9\n");
        wifi_status_update(&g_ssid_status);
        snprintf(g_ssid_info_config.name, sizeof g_ssid_info_config.name, "lab");
        g_ssid_info_config.auth = WIFI_AUTH_WPA2_PSK;
        g_conn_state            = CONN_OK;
        ble_data_status_update();
        CHECK(tst_status_n == 1 &&
                        !strcmp(tst_status,
                                "{\"device\":\"Avalon-ab12\",\"ip\":\"192.168.1.9\","
                                "\"ssid\":\"6C6162\",\"auth\":7,\"conn_state\":2}"),
                "9.1 the BLE status JSON carries exactly device, ip, hex ssid, auth "
                "and conn_state, with the IP from the ip_address line (got %s)",
                tst_status);

        reset();
        snprintf(g_dev_name, sizeof g_dev_name, "Avalon-ab12");
        ble_data_status_update();
        CHECK(!strcmp(tst_status,
                      "{\"device\":\"Avalon-ab12\",\"ip\":\"\","
                      "\"ssid\":\"\",\"auth\":0,\"conn_state\":0}"),
                "9.2 before any status or setssid the JSON keeps every key, with "
                "empty strings and zeros (got %s)",
                tst_status);

        reset();
        snprintf(g_dev_name, sizeof g_dev_name, "Avalon-ab12");
        snprintf(g_ssid_info_config.name, sizeof g_ssid_info_config.name, "%s", SSID_MB32);
        snprintf(g_ssid_status.ip, sizeof g_ssid_status.ip, "255.255.255.255");
        g_ssid_info_config.auth = WIFI_AUTH_WPA2_EAP;
        g_conn_state            = CONN_FAIL;
        hexify(SSID_MB32, hs);
        snprintf(want, sizeof want,
                "{\"device\":\"Avalon-ab12\",\"ip\":\"255.255.255.255\","
                "\"ssid\":\"%s\",\"auth\":8,\"conn_state\":3}",
                hs);
        ble_data_status_update();
        CHECK(strlen(hs) == WIFI_SSID_HEX_CHARS_MAX && !strcmp(tst_status, want),
                "9.3 a 128-character escaped SSID is sent as all %d hex digits with "
                "the longest IP (got %zu bytes, want %zu)",
                WIFI_SSID_HEX_CHARS_MAX, strlen(tst_status), strlen(want));
    }

    {
        static const struct {
            const char *conf; /* wpa_supplicant.conf contents */
            int         want; /* expected wifi_has_ssid() result */
            const char *name; /* case description */
        } cases[] = {
            { "network={\n\tssid=\"lab\"\n\tpsk=\"12345678\"\n}\n", 1,
                    "10.2 a block with a quoted ssid and a psk line has an SSID" },
            { "network={\nssid=\"lab\"\n}\n", 1, "10.3 an unindented quoted ssid line counts" },
            { "network={\n    ssid=E4B8ADE69687\n}\n", 1,
                    "10.4 an upper-case hex ssid indented with spaces counts" },
            { "network={\n\tssid=\"a\\\"b\"\n\tpsk=\"12345678\"\n}\n", 1,
                    "10.5 a quoted ssid holding an escaped quote counts" },
            { "network={\n\tssid=\"\\xe4\\xb8\\xad\"\n}\n", 1,
                    "10.6 a quoted ssid holding \\x escapes counts" },
            { "network={\n\tpsk=\"12345678\"\n\tkey_mgmt=WPA-PSK\n}\n", 0,
                    "10.7 a block with a psk but no ssid line has no SSID" },
            { "network={\n\tscan_ssid=1\n\tbssid=001122334455\n}\n", 0,
                    "10.8 scan_ssid and bssid lines are not ssid lines" },
            { "network={\n\tssid=\"lab\"\n\tdisabled=1\n}\n", 0,
                    "10.9 a disabled block does not count" },
            { "network={\n\tssid=\"old\"\n\tdisabled=1\n}\n"
              "network={\n\tssid=\"lab\"\n\tpsk=\"12345678\"\n}\n",
                    1, "10.10 a disabled block followed by an enabled one counts" },
            { "network={\n\tssid=\"lab\"\n}\n"
              "network={\n\tssid=\"old\"\n\tdisabled=1\n}\n",
                    1, "10.11 an enabled block followed by a disabled one counts" },
            { "network={\n\tssid=\"a\"\n\tdisabled=1\n}\n"
              "network={\n\tssid=\"b\"\n\tdisabled=1\n}\n",
                    0, "10.12 two disabled blocks have no usable SSID" },
            { "network={\n\tdisabled=1\n}\nnetwork={\n\tssid=\"lab\"\n}\n", 1,
                    "10.13 the disabled flag does not leak into the next block" },
            { "network={\n\tssid=\"lab\"\n\tpsk=\"12345678\"\n", 0,
                    "10.14 an unterminated block does not count" },
            { "ctrl_interface=/var/run/wpa_supplicant\n# ssid=\"note\"\n"
              "update_config=1\n",
                    0, "10.15 a comment outside any block does not count" },
            { "network={\n\tssid=\"lab\"\n\tpsk=\"disabled=1x\"\n}\n", 1,
                    "10.16 a psk holding disabled= does not disable the block" },
            { "network={\n\tssid=\"lab\"\n\tpsk=\"1234}5678\"\n\tdisabled=1\n}\n", 0,
                    "10.17 a psk holding } does not close the block before disabled=1" },
            { "network={\n\tssid=\"lab\"\n\tdisabled=0\n}\n", 1,
                    "10.18 disabled=0 leaves the block enabled" },
            { "network={\n\tssid=\"lab\"\n\t#disabled=1\n}\n", 1,
                    "10.19 a commented-out disabled=1 does not disable the block" },
            { "#network={\n#\tssid=\"lab\"\n#}\n", 0,
                    "10.20 a fully commented-out block does not count" },
            { "network={\n\tssid=P\"lab\"\n}\n", 1, "10.21 a P\"...\" printf-escaped ssid counts" },
            { "network={\n\tpsk=\"12345678 ssid=abcd\"\n}\n", 0,
                    "10.22 a psk holding ssid= and hex is not an ssid line" },
            { "network={\n\tssid=\"lab\"\n\tdisabled=1 # off\n}\n", 0,
                    "10.23 disabled=1 followed by a trailing comment disables the block" },
            { "network={\n\tssid=\"lab\"\n\tdisabled=2\n}\n", 0,
                    "10.24 disabled=2 disables the block" },
        };

        reset();
        CHECK(wifi_has_ssid() == 0, "10.1 a missing wpa_supplicant.conf has no SSID");
        for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            reset();
            tst_wpa_conf = cases[i].conf;
            CHECK(wifi_has_ssid() == cases[i].want, "%s", cases[i].name);
        }
    }

    {
        reset();
        tst_lock_held     = 0;
        tst_conn_status_n = 0;
        tst_conn_injected = 0;
        tst_conn_ssid[0] = tst_conn_psk[0] = '\0';
        CHECK(setssid("A", "passwordA", "1", 7) == 0 && (g_dev_action & WIFI_ACT_CONNECT),
                "11.1 setssid A queues a connect");
        tst_exec_hook = hook_connect;
        wifi_connect();
        tst_exec_hook = NULL;
        CHECK(tst_conn_injected && !strcmp(tst_conn_ssid, "P\"A\"") &&
                        !strcmp(tst_conn_psk, "\"passwordA\""),
                "11.2 a setssid B arriving while A is being dialled does not give A "
                "B's password (ssid %s, psk %s)",
                tst_conn_ssid, tst_conn_psk);
        CHECK((g_dev_action & WIFI_ACT_CONNECT) && !strcmp(g_ssid_info_config.name, "B"),
                "11.3 ...and B's connect request stays queued (actions 0x%x, config %s)",
                g_dev_action, g_ssid_info_config.name);
        CHECK(tst_lock_held == 0,
                "11.4 no wpa_cli, shell or BLE call runs with g_wifi_lock held (%d)",
                tst_lock_held);
    }

    {
        char list[BLE_DATA_RD_LEN], want[BLE_DATA_RD_LEN];
        char h1[64], h2[64], h3[64];

        reset();
        canned_reply("scan_results",
                "bssid / frequency / signal level / flags / ssid\n"
                "aa:bb:cc:dd:ee:01\t2412\t-40\t[WPA2-PSK-CCMP][ESS]\tOffice-Guest\n"
                "aa:bb:cc:dd:ee:02\t2437\t-50\t[WPA2-PSK-CCMP][ESS]\tOffice\n"
                "aa:bb:cc:dd:ee:03\t2462\t-60\t[WPA2-PSK-CCMP][ESS]\tOffice\n"
                "aa:bb:cc:dd:ee:04\t2462\t-70\t[ESS]\t@\n");
        hexify("Office-Guest", h1);
        hexify("Office", h2);
        hexify("@", h3);
        snprintf(want, sizeof want, "%s,7,-40,%s,7,-50,%s,1,-70", h1, h2, h3);
        CHECK(wifi_scan() == 4, "12.1 four scan entries are parsed");
        wifi_scan_ssid_results(list, sizeof list, WIFI_SSID_MAX_NUM);
        CHECK(!strcmp(list, want),
                "12.2 the BLE scan list drops only whole-SSID repeats: \"Office\" is "
                "kept beside \"Office-Guest\" and \"@\" (hex 40) beside \"-40\" "
                "(got \"%s\")",
                list);
    }

    {
        reset();
        CHECK(setssid("CorpNet", "eapSecret1", "alice@corp", WIFI_AUTH_WPA2_EAP) == 0 &&
                        strstr(tst_logbuf, "WiFi provisioning: SSID 'CorpNet'") &&
                        !strstr(tst_logbuf, "alice@corp") && !strstr(tst_logbuf, "eapSecret1"),
                "13.1 an enterprise setssid logs the SSID but neither the EAP identity "
                "nor the password");
        tst_addnet_reply = "0\n";
        tst_exec_hook    = hook_addnet;
        wifi_connect();
        tst_exec_hook = NULL;
        CHECK(strstr(tst_logbuf, "WiFi connecting to 'CorpNet'") &&
                        !strstr(tst_logbuf, "alice@corp") && !strstr(tst_logbuf, "eapSecret1"),
                "13.2 ...nor does connecting to it (log \"%s\")", tst_logbuf);
        reset();
        CHECK(setssid("CorpNet", "eapSecret1", "alice\n@corp", WIFI_AUTH_WPA2_EAP) == 1 &&
                        rejected() && !strstr(tst_logbuf, "alice") &&
                        !strstr(tst_logbuf, "eapSecret1"),
                "13.3 a refused enterprise setssid logs neither the identity nor the "
                "password (log \"%s\")",
                tst_logbuf);
    }

    {
        CHECK(prov_field_ok("ab", 4, 0, 63) && prov_field_ok("ab", 5, 0, 63) &&
                        prov_field_ok("", 0, 0, 63),
                "14.1 prov_field_ok takes whole hex pairs, an odd tail dropped, and "
                "an empty field with no minimum");
        CHECK(!prov_field_ok("a", 4, 0, 63) && !prov_field_ok("a\nb", 6, 0, 63) &&
                        !prov_field_ok("a\rb", 6, 0, 63) && !prov_field_ok("abc", 6, 0, 2) &&
                        !prov_field_ok("1234567", 14, 8, 63),
                "14.2 ...and refuses an embedded NUL, CR, LF, and a length outside "
                "its bounds");
        CHECK(!prov_ssid_ok("", 0) && !prov_ssid_ok("\\", 2) && prov_ssid_ok("\\x41", 8),
                "14.3 prov_ssid_ok refuses an SSID that decodes to no octet");
        CHECK(wpa_esc_decoded_len("\\x4") == 1 && wpa_esc_decoded_len("\\\"") == 1 &&
                        wpa_esc_decoded_len("a\"b") == 3,
                "14.4 wpa_esc_decoded_len counts a one-digit \\x and quotes as one "
                "octet each");

        reset();
        CHECK(setssid("lab", "", "", WIFI_AUTH_NONE_OPEN) == 0 && g_conn_state == CONN_PEND &&
                        g_ssid_info_config.passwd[0] == '\0' &&
                        g_ssid_info_config.identity[0] == '\0' && (g_dev_action & WIFI_ACT_CONNECT),
                "14.5 an open network with empty password and identity fields is "
                "dialled");

        reset();
        CHECK(setssid("lab", "1234567", "1", WIFI_AUTH_WPA2_PSK) == 1 && rejected(),
                "14.6 a 7-byte WPA2 passphrase is refused with conn_state 3");
        reset();
        CHECK(setssid("lab", "1234567", "1", WIFI_AUTH_WPA_PSK) == 1 && rejected(),
                "14.7 ...and so is a 7-byte WPA passphrase");
        reset();
        CHECK(setssid("lab", "", "1", WIFI_AUTH_WPA2_PSK) == 1 && rejected(),
                "14.8 ...and an empty one");
        reset();
        rep(big, sizeof big, 'p', 64);
        CHECK(setssid("lab", big, "1", WIFI_AUTH_WPA_PSK) == 1 && rejected(),
                "14.9 ...and a 64-byte one");

        reset();
        CHECK(ble_cmd_process("setssid,,3132333435363738,31,7") == 1 && rejected(),
                "14.10 an empty SSID field is refused with conn_state 3");
        reset();
        CHECK(ble_cmd_process("setssid,6C6162,3132333435363738,31") == 1 && rejected(),
                "14.11 a setssid with four fields is refused with conn_state 3");
        reset();
        CHECK(ble_cmd_process("setssid,6C6162,3132333435363738,31,7,7") == 1 && rejected(),
                "14.12 ...and one with six");
        reset();
        CHECK(ble_cmd_process("setssid,6C6162,3132333435363738,31,7x") == 1 && rejected(),
                "14.13 ...and one whose security field is not a number");
        reset();
        CHECK(ble_cmd_process("setssid,6C6162,3132333435363738,31,") == 1 && rejected(),
                "14.14 ...and one whose security field is empty");
        reset();
        CHECK(ble_cmd_process("setssid,6C6162,31323334353637ZZ,31,7") == 1 && rejected(),
                "14.15 a password field that is not hex is refused");
        reset();
        CHECK(ble_cmd_process("setssid,6C6162,3132333400353637,31,7") == 1 && rejected(),
                "14.16 a password with an embedded NUL is refused");

        reset();
        CHECK(setssid("lab", "pass\nword", "1", WIFI_AUTH_WPA2_PSK) == 1 && rejected(),
                "14.17 a newline in the password is refused");
        reset();
        CHECK(setssid("la\nb", "12345678", "1", WIFI_AUTH_WPA2_PSK) == 1 && rejected(),
                "14.18 ...in the SSID too");
        reset();
        CHECK(setssid("lab", "12345678", "a\rb", WIFI_AUTH_WPA2_EAP) == 1 && rejected(),
                "14.19 ...and a carriage return in the identity");

        reset();
        CHECK(setssid("lab", "pa\"ss\"word", "\"id\"", WIFI_AUTH_WPA2_EAP) == 0 &&
                        !strcmp(g_ssid_info_config.passwd, "pa\"ss\"word") &&
                        !strcmp(g_ssid_info_config.identity, "\"id\""),
                "14.20 quotes in the password and identity are kept verbatim");
        reset();
        canned_reply("set_network", "OK\n");
        CHECK(set_network_param(0, "psk", "pa\"ss", true) == 0 &&
                        !strcmp(tst_setnet_val, "\"pa\"ss\""),
                "14.21 ...and reach wpa_cli as one argument (got %s)", tst_setnet_val);

        reset();
        rep(big, sizeof big, 'i', 128);
        CHECK(setssid("lab", "12345678", big, WIFI_AUTH_WPA2_EAP) == 0 &&
                        strlen(g_ssid_info_config.identity) == 128,
                "14.22 a 128-byte identity is accepted");
        reset();
        rep(big, sizeof big, 'i', 129);
        CHECK(setssid("lab", "12345678", big, WIFI_AUTH_WPA2_EAP) == 1 && rejected(),
                "14.23 a 129-byte identity, a 258-character field, is refused with "
                "conn_state 3");
        reset();
        rep(big, sizeof big, 'p', 129);
        CHECK(setssid("lab", big, "1", WIFI_AUTH_WPA2_PSK) == 1 && rejected(),
                "14.24 ...and so is a 258-character password field");

        reset();
        CHECK(setssid("old", "12345", "1", WIFI_AUTH_NONE_WEP) == 1 && rejected() &&
                        g_ssid_info_config.name[0] == '\0',
                "14.28 a WEP open-system setssid is refused with conn_state 3");
        reset();
        CHECK(setssid("old", "12345", "1", WIFI_AUTH_NONE_WEP_SHARED) == 1 && rejected() &&
                        g_ssid_info_config.name[0] == '\0',
                "14.29 ...and so is a WEP shared-key one");

        reset();
        CHECK(ble_cmd_process(NULL) == 1 && rejected(),
                "14.25 a line that overflowed the BLE write buffer is answered with "
                "conn_state 3");

        reset();
        CHECK(ble_cmd_process("scan") == 0 && (g_dev_action & WIFI_ACT_SCAN) && tst_status_n == 0,
                "14.26 scan queues a scan");
        reset();
        CHECK(ble_cmd_process("scan,1") == 1 && ble_cmd_process("foo,1,2,3,4") == 1 &&
                        ble_cmd_process("") == 1 && g_dev_action == 0 && tst_status_n == 0 &&
                        g_conn_state == CONN_INIT,
                "14.27 an unknown command changes nothing");
    }

    {
        char hs[600], hp[600], hi[600], line[BLE_DATA_WR_LEN * 2];

        rep(big, sizeof big, 'p', WIFI_PASSWD_BYTES_MAX);
        hexify(SSID_MB32, hs);
        hexify(big, hp);
        rep(big, sizeof big, 'i', WIFI_SSID_LEN_MAX - 1);
        hexify(big, hi);
        snprintf(line, sizeof line, "setssid,%s,%s,%s,%d$$$$", hs, hp, hi, WIFI_AUTH_WPA2_EAP);
        CHECK(strlen(line) <= BLE_DATA_WR_LEN - 1,
                "15.1 a setssid with every field at its maximum fits the BLE write "
                "buffer (%zu bytes)",
                strlen(line));
        line[strlen(line) - 4] = '\0';
        reset();
        CHECK(ble_cmd_process(line) == 0 && g_conn_state == CONN_PEND, "15.2 ...and is accepted");
    }

    CHECK(addnet_try("0\n") == 0 && tst_addnet_setnet_n > 0 && !strcmp(tst_addnet_sel, "0"),
            "16.1 add_network reply 0 selects network 0");
    CHECK(addnet_try("12\n") == 0 && !strcmp(tst_addnet_sel, "12"),
            "16.2 ...and reply 12 network 12");
    CHECK(addnet_try("FAIL\n") == -1 && tst_addnet_setnet_n == 0 && tst_addnet_sel[0] == '\0' &&
                    (g_dev_action & WIFI_ACT_CONNECT),
            "16.3 a FAIL reply to add_network stops the attempt before set_network "
            "and queues a retry");
    CHECK(addnet_try("3x\n") == -1 && tst_addnet_setnet_n == 0,
            "16.4 ...and so does a reply with trailing text");
    CHECK(addnet_try("") == -1 && tst_addnet_setnet_n == 0, "16.5 ...and an empty reply");
    CHECK(addnet_try("-1\n") == -1 && tst_addnet_setnet_n == 0, "16.6 ...and a negative id");
    CHECK(addnet_try("99999999999\n") == -1 && tst_addnet_setnet_n == 0,
            "16.7 ...and an id out of int range");

    {
        reset();
        canned_reply("scan_results",
                "bssid / frequency / signal level / flags / ssid\n"
                "aa:bb:cc:dd:ee:01\t2412\t-40\t[WPA2-PSK-CCMP][ESS]\tlab\n");
        CHECK(wifi_scan() == 1 && g_ssid_list_cnt == 1, "17.1 a scan fills the list");
        canned_reply("scan_results",
                "bssid / frequency / signal level / flags / ssid\n"
                "aa:bb:cc:dd:ee:09\t5180\tzz\n");
        CHECK(wifi_scan() == 0 && g_ssid_list_cnt == 1 &&
                        !strcmp(g_ssid_list[0].bssid, "aa:bb:cc:dd:ee:01") &&
                        g_ssid_list[0].freq == 2412 && !strcmp(g_ssid_list[0].ssid, "lab") &&
                        (g_dev_action & WIFI_ACT_SCAN) == 0,
                "17.2 a scan with no valid entry leaves the previous list intact "
                "(bssid %s, freq %d)",
                g_ssid_list[0].bssid, g_ssid_list[0].freq);
    }

    {
        char list[BLE_DATA_RD_LEN], want[BLE_DATA_RD_LEN], h1[64], h2[64];

        reset();
        canned_reply("scan_results",
                "bssid / frequency / signal level / flags / ssid\n"
                "aa:bb:cc:dd:ee:01\t2412\t-40\t[WEP][ESS]\told\n"
                "aa:bb:cc:dd:ee:02\t2437\t-50\t[WPA2-PSK-CCMP][ESS]\tlab\n"
                "aa:bb:cc:dd:ee:03\t2462\t-60\t[ESS]\topen\n");
        hexify("lab", h1);
        hexify("open", h2);
        snprintf(want, sizeof want, "%s,7,-50,%s,1,-60", h1, h2);
        CHECK(wifi_scan() == 3 && g_ssid_list[0].auth == WIFI_AUTH_NONE_WEP,
                "18.1 a WEP network is parsed as WEP");
        wifi_scan_ssid_results(list, sizeof list, WIFI_SSID_MAX_NUM);
        CHECK(!strcmp(list, want), "18.2 ...and left out of the BLE scan list (got \"%s\")", list);
    }

    return tst_done();
}
