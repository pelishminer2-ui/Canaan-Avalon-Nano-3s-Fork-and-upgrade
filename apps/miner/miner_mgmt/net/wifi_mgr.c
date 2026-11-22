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

#define _DEFAULT_SOURCE
#include <ctype.h>
#include <errno.h>
#include <limits.h>
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
#include <stdatomic.h>

#include <arpa/inet.h>

#include "wifi_mgr.h"
#include "ble_prov.h"
#include "mc_paths.h"
#include "net_util.h"
#include "net_log.h"
#include "net_state.h"
#include "net_linux_common.h"
#include "hex.h"
#include "ini_file.h"

#define WIFI_STR_(x) #x
/* stringifies x after macro expansion */
#define WIFI_STR(x) WIFI_STR_(x)
/* scan_results ssid field width, tab plus escaped SSID, chars */
#define WIFI_SCAN_SSID_FIELD_W 129
/* BLE device name buffer size, bytes, including NUL */
#define WIFI_NAME_LEN_MAX 16

/*
 * Scan entries kept before the RSSI sort. scan_results come in BSS order, not
 * signal order, so this must hold the whole neighbourhood or a strong AP
 * listed late is dropped before the sort.
 */
#define SSID_LIST_CNT_MAX 64
#define PROV_FIELDS       5
#define WIFI_ACT_SCAN     (0x1 << 0)
/* WiFi action bit: connect to the provisioned network */
#define WIFI_ACT_CONNECT (0x1 << 1)

/* Provisioning retry backoff while association keeps failing, in seconds. */
#define WIFI_RETRY_BACKOFF_MIN 3
#define WIFI_RETRY_BACKOFF_MAX 30
/* wpa_supplicant control socket of wlan0 */
#define WPA_CTRL_FILE "/var/run/wpa_supplicant/" WIFI_DEV_STA_NAME
/* wpa_supplicant pid file */
#define WPA_SUPP_PID_FILE "/var/run/wpa_supplicant.pid"
/* wpa_cli action monitor pid file */
#define WPA_PID_FILE "/var/run/wpa_cli.pid"
/*
 * Wait after a daemon start before checking it again, ms: wpa_supplicant
 * writes its pid file only after -B has returned
 */
#define WIFI_DAEMON_RETRY_MS 10000u
#define UDHCPC_PID_FILE      NET_DHCP_PIDF(WIFI_DEV_STA_NAME)

/*
 * -T 1 -A 2: DISCOVER every second with a 2 s pause after a failed cycle, so
 * the lease arrives about a second after association. The busybox defaults
 * (-T 3 -A 20) stall DHCP for ~30 s when the first DISCOVER precedes
 * association.
 */
#define UDHCPC_CMD \
    "udhcpc -i " WIFI_DEV_STA_NAME " -b -T 1 -A 2 -s " NET_UDHCPC_SCRIPT " -p " UDHCPC_PID_FILE " &"
/* wpa_cli reply buffer size, bytes */
#define BUFF_SIZE    1024
#define CONN_TIMEOUT 30 /* connect attempt timeout, s */
/* networks in the BLE scan list, max */
#define WIFI_SSID_MAX_NUM     10
#define WIFI_PASSWD_BYTES_MIN 8
#define WIFI_PASSWD_BYTES_MAX 63                  /* WPA passphrase maximum, bytes */
#define PROV_DELAY_SEC        (60 + CONN_TIMEOUT) /* provisioning window from connect start, s */
#define WIFI_RSSI_TIMEOUT_MS  1000

_Static_assert(WIFI_SSID_ESC_CHARS_MAX == WIFI_SSID_OCTETS_MAX * 4,
        "wpa_supplicant printf_encode expands every byte outside 32..126 to four "
        "characters, so a 32-octet SSID - the IEEE maximum - is 128 characters");
_Static_assert(WIFI_SSID_LEN_MAX == WIFI_SSID_ESC_CHARS_MAX + 1,
        "every SSID buffer holds the escaped form plus its NUL: the raw octets are "
        "never stored, wpa's P\"...\" form decodes them on the far side");
_Static_assert(WIFI_SSID_HEX_CHARS_MAX == WIFI_SSID_ESC_CHARS_MAX * 2,
        "the BLE scan list and the BLE provisioning line carry the escaped form "
        "ASCII-hex encoded, two hex characters per escaped character");
_Static_assert(WIFI_SCAN_SSID_FIELD_W == WIFI_SSID_ESC_CHARS_MAX + 1,
        "the scan_results ssid field still carries the tab that separates it from "
        "flags, which the copy into the scan list drops by starting at [1]");
_Static_assert(WIFI_SCAN_SSID_FIELD_W + 1 >= WIFI_SSID_LEN_MAX + 1,
        "that copy reads sizeof(ssid_item_t.ssid) - 1 bytes starting at ssid_str[1], "
        "so ssid_str must be one byte longer than the scan list entry");
_Static_assert(WIFI_SSID_LEN_MAX * 2 >= WIFI_SSID_HEX_CHARS_MAX + 1,
        "the hex staging buffers must hold a full hex-encoded SSID and its NUL");

enum conn_state {
    CONN_INIT, /* not connected */
    CONN_PEND = 1,
    CONN_OK,
    CONN_FAIL,
};

enum {
    WIFI_PROV_NONE,
    WIFI_PROV_ING,
    WIFI_PROV_OK, /* connected; provisioning not yet finished */
};

enum {
    WIFI_AUTH_UNKNOWN   = 0,
    WIFI_AUTH_NONE_OPEN = 1,   /* open network, no encryption */
    WIFI_AUTH_NONE_WEP,        /* WEP, open system */
    WIFI_AUTH_NONE_WEP_SHARED, /* WEP, shared key */
    WIFI_AUTH_IEEE8021X,
    WIFI_AUTH_WPA_PSK  = 5, /* WPA personal */
    WIFI_AUTH_WPA_EAP  = 6, /* WPA enterprise */
    WIFI_AUTH_WPA2_PSK = 7, /* WPA2 personal */
    WIFI_AUTH_WPA2_EAP = 8  /* WPA2 enterprise */
};

/* One scan_results entry */
typedef struct {
    char  bssid[32];                /* BSSID, text */
    int   freq;                     /* frequency, MHz */
    int   signal;                   /* signal level, dBm */
    char  flags[WIFI_SSID_LEN_MAX]; /* flags field, e.g. [WPA2-PSK-CCMP][ESS] */
    char  ssid[WIFI_SSID_LEN_MAX];  /* SSID, wpa_supplicant escaped form */
    short auth;                     /* security type, WIFI_AUTH_* */
} ssid_item_t;

/* wpa_cli status fields */
typedef struct {
    char ssid[WIFI_SSID_LEN_MAX]; /* SSID, wpa_supplicant escaped form */
    char wpa_state[32];           /* wpa_supplicant state name */
    char ip[16];                  /* IPv4 address, dotted decimal */
} wifi_status_t;

