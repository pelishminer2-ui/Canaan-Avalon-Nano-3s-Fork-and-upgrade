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

#include "router.h"
#include "webform.h"
#include "web_server.h"
#include "telemetry.h"
#include "hex.h"
#include "logexport.h"
#include "pool_config.h"
#include "web_logfmt.h"
#include "osif_logpath.h"
#include "webcred.h"
#include "tlscert.h"
#include "webstatus.h"
#include "ini.h"
#include "ini_file.h"
#include "board_nano3s.h"
#include <civetweb.h>
#include "upload.h"
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

/* Log export read size. Heap-allocated: civetweb workers run on a fixed
 * 100 KiB stack (USE_STACK_SIZE). */
#define LOG_EXPORT_CHUNK 65536

#define MINER_WEB_ROOT_DEFAULT "/usr/share/miner-web/www"

#define TZSET_PERIOD_S 3

#define ZONEINFO_DIR   "/usr/share/zoneinfo" /* TZif files mm_miner links to */
#define LOCALTIME_PATH "/etc/localtime"      /* active zone link */

#define WEB_HTTPS_PORT     "443"
#define WEB_REST_PORT      "8443"
#define WEB_REST_PREFIX    "/api/v1/"
#define WEB_TLS_CERT       "/data/userconfig/miner-web.pem" /* kept across reboots and OTA */
#define WEB_TLS_CERT_TMP   "/tmp/miner-web.pem"             /* used when /data cannot hold it */
#define WEB_TLS_CN_DEFAULT "avalon-miner"                   /* subject when there is no host name */
#define WEB_TLS_PROTOCOL   "4" /* civetweb ssl_protocol_version: TLS 1.2+ */

#define QR_ADDR_N       16
#define QR_BEGIN_GAP_MS 60000

typedef struct {
    char     addr[48];
    char     token[33];
    uint64_t opened_ms;
    int      used;
} qr_addr;

static web_ctx     g_ctx; /* request context template */
static const char *g_systemcfg;

/* Workers serve requests concurrently. One refresher at a time rebuilds
 * g_next without holding g_tel_lock, then publishes it into g_tel; every
 * request reads its own copy of g_tel. */
static pthread_mutex_t g_refresh_lock = PTHREAD_MUTEX_INITIALIZER;
/* guards g_tel* and g_activated */
static pthread_mutex_t g_tel_lock = PTHREAD_MUTEX_INITIALIZER;
static mw_telemetry    g_next;   /* refresh in progress */
static mw_telemetry    g_tel;    /* last telemetry read from the API */
static int             g_tel_ok; /* g_tel holds a successful refresh */

static int             g_activated = WEB_ACTIVATED_UNKNOWN;
static FILE           *g_access_log;
static pthread_mutex_t g_qr_lock = PTHREAD_MUTEX_INITIALIZER;
static qr_addr         g_qr_addr[QR_ADDR_N];

/* request paths served from the document root; "/" serves the first index_files page */
static const char *const g_static_files[] = {
    "/",
    "/login.html",
    "/dashboard.html",
    "/pools.html",
    "/qr_login.html",
    "/upgrade.html",
    "/network.html",
    "/log.html",
    "/password.html",
    "/reset.html",
    "/chrome.css",
    "/chrome.js",
    "/sha256.min.js",
    "/qrcode.min.js",
    "/pelish-logo.png",
    "/canaan-logo.png",
};
static const char g_qr_begin_uri[] = "/api/v1/auth";
static const char g_qr_poll_uri[]  = "/api/v1/qr-login";

static int cfg_flag(const char *path, const char *sect, const char *key)
{
    ini_t      *ini;
    const char *s;
    int         v = 0;

    if (!path)
        return 0;
    ini = ini_load(path);
    if (!ini)
        return 0;
    s = ini_get(ini, sect, key);
    if (s && s[0] == '1' && s[1] == '\0')
        v = 1;
    ini_free(ini);
    return v;
}

static int lcd_pelish_from_cfg(const char *path)
{
    return cfg_flag(path, "lcd", "pelish");
}

static int wifi_eth_from_cfg(const char *path)
{
    return cfg_flag(path, "net", "wifi_eth");
}

