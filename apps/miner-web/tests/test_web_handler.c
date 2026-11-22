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
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <civetweb.h>
#include "stub_civetweb.h"
#include "telemetry.h"
#include "router.h"
#include "webtoken.h"
#include "tlscert.h"
#include "osif_logpath.h"

#undef MINER_LOG_PATH
#undef MINER_OP_LOG_PATH
#undef MINER_WEB_LOG_PATH
#define MINER_LOG_PATH     "handler_log/miner.log"
#define MINER_OP_LOG_PATH  "handler_log/miner_op.log"
#define MINER_WEB_LOG_PATH "handler_weblog/web.log"

#include "web_logfmt.h"
#undef _DEFAULT_SOURCE

#define HANDLER_COOKIE  "auth=000000008c6976e5b5410415bde908bd"
#define QUERY_LOCATION  "\r\nLocation: https://192.168.1.5/dashboard.html?lang=en&x=%2F1\r\n"
#define TST_MONO_BASE_S 1000 /* CLOCK_MONOTONIC at g_mono_off_ms 0, s */
/* civetweb fixes each worker's stack at build time (USE_STACK_SIZE, 100 KiB on
 * the target); a log export must leave most of it free. */
#define EXPORT_STACK (64 * 1024)
/* Hammer the dashboard; count replies whose fields are empty or mixed. */
#define DASH_READERS 4

static int64_t     g_mono_off_ms;
static int         tst_rest_up;
static long long   tst_fstat_skew; /* bytes added to every st_size fstat reports, may be negative */
static long long   tst_sent;       /* bytes the glue passed to mg_write */
static long long   tst_clen = -1; /* Content-Length of the last header the glue sent, -1 for none */
static const char *tst_vanish;
static int         tst_write_fail;
static int         tst_drop_serve_name;
static int         tst_tls = 1;          /* civetweb is built with TLS */
static const char *tst_cert_fail;        /* certificate path that cannot be made, "*" for all */
static int         tst_cert_tries;       /* web_tls_cert_ensure calls */
static int         tst_https_up;         /* mg_get_server_ports reports an HTTPS port */
static struct mg_context *tst_mgc;       /* what mg_start returns */
static const char        *tst_opt_ports; /* listening_ports passed to mg_start */
static const char        *tst_opt_cert;  /* ssl_certificate passed to mg_start */
static int                tst_starts, tst_stops; /* mg_start and mg_stop calls */

static char registered[33]; /* token registered by mw_qr_login_begin */
/* 1 = QR login approved; mw_qr_login_take calls; mw_qr_login_begin result */
static int approved, claims, begin_ok = 1, begin_calls;
static int torn, replies; /* torn dashboard replies; all 200 replies */
/* slow_refresh: rebuild the whole snapshot the way the real refresh does
 * (wipe, then fill over a delay), stamping every field with one round. */
static int slow_refresh;
/* refreshes running now, their peak, refreshes started */
static int in_refresh, max_in_refresh, refresh_round;
/* guards the refresh counters and torn */
static pthread_mutex_t refresh_mu = PTHREAD_MUTEX_INITIALIZER;

static int      tst_open(const char *path, int flags);
static int      tst_fstat(int fd, struct stat *st);
static int      tst_mg_write(struct mg_connection *c, const void *buf, size_t len);
static int      tst_mg_printf(struct mg_connection *c, const char *fmt, ...);
static int      tst_web_route(const web_request *req, const web_ctx *ctx, char *out, size_t cap,
             web_response *resp);
static unsigned tst_check_feature(unsigned feature);
static int      tst_cert_ensure(const char *path, const char *common_name);
static int tst_server_ports(const struct mg_context *ctx, int size, struct mg_server_port *ports);

static int tst_clock_gettime(clockid_t id, struct timespec *ts)
{
    if (id != CLOCK_MONOTONIC)
        return clock_gettime(id, ts);
    ts->tv_sec  = (time_t)(TST_MONO_BASE_S + g_mono_off_ms / 1000);
    ts->tv_nsec = (long)(g_mono_off_ms % 1000 * 1000000);
    return 0;
}

#define clock_gettime                tst_clock_gettime
#define web_log                      glue_web_log /* renames the web_log of civetweb_glue.c */
#define open(path, flags)            tst_open((path), (flags))
#define fstat(fd, st)                tst_fstat((fd), (st)) /* glue fstat, size + tst_fstat_skew */
#define mg_write(c, b, n)            tst_mg_write((c), (b), (n)) /* glue mg_write, tst_sent += n */
#define mg_printf(c, ...)            tst_mg_printf((c), __VA_ARGS__) /* records Content-Length */
#define web_route(q, x, o, n, r)     tst_web_route((q), (x), (o), (n), (r))
#define mg_check_feature(f)          tst_check_feature(f)
#define web_tls_cert_ensure(p, n)    tst_cert_ensure((p), (n))
#define mg_get_server_ports(x, n, p) tst_server_ports((x), (n), (p))
#include "../web/civetweb_glue.c"
#undef mg_get_server_ports
#undef web_tls_cert_ensure
#undef mg_check_feature
#undef web_route
#undef mg_printf
#undef mg_write
#undef fstat
#undef open
#undef web_log
#undef clock_gettime
#include "../web/logexport.c"

static unsigned tst_check_feature(unsigned feature)
{
    return tst_tls ? mg_check_feature(feature) : 0;
}

static int tst_cert_ensure(const char *path, const char *common_name)
{
    (void)common_name;
    tst_cert_tries++;
    if (tst_cert_fail && (!strcmp(tst_cert_fail, "*") || !strcmp(tst_cert_fail, path)))
        return -1;
    return 0;
}

static int tst_server_ports(const struct mg_context *ctx, int size, struct mg_server_port *ports)
{
    int n = 0;

    (void)ctx;
    if (size < 3)
        return 0;
    memset(ports, 0, 3 * sizeof *ports);
    ports[n++].port = 80;
    if (tst_https_up) {
        ports[n].port     = 443;
        ports[n++].is_ssl = 1;
    }
    if (tst_rest_up) {
        ports[n].port     = 8443;
        ports[n++].is_ssl = 1;
    }
    return n;
}

static int tst_open(const char *path, int flags)
{
    if (tst_vanish && !strcmp(path, tst_vanish))
        remove(path);
    return open(path, flags);
}

static int tst_web_route(const web_request *req, const web_ctx *ctx, char *out, size_t cap,
        web_response *resp)
{
    int rc = web_route(req, ctx, out, cap, resp);

    if (tst_drop_serve_name)
        resp->serve_name[0] = '\0';
    return rc;
}