static int           g_ssid_list_cnt;
static ssid_item_t   g_ssid_list[SSID_LIST_CNT_MAX]; /* scan results, in scan order */
static wifi_status_t g_ssid_status;                  /* last wlan0 status */
/* network wlan0 is connected to */
static wifi_ssid_t g_ssid_info;
/* credentials received from the phone */
static wifi_ssid_t g_ssid_info_config;
/* connect start, CLOCK_MONOTONIC s, 0 if unset */
static time_t g_conn_time;
/* connection state, enum conn_state */
static uint8_t g_conn_state;
/* 1 while wlan0 is up with an IP address */
static int g_wifi_link;
/* provisioning state, WIFI_PROV_* */
static int g_wifi_is_prov;
/* 1 while provisioning runs without BLE */
static int g_prov_quiet;
/* provision thread wake-up semaphore */
static sem_t g_prov_sem;
/* pending WIFI_ACT_* bits */
static int  g_dev_action;
static char g_dev_name[WIFI_NAME_LEN_MAX];
/* cached wlan0 RSSI, dBm, 0 if unknown */
static atomic_int g_rssi;
/* g_rssi generation, bumped on link loss */
static atomic_uint g_rssi_gen;
/* guards g_rssi against g_rssi_gen */
static pthread_mutex_t g_rssi_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_wifi_lock = PTHREAD_MUTEX_INITIALIZER;
/* next reconnect retry time, CLOCK_MONOTONIC s, 0 if none */
static time_t g_wifi_retry_next;
/* reconnect retry interval, s, 0 if none */
static int g_wifi_retry_backoff;
/* g_ssid_list indexes by descending signal */
static int g_rssi_idx[SSID_LIST_CNT_MAX];
/* 1 while saving WiFi settings is blocked */
static atomic_int g_wifi_cfg_frozen;
/* Serializes wifi_check_service() */
static pthread_mutex_t g_svc_lock = PTHREAD_MUTEX_INITIALIZER;
/* CLOCK_MONOTONIC ms of the last wifi_check_service(), 0 before the first */
static _Atomic uint64_t g_svc_at;

static int      wifi_iface_present(void);
static void     wifi_check_service(void);
static int      wifi_supp_alive(void);
static uint64_t wifi_now_ms(void);
static int      wifi_cli_alive(void);
static void     wifi_status_update(wifi_status_t *pstatus);

static void ssid_copy(char *dst, const char *src)
{
    memset(dst, 0, WIFI_SSID_LEN_MAX - 1);
    memcpy(dst, src, strnlen(src, WIFI_SSID_LEN_MAX - 1));
}

static void selection_sort_idx(const ssid_item_t list[], int idx[], int len)
{
    int i, j, max_idx, temp;
    for (i = 0; i < len - 1; i++) {
        max_idx = i;
        for (j = i + 1; j < len; j++)
            if (list[idx[j]].signal > list[idx[max_idx]].signal) {
                max_idx = j;
            }

        if (max_idx != i) {
            temp         = idx[max_idx];
            idx[max_idx] = idx[i];
            idx[i]       = temp;
        }
    }
}

static int parse_ssid_flags(const char *ssid, int *auth)
{
    int i;

    for (i = 0; i < g_ssid_list_cnt; i++) {
        if (strcmp(g_ssid_list[i].ssid, ssid) == 0) {
            *auth = g_ssid_list[i].auth;
            return 0;
        }
    }
    return -1;
}

static void run_argv(const char *const argv[])
{
    char out[BUFF_SIZE];
    exec_argv(argv, out, sizeof(out));
}

static void wifi_udhcpc_stop(void)
{
    net_linux_udhcpc_stop(UDHCPC_PID_FILE);
}

static int set_wpa_ok_argv(const char *const argv[])
{
    char reply[8];
    if (!exec_argv(argv, reply, sizeof(reply))) {
        if (strncmp(reply, "OK", 2) == 0)
            return 0;
    }

    return -1;
}

static int wpa_set_network(int id, const char *field, const char *fmt, const char *value)
{
    char        idbuf[16];
    char        valbuf[WIFI_SSID_LEN_MAX + 8];
    const char *argv[] = { "wpa_cli", "-i", WIFI_DEV_STA_NAME, "set_network", idbuf, field, valbuf,
        NULL };
    int         ret;

    snprintf(idbuf, sizeof(idbuf), "%d", id);
    snprintf(valbuf, sizeof(valbuf), fmt, value);
    ret = set_wpa_ok_argv(argv);
    explicit_bzero(valbuf, sizeof(valbuf));
    return ret;
}

static int set_network_param(int id, const char *field, const char *value, bool quote)
{
    return wpa_set_network(id, field, quote ? "\"%s\"" : "%s", value);
}

static int str_to_nonneg(const char *s)
{
    char *end;
    long  v;

    if (!isdigit((unsigned char)*s))
        return -1;
    errno = 0;
    v     = strtol(s, &end, 10);
    while (isspace((unsigned char)*end))
        end++;
    if (errno || *end || v > INT_MAX)
        return -1;
    return (int)v;
}

static int wifi_net_auth_rejected(void)
{
    const char *argv[]         = { "wpa_cli", "-i", WIFI_DEV_STA_NAME, "list_networks", NULL };
    char        out[BUFF_SIZE] = { 0 };

    if (exec_argv(argv, out, sizeof(out)) != 0)
        return 0;
    return strstr(out, "TEMP-DISABLED") != NULL;
}

static int wifi_get_rssi()
{
    const char *argv[]         = { "wpa_cli", "-i", WIFI_DEV_STA_NAME, "signal_poll", NULL };
    char        out[BUFF_SIZE] = { 0 };
    const char *p;

    if (!wifi_iface_present() || access(WPA_CTRL_FILE, F_OK) != 0)
        return 0;

    if (exec_argv_timeout(argv, out, sizeof(out), WIFI_RSSI_TIMEOUT_MS) != 0)
        return 0;
    p = strstr(out, "RSSI=");
    if (p == NULL)
        return 0;
    return atoi(p + 5);
}