static int wifi_eth_write_cb(ini_t *ini, void *ctx)
{
    return ini_set(ini, "net", "wifi_eth", *(int *)ctx ? "1" : "0");
}

static int wifi_eth_set_cfg(int on)
{
    int v = on ? 1 : 0;

    if (!g_systemcfg)
        return 0;
    return ini_file_update(g_systemcfg, wifi_eth_write_cb, &v) == 0;
}

static int syscfg_flag_write(ini_t *ini, void *ctx)
{
    const char *const *kv = ctx;
    return ini_set(ini, "syscfg", kv[0], kv[1]);
}

static int syscfg_flag_set(const char *key, int on)
{
    const char *kv[2];

    if (!g_systemcfg || !key)
        return 0;
    kv[0] = key;
    kv[1] = on ? "1" : "0";
    return ini_file_update(g_systemcfg, syscfg_flag_write, kv) == 0;
}

static int autotune_set_cfg(int mode)
{
    return syscfg_flag_set("autotune", mode);
}

static int aftermarket_psu_set_cfg(int on)
{
    return syscfg_flag_set("aftermarket_psu", on);
}

static void telemetry_refresh_maybe(const char *path)
{
    static time_t last;
    if (!path || strncmp(path, WEB_REST_PREFIX, strlen(WEB_REST_PREFIX)))
        return;
    /* a refresh already in flight: serve the previous snapshot */
    if (pthread_mutex_trylock(&g_refresh_lock) != 0)
        return;
    time_t now = time(NULL);
    if (now != last) {
        last   = now;
        int ok = mw_telemetry_refresh(&g_next) == 0;
        pthread_mutex_lock(&g_tel_lock);
        mw_telemetry_copy(&g_tel, &g_next);
        g_tel_ok = ok;
        if (ok)
            g_activated = g_tel.activation;
        pthread_mutex_unlock(&g_tel_lock);
    }
    pthread_mutex_unlock(&g_refresh_lock);
}

static void access_log_open(void)
{
    char  dir[sizeof MINER_WEB_LOG_PATH];
    char *slash;

    snprintf(dir, sizeof dir, "%s", MINER_WEB_LOG_PATH);
    slash = strrchr(dir, '/');
    if (slash && slash != dir) {
        *slash = 0;
        for (char *p = dir + 1; *p; p++) {
            if (*p != '/')
                continue;
            *p = 0;
            mkdir(dir, 0755);
            *p = '/';
        }
        mkdir(dir, 0755);
    }
    g_access_log = fopen(MINER_WEB_LOG_PATH, "a");
}

void web_log(const char *lvl, const char *fmt, ...)
{
    FILE   *sink = g_access_log ? g_access_log : stderr;
    char    stamp[WEB_LOG_STAMP_MAX];
    va_list ap;

    if (web_log_rank(lvl) > web_log_threshold())
        return;
    web_log_stamp(stamp, lvl);
    flockfile(sink);
    fputs(stamp, sink);
    va_start(ap, fmt);
    vfprintf(sink, fmt, ap);
    va_end(ap);
    fputc('\n', sink);
    fflush(sink);
    funlockfile(sink);
}

static web_method method_of(const char *m)
{
    if (m && !strcmp(m, "GET"))
        return WEB_GET;
    if (m && !strcmp(m, "POST"))
        return WEB_POST;
    if (m && !strcmp(m, "DELETE"))
        return WEB_DELETE;
    return WEB_METHOD_OTHER;
}

static int make_qr_token(char out[33])
{
    unsigned char b[16];
    FILE         *f = fopen("/dev/urandom", "rb");
    if (!f)
        return 0;
    int ok = fread(b, 1, sizeof b, f) == sizeof b;
    fclose(f);
    if (!ok)
        return 0;
    hex_encode(out, b, sizeof b);
    return 1;
}

static uint64_t qr_mono_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static qr_addr *qr_addr_find(const char *addr)
{
    for (size_t i = 0; i < QR_ADDR_N; i++)
        if (g_qr_addr[i].used && !strcmp(g_qr_addr[i].addr, addr))
            return &g_qr_addr[i];
    return NULL;
}