static int tst_fstat(int fd, struct stat *st)
{
    int rc = fstat(fd, st);
    if (rc == 0)
        st->st_size += tst_fstat_skew;
    return rc;
}

static int tst_mg_write(struct mg_connection *c, const void *buf, size_t len)
{
    if (tst_write_fail)
        return -1;
    tst_sent += (long long)len;
    return mg_write(c, buf, len);
}

static int tst_mg_printf(struct mg_connection *c, const char *fmt, ...)
{
    char        line[512];
    const char *cl, *body;
    va_list     ap;
    int         n;

    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    cl       = strstr(line, "Content-Length: ");
    tst_clen = cl ? atoll(cl + 16) : -1;
    n        = mg_printf(c, "%s", line);
    body     = strstr(line, "\r\n\r\n");
    if (body && body[4])
        mg_write(c, body + 4, strlen(body + 4));
    return n;
}

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");

    assert(f);
    fputs(text, f);
    fclose(f);
}

static size_t slurp(const char *path, char *buf, size_t cap)
{
    FILE  *f = fopen(path, "rb");
    size_t n = f ? fread(buf, 1, cap - 1, f) : 0;

    if (f)
        fclose(f);
    buf[n] = '\0';
    return n;
}

static const char *start_errs(char *buf, size_t cap)
{
    fflush(stderr);
    slurp("handler_start.txt", buf, cap);
    assert(truncate("handler_start.txt", 0) == 0);
    return buf;
}

static int op_lines(char *buf, size_t cap)
{
    int n = 0;

    slurp(MINER_OP_LOG_PATH, buf, cap);
    for (const char *p = buf; (p = strstr(p, "[OP] ")) != NULL; p++)
        n++;
    return n;
}

static const char *runlog_token(void)
{
    static char tok[WEB_TOKEN_MAX];
    mc_webcred  creds;

    if (!tok[0]) {
        assert(web_creds_load(g_systemcfg, &creds) == 0 &&
                web_token_issue(&creds, tok, sizeof tok) == 0);
        memset(&creds, 0, sizeof creds);
    }
    return tok;
}

static int runlog(const char *name, stub_rsp *rsp)
{
    stub_req req = { .method = "POST",
        .uri                 = "/runlog",
        .token               = runlog_token(),
        .is_ssl              = 1,
        .body                = name,
        .body_len            = strlen(name),
        .unread              = strlen(name),
        .content_length      = (long long)strlen(name) };

    return stub_invoke("test", &req, rsp);
}

struct mg_context *mg_start(const struct mg_callbacks *cb, void *data, const char **opts)
{
    (void)cb;
    (void)data;
    tst_starts++;
    tst_opt_ports = tst_opt_cert = NULL;
    for (size_t i = 0; opts[i]; i += 2) {
        if (!strcmp(opts[i], "listening_ports"))
            tst_opt_ports = opts[i + 1];
        else if (!strcmp(opts[i], "ssl_certificate"))
            tst_opt_cert = opts[i + 1];
    }
    return tst_mgc;
}
void mg_stop(struct mg_context *ctx)
{
    assert(ctx == tst_mgc);
    tst_stops++;
}

int mw_telemetry_refresh(mw_telemetry *t)
{
    if (!slow_refresh) {
        strcpy(t->dna, "0201000012345678");
        t->activation = 1;
        return 0;
    }
    pthread_mutex_lock(&refresh_mu);
    if (++in_refresh > max_in_refresh)
        max_in_refresh = in_refresh;
    int round = ++refresh_round;
    pthread_mutex_unlock(&refresh_mu);
    memset(t, 0, sizeof *t);
    t->dash.hwtype      = t->hwtype;
    t->dash.sys_status  = t->sys_status;
    t->dash.elapsed     = t->elapsed;
    t->dash.workingmode = t->workingmode;
    nanosleep(&(struct timespec){ .tv_nsec = 20000000 }, NULL);
    snprintf(t->hwtype, MW_STR, "r%d", round);
    snprintf(t->sys_status, MW_STR, "r%d", round);
    snprintf(t->elapsed, MW_STR, "r%d", round);
    snprintf(t->workingmode, MW_STR, "r%d", round);
    strcpy(t->dna, "0201000012345678");
    t->activation = 1;
    pthread_mutex_lock(&refresh_mu);
    in_refresh--;
    pthread_mutex_unlock(&refresh_mu);
    return 0;
}
int mw_qr_login_begin(const char *token)
{
    begin_calls++;
    strcpy(registered, token);
    return begin_ok;
}
int mw_qr_login_take(const char *token)
{
    claims++;
    if (!strcmp(token, registered) && approved) {
        approved = 0;
        return 1;
    }
    return 0;
}
int mw_reboot_web(void)
{
    return 0;
}
int mw_time_get(char *tz, size_t cap)
{
    if (cap)
        tz[0] = 0;
    return -1;
}
int mw_time_set(const char *tz, long epoch)
{
    (void)tz;
    (void)epoch;
    return 0;
}
int mw_tz_from_link(const char *link, char *tz, size_t cap)
{
    (void)link;
    if (cap)
        tz[0] = 0;
    return -1;
}

static int export_handler(struct mg_connection *c, void *cbdata)
{
    return serve_logfile(c, (const char *)cbdata, 1);
}