static int wifi_connect()
{
    char            idbuf[16] = { 0 };
    char            buf[256]  = { 0 };
    int             id = -1, ret = 0;
    int             auth     = 0;
    const char     *key_mgmt = NULL;
    wifi_ssid_t     cfg;
    wifi_ssid_t    *pssid = &cfg;
    wifi_status_t   live;
    struct timespec ts;
    const char     *remove0_argv[] = { "wpa_cli", "-i", WIFI_DEV_STA_NAME, "remove_network", "0",
            NULL };
    const char     *add_argv[]     = { "wpa_cli", "-i", WIFI_DEV_STA_NAME, "add_network", NULL };
    const char     *select_argv[]  = { "wpa_cli", "-i", WIFI_DEV_STA_NAME, "select_network", idbuf,
             NULL };

    pthread_mutex_lock(&g_wifi_lock);
    cfg = g_ssid_info_config;
    g_dev_action &= ~WIFI_ACT_CONNECT;
    pthread_mutex_unlock(&g_wifi_lock);

    /*
     * Already on the requested SSID: do not rebuild the link. That takes
     * ~8 s and dips conn_state 2->1->2, and the phone app gives up before
     * it settles.
     */
    wifi_status_update(&live);
    if (strcmp(live.wpa_state, "COMPLETED") == 0 && live.ip[0] &&
            strcmp(live.ssid, pssid->name) == 0) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        pthread_mutex_lock(&g_wifi_lock);
        g_ssid_status = live;
        g_conn_time   = ts.tv_sec;
        /*
         * CONN_PEND, not CONN_OK, so the provision monitor runs the
         * normal first-connected path (save_config, PROV_OK, status
         * push) on its next tick. The app rejects conn_state:2 on its
         * first poll after setssid as stale; it must see conn_state:1
         * with an empty ip first, so blank the ip for this push.
         */
        g_conn_state        = CONN_PEND;
        g_ssid_status.ip[0] = '\0';
        pthread_mutex_unlock(&g_wifi_lock);
        applog(LOG_INFO, "WiFi already connected to '%s'; keeping the connection", pssid->name);
        explicit_bzero(&cfg, sizeof(cfg));
        return 0;
    }
    pthread_mutex_lock(&g_wifi_lock);
    g_ssid_status = live;
    pthread_mutex_unlock(&g_wifi_lock);

    auth = pssid->auth;
    if (auth == 0) {
        if (parse_ssid_flags(pssid->name, &auth) < 0) {
            pthread_mutex_lock(&g_wifi_lock);
            g_dev_action |= WIFI_ACT_SCAN | WIFI_ACT_CONNECT;
            pthread_mutex_unlock(&g_wifi_lock);
            applog(LOG_ERR, "WiFi: security type of network '%s' unknown; rescanning", pssid->name);
            explicit_bzero(&cfg, sizeof(cfg));
            return -1;
        }
    }
    pssid->auth = auth;
    pthread_mutex_lock(&g_wifi_lock);
    if (strcmp(g_ssid_info_config.name, pssid->name) == 0)
        g_ssid_info_config.auth = auth;
    pthread_mutex_unlock(&g_wifi_lock);

    set_wpa_ok_argv(remove0_argv);
    ret = exec_argv(add_argv, buf, sizeof(buf));
    id  = ret ? -1 : str_to_nonneg(buf);
    if (id < 0) {
        pthread_mutex_lock(&g_wifi_lock);
        g_dev_action |= WIFI_ACT_CONNECT;
        pthread_mutex_unlock(&g_wifi_lock);
        applog(LOG_ERR, "WiFi: wpa_supplicant refused a new network entry (id %d)", id);
        explicit_bzero(&cfg, sizeof(cfg));
        return -1;
    }

    switch (auth) {
    case WIFI_AUTH_NONE_OPEN:
    case WIFI_AUTH_NONE_WEP:
    case WIFI_AUTH_NONE_WEP_SHARED:
        key_mgmt = "NONE";
        break;
    case WIFI_AUTH_IEEE8021X:
        key_mgmt = "IEEE8021X";
        break;
    case WIFI_AUTH_WPA_PSK:
    case WIFI_AUTH_WPA2_PSK:
        key_mgmt = "WPA-PSK";
        break;
    case WIFI_AUTH_WPA_EAP:
    case WIFI_AUTH_WPA2_EAP:
        key_mgmt = "WPA-EAP";
        break;
    }

    ret |= wpa_set_network(id, "ssid", "P\"%s\"", pssid->name);
    if (key_mgmt)
        ret |= set_network_param(id, "key_mgmt", key_mgmt, false);

    if (auth == WIFI_AUTH_WPA_PSK || auth == WIFI_AUTH_WPA2_PSK) {
        ret |= set_network_param(id, "psk", pssid->passwd, true);
    } else if (auth == WIFI_AUTH_WPA_EAP || auth == WIFI_AUTH_WPA2_EAP) {
        ret |= set_network_param(id, "eap", "PEAP", false);
        ret |= set_network_param(id, "identity", pssid->identity, true);
        ret |= set_network_param(id, "password", pssid->passwd, true);
    }

    snprintf(idbuf, sizeof(idbuf), "%d", id);
    ret |= set_wpa_ok_argv(select_argv);
    if (ret) {
        applog(LOG_WARNING,
                "WiFi: wpa_supplicant rejected part of the settings "
                "for network %d; connecting anyway",
                id);
    }

    clock_gettime(CLOCK_MONOTONIC, &ts);
    pthread_mutex_lock(&g_wifi_lock);
    g_conn_time  = ts.tv_sec;
    g_conn_state = CONN_PEND;
    pthread_mutex_unlock(&g_wifi_lock);

    /*
     * Wait (up to ~6 s) for COMPLETED before starting udhcpc. Started
     * before association, its first DISCOVERs go unanswered and its retry
     * pause delays the lease past the phone app's provisioning deadline.
     * The BLE setssid handler already pushed conn_state:1, so blocking
     * here does not delay the app's first poll.
     */
    {
        wifi_status_t st;
        int           i;
        for (i = 0; i < 30; i++) {
            memset(&st, 0, sizeof st);
            wifi_status_update(&st);
            if (strcmp(st.wpa_state, "COMPLETED") == 0)
                break;
            usleep(200 * 1000);
        }
        if (i >= 30)
            applog(LOG_WARNING,
                    "WiFi not associated after %d ms (state %s); "
                    "requesting an IP address anyway, giving up after %d s",
                    i * 200, st.wpa_state, CONN_TIMEOUT);
        else
            applog(LOG_DEBUG, "WiFi associated after %d ms (state %s)", i * 200, st.wpa_state);
    }

    wifi_udhcpc_stop();
    system(UDHCPC_CMD);
    applog(LOG_INFO, "WiFi connecting to '%s' (security %d, network %d)", pssid->name, pssid->auth,
            id);
    explicit_bzero(&cfg, sizeof(cfg));

    return 0;
}

static void parse_wifi_flags(char *flags, int *auth)
{
    if (strstr(flags, "[WPA2-EAP"))
        *auth = WIFI_AUTH_WPA2_EAP;
    else if (strstr(flags, "[WPA-EAP"))
        *auth = WIFI_AUTH_WPA_EAP;
    else if (strstr(flags, "[WPA2-PSK"))
        *auth = WIFI_AUTH_WPA2_PSK;
    else if (strstr(flags, "[WPA-PSK"))
        *auth = WIFI_AUTH_WPA_PSK;
    else
        *auth = WIFI_AUTH_NONE_OPEN;

    if (!strstr(flags, "-CCMP") && !strstr(flags, "-TKIP") && strstr(flags, "WEP") &&
            *auth == WIFI_AUTH_NONE_OPEN)
        *auth = WIFI_AUTH_NONE_WEP;
}