static int qr_addr_open(const char *addr)
{
    uint64_t now  = qr_mono_ms();
    int      open = 1;

    pthread_mutex_lock(&g_qr_lock);
    qr_addr *a = qr_addr_find(addr);
    if (a && now - a->opened_ms < QR_BEGIN_GAP_MS) {
        open = 0;
    } else {
        if (!a) {
            a = &g_qr_addr[0];
            for (size_t i = 1; i < QR_ADDR_N && a->used; i++)
                if (!g_qr_addr[i].used || g_qr_addr[i].opened_ms < a->opened_ms)
                    a = &g_qr_addr[i];
            snprintf(a->addr, sizeof a->addr, "%s", addr);
            a->used = 1;
        }
        a->opened_ms = now;
        a->token[0]  = '\0';
    }
    pthread_mutex_unlock(&g_qr_lock);
    return open;
}

static void qr_addr_set(const char *addr, const char *token)
{
    pthread_mutex_lock(&g_qr_lock);
    qr_addr *a = qr_addr_find(addr);
    if (a)
        snprintf(a->token, sizeof a->token, "%s", token);
    pthread_mutex_unlock(&g_qr_lock);
}

static int qr_addr_owns(const char *addr, const char *token)
{
    pthread_mutex_lock(&g_qr_lock);
    qr_addr *a    = qr_addr_find(addr);
    int      owns = a && a->token[0] && !strcmp(a->token, token);
    pthread_mutex_unlock(&g_qr_lock);
    return owns;
}

static int request_error(struct mg_connection *c, int status, const char *message)
{
    mg_printf(c,
            "HTTP/1.1 %d %s\r\nContent-Type: text/plain\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
            status, mg_get_response_code_text(c, status), strlen(message), message);
    return status;
}

static void refused_log(struct mg_connection *c, int status, const char *message)
{
    const struct mg_request_info *ri = mg_get_request_info(c);
    char                          safe[WEB_SERVE_PATH_MAX];

    logexport_safe_name(ri->local_uri ? ri->local_uri : ri->request_uri, safe, sizeof safe);
    web_logf(stderr, "WARN", "%s %s refused (HTTP %d): %s\n", ri->request_method, safe, status,
            message);
}

static int request_refused(struct mg_connection *c, int status, const char *message)
{
    refused_log(c, status, message);
    return request_error(c, status, message);
}

static int rest_refused(struct mg_connection *c, int status, int code, const char *message)
{
    char   out[256];
    size_t len = web_status_json("E", code, message, out, sizeof out);

    refused_log(c, status, message);
    mg_printf(c,
            "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n",
            status, mg_get_response_code_text(c, status), len);
    mg_write(c, out, len);
    return status;
}

static int not_found(struct mg_connection *c)
{
    static const char nf[] = "Not found\n";

    mg_printf(c,
            "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
            sizeof nf - 1, nf);
    return 404;
}

static FILE *open_regular(const char *path)
{
    struct stat st;
    int         fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    FILE       *f;

    if (fd < 0)
        return NULL;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || !(f = fdopen(fd, "rb"))) {
        close(fd);
        return NULL;
    }
    return f;
}