static time_t mono_s(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

static void *dash_reader(void *arg)
{
    time_t end = *(const time_t *)arg;
    while (mono_s() < end) {
        stub_req req = { .method = "GET", .uri = "/api/v1/dashboard", .content_length = -1 };
        stub_rsp rsp;
        char     v[4][16];
        if (stub_invoke("test", &req, &rsp) != 200)
            continue;
        __atomic_add_fetch(&replies, 1, __ATOMIC_RELAXED);
        if (sscanf(rsp.body,
                    "{\"hwtype\":\"%15[^\"]\",\"sys_status\":\"%15[^\"]\","
                    "\"elapsed\":\"%15[^\"]\",\"workingmode\":\"%15[^\"]\"",
                    v[0], v[1], v[2], v[3]) != 4 ||
                strcmp(v[0], v[1]) || strcmp(v[0], v[2]) || strcmp(v[0], v[3])) {
            pthread_mutex_lock(&refresh_mu);
            torn++;
            pthread_mutex_unlock(&refresh_mu);
        }
    }
    return NULL;
}

static void *export_thread(void *arg)
{
    stub_req req = { .method = "GET", .uri = "/export", .content_length = -1 };
    stub_invoke("export", &req, (stub_rsp *)arg);
    return NULL;
}
static long access_log_bytes(const char *level, const char *line_level)
{
    FILE *lf = tmpfile();
    assert(lf);
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        setenv("MINER_LOG_LEVEL", level, 1);
        g_access_log = lf;
        glue_web_log(line_level, "access %d", 1);
        _exit(0);
    }
    int st;
    assert(waitpid(pid, &st, 0) == pid && WIFEXITED(st) && WEXITSTATUS(st) == 0);
    fseek(lf, 0, SEEK_END);
    long n = ftell(lf);
    fclose(lf);
    return n;
}
int main(void)
{
    mkdir("handler_weblog", 0755);
    remove(MINER_WEB_LOG_PATH);
    assert(web_log_web_fd() >= 0);
    assert(access_log_bytes("INFO", "INFO") > 0);
    assert(access_log_bytes("WARN", "INFO") == 0);
    assert(access_log_bytes("WARN", "WARN") > 0);
    FILE *f = fopen("handler.ini", "w");
    assert(f);
    fputs("[syscfg]\nwebuser=admin\n"
          "webpass=8c6976e5b5410415bde908bd4dee15dfb167a9c873fc4bb8a81f6f2ab448a918\n",
            f);
    fclose(f);
    g_systemcfg = "handler.ini";
    stub_reset();
    mg_set_request_handler(NULL, "test", on_request, NULL);
    stub_req req = { .method = "GET", .uri = "/api/v1/activation", .content_length = -1 };
    stub_rsp rsp;
    assert(g_activated == WEB_ACTIVATED_UNKNOWN);
    assert(stub_invoke("test", &req, &rsp) == 200 && strstr(rsp.body, "\"activated\":1") &&
            !strstr(rsp.body, "starting"));
    req.uri = "/api/v1/dashboard";
    assert(stub_invoke("test", &req, &rsp) == 200 && rsp.body[0] == '{');
    registered[0] = 0;
    req.uri       = "/api/v1/authx";
    assert(stub_invoke("test", &req, &rsp) == 404 && registered[0] == 0);
    req.uri = "/api/v1/auth/x";
    assert(stub_invoke("test", &req, &rsp) == 404 && registered[0] == 0);
    req.method = "POST";
    req.uri    = "/api/v1/auth";
    assert(stub_invoke("test", &req, &rsp) == 405 && registered[0] == 0 && begin_calls == 0 &&
            strstr(rsp.body, "\"Code\":505,\"Msg\":\"Method not allowed\"") &&
            !strstr(rsp.head, "Cache-Control"));
    req.method = "HEAD";
    assert(stub_invoke("test", &req, &rsp) == 405 && registered[0] == 0 && begin_calls == 0);
    req.method = "GET";
    assert(stub_invoke("test", &req, &rsp) == 200 && begin_calls == 1);
    assert(strlen(registered) == 32 && strstr(rsp.body, registered) &&
            strstr(rsp.head, "\r\nCache-Control: no-store\r\n"));
    char session_body[64];
    snprintf(session_body, sizeof session_body, "session=%s", registered);
    req.uri  = "/api/v1/qr-login";
    approved = 1;
    assert(stub_invoke("test", &req, &rsp) == 405 && claims == 0);
    req.method   = "POST";
    req.body     = "session=ffffffffffffffffffffffffffffffff";
    req.body_len = req.unread = (size_t)(req.content_length = strlen(req.body));
    assert(stub_invoke("test", &req, &rsp) == 200 && !strcmp(rsp.body, "{}") && approved == 1);
    int claims_before = claims;
    req.uri           = "/api/v1/qr-loginx";
    req.body          = session_body;
    req.body_len = req.unread = (size_t)(req.content_length = strlen(req.body));
    assert(stub_invoke("test", &req, &rsp) == 404 && approved == 1 && claims == claims_before);
    req.uri      = "/api/v1/qr-login";
    req.body_len = req.unread = (size_t)(req.content_length = strlen(req.body));
    assert(stub_invoke("test", &req, &rsp) == 200 && approved == 0);
    assert(strstr(rsp.body, "\"auth\":\"") && !strstr(rsp.body, "code"));
    assert(strstr(rsp.head, "\r\nSet-Cookie: auth=") &&
            strstr(rsp.head, "; HttpOnly; SameSite=Strict\r\n"));
    g_mono_off_ms += QR_BEGIN_GAP_MS;
    req.method         = "GET";
    req.uri            = "/api/v1/auth";
    req.content_length = -1;
    req.body           = NULL;
    req.body_len = req.unread = 0;
    begin_ok                  = 0;
    assert(stub_invoke("test", &req, &rsp) == 503 &&
            !strncmp(rsp.head, "HTTP/1.1 503 Service Unavailable\r\n", 34) &&
            strstr(rsp.head, "\r\n\r\nQR login unavailable, try again shortly"));
    begin_ok           = 1;
    req.method         = "POST";
    req.uri            = "/api/v1/pools";
    req.content_length = 4096;
    assert(stub_invoke("test", &req, &rsp) == 413 &&
            !strncmp(rsp.head, "HTTP/1.1 413 Request Entity Too Large\r\n", 39));
    req.content_length = 10;
    req.unread         = 3;
    req.body           = "abc";
    req.body_len       = 3;
    assert(stub_invoke("test", &req, &rsp) == 400);
    req.content_length = 3;
    req.body           = "a\0b";
    assert(stub_invoke("test", &req, &rsp) == 400);
    req.method     = "DELETE";
    req.uri        = "/api/v1/session";
    req.body       = "abc";
    req.read_chunk = 1;
    assert(stub_invoke("test", &req, &rsp) == 200 && rsp.reads == 3 &&
            strstr(rsp.head, "Set-Cookie: auth=; Path=/; HttpOnly; Max-Age=0"));
    req.method         = "HEAD";
    req.content_length = -1;
    req.body           = NULL;
    req.body_len = req.unread = 0;
    req.read_chunk            = 0;
    req.cookie                = HANDLER_COOKIE;
    assert(stub_invoke("test", &req, &rsp) == 405 && !strstr(rsp.head, "Set-Cookie") &&
            strstr(rsp.body, "\"Code\":505,"));
    req.cookie = NULL;

    req.method        = "GET";
    req.uri           = "/api/v1/summary";
    req.authorization = "Basic YWRtaW46YWRtaW4=";
    assert(stub_invoke("test", &req, &rsp) == 401 &&
            !strncmp(rsp.head, "HTTP/1.1 401 Unauthorized\r\n", 27) &&
            strstr(rsp.body, "\"Code\":602,\"Msg\":\"Not logged in\"") &&
            !strstr(rsp.head, "WWW-Authenticate"));
    req.authorization = NULL;
    req.cookie        = HANDLER_COOKIE;
    assert(stub_invoke("test", &req, &rsp) == 504 &&
            !strncmp(rsp.head, "HTTP/1.1 504 Gateway Timeout\r\n", 30) &&
            strstr(rsp.body, "\"Code\":500,\"Msg\":\"Command timeout\""));
    req.cookie = NULL;
    {
        static const char tbody[] = "{\"command\":\"token\",\"user\":\"admin\","
                                    "\"password\":\"admin\"}";
        stub_req          t       = { .method = "POST",
                           .uri               = "/api/v1/token",
                           .content_type      = "application/json",
                           .body              = tbody,
                           .body_len          = sizeof tbody - 1,
                           .unread            = sizeof tbody - 1,
                           .content_length    = sizeof tbody - 1,
                           .is_ssl            = 1,
                           .remote_addr       = "192.168.1.30" };
        char              token[WEB_TOKEN_MAX];
        const char       *p;
        size_t            n;

        stub_req b = { .method = "GET",
            .uri               = "/api/v1/summary",
            .content_length    = -1,
            .authorization     = "Basic YWRtaW46d3Jvbmc=",
            .is_ssl            = 1,
            .remote_addr       = "192.168.1.30" };
        for (int i = 0; i < 6; i++)
            assert(stub_invoke("test", &b, &rsp) == 401);
        b.uri           = "/runlog";
        b.method        = "POST";
        b.authorization = "Basic YWRtaW46YWRtaW4=";
        for (int i = 0; i < 6; i++)
            assert(stub_invoke("test", &b, &rsp) == 401);
        assert(stub_invoke("test", &t, &rsp) == 200 &&
                (p = strstr(rsp.body, "\"Token\":\"")) != NULL);
        p += 9;
        n = strcspn(p, "\"");
        assert(p[n] == '"' && n < sizeof token);
        memcpy(token, p, n);
        token[n] = '\0';
        t        = (stub_req){ .method = "GET",
                   .uri                = "/api/v1/summary",
                   .content_length     = -1,
                   .token              = token,
                   .is_ssl             = 1 };
        assert(stub_invoke("test", &t, &rsp) == 504 && strstr(rsp.body, "\"Code\":500,"));
        token[n - 2] = token[n - 2] == 'A' ? 'B' : 'A';
        assert(stub_invoke("test", &t, &rsp) == 401 &&
                !strncmp(rsp.head, "HTTP/1.1 401 Unauthorized\r\n", 27) &&
                strstr(rsp.body,
                        "\"Code\":602,\"Msg\":\"Token expired, request a new one\"}],\"id\":1}"));
        t = (stub_req){
            .method = "DELETE", .uri = "/api/v1/token", .content_length = -1, .is_ssl = 1
        };
        assert(stub_invoke("test", &t, &rsp) == 405);
    }

    {
        stub_req q = { .method = "GET",
            .uri               = "/api/v1/auth",
            .content_length    = -1,
            .remote_addr       = "192.168.1.20" };
        char     tok20[33], tok21[33], qbody[64];
        int      calls = begin_calls;

        g_mono_off_ms += QR_BEGIN_GAP_MS;
        assert(stub_invoke("test", &q, &rsp) == 200 && begin_calls == calls + 1);
        strcpy(tok20, registered);
        assert(strstr(rsp.body, tok20));
        for (int i = 0; i < 5; i++) {
            assert(stub_invoke("test", &q, &rsp) == 200 && begin_calls == calls + 1);
            assert(strstr(rsp.body, "\"auth\":\"") && !strstr(rsp.body, "\"auth\":\"\"") &&
                    strstr(rsp.body, "\"session\":\"\""));
        }
        q.remote_addr = "192.168.1.21";
        assert(stub_invoke("test", &q, &rsp) == 200 && begin_calls == calls + 2);
        strcpy(tok21, registered);
        assert(strcmp(tok20, tok21) && strstr(rsp.body, tok21));
        q.remote_addr = "192.168.1.20";
        g_mono_off_ms += 59000;
        assert(stub_invoke("test", &q, &rsp) == 200 && begin_calls == calls + 2 &&
                strstr(rsp.body, "\"session\":\"\""));
        g_mono_off_ms += 1000;
        assert(stub_invoke("test", &q, &rsp) == 200 && begin_calls == calls + 3);
        assert(!strstr(rsp.body, tok20));

        stub_req p = { .method = "POST", .uri = "/api/v1/qr-login", .remote_addr = "192.168.1.20" };
        int      c0 = claims;
        snprintf(qbody, sizeof qbody, "session=%s", tok20);
        p.body     = qbody;
        p.body_len = p.unread = (size_t)(p.content_length = strlen(qbody));
        assert(stub_invoke("test", &p, &rsp) == 200 && claims == c0);
        snprintf(qbody, sizeof qbody, "session=%s", tok21);
        p.body_len = p.unread = (size_t)(p.content_length = strlen(qbody));
        assert(stub_invoke("test", &p, &rsp) == 200 && claims == c0);
        p.remote_addr = "192.168.1.21";
        p.body_len = p.unread = (size_t)(p.content_length = strlen(qbody));
        assert(stub_invoke("test", &p, &rsp) == 200 && claims == c0 + 1);
    }

    req = (stub_req){
        .method = "GET", .uri = "/api/v1/activation", .content_length = -1, .host = "evil.example"
    };
    assert(stub_invoke("test", &req, &rsp) == 403 && rsp.status == 403);
    req.host = "192.168.1.5:80";
    assert(stub_invoke("test", &req, &rsp) == 200);
    req.host = "[fe80::1]";
    assert(stub_invoke("test", &req, &rsp) == 200);
    req.host = "localhost";
    assert(stub_invoke("test", &req, &rsp) == 200);
    req = (stub_req){ .method = "POST",
        .uri                  = "/api/v1/pools",
        .content_length       = -1,
        .host                 = "192.168.1.5",
        .origin               = "http://evil.example" };
    assert(stub_invoke("test", &req, &rsp) == 403 && rsp.reads == 0);
    req.origin = "http://192.168.1.5";
    assert(stub_invoke("test", &req, &rsp) == 401);
    req.origin = NULL;
    assert(stub_invoke("test", &req, &rsp) == 401);
    req.referer = "http://evil.example/x.html";
    assert(stub_invoke("test", &req, &rsp) == 403);
    req.referer = "http://192.168.1.5/pools.html";
    assert(stub_invoke("test", &req, &rsp) == 401);
    {
        static char wl[65536];
        size_t      wn = slurp(MINER_WEB_LOG_PATH, wl, sizeof wl - 1);

        wl[wn] = '\0';
        assert(strstr(wl,
                "] [WARN] test_web_handler: POST /api/v1/pools refused (HTTP 403): "
                "cross-origin request refused\n"));
        assert(strstr(wl,
                "] [WARN] test_web_handler: POST /api/v1/pools refused (HTTP 413): "
                "request body too large\n"));
        assert(strstr(wl,
                "] [WARN] test_web_handler: saving pool settings from the web page "
                "refused: not logged in\n"));
        assert(strstr(wl, "] [INFO] test_web_handler: web logout\n"));
    }
    req = (stub_req){ .method = "GET",
        .uri                  = "/api/v1/system/reboot",
        .content_length       = -1,
        .cookie               = HANDLER_COOKIE };
    assert(stub_invoke("test", &req, &rsp) == 405);

    /* /runlog: method, token login, Origin and name gates through on_request */
    static char long_name[128];
    memset(long_name, 'a', 100);
    strcpy(long_name + 100, ".log");
    req = (stub_req){ .method = "POST",
        .uri                  = "/runlog",
        .body                 = "miner.log",
        .body_len             = 9,
        .unread               = 9,
        .content_length       = 9,
        .token                = runlog_token() };
    assert(stub_invoke("test", &req, &rsp) == 403 && strstr(rsp.body, "HTTPS required"));
    req = (stub_req){ .method = "GET",
        .uri                  = "/runlog",
        .content_length       = -1,
        .token                = runlog_token(),
        .is_ssl               = 1 };
    assert(stub_invoke("test", &req, &rsp) == 405);
    req = (stub_req){ .method = "POST",
        .uri                  = "/runlog",
        .body                 = "miner.log",
        .body_len             = 9,
        .unread               = 9,
        .content_length       = 9,
        .is_ssl               = 1 };
    assert(stub_invoke("test", &req, &rsp) == 401 && !strcmp(rsp.body, "unauthorized") &&
            !strstr(rsp.head, "WWW-Authenticate"));
    req.authorization = "Basic YWRtaW46YWRtaW4=";
    assert(stub_invoke("test", &req, &rsp) == 401 && !strcmp(rsp.body, "unauthorized") &&
            !strstr(rsp.head, "WWW-Authenticate"));
    req.authorization = NULL;
    req.cookie        = HANDLER_COOKIE;
    assert(stub_invoke("test", &req, &rsp) == 401 && !strcmp(rsp.body, "unauthorized"));
    req.cookie = NULL;
    req.token  = runlog_token();
    req.host   = "192.168.1.5";
    req.origin = "http://evil.example";
    assert(stub_invoke("test", &req, &rsp) == 403 && rsp.reads == 0);
    req.host     = NULL;
    req.origin   = NULL;
    req.body     = "bogus";
    req.body_len = req.unread = 5;
    req.content_length        = 5;
    assert(stub_invoke("test", &req, &rsp) == 404 && !strcmp(rsp.body, "Not found\n"));
    req.body     = long_name;
    req.body_len = req.unread = strlen(long_name);
    req.content_length        = (long long)strlen(long_name);
    assert(stub_invoke("test", &req, &rsp) == 404 && !strcmp(rsp.body, "Not found\n"));

    char op[4096], errs[8192];
    int  saved = dup(2), efd;
    mkdir("handler_log", 0755);
    put("handler_log/miner_op.log.1", "old\n");
    put("handler_log/miner_op.log", "new\n");
    put("handler_log/web.log", "0123456789");
    put("handler_log/\xc3\xa9.log", "x\n");
    fflush(stderr);
    efd = open("handler_log/stderr.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(efd >= 0 && dup2(efd, 2) == 2);
    close(efd);

    assert(runlog("miner_op.log", &rsp) == 200 && rsp.status == 200 &&
            !strcmp(rsp.body, "old\nnew\n"));
    assert(op_lines(op, sizeof op) == 1 &&
            strstr(op, "file miner_op.log downloaded from the web page\n"));

    tst_vanish = "handler_log/web.log";
    assert(runlog("web.log", &rsp) == 404 && rsp.status == 404 && !strcmp(rsp.body, "Not found\n"));
    tst_vanish = NULL;
    assert(op_lines(op, sizeof op) == 1);

    assert(runlog("\xc3\xa9.log", &rsp) == 404 && !strcmp(rsp.body, "Not found\n"));
    assert(op_lines(op, sizeof op) == 1);

    put("handler_tokenkey", "secret-token-key\n");
    assert(symlink("../handler_tokenkey", "handler_log/miner.log") == 0);
    assert(runlog("miner.log", &rsp) == 404 && !strcmp(rsp.body, "Not found\n") &&
            !strstr(rsp.body, "secret-token-key"));
    remove("handler_log/miner.log");
    assert(runlog("%2e%2e%2ffactory%2fminer-web.tokenkey.log", &rsp) == 404 &&
            !strcmp(rsp.body, "Not found\n"));
    assert(op_lines(op, sizeof op) == 1);
    remove("handler_tokenkey");

    put("handler_log/web.log", "0123456789");
    tst_fstat_skew = 1;
    assert(runlog("web.log", &rsp) == 500 && rsp.status == 200 && !strcmp(rsp.body, "0123456789"));
    tst_fstat_skew = 0;
    assert(op_lines(op, sizeof op) == 1);

    tst_write_fail = 1;
    assert(runlog("web.log", &rsp) == 500 && rsp.status == 200 && !rsp.body_len);
    tst_write_fail = 0;
    assert(op_lines(op, sizeof op) == 1);

    tst_drop_serve_name = 1;
    assert(runlog("web.log", &rsp) == 200 && !strcmp(rsp.body, "0123456789"));
    tst_drop_serve_name = 0;
    assert(op_lines(op, sizeof op) == 2 &&
            strstr(op, "file handler_log/web.log downloaded from the web page\n"));

    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    slurp("handler_log/stderr.txt", errs, sizeof errs);
    assert(strstr(errs, "handler_log/web.log download refused: no file to send\n"));
    assert(strstr(errs,
            "log export of handler_log/web.log ended 1 bytes short of the 11 "
            "announced"));
    assert(strstr(errs,
            "log export of handler_log/web.log stopped: the client closed the "
            "connection\n"));
    remove("handler_log/stderr.txt");
    remove("handler_log/web.log");
    remove("handler_log/\xc3\xa9.log");
    remove("handler_log/miner_op.log");
    remove("handler_log/miner_op.log.1");
    rmdir("handler_log");
    remove(MINER_WEB_LOG_PATH);
    rmdir("handler_weblog");

    /* concurrent dashboard readers never see a half-rebuilt snapshot */
    slow_refresh       = 1;
    req.method         = "GET";
    req.uri            = "/api/v1/dashboard";
    req.content_length = -1;
    while (refresh_round == 0) {
        assert(stub_invoke("test", &req, &rsp) == 200);
        nanosleep(&(struct timespec){ .tv_nsec = 100000000 }, NULL);
    }
    time_t    end = mono_s() + 3;
    pthread_t rd[DASH_READERS];
    for (int i = 0; i < DASH_READERS; i++)
        assert(pthread_create(&rd[i], NULL, dash_reader, &end) == 0);
    for (int i = 0; i < DASH_READERS; i++)
        assert(pthread_join(rd[i], NULL) == 0);
    slow_refresh = 0;
    fprintf(stderr, "dashboard: %d refreshes, %d of %d replies torn, max %d concurrent\n",
            refresh_round, torn, replies, max_in_refresh);
    assert(refresh_round >= 2 && torn == 0 && max_in_refresh == 1);

    /* log export on a small worker stack, rotated segment first */
    f = fopen("web.log.1", "w");
    assert(f);
    fputs("old\n", f);
    fclose(f);
    f = fopen("web.log", "w");
    assert(f);
    fputs("new\n", f);
    fclose(f);
    mg_set_request_handler(NULL, "export", export_handler, (void *)"web.log");
    pthread_attr_t attr;
    pthread_t      tid;
    assert(pthread_attr_init(&attr) == 0);
    assert(pthread_attr_setstacksize(&attr, EXPORT_STACK) == 0);
    assert(pthread_create(&tid, &attr, export_thread, &rsp) == 0);
    assert(pthread_join(tid, NULL) == 0);
    pthread_attr_destroy(&attr);
    assert(rsp.status == 200 && !strcmp(rsp.body, "old\nnew\n"));
    remove("web.log.1");
    remove("web.log");

    f = fopen("web.log", "w");
    assert(f);
    fputs("0123456789", f);
    fclose(f);
    req            = (stub_req){ .method = "GET", .uri = "/export", .content_length = -1 };
    tst_fstat_skew = -4;
    tst_sent       = 0;
    tst_clen       = -1;
    assert(stub_invoke("export", &req, &rsp) == 200);
    assert(tst_clen == 6 && tst_sent == tst_clen && !strcmp(rsp.body, "012345"));

    f = fopen("web.log.1", "w");
    assert(f);
    fputs("old\n", f);
    fclose(f);
    f = fopen("web.log", "w");
    assert(f);
    fputs("new\n", f);
    fclose(f);
    tst_fstat_skew = -1;
    tst_sent       = 0;
    tst_clen       = -1;
    assert(stub_invoke("export", &req, &rsp) == 200);
    assert(tst_clen == 6 && tst_sent == tst_clen && !strcmp(rsp.body, "oldnew"));

    tst_fstat_skew = 1;
    tst_sent       = 0;
    tst_clen       = -1;
    assert(stub_invoke("export", &req, &rsp) == 500 && rsp.status == 200);
    assert(tst_clen == 10 && tst_sent == 4 && !strcmp(rsp.body, "old\n"));
    remove("web.log.1");

    f = fopen("web.log", "w");
    assert(f);
    fputs("0123456789", f);
    fclose(f);
    tst_fstat_skew = 4;
    tst_sent       = 0;
    tst_clen       = -1;
    assert(stub_invoke("export", &req, &rsp) == 500 && rsp.status == 200);
    assert(tst_clen == 14 && tst_sent == 10 && !strcmp(rsp.body, "0123456789"));

    f = fopen("web.log", "w");
    assert(f);
    for (int i = 0; i < LOG_EXPORT_CHUNK + 4000; i++)
        fputc('a' + i % 26, f);
    fclose(f);
    tst_fstat_skew = -1000;
    tst_sent       = 0;
    tst_clen       = -1;
    assert(stub_invoke("export", &req, &rsp) == 200);
    assert(tst_clen == LOG_EXPORT_CHUNK + 3000 && tst_sent == tst_clen);

    tst_fstat_skew = 0;
    remove("web.log");

    /* only the listed static files reach civetweb's file server */
    static const char *const served[] = {
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
    static const char *const hidden[] = {
        "/THIRD-PARTY.md",
        "/index.html",
        "/favicon.ico",
        "/README.md",
        "/LOGIN.HTML",
        "/login.html/",
        "/login.htm",
        "/login.html.gz",
        "/THIRD-PARTY%2Emd",
        "/..%2fTHIRD-PARTY.md",
        "/login.html%00",
        "/assets/login.html",
        "/www/login.html",
        "/js-sha256/sha256.min.js",
        "/api/v1",
        "/cgi-bin",
        "/cgi-bin/luci",
        "/cgi-bin/upload.aup",
        "/a/b/reboot.cgi",
        "/ws",
        "/upload",
        "/miner-web.pem",
        "/../../data/factory/miner-web.tokenkey",
        "/%2e%2e/%2e%2e/tmp/miner-web.tokenkey",
        "/data/userconfig/systemcfg.ini",
    };
    static const char *const routed[][2] = {
        { "/api/v1/dashboard", "/api/v1/" },
        { "/api/v1/session", "/api/v1/" },
        { "/api/v1/upgrade", "/api/v1/upgrade" },
        { "/api/v1/firmware", "/api/v1/" },
        { "/runlog", "/runlog" },
        { "/runlogs", "/runlog" },
    };
    static const char *const removed[] = { "login", "logout", "qr_login", "is_login", "dashboard",
        "get_dashboard", "status", "get_activation", "get_auth", "poolconfig", "pools",
        "timezoneconf", "reboot" };
    static const char *const removed_fmt[] = { "/%s.cgi", "/cgi-bin/%s", "/cgi-bin/%s.cgi" };
    stub_reset();
    handlers_register(NULL);
    req = (stub_req){ .content_length = -1 };
    for (size_t i = 0; i < sizeof served / sizeof served[0]; i++) {
        assert(!strcmp(stub_route(served[i]), "/"));
        req.uri    = served[i];
        req.method = "GET";
        assert(stub_invoke("/", &req, &rsp) == 0 && rsp.status == 0);
        req.method = "HEAD";
        assert(stub_invoke("/", &req, &rsp) == 0 && rsp.status == 0);
        req.method = "POST";
        assert(stub_invoke("/", &req, &rsp) == 404);
        req.method = "PUT";
        assert(stub_invoke("/", &req, &rsp) == 404);
    }
    req.method = "GET";
    for (size_t i = 0; i < sizeof hidden / sizeof hidden[0]; i++) {
        assert(!strcmp(stub_route(hidden[i]), "/"));
        req.uri = hidden[i];
        assert(stub_invoke("/", &req, &rsp) == 404 && rsp.status == 404);
    }
    for (size_t i = 0; i < sizeof routed / sizeof routed[0]; i++)
        assert(!strcmp(stub_route(routed[i][0]), routed[i][1]));
    for (size_t i = 0; i < sizeof removed / sizeof removed[0]; i++) {
        for (size_t j = 0; j < sizeof removed_fmt / sizeof removed_fmt[0]; j++) {
            char uri[64];

            snprintf(uri, sizeof uri, removed_fmt[j], removed[i]);
            assert(!strcmp(stub_route(uri), "/"));
            req = (stub_req){ .method = "GET",
                .uri                  = uri,
                .content_length       = -1,
                .cookie               = HANDLER_COOKIE,
                .is_ssl               = 1 };
            assert(stub_invoke("/", &req, &rsp) == 404 && !strcmp(rsp.body, "Not found\n") &&
                    !strstr(rsp.head, "Set-Cookie") && !strstr(rsp.head, "Location"));
            req.method = "POST";
            assert(stub_invoke("/", &req, &rsp) == 404 && !strcmp(rsp.body, "Not found\n"));
        }
    }
    req = (stub_req){ .method = "POST", .uri = "/upload", .content_length = -1, .is_ssl = 1 };
    assert(stub_invoke("/", &req, &rsp) == 404 && rsp.status == 404 &&
            !strcmp(rsp.body, "Not found\n"));
    req.uri = "/ws";
    assert(stub_invoke("/", &req, &rsp) == 404 && !strcmp(rsp.body, "Not found\n"));
    req = (stub_req){
        .method = "GET", .uri = "/login.html", .content_length = -1, .host = "evil.example"
    };
    assert(stub_invoke("/", &req, &rsp) == 403 && rsp.status == 403);
    req.host = "192.168.1.5";
    assert(stub_invoke("/", &req, &rsp) == 0 && rsp.status == 0);

    /* plain-HTTP gate */
    req = (stub_req){
        .method = "POST", .uri = "/no-such-route", .content_length = -1, .host = "192.168.1.5"
    };
    assert(stub_call(plain_gate, &req, &rsp) == 403 && rsp.status == 403 &&
            !strcmp(rsp.body, "HTTPS required") &&
            strstr(rsp.head, "\r\nContent-Type: text/plain\r\n"));
    req.uri = "/runlog";
    assert(stub_call(plain_gate, &req, &rsp) == 403 && rsp.status == 403);
    req.uri = "/api/v1/upgrade";
    assert(stub_call(plain_gate, &req, &rsp) == 403 && rsp.status == 403 &&
            strstr(rsp.head, "\r\nContent-Type: application/json\r\n") &&
            strstr(rsp.body, "{\"STATUS\":[{\"STATUS\":\"E\",") == rsp.body &&
            strstr(rsp.body, ",\"Code\":503,\"Msg\":\"HTTPS required\"}]"));
    req = (stub_req){
        .method = "GET", .uri = "/api/v1/auth", .content_length = -1, .host = "192.168.1.5"
    };
    assert(stub_call(plain_gate, &req, &rsp) == 302 && rsp.status == 302 &&
            strstr(rsp.head, "\r\nLocation: https://192.168.1.5/api/v1/auth\r\n"));
    req.uri = "/dashboard.html";
    assert(stub_call(plain_gate, &req, &rsp) == 302 && rsp.status == 302 &&
            strstr(rsp.head, "\r\nLocation: https://192.168.1.5/dashboard.html\r\n"));
    req.query = "lang=en&x=%2F1";
    assert(stub_call(plain_gate, &req, &rsp) == 302 && strstr(rsp.head, QUERY_LOCATION));
    req.method = "HEAD";
    assert(stub_call(plain_gate, &req, &rsp) == 302 && strstr(rsp.head, QUERY_LOCATION));
    req.method = "GET";
    req.query  = "a=1\r\nSet-Cookie: x=1";
    assert(stub_call(plain_gate, &req, &rsp) == 302 &&
            strstr(rsp.head, "\r\nLocation: https://192.168.1.5/dashboard.html\r\n") &&
            !strstr(rsp.head, "Set-Cookie"));
    req.query = NULL;
    req.host  = "192.168.1.5:80";
    req.uri   = "/";
    assert(stub_call(plain_gate, &req, &rsp) == 302 &&
            strstr(rsp.head, "\r\nLocation: https://192.168.1.5/\r\n"));
    req.host = NULL;
    assert(stub_call(plain_gate, &req, &rsp) == 403 && rsp.status == 403);
    req.host = "evil.example";
    assert(stub_call(plain_gate, &req, &rsp) == 403 && rsp.status == 403);
    req = (stub_req){
        .method = "POST", .uri = "/api/v1/session", .content_length = -1, .host = "192.168.1.5"
    };
    assert(stub_call(plain_gate, &req, &rsp) == 403 && rsp.status == 403);
    req.uri = "/api/v1/pools";
    assert(stub_call(plain_gate, &req, &rsp) == 403);
    req.is_ssl = 1;
    assert(stub_call(plain_gate, &req, &rsp) == 0 && rsp.status == 0);
    req.uri = "/api/v1/session";
    assert(stub_call(plain_gate, &req, &rsp) == 0 && rsp.status == 0);
    req = (stub_req){ .method = "GET",
        .uri                  = "/api/v1/summary",
        .content_length       = -1,
        .host                 = "192.168.1.5",
        .token                = "x" };
    assert(stub_call(plain_gate, &req, &rsp) == 403 && rsp.status == 403 &&
            strstr(rsp.body, ",\"Code\":503,\"Msg\":\"HTTPS required\"}]") &&
            !strstr(rsp.head, "Location:"));
    req.token = NULL;
    req.uri   = "/api/v1/token";
    assert(stub_call(plain_gate, &req, &rsp) == 403 && rsp.status == 403);
    req.method = "POST";
    assert(stub_call(plain_gate, &req, &rsp) == 403 && rsp.status == 403);
    req.is_ssl = 1;
    assert(stub_call(plain_gate, &req, &rsp) == 0 && rsp.status == 0);

    /* port 8443 serves /api/v1 only */
    {
        static const char *const rest[]  = { "/api/v1/summary", "/api/v1/token", "/api/v1/upgrade",
             "/api/v1/wallpaper" };
        static const char *const other[] = { "/", "/login.html", "/dashboard.html",
            "/sha256.min.js", "/pelish-logo.png", "/canaan-logo.png", "/get_dashboard.cgi",
            "/cgi-bin/x", "/runlog", "/upload", "/ws", "/api/v1", "/API/V1/summary",
            "/no-such-route" };

        req = (stub_req){ .method = "GET",
            .content_length       = -1,
            .host                 = "192.168.1.5",
            .is_ssl               = 1,
            .server_port          = 8443 };
        for (size_t i = 0; i < sizeof rest / sizeof rest[0]; i++) {
            req.uri         = rest[i];
            req.server_port = 8443;
            assert(stub_call(request_gate, &req, &rsp) == 0 && rsp.status == 0);
            req.server_port = 443;
            assert(stub_call(request_gate, &req, &rsp) == 0 && rsp.status == 0);
        }
        for (size_t i = 0; i < sizeof other / sizeof other[0]; i++) {
            req.uri         = other[i];
            req.method      = "GET";
            req.server_port = 8443;
            assert(stub_call(request_gate, &req, &rsp) == 404 && rsp.status == 404 &&
                    !strcmp(rsp.body, "Not found\n"));
            req.method = "POST";
            assert(stub_call(request_gate, &req, &rsp) == 404 && rsp.status == 404);
            req.server_port = 443;
            assert(stub_call(request_gate, &req, &rsp) == 0 && rsp.status == 0);
            req.method = "GET";
            assert(stub_call(request_gate, &req, &rsp) == 0 && rsp.status == 0);
        }
        req = (stub_req){ .method = "GET",
            .uri                  = "/dashboard.html",
            .content_length       = -1,
            .host                 = "192.168.1.5",
            .server_port          = 80 };
        assert(stub_call(request_gate, &req, &rsp) == 302 &&
                strstr(rsp.head, "\r\nLocation: https://192.168.1.5/dashboard.html\r\n"));
        req.uri    = "/api/v1/token";
        req.method = "POST";
        assert(stub_call(request_gate, &req, &rsp) == 403 && strstr(rsp.body, "HTTPS required"));
    }
    /* the handler tells the router whether the request came over HTTPS */
    req = (stub_req){
        .method = "DELETE", .uri = "/api/v1/session", .content_length = -1, .is_ssl = 1
    };
    assert(stub_invoke(stub_route("/api/v1/session"), &req, &rsp) == 200 &&
            strstr(rsp.head, "Set-Cookie: auth=; Path=/; HttpOnly; Max-Age=0; Secure\r\n"));
    req.is_ssl = 0;
    assert(stub_invoke(stub_route("/api/v1/session"), &req, &rsp) == 200 &&
            strstr(rsp.head, "Set-Cookie: auth=; Path=/; HttpOnly; Max-Age=0\r\n"));

    /* the web server runs only with HTTPS up */
    static char mgc[1]; /* stands in for the opaque civetweb context */
    saved = dup(2);
    fflush(stderr);
    efd = open("handler_start.txt", O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    assert(saved >= 0 && efd >= 0 && dup2(efd, 2) == 2);
    stub_reset();
    tst_tls = 0;
    assert(web_server_run("80", "handler.ini") == -1 && tst_starts == 0 && tst_cert_tries == 0);
    assert(strstr(start_errs(errs, sizeof errs),
            "] [ERR] test_web_handler: civetweb is built without TLS"));
    tst_tls       = 1;
    tst_cert_fail = "*";
    assert(web_server_run("80", "handler.ini") == -1 && tst_starts == 0 && tst_cert_tries == 2);
    assert(strstr(start_errs(errs, sizeof errs),
            "] [ERR] test_web_handler: no HTTPS certificate could be made"));
    tst_cert_fail = NULL;
    assert(web_server_run("80", "handler.ini") == -1 && tst_starts == 1 && tst_stops == 0);
    assert(strstr(start_errs(errs, sizeof errs),
            "] [ERR] test_web_handler: web server could not start on ports 443 and 80"));
    tst_mgc = (struct mg_context *)mgc;
    assert(web_server_run("80", "handler.ini") == -1 && tst_starts == 2 && tst_stops == 1);
    assert(strstr(start_errs(errs, sizeof errs),
            "] [ERR] test_web_handler: HTTPS could not start on port 443"));
    assert(stub_handler_count() == 0);
    tst_rest_up = 1;
    assert(web_server_run("80", "handler.ini") == -1 && tst_starts == 3 && tst_stops == 2);
    assert(strstr(start_errs(errs, sizeof errs),
            "] [ERR] test_web_handler: HTTPS could not start on port 443"));
    assert(stub_handler_count() == 0);
    tst_https_up = 1;
    tst_rest_up  = 0;
    assert(web_start("80", "handler.ini") == tst_mgc && tst_starts == 4 && tst_stops == 2);
    assert(!strcmp(tst_opt_ports, "80,443s,8443so") && !strcmp(tst_opt_cert, WEB_TLS_CERT));
    assert(!strcmp(stub_route(WEB_UPGRADE_URI), WEB_UPGRADE_URI) &&
            !strcmp(stub_route("/upload"), "/") && !strcmp(stub_route("/ws"), "/") &&
            !strcmp(stub_route("/login.cgi"), "/"));
    start_errs(errs, sizeof errs);
    assert(strstr(errs,
            "] [ERR] test_web_handler: HTTPS could not start on port 8443; the REST API is "
            "served on port 443 only\n"));
    assert(strstr(errs, "] [INFO] test_web_handler: web server on port 443\n"));
    stub_reset();
    tst_rest_up = 1;
    assert(web_start("80", "handler.ini") == tst_mgc && tst_starts == 5 && tst_stops == 2);
    start_errs(errs, sizeof errs);
    assert(!strstr(errs, "[ERR]") &&
            strstr(errs, "] [INFO] test_web_handler: web server on port 443\n"));
    stub_reset();
    tst_cert_fail = WEB_TLS_CERT;
    assert(web_start("80", "handler.ini") == tst_mgc && !strcmp(tst_opt_cert, WEB_TLS_CERT_TMP));
    fflush(stderr);
    assert(dup2(saved, 2) == 2);
    close(saved);
    close(efd);
    remove("handler_start.txt");
    remove(MINER_WEB_LOG_PATH);
    rmdir("handler_weblog");

    remove("handler.ini");
    return 0;
}