static int wifi_scan()
{
    char        ssid_str[WIFI_SCAN_SSID_FIELD_W + 1] = { 0 };
    ssid_item_t pitem[SSID_LIST_CNT_MAX];
    int         idx[SSID_LIST_CNT_MAX];
    int         i = 0, m = 0;
    int         cnt = 0, ret = 0, len = 0;
    char        result[BUFF_SIZE * 10] = { 0 }; /* a dense-area scan, untruncated */
    int         auth                   = 0;
    const char *scan_argv[]            = { "wpa_cli", "-i", WIFI_DEV_STA_NAME, "scan", NULL };
    const char *results_argv[] = { "wpa_cli", "-i", WIFI_DEV_STA_NAME, "scan_results", NULL };

    memset(pitem, 0, sizeof(pitem));
    run_argv(scan_argv);
    exec_argv(results_argv, result, sizeof(result));

    len = strlen(result);
    for (i = 0; i < len; i++) {
        if (result[i] == '\n') {
            if (m == 0) {
                m = i + 1;
                continue;
            }

            result[i] = 0;
            ret       = sscanf(&result[m],
                          "%31s\t%d\t%d\t%127s%" WIFI_STR(WIFI_SCAN_SSID_FIELD_W) "[^\n]s",
                          pitem[cnt].bssid, &pitem[cnt].freq, &pitem[cnt].signal, pitem[cnt].flags,
                          ssid_str);
            /* ssid_str is the separating tab plus the SSID */
            if ((ret == 5) && (strlen(ssid_str) != 1)) {
                memcpy(pitem[cnt].ssid, &ssid_str[1], sizeof(pitem[cnt].ssid) - 1);
                pitem[cnt].ssid[sizeof(pitem[cnt].ssid) - 1] = 0;
                parse_wifi_flags(pitem[cnt].flags, &auth);
                pitem[cnt].auth = auth;
                cnt++;
                if (cnt >= SSID_LIST_CNT_MAX)
                    break;
            }
            m = i + 1;
        }
    }

    if (cnt) {
        for (i = 0; i < cnt; i++)
            idx[i] = i;
        selection_sort_idx(pitem, idx, cnt);
        memcpy(g_ssid_list, pitem, sizeof(pitem[0]) * cnt);
        memcpy(g_rssi_idx, idx, sizeof(idx[0]) * cnt);
        g_ssid_list_cnt = cnt;

        pthread_mutex_lock(&g_wifi_lock);
        g_dev_action &= ~(WIFI_ACT_SCAN);
        pthread_mutex_unlock(&g_wifi_lock);
    }

    return cnt;
}

static void wifi_scan_ssid_results(char *buffer, int buffer_size, int count)
{
    ssid_item_t *pitem;
    int          i, j, k;
    int          sent[WIFI_SSID_MAX_NUM];
    char         tmp_ssid[WIFI_SSID_LEN_MAX * 2], tmp_buf[32];

    buffer[0] = '\0';
    if (count > WIFI_SSID_MAX_NUM)
        count = WIFI_SSID_MAX_NUM;
    for (i = 0, j = 0; (i < g_ssid_list_cnt) && (j < count); i++) {
        pitem = &g_ssid_list[g_rssi_idx[i]];
        if (pitem->signal >= 0) {
            continue;
        }
        if (pitem->auth == WIFI_AUTH_NONE_WEP || pitem->auth == WIFI_AUTH_NONE_WEP_SHARED)
            continue;

        if (str_to_hex(pitem->ssid, strlen(pitem->ssid), tmp_ssid, sizeof(tmp_ssid)))
            continue;
        for (k = 0; k < j; k++)
            if (strcmp(g_ssid_list[sent[k]].ssid, pitem->ssid) == 0)
                break;
        if (k < j)
            continue;

        if ((int)(strlen(buffer) + strlen(tmp_ssid) + 10) >= buffer_size)
            break;

        strcat(buffer, tmp_ssid);
        snprintf(tmp_buf, sizeof tmp_buf, ",%d,%d,", pitem->auth, pitem->signal);
        strcat(buffer, tmp_buf);
        sent[j++] = g_rssi_idx[i];
    }
    if (j > 0)
        buffer[strlen(buffer) - 1] = '\0';

    applog(LOG_INFO, "WiFi scan: %d network(s) found [%s]", j, buffer);
}

static void wifi_status_update(wifi_status_t *pstatus)
{
    char        result[BUFF_SIZE] = { 0 };
    char       *line, *next;
    const char *status_argv[] = { "wpa_cli", "-i", WIFI_DEV_STA_NAME, "status", NULL };

    exec_argv(status_argv, result, sizeof(result));

    memset(pstatus, 0, sizeof(wifi_status_t));
    for (line = result; *line; line = next) {
        next = strchr(line, '\n');
        if (next)
            *next++ = '\0';
        else
            next = line + strlen(line);

        if (strncmp(line, "wpa_state=", 10) == 0) {
            snprintf(pstatus->wpa_state, sizeof(pstatus->wpa_state), "%s", line + 10);
        } else if (strncmp(line, "ip_address=", 11) == 0) {
            snprintf(pstatus->ip, sizeof(pstatus->ip), "%s", line + 11);
        } else if (strncmp(line, "ssid=", 5) == 0) {
            snprintf(pstatus->ssid, sizeof(pstatus->ssid), "%s", line + 5);
        }
    }
}

static void ble_data_status_update(void)
{
    char tmpbuf[WIFI_SSID_LEN_MAX * 2];
    char buffer[BLE_DATA_STATUS_LEN];

    pthread_mutex_lock(&g_wifi_lock);
    if (str_to_hex(g_ssid_info_config.name, strlen(g_ssid_info_config.name), tmpbuf,
                sizeof(tmpbuf)))
        tmpbuf[0] = '\0';
    /* No other fields: the phone app expects exactly this set. */
    snprintf(buffer, sizeof(buffer),
            "{\"device\":\"%s\",\"ip\":\"%s\",\"ssid\":\"%s\",\"auth\":%u,\"conn_state\":%u}",
            g_dev_name, g_ssid_status.ip, tmpbuf, g_ssid_info_config.auth, g_conn_state);
    pthread_mutex_unlock(&g_wifi_lock);
    ble_set_data_status(buffer);
    applog(LOG_DEBUG, "WiFi provisioning status sent to phone: %s", buffer);
}

static void ble_data_update(void)
{
    char buffer[BLE_DATA_RD_LEN] = { '\0' };
    wifi_scan_ssid_results(buffer, sizeof(buffer), WIFI_SSID_MAX_NUM);
    ble_set_data(buffer);
}

static int wpa_esc_decoded_len(const char *s)
{
    const char *p = s;
    int         n = 0;

    while (*p) {
        if (*p != '\\') {
            p++;
            n++;
            continue;
        }
        p++;
        switch (*p) {
        case '\\':
        case '"':
        case 'n':
        case 'r':
        case 't':
        case 'e':
            p++;
            n++;
            break;
        case 'x':
            p++;
            if (hex_nibble((unsigned char)*p) >= 0) {
                p++;
                if (hex_nibble((unsigned char)*p) >= 0)
                    p++;
                n++;
            }
            break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
            p++;
            if (*p >= '0' && *p <= '7')
                p++;
            if (*p >= '0' && *p <= '7')
                p++;
            n++;
            break;
        default:
            break;
        }
    }

    return n;
}