static int serve_logfile(struct mg_connection *c, const char *path, int concat)
{
    char        rot1[288];
    char        safe[sizeof rot1];
    FILE       *segs[2];
    const char *segname[2];
    long long   seglen[2];
    int         nseg = 0;
    if (concat) {
        snprintf(rot1, sizeof rot1, "%s.1", path);
        segname[nseg] = rot1;
        if ((segs[nseg] = open_regular(rot1)) != NULL)
            nseg++;
    }
    segname[nseg] = path;
    if ((segs[nseg] = open_regular(path)) != NULL)
        nseg++;

    if (nseg == 0) {
        logexport_safe_name(path, safe, sizeof safe);
        web_logf(stderr, "WARN", "%s download refused: no file to send\n", safe);
        return not_found(c);
    }

    char *buf = malloc(LOG_EXPORT_CHUNK);
    if (!buf) {
        for (int i = 0; i < nseg; i++)
            fclose(segs[i]);
        const char *name = strrchr(path, '/');

        logexport_safe_name(name ? name + 1 : path, safe, sizeof safe);
        web_logf(stderr, "ERR", "%s download failed: out of memory\n", safe);
        return request_error(c, 503, "temporarily unavailable, try again shortly");
    }

    long long total = 0;
    for (int i = 0; i < nseg; i++) {
        struct stat st;
        seglen[i] = fstat(fileno(segs[i]), &st) == 0 ? (long long)st.st_size : 0;
        total += seglen[i];
    }
    mg_printf(c,
            "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
            "Content-Length: %lld\r\nConnection: close\r\n\r\n",
            total);
    long long sent = 0;
    int       rc   = 200;
    for (int i = 0; i < nseg && rc == 200; i++) {
        long long left = seglen[i];
        while (left > 0) {
            size_t r = fread(buf, 1, left < LOG_EXPORT_CHUNK ? (size_t)left : LOG_EXPORT_CHUNK,
                    segs[i]);
            if (r == 0) {
                logexport_safe_name(segname[i], safe, sizeof safe);
                web_logf(stderr, "WARN",
                        "log export of %s ended %lld bytes short of the "
                        "%lld announced; the file shrank or could not be read, so the "
                        "download is incomplete\n",
                        safe, total - sent, total);
                rc = 500;
                break;
            }
            if (mg_write(c, buf, r) <= 0) {
                logexport_safe_name(path, safe, sizeof safe);
                web_logf(stderr, "WARN",
                        "log export of %s stopped: the client closed the "
                        "connection\n",
                        safe);
                rc = 500;
                break;
            }
            left -= (long long)r;
            sent += (long long)r;
        }
    }
    for (int i = 0; i < nseg; i++)
        fclose(segs[i]);
    free(buf);
    return rc;
}