static int prov_field_ok(const char *s, int hexlen, int min_bytes, int max_bytes)
{
    int n = (hexlen & ~1) / 2;

    if (n < min_bytes || n > max_bytes)
        return 0;
    if ((int)strlen(s) != n)
        return 0;
    if (strpbrk(s, "\r\n"))
        return 0;
    return 1;
}

static int prov_ssid_ok(const char *s, int hexlen)
{
    int octets;

    if (!prov_field_ok(s, hexlen, 1, WIFI_SSID_LEN_MAX - 1))
        return 0;

    octets = wpa_esc_decoded_len(s);
    return (octets >= 1) && (octets <= WIFI_SSID_OCTETS_MAX);
}

static void prov_reject(void)
{
    pthread_mutex_lock(&g_wifi_lock);
    g_conn_state = CONN_FAIL;
    pthread_mutex_unlock(&g_wifi_lock);
    ble_data_status_update();
}

static int prov_split(const char *cfg, char *field[], const size_t cap[])
{
    const char *p = cfg;
    size_t      len;
    int         n = 0, toolong = 0;

    for (;;) {
        len = strcspn(p, ",");
        if (n < PROV_FIELDS) {
            if (len >= cap[n]) {
                toolong = 1;
            } else {
                memcpy(field[n], p, len);
                field[n][len] = '\0';
            }
        }
        n++;
        if (p[len] == '\0')
            break;
        p += len + 1;
    }
    return toolong ? -1 : n;
}

static int ble_cmd_process(char *cfg)
{
    int          n, unknown_cmd = 0, auth = 0, pw_min;
    char         cmd[16]                         = { 0 };
    char         ssid[WIFI_SSID_LEN_MAX * 2]     = { 0 };
    char         passwd[WIFI_SSID_LEN_MAX * 2]   = { 0 };
    char         identity[WIFI_SSID_LEN_MAX * 2] = { 0 };
    char         authbuf[12]                     = { 0 };
    char        *field[PROV_FIELDS]              = { cmd, ssid, passwd, identity, authbuf };
    const size_t cap[PROV_FIELDS]                = { sizeof(cmd), WIFI_SSID_HEX_CHARS_MAX + 1,
                       WIFI_SSID_HEX_CHARS_MAX + 1, WIFI_SSID_HEX_CHARS_MAX + 1, sizeof(authbuf) };

    if (cfg == NULL) {
        applog(LOG_WARNING, "WiFi provisioning rejected: line from phone longer than %d bytes",
                BLE_DATA_WR_LEN - 1);
        prov_reject();
        return 1;
    }

    n = prov_split(cfg, field, cap);
    /* The setssid payload carries the passphrase: log the command name only. */
    applog(LOG_INFO, "WiFi provisioning: phone sent '%s'", cmd[0] ? cmd : "(unreadable)");
    if (!strcmp(cmd, "scan") && n == 1) {
        wifi_action_set(WIFI_ACT_SCAN);
    } else if (!strcmp(cmd, "setssid")) {
        struct timespec ts;
        wifi_ssid_t     cfgssid;

        memset(&cfgssid, 0, sizeof(cfgssid));
        auth         = n == PROV_FIELDS ? str_to_nonneg(authbuf) : -1;
        cfgssid.auth = auth;
        if (auth < 0 || auth == WIFI_AUTH_NONE_WEP || auth == WIFI_AUTH_NONE_WEP_SHARED ||
                hex_to_str(ssid, strlen(ssid), cfgssid.name, sizeof(cfgssid.name)) ||
                hex_to_str(passwd, strlen(passwd), cfgssid.passwd, sizeof(cfgssid.passwd)) ||
                hex_to_str(identity, strlen(identity), cfgssid.identity,
                        sizeof(cfgssid.identity))) {
            applog(LOG_WARNING,
                    "WiFi provisioning rejected: malformed or oversized "
                    "field from phone");
            prov_reject();
            explicit_bzero(&cfgssid, sizeof(cfgssid));
            explicit_bzero(passwd, sizeof(passwd));
            explicit_bzero(identity, sizeof(identity));
            return 1;
        }
        pw_min = (auth == WIFI_AUTH_WPA_PSK || auth == WIFI_AUTH_WPA2_PSK) ? WIFI_PASSWD_BYTES_MIN :
                                                                             0;
        if (!prov_ssid_ok(cfgssid.name, (int)strlen(ssid)) ||
                !prov_field_ok(cfgssid.passwd, (int)strlen(passwd), pw_min,
                        WIFI_PASSWD_BYTES_MAX) ||
                !prov_field_ok(cfgssid.identity, (int)strlen(identity), 0,
                        (int)sizeof(cfgssid.identity) - 1)) {
            applog(LOG_WARNING,
                    "WiFi provisioning rejected: SSID %zu chars/%d bytes, "
                    "identity %zu bytes, security %d",
                    strlen(cfgssid.name), wpa_esc_decoded_len(cfgssid.name),
                    strlen(cfgssid.identity), auth);
            prov_reject();
            explicit_bzero(&cfgssid, sizeof(cfgssid));
            explicit_bzero(passwd, sizeof(passwd));
            explicit_bzero(identity, sizeof(identity));
            return 1;
        }
        applog(LOG_INFO, "WiFi provisioning: SSID '%s', security %d", cfgssid.name, auth);
        clock_gettime(CLOCK_MONOTONIC, &ts);
        pthread_mutex_lock(&g_wifi_lock);
        memcpy(&g_ssid_info_config, &cfgssid, sizeof(g_ssid_info_config));
        /*
             * Report conn_state:1 now, from the BLE thread: the phone app
             * polls status every ~3.3 s starting right after this write and
             * gives up unless it sees 0->1->2 within its deadline.
             */
        g_conn_state        = CONN_PEND;
        g_conn_time         = ts.tv_sec;
        g_ssid_status.ip[0] = '\0';
        pthread_mutex_unlock(&g_wifi_lock);
        explicit_bzero(&cfgssid, sizeof(cfgssid));
        ble_data_status_update();
        wifi_action_set(WIFI_ACT_CONNECT);
    } else {
        unknown_cmd = 1;
    }

    explicit_bzero(passwd, sizeof(passwd));
    explicit_bzero(identity, sizeof(identity));
    return unknown_cmd;
}

static void ble_startup()
{
    wifi_status_t   live;
    struct timespec ts;
    int             quiet;

    pthread_mutex_lock(&g_wifi_lock);
    quiet = g_prov_quiet;
    g_dev_action &= ~(WIFI_ACT_PROV);
    pthread_mutex_unlock(&g_wifi_lock);

    if (!quiet)
        applog(LOG_OP, "WiFi provisioning started (Bluetooth name %s)", g_dev_name);

    /*
     * Start wpa_supplicant, the wpa_cli action monitor and udhcpc before
     * setssid arrives: wpa_supplicant reconnects a saved network on its
     * own, and warm daemons get the lease ~1 s after association.
     */
    wifi_check_service();

    /*
     * After a restart the link may still be up. Sample it before resetting
     * state and seed CONN_OK, so the LCD never drops to "configure wifi".
     * This must run before the scan, which hides COMPLETED behind SCANNING.
     */
    wifi_status_update(&live);
    int connected = (strcmp(live.wpa_state, "COMPLETED") == 0 && live.ip[0]);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    pthread_mutex_lock(&g_wifi_lock);
    memset(&g_ssid_status, 0, sizeof(wifi_status_t));
    memset(&g_ssid_info, 0, sizeof(wifi_ssid_t));
    memset(&g_ssid_info_config, 0, sizeof(wifi_ssid_t));
    g_conn_time = 0;

    if (connected) {
        memcpy(&g_ssid_status, &live, sizeof(wifi_status_t));
        ssid_copy(g_ssid_info.name, live.ssid);
        g_conn_state   = CONN_OK;
        g_wifi_is_prov = WIFI_PROV_OK;
        g_wifi_link    = 1;
        if (quiet)
            g_conn_time = ts.tv_sec;
    } else {
        g_conn_state = CONN_INIT;
    }
    pthread_mutex_unlock(&g_wifi_lock);

    if (connected)
        applog(LOG_INFO, "WiFi already connected to '%s' (IP %s)", live.ssid, live.ip);

    /*
     * Advertise without tearing the link down, so a saved network survives
     * every boot. wifi_connect() switches networks when new credentials
     * arrive.
     */
    if (!quiet) {
        if (!ble_dev_is_run())
            ble_dev_startup();
        else
            ble_dev_set_power(1);
    }

    ble_data_status_update();
}

void wifi_rssi_refresh(void *ctx, uint64_t now_ms)
{
    (void)ctx;
    (void)now_ms;
    unsigned gen  = atomic_load(&g_rssi_gen);
    int      rssi = wifi_get_rssi();

    pthread_mutex_lock(&g_rssi_lock);
    if (atomic_load(&g_rssi_gen) == gen)
        atomic_store(&g_rssi, rssi);
    pthread_mutex_unlock(&g_rssi_lock);
}

int wifi_update_ssid_info(char *ssid, int *rssi)
{
    int changed = 0;

    *rssi = atomic_load(&g_rssi);

    pthread_mutex_lock(&g_wifi_lock);
    if (strcmp(ssid, g_ssid_info.name) != 0) {
        memcpy(ssid, g_ssid_info.name, WIFI_SSID_LEN_MAX - 1);
        ssid[WIFI_SSID_LEN_MAX - 1] = 0;
        changed                     = 1;
    }
    pthread_mutex_unlock(&g_wifi_lock);
    return changed;
}

int wifi_get_state(char *ip, int ip_len)
{
    uint8_t state;

    pthread_mutex_lock(&g_wifi_lock);
    if (ip && ip_len > 0) {
        strncpy(ip, g_ssid_status.ip, ip_len - 1);
        ip[ip_len - 1] = '\0';
    }
    state = g_conn_state;
    pthread_mutex_unlock(&g_wifi_lock);

    /*
     * CONN_OK latches until re-provisioning and the monitor parks after
     * provisioning, so check the kernel carrier on every read. The ip from
     * `wpa_cli status` can transiently miss under load; carrier is stable.
     */
    if (state == CONN_OK)
        return wifi_carrier_up() ? 2 : 0;
    if (state == CONN_PEND)
        return 1;
    return 0;
}