static int on_request(struct mg_connection *c, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri = mg_get_request_info(c);
    const char *refused = web_request_refused(mg_get_header(c, "Host"), mg_get_header(c, "Origin"),
            mg_get_header(c, "Referer"),
            strcmp(ri->request_method, "GET") && strcmp(ri->request_method, "HEAD"));
    if (refused)
        return request_refused(c, 403, refused);

    char      body[4096];
    long long body_length = ri->content_length;
    if (body_length < 0) {
        if (mg_get_header(c, "Transfer-Encoding"))
            return request_refused(c, 411, "content length required");
        body_length = 0;
    }
    if ((unsigned long long)body_length >= sizeof body)
        return request_refused(c, 413, "request body too large");
    int blen = 0;
    while (blen < body_length) {
        int n = mg_read(c, body + blen, (size_t)body_length - (size_t)blen);
        if (n <= 0) {
            explicit_bzero(body, sizeof body);
            return request_refused(c, 400, "incomplete request body");
        }
        blen += n;
    }
    if (memchr(body, 0, (size_t)blen)) {
        explicit_bzero(body, sizeof body);
        return request_refused(c, 400, "invalid request body");
    }
    body[blen] = 0;

    char        cookie_auth[64] = { 0 };
    const char *cookie          = mg_get_header(c, "Cookie");
    if (cookie)
        mg_get_cookie(cookie, "auth", cookie_auth, sizeof cookie_auth);

    const char *uri = ri->local_uri ? ri->local_uri : ri->request_uri;
    telemetry_refresh_maybe(uri);
    mw_telemetry tel;
    int          tel_ok, activated;
    pthread_mutex_lock(&g_tel_lock);
    mw_telemetry_copy(&tel, &g_tel);
    tel_ok    = g_tel_ok;
    activated = g_activated;
    pthread_mutex_unlock(&g_tel_lock);

    mc_webcred creds;
    int        have_creds = g_systemcfg && web_creds_load(g_systemcfg, &creds) == 0;

    char  nonce[9] = "", qr_token[33] = "";
    char *reply = NULL;
    int   rc;
    int   qr_begin = !strcmp(uri, g_qr_begin_uri) && !strcmp(ri->request_method, "GET");
    int   qr_poll  = !strcmp(uri, g_qr_poll_uri) && !strcmp(ri->request_method, "POST");
    if (qr_begin) {
        if (!have_creds || !make_qr_token(qr_token)) {
            rc = request_error(c, 503, "QR login unavailable, try again shortly");
            goto out;
        }
        memcpy(nonce, qr_token, 8);
        if (!qr_addr_open(ri->remote_addr)) {
            qr_token[0] = '\0';
        } else if (mw_qr_login_begin(qr_token)) {
            qr_addr_set(ri->remote_addr, qr_token);
        } else {
            rc = request_error(c, 503, "QR login unavailable, try again shortly");
            goto out;
        }
    } else if (qr_poll) {
        if (web_form_get(body, "session", qr_token, sizeof qr_token) != 1 ||
                strlen(qr_token) != 32 || strspn(qr_token, "0123456789abcdef") != 32)
            qr_token[0] = 0;
        if (qr_token[0])
            memcpy(nonce, qr_token, 8);
    }
    web_ctx ctx    = g_ctx;
    ctx.lcd_pelish       = lcd_pelish_from_cfg(g_systemcfg);
    ctx.wifi_eth         = wifi_eth_from_cfg(g_systemcfg);
    ctx.autotune         = cfg_flag(g_systemcfg, "syscfg", "autotune");
    ctx.aftermarket_psu  = cfg_flag(g_systemcfg, "syscfg", "aftermarket_psu");
    ctx.telemetry  = tel_ok ? &tel.dash : NULL;
    ctx.dna        = tel.dna[0] ? tel.dna : NULL;
    ctx.creds      = have_creds ? &creds : NULL;
    ctx.auth_nonce = nonce;
    ctx.qr_session = qr_begin ? qr_token : NULL;
    ctx.activated  = activated;
    if (activated > 0 && have_creds && !mc_api_webpass_unset(&creds) && qr_token[0] && qr_poll &&
            qr_addr_owns(ri->remote_addr, qr_token))
        ctx.qr_approved = mw_qr_login_take(qr_token);

    web_request req = {
        .method       = method_of(ri->request_method),
        .path         = uri,
        .body         = blen ? body : NULL,
        .content_type = mg_get_header(c, "Content-Type"),
        .query        = ri->query_string,
        .auth_cookie  = cookie_auth[0] ? cookie_auth : NULL,
        .auth_token   = mg_get_header(c, "Token"),
        .tls          = ri->is_ssl,
        .remote_addr  = ri->remote_addr,
    };

    web_response resp;
    reply = malloc(MW_RPC_REPLY_CAP);
    if (!reply) {
        rc = request_error(c, 503, "temporarily unavailable, try again shortly");
        goto out;
    }
    web_route(&req, &ctx, reply, MW_RPC_REPLY_CAP, &resp);
    if (qr_begin)
        snprintf(resp.extra_headers, sizeof resp.extra_headers, "Cache-Control: no-store\r\n");

    if (resp.serve_file) {
        char served[WEB_SERVE_PATH_MAX];

        rc = serve_logfile(c, resp.serve_file, resp.serve_concat);
        if (rc == 200) {
            logexport_safe_name(resp.serve_name[0] ? resp.serve_name : resp.serve_file, served,
                    sizeof served);
            web_logf(stderr, WEB_LOG_OP, "file %s downloaded from the web page\n", served);
        }
        goto out;
    }

    mg_printf(c,
            "HTTP/1.1 %d %s\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "%s"
            "Connection: close\r\n\r\n",
            resp.status, mg_get_response_code_text(c, resp.status),
            resp.content_type ? resp.content_type : "text/plain", resp.body_len,
            resp.extra_headers);
    mg_write(c, reply, resp.body_len);
    rc = resp.status;
out:
    explicit_bzero(body, sizeof body);
    explicit_bzero(cookie_auth, sizeof cookie_auth);
    explicit_bzero(&tel, sizeof tel);
    explicit_bzero(qr_token, sizeof qr_token);
    explicit_bzero(&creds, sizeof creds);
    if (reply) {
        explicit_bzero(reply, MW_RPC_REPLY_CAP);
        free(reply);
    }
    explicit_bzero(&resp, sizeof resp);
    return rc;
}

static int on_static(struct mg_connection *c, void *cbdata)
{
    (void)cbdata;
    const struct mg_request_info *ri  = mg_get_request_info(c);
    const char                   *uri = ri->local_uri ? ri->local_uri : ri->request_uri;
    const char *refused = web_request_refused(mg_get_header(c, "Host"), NULL, NULL, 0);

    if (refused)
        return request_error(c, 403, refused);
    if (strcmp(ri->request_method, "GET") && strcmp(ri->request_method, "HEAD"))
        return not_found(c);
    for (size_t i = 0; i < sizeof g_static_files / sizeof g_static_files[0]; i++)
        if (!strcmp(uri, g_static_files[i]))
            return 0;
    return not_found(c);
}

static int plain_gate(struct mg_connection *c)
{
    const struct mg_request_info *ri  = mg_get_request_info(c);
    const char                   *uri = ri->local_uri ? ri->local_uri : ri->request_uri;
    char                          loc[WEB_SERVE_PATH_MAX + 64];

    if (ri->is_ssl)
        return 0;
    switch (web_plain_policy(ri->request_method, uri, mg_get_header(c, "Token"))) {
    case WEB_PLAIN_REDIRECT:
        if (web_https_location(mg_get_header(c, "Host"), uri, ri->query_string, loc, sizeof loc) ==
                0) {
            mg_printf(c,
                    "HTTP/1.1 302 Found\r\nLocation: %s\r\n"
                    "Content-Length: 0\r\nConnection: close\r\n\r\n",
                    loc);
            return 302;
        }
        break;
    case WEB_PLAIN_REFUSE:
        break;
    }
    if (!strncmp(uri, WEB_REST_PREFIX, strlen(WEB_REST_PREFIX)))
        return rest_refused(c, 403, WEB_STATUS_HTTPS_CODE, WEB_STATUS_HTTPS_MSG);
    return request_refused(c, 403, "HTTPS required");
}

static int request_gate(struct mg_connection *c)
{
    const struct mg_request_info *ri  = mg_get_request_info(c);
    const char                   *uri = ri->local_uri ? ri->local_uri : ri->request_uri;

    if (ri->server_port == atoi(WEB_REST_PORT) &&
            strncmp(uri, WEB_REST_PREFIX, strlen(WEB_REST_PREFIX)))
        return not_found(c);
    return plain_gate(c);
}

static void handlers_register(struct mg_context *mgc)
{
    mg_set_request_handler(mgc, WEB_RUNLOG_URI, on_request, NULL);
    mg_set_request_handler(mgc, "/api/v1/", on_request, NULL);
    web_upload_register(mgc, MM_OTA_SOCK_PATH, g_systemcfg);
    web_wallpaper_register(mgc, g_systemcfg);
    mg_set_request_handler(mgc, "/", on_static, NULL);
}

static int device_reboot(void)
{
    return mw_reboot_web();
}

static int device_tz_get(char *out, size_t cap)
{
    if (mw_time_get(out, cap) == 0 && out[0])
        return 0;
    char    link[256];
    ssize_t n = readlink(LOCALTIME_PATH, link, sizeof link - 1);
    if (n <= 0)
        return -1;
    link[n] = '\0';
    return mw_tz_from_link(link, out, cap);
}

static int device_tz_set(const char *tz)
{
    char        zi[256];
    struct stat st;
    if (snprintf(zi, sizeof zi, "%s/%s", ZONEINFO_DIR, tz) >= (int)sizeof zi ||
            stat(zi, &st) != 0 || !S_ISREG(st.st_mode))
        return -1;
    return mw_time_set(tz, (long)time(NULL));
}

static int https_listening(const struct mg_context *mgc, const char *port)
{
    struct mg_server_port sp[4];
    int                   n = mg_get_server_ports(mgc, (int)(sizeof sp / sizeof sp[0]), sp);

    for (int i = 0; i < n; i++)
        if (sp[i].is_ssl && sp[i].port == atoi(port))
            return 1;
    return 0;
}