int wifi_has_ssid(void)
{
    int   found = 0, in_block = 0, ssid_found = 0, disable_found = 0;
    FILE *fp;
    char  buf[512];
    char *pos, *end, *val;
    int   c;

    fp = fopen(WPA_CONF_PATH, "re");
    if (fp == NULL)
        return 0;

    while (fgets(buf, sizeof(buf) - 1, fp) != NULL) {
        if (strchr(buf, '\n') == NULL)
            while ((c = fgetc(fp)) != EOF && c != '\n')
                ;

        pos = buf;
        while (*pos == ' ' || *pos == '\t' || *pos == '\r')
            pos++;
        if (*pos == '#' || *pos == '\n' || *pos == '\0')
            continue;

        end = strchr(pos, '"');
        if (end != NULL)
            end = strrchr(end + 1, '"');
        end = strchr(end != NULL ? end : pos, '#');
        if (end != NULL)
            *end = '\0';
        end = pos + strlen(pos);
        while (end > pos &&
                (end[-1] == '\n' || end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
            *--end = '\0';
        if (*pos == '\0')
            continue;

        if (!in_block) {
            in_block      = strcmp(pos, "network={") == 0;
            ssid_found    = 0;
            disable_found = 0;
            continue;
        }

        if (strcmp(pos, "}") == 0) {
            if ((ssid_found == 1) && (disable_found == 0)) {
                found = 1;
                break;
            }

            in_block = 0;
            continue;
        }

        val = strchr(pos, '=');
        if (val == NULL)
            continue;
        *val++ = '\0';
        if (strcmp(pos, "ssid") == 0)
            ssid_found = *val == '"' || (val[0] == 'P' && val[1] == '"') ||
                    hex_nibble((unsigned char)*val) >= 0;
        else if (strcmp(pos, "disabled") == 0)
            disable_found = strtol(val, &end, 10) != 0 && end != val && *end == '\0';
    }

    fclose(fp);
    explicit_bzero(buf, sizeof(buf));
    return found;
}

void wifi_action_set(unsigned int action)
{
    pthread_mutex_lock(&g_wifi_lock);
    /* A user request cancels the backoff so it is serviced at once. */
    g_wifi_retry_backoff = 0;
    g_wifi_retry_next    = 0;

    if (action & WIFI_ACT_PROV) {
        if (g_prov_quiet) {
            g_prov_quiet = 0;
            g_dev_action |= WIFI_ACT_SCAN | WIFI_ACT_PROV;
            g_conn_time = 0;
        } else if (WIFI_PROV_NONE == g_wifi_is_prov) {
            g_dev_action |= WIFI_ACT_SCAN | WIFI_ACT_PROV;
            sem_post(&g_prov_sem);
        }
    } else {
        g_dev_action |= action;
    }
    pthread_mutex_unlock(&g_wifi_lock);
}

void wifi_startup(void)
{
    int quiet = wifi_has_ssid();

    pthread_mutex_lock(&g_wifi_lock);
    g_prov_quiet = quiet;
    g_dev_action |= quiet ? WIFI_ACT_PROV : (WIFI_ACT_SCAN | WIFI_ACT_PROV);
    pthread_mutex_unlock(&g_wifi_lock);
    sem_post(&g_prov_sem);
}

static void wifi_action_process()
{
    unsigned int action;

    pthread_mutex_lock(&g_wifi_lock);
    action = g_dev_action;
    pthread_mutex_unlock(&g_wifi_lock);

    if (action == 0)
        return;

    applog(LOG_DEBUG, "WiFi action mask 0x%x", action);

    /*
     * wlan0 may hot-plug after boot, so wpa_supplicant may not be
     * running yet. Sleep only when it had to be spawned: an
     * unconditional delay pushes conn_state:2 past the phone app's
     * provisioning deadline.
     */
    if (action & (WIFI_ACT_CONNECT | WIFI_ACT_SCAN)) {
        if (access(WPA_CTRL_FILE, F_OK) != 0) {
            wifi_check_service();
            sleep(2);
        }
    }

    if (action & WIFI_ACT_CONNECT) {
        wifi_connect();
        ble_data_status_update();
    }

    /*
     * PROV before SCAN: ble_startup() samples the live wpa state, which a
     * scan would turn into SCANNING.
     */
    if (action & WIFI_ACT_PROV) {
        ble_startup();
    }

    if (action & WIFI_ACT_SCAN) {
        if (wifi_scan() > 0) {
            ble_data_update();
        }
    }
}

static void wifi_check_service(void)
{
    const char *supp_argv[] = { "wpa_supplicant", "-B", "-D", "nl80211", "-i", WIFI_DEV_STA_NAME,
        "-c", WPA_CONF_PATH, "-P", WPA_SUPP_PID_FILE, NULL };
    const char *cli_argv[] = { "wpa_cli", "-a", "/etc/wpa_action.sh", "-B", "-i", WIFI_DEV_STA_NAME,
        "-P", WPA_PID_FILE, NULL };

    pthread_mutex_lock(&g_svc_lock);
    atomic_store(&g_svc_at, wifi_now_ms());
    if (!wifi_supp_alive()) {
        int seeded = ini_file_seed(WPA_CONF_PATH, WPA_CONF_FACTORY);
        if (seeded > 0)
            applog(LOG_INFO, "wpa_supplicant.conf missing — copied from the factory default");
        else if (seeded < 0)
            applog(LOG_ERR, "wpa_supplicant.conf missing and not copied from the factory default");
        int added = seeded == 0 ? ini_file_head_fill(WPA_CONF_PATH, WPA_CONF_FACTORY) : 0;
        if (added > 0)
            applog(LOG_INFO,
                    "wpa_supplicant.conf: %d missing header line(s) inserted from "
                    "the factory default",
                    added);
        else if (added < 0)
            applog(LOG_ERR,
                    "wpa_supplicant.conf: missing header lines not inserted from "
                    "the factory default");
        run_argv(supp_argv);
    }

    if (access(UDHCPC_PID_FILE, F_OK) != 0) {
        system(UDHCPC_CMD);
    }

    if (!wifi_cli_alive()) {
        run_argv(cli_argv);
    }
    pthread_mutex_unlock(&g_svc_lock);
}

static uint64_t wifi_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int wifi_supp_alive(void)
{
    return net_linux_pidfile_pid(WPA_SUPP_PID_FILE, "wpa_supplicant") > 0;
}

static int wifi_cli_alive(void)
{
    return net_linux_pidfile_pid(WPA_PID_FILE, "wpa_cli") > 0;
}

static void wifi_daemon_repair(uint64_t now_ms)
{
    uint64_t at = atomic_load(&g_svc_at);
    unsigned start;

    if (at && (int64_t)(now_ms - at) < (int64_t)WIFI_DAEMON_RETRY_MS)
        return;
    start = net_wifi_daemons_pick(at != 0, wifi_iface_present(), wifi_supp_alive(),
            wifi_cli_alive());
    if (!start)
        return;
    applog(LOG_WARNING, "WiFi: %s%s%s not running; restarting",
            (start & NET_WIFI_START_SUPP) ? "wpa_supplicant" : "",
            start == (NET_WIFI_START_SUPP | NET_WIFI_START_CLI) ? " and " : "",
            (start & NET_WIFI_START_CLI) ? "wpa_cli" : "");
    wifi_check_service();
}

void wifi_exception(void)
{
    int known;

    pthread_mutex_lock(&g_wifi_lock);
    known = g_ssid_info.name[0] || g_ssid_info_config.name[0];
    pthread_mutex_unlock(&g_wifi_lock);
    if (known) {
        wifi_udhcpc_stop();
        system(UDHCPC_CMD);
    }
}

static void wifi_retry_recover(void)
{
    char name[WIFI_SSID_LEN_MAX];
    int  stuck;

    pthread_mutex_lock(&g_wifi_lock);
    stuck = g_conn_state == CONN_FAIL && strcmp(g_ssid_status.wpa_state, "COMPLETED") == 0 &&
            !g_ssid_status.ip[0];
    memcpy(name, g_ssid_info_config.name, sizeof(name));
    pthread_mutex_unlock(&g_wifi_lock);
    if (!stuck)
        return;

    applog(LOG_WARNING, "WiFi connected to '%s' but has no IP address; restarting DHCP", name);
    wifi_exception();
}

static int wifi_iface_present(void)
{
    return access("/sys/class/net/" WIFI_DEV_STA_NAME, F_OK) == 0;
}

int wifi_carrier_up(void)
{
    FILE *f = fopen("/sys/class/net/" WIFI_DEV_STA_NAME "/carrier", "re");
    if (!f)
        return 0;
    int c = fgetc(f);
    fclose(f);
    return c == '1';
}

static int wifi_udhcpc_alive(void)
{
    return net_linux_udhcpc_pid(UDHCPC_PID_FILE) > 0;
}

void wifi_link_repair_tick(void *ctx, uint64_t now_ms)
{
    char ip[INET_ADDRSTRLEN];
    (void)ctx;
    wifi_daemon_repair(now_ms);
    switch (net_wifi_repair_pick(wifi_iface_present(), wifi_carrier_up(),
            net_iface_ipv4(WIFI_DEV_STA_NAME, ip, sizeof ip) == 0, wifi_udhcpc_alive())) {
    case NET_WIFI_REPAIR_DHCP:
        applog(LOG_WARNING, "WiFi (%s) connected but has no IP address; restarting DHCP",
                WIFI_DEV_STA_NAME);
        wifi_udhcpc_stop();
        system(UDHCPC_CMD);
        break;
    case NET_WIFI_REPAIR_STOP:
        wifi_udhcpc_stop();
        break;
    case NET_WIFI_REPAIR_NONE:
        break;
    }
}

void wifi_cfg_freeze(int on)
{
    atomic_store(&g_wifi_cfg_frozen, on);
}

static void wifi_prov_connected(time_t now)
{
    char        name[WIFI_SSID_LEN_MAX], ip[sizeof(g_ssid_status.ip)];
    const char *save_argv[] = { "wpa_cli", "-i", WIFI_DEV_STA_NAME, "save_config", NULL };

    pthread_mutex_lock(&g_wifi_lock);
    g_conn_state   = CONN_OK;
    g_wifi_is_prov = WIFI_PROV_OK;
    if (g_prov_quiet && !g_conn_time)
        g_conn_time = now;
    if (g_ssid_info_config.name[0])
        memcpy(&g_ssid_info, &g_ssid_info_config, sizeof(wifi_ssid_t));
    else
        ssid_copy(g_ssid_info.name, g_ssid_status.ssid);
    memcpy(name, g_ssid_info.name, sizeof(name));
    memcpy(ip, g_ssid_status.ip, sizeof(ip));
    pthread_mutex_unlock(&g_wifi_lock);
    ble_data_status_update();
    if (atomic_load(&g_wifi_cfg_frozen))
        applog(LOG_WARNING, "WiFi settings not saved: factory reset in progress");
    else
        set_wpa_ok_argv(save_argv);
    applog(LOG_OP, "WiFi connected to '%s' (IP %s)", name, ip);
}

static int wifi_prov_finish(time_t now)
{
    int quiet;

    pthread_mutex_lock(&g_wifi_lock);
    if ((g_conn_state != CONN_OK) || !g_conn_time || ((now - g_conn_time) < PROV_DELAY_SEC) ||
            (g_dev_action & WIFI_ACT_PROV)) {
        pthread_mutex_unlock(&g_wifi_lock);
        return 0;
    }

    g_wifi_is_prov = WIFI_PROV_NONE;
    quiet          = g_prov_quiet;
    g_prov_quiet   = 0;
    pthread_mutex_unlock(&g_wifi_lock);
    if (quiet)
        return 1;
    ble_dev_set_power(0);
    applog(LOG_INFO, "WiFi provisioning finished; Bluetooth turned off");
    return 1;
}

void *wifi_provision(void *arg)
{
    struct timespec ts;
    wifi_status_t   st;
    char            name[WIFI_SSID_LEN_MAX];
    int             prov, state, recover, failed;

    (void)arg;
    applog(LOG_DEBUG, "WiFi provisioning thread started");
    while (1) {
        pthread_mutex_lock(&g_wifi_lock);
        prov = g_wifi_is_prov;
        pthread_mutex_unlock(&g_wifi_lock);
        if (prov == WIFI_PROV_NONE) {
            sem_wait(&g_prov_sem);
            pthread_mutex_lock(&g_wifi_lock);
            g_wifi_is_prov = WIFI_PROV_ING;
            pthread_mutex_unlock(&g_wifi_lock);
        }

        if (!wifi_iface_present()) {
            pthread_mutex_lock(&g_wifi_lock);
            g_wifi_retry_backoff = 0;
            g_wifi_retry_next    = 0;
            pthread_mutex_unlock(&g_wifi_lock);
            sleep(2);
            continue;
        }

        /* Not connected: space retries by the geometric backoff. */
        int act_now = 1;
        recover     = 0;
        {
            struct timespec rt;
            clock_gettime(CLOCK_MONOTONIC, &rt);
            pthread_mutex_lock(&g_wifi_lock);
            int retrying = (g_conn_state != CONN_PEND) && (g_conn_state != CONN_OK);
            if (retrying && rt.tv_sec < g_wifi_retry_next) {
                act_now = 0;
            } else if (retrying) {
                int next = g_wifi_retry_backoff ? g_wifi_retry_backoff * 2 : WIFI_RETRY_BACKOFF_MIN;
                if (next > WIFI_RETRY_BACKOFF_MAX)
                    next = WIFI_RETRY_BACKOFF_MAX;
                g_wifi_retry_backoff = next;
                g_wifi_retry_next    = rt.tv_sec + next;
                recover              = 1;
            } else {
                g_wifi_retry_backoff = 0;
                g_wifi_retry_next    = 0;
            }
            pthread_mutex_unlock(&g_wifi_lock);
        }
        if (recover)
            wifi_retry_recover();

        if (act_now)
            wifi_action_process();

        /*
         * A saved network auto-connects at boot with no provisioning, from
         * CONN_INIT; promote it to CONN_OK like a fresh provision.
         */
        clock_gettime(CLOCK_MONOTONIC, &ts);
        wifi_status_update(&st);
        int associated = (strcmp(st.wpa_state, "COMPLETED") == 0);
        int addressed  = (st.ip[0] != '\0');
        int connected  = associated && addressed;

        pthread_mutex_lock(&g_wifi_lock);
        g_ssid_status = st;
        state         = g_conn_state;
        memcpy(name, g_ssid_info.name, sizeof(name));
        pthread_mutex_unlock(&g_wifi_lock);

        if (g_wifi_link && !connected) {
            pthread_mutex_lock(&g_rssi_lock);
            atomic_fetch_add(&g_rssi_gen, 1);
            atomic_store(&g_rssi, 0);
            pthread_mutex_unlock(&g_rssi_lock);
            applog(LOG_WARNING, "WiFi disconnected from '%s'", name);
        } else if (!g_wifi_link && connected && state == CONN_OK)
            applog(LOG_INFO, "WiFi connected to '%s' (IP %s)", name, st.ip);
        g_wifi_link = connected;

        if (state == CONN_INIT && !connected) {
            sleep(1);
            continue;
        }

        if (connected) {
            if (state != CONN_OK)
                wifi_prov_connected(ts.tv_sec);
        } else if (state == CONN_PEND) {
            pthread_mutex_lock(&g_wifi_lock);
            failed = g_conn_state == CONN_PEND && g_conn_time &&
                    (ts.tv_sec - g_conn_time) >= CONN_TIMEOUT;
            if (failed)
                g_conn_state = CONN_FAIL;
            memcpy(name, g_ssid_info_config.name, sizeof(name));
            pthread_mutex_unlock(&g_wifi_lock);
            if (failed) {
                ble_data_status_update();
                if (!associated)
                    applog(LOG_ERR,
                            "WiFi provisioning to '%s' failed: could not join "
                            "the network (state %s%s)",
                            name, st.wpa_state[0] ? st.wpa_state : "?",
                            wifi_net_auth_rejected() ? ", likely wrong password" : "");
                else
                    applog(LOG_ERR,
                            "WiFi provisioning to '%s' failed: joined but got "
                            "no IP address within %d s",
                            name, CONN_TIMEOUT);
            }
        }

        if (wifi_prov_finish(ts.tv_sec))
            continue;

        pthread_mutex_lock(&g_wifi_lock);
        state = g_conn_state;
        pthread_mutex_unlock(&g_wifi_lock);
        /*
         * Poll fast while connecting so conn_state:2 goes out within ~250 ms
         * of the IP: the phone app accepts provisioning only if it sees it
         * within about two status polls (~6 s) of setssid.
         */
        if (state == CONN_PEND)
            usleep(250 * 1000);
        else
            sleep(1);
    }

    applog(LOG_WARNING, "WiFi provisioning thread stopped");
    return NULL;
}

int wifi_init(char *name)
{
    pthread_t tid;

    g_wifi_is_prov = WIFI_PROV_NONE;
    strncpy(g_dev_name, name, WIFI_NAME_LEN_MAX - 1);

    ble_dev_init(name, ble_cmd_process);
    /*
     * Before the thread exists: sem_init() on a semaphore the thread already
     * waits on loses the waiter, and the provision thread never wakes.
     */
    sem_init(&g_prov_sem, 0, 0);
    if (pthread_create(&tid, NULL, wifi_provision, NULL)) {
        applog(LOG_ERR, "WiFi provisioning unavailable: thread could not be started");
        return -1;
    } else {
        pthread_detach(tid);
    }

    return 0;
}