static const char *tls_cert(void)
{
    char host[HOST_NAME_MAX + 1] = "";

    if (gethostname(host, sizeof host - 1) != 0 || !host[0])
        snprintf(host, sizeof host, "%s", WEB_TLS_CN_DEFAULT);
    if (web_tls_cert_ensure(WEB_TLS_CERT, host) == 0)
        return WEB_TLS_CERT;
    web_logf(stderr, "WARN",
            "cannot keep the HTTPS certificate on the data partition; using a new one "
            "until reboot\n");
    if (web_tls_cert_ensure(WEB_TLS_CERT_TMP, host) == 0)
        return WEB_TLS_CERT_TMP;
    return NULL;
}

static struct mg_context *web_start(const char *port, const char *systemcfg_ini)
{
    char        threads[12], ports[64];
    const char *cert;

    g_systemcfg     = systemcfg_ini;
    g_ctx.pool_conf = POOL_CONFIG_DEFAULT;
    g_ctx.reboot    = device_reboot;
    g_ctx.tz_get       = device_tz_get;
    g_ctx.tz_set       = device_tz_set;
    g_ctx.wifi_eth_set          = wifi_eth_set_cfg;
    g_ctx.autotune_set          = autotune_set_cfg;
    g_ctx.aftermarket_psu_set   = aftermarket_psu_set_cfg;
    g_ctx.power_cap_stock_w     = PD_STOCK_CAP_W;
    g_ctx.power_cap_aftermarket_w = PD_AFTERMARKET_CAP_W;
    g_ctx.power_cap_fault_w     = MAX_POUT_LIMIT_ABNORMAL;
    snprintf(threads, sizeof threads, "%d", WEB_NUM_THREADS);
    snprintf(ports, sizeof ports, "%s,%ss,%sso", port, WEB_HTTPS_PORT, WEB_REST_PORT);

    /* clang-format off */
    const char *opts[] = {
        "listening_ports", ports,
        "num_threads", threads,
        "document_root", MINER_WEB_ROOT_DEFAULT,
        "enable_directory_listing", "no",
        "ssi_pattern", "",
        "index_files", "login.html,dashboard.html,index.html",
        "additional_header", "Cache-Control: public, max-age=300",
        "ssl_certificate", NULL,
        "ssl_protocol_version", WEB_TLS_PROTOCOL,
        NULL
    };
    /* clang-format on */
    const size_t        cert_opt = sizeof opts / sizeof opts[0] - 4;
    struct mg_callbacks cb       = { .begin_request = request_gate };
    struct mg_context  *mgc;

    access_log_open();
    web_log_op_fd();
    web_log_web_fd();

    if (!mg_check_feature(MG_FEATURES_TLS)) {
        web_logf(stderr, "ERR",
                "civetweb is built without TLS; the web server does not "
                "start\n");
        return NULL;
    }
    cert = tls_cert();
    if (!cert) {
        web_logf(stderr, "ERR",
                "no HTTPS certificate could be made; the web server does "
                "not start\n");
        return NULL;
    }
    opts[cert_opt] = cert;
    mgc            = mg_start(&cb, NULL, opts);
    if (!mgc) {
        web_logf(stderr, "ERR", "web server could not start on ports %s and %s\n", WEB_HTTPS_PORT,
                port);
        return NULL;
    }
    if (!https_listening(mgc, WEB_HTTPS_PORT)) {
        web_logf(stderr, "ERR",
                "HTTPS could not start on port %s; the web server "
                "stops\n",
                WEB_HTTPS_PORT);
        mg_stop(mgc);
        return NULL;
    }
    if (!https_listening(mgc, WEB_REST_PORT))
        web_logf(stderr, "ERR",
                "HTTPS could not start on port %s; the REST API is served "
                "on port %s only\n",
                WEB_REST_PORT, WEB_HTTPS_PORT);
    web_logf(stderr, "INFO", "web server on port %s\n", WEB_HTTPS_PORT);
    handlers_register(mgc);
    return mgc;
}

int web_server_run(const char *port, const char *systemcfg_ini)
{
    struct mg_context *mgc = web_start(port, systemcfg_ini);
    if (!mgc)
        return -1;
    for (;;) {
        sleep(TZSET_PERIOD_S);
        tzset();
    }
}
