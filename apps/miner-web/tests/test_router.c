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

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/timex.h>

#include "osif_logpath.h"
#include "router.h"
#include "core_api.h"
#include "pool_config.h"
#include "web_plugin.h"
#include "webtoken.h"

#undef MINER_LOG_PATH
#undef MINER_OP_LOG_PATH
#undef MINER_WEB_LOG_PATH
#define MINER_LOG_PATH     "router_log/miner.log"
#define MINER_OP_LOG_PATH  "router_log/miner_op.log"
#define MINER_WEB_LOG_PATH "router_weblog/web.log"
#undef _DEFAULT_SOURCE

#include "logexport.c"

static int test_clock_gettime(clockid_t id, struct timespec *ts);
static int test_adjtimex(struct timex *tx);

#undef WEB_TOKEN_KEY
#undef WEB_TOKEN_KEY_TMP
#undef WEB_TOKEN_BOOT_ID
#define WEB_TOKEN_KEY     "router_log/tokenkey"
#define WEB_TOKEN_KEY_TMP "router_log/tokenkey.tmpfs"
#define WEB_TOKEN_BOOT_ID "router_log/boot_id"
#define clock_gettime     test_clock_gettime
#define adjtimex          test_adjtimex
#undef _DEFAULT_SOURCE
#include "webtoken.c"
#undef adjtimex
#undef clock_gettime

#define ROUTER_CONF "test_router_pools.conf"

#define CORE_OK  "{\"STATUS\":[{\"STATUS\":\"S\",\"Code\":0,\"Msg\":\"ok\"}],\"id\":1}"
#define CORE_ERR "{\"STATUS\":[{\"STATUS\":\"E\",\"Code\":45,\"Msg\":\"Access denied\"}],\"id\":1}"

#define TOKEN_BODY  "{\"command\":\"token\",\"user\":\"admin\",\"password\":\"tokpass\"}"
#define TOKEN_WRONG "{\"command\":\"token\",\"user\":\"admin\",\"password\":\"nope\"}"
#define JWT_HEADER  "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9"
#define NO_TOKEN    JWT_HEADER ".e30.AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
#define TOKEN_STALE \
    "{\"STATUS\":[{\"STATUS\":\"E\",\"When\":,\"Code\":602," \
    "\"Msg\":\"Token expired, request a new one\"}],\"id\":1}"
#define REST_UNAUTHORIZED \
    "{\"STATUS\":[{\"STATUS\":\"E\",\"When\":,\"Code\":602," \
    "\"Msg\":\"Not logged in\"}],\"id\":1}"
#define REST_LOCKED \
    "{\"STATUS\":[{\"STATUS\":\"E\",\"When\":,\"Code\":506," \
    "\"Msg\":\"Too many failed attempts, try again later\"}],\"id\":1}"
#define TOKEN_DENIED     "\"Code\":601,\"Msg\":\"Invalid user or password\""
#define TOKEN_BAD_REQ    "\"Code\":601,\"Msg\":\"Invalid token request\""
#define NO_STORE         "Cache-Control: no-store\r\n"
#define CRED_PASS        "d30122d73dce746fd1ac33631c18bb75756c0a38ae69cae4187b931b6f3c2690"
#define CRED_PASS_PREFIX "d30122d7"
#define COOKIE           "4d43ac86d30122d73dce746fd1ac3363"

/* Counts a failed cond in fails and prints msg as FAIL or ok */
#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "  FAIL: %s\n", msg); \
            fails++; \
        } else \
            printf("  ok: %s\n", msg); \
    } while (0)

static long long g_up_off_s; /* seconds added to CLOCK_BOOTTIME */
static long long g_up_step_s;
static int       g_synced = 1;

static int fails;

static int      g_read_calls;
static int      g_read_rc;
static pool_set g_read_set;

static mc_webcred   cred; /* credentials the router checks against */
static mc_dashboard tele; /* dashboard telemetry served by the router */
static web_ctx      ctx;

static int g_rebooted;      /* 1 once rb_stub has been called */
static int g_reboot_ok = 1; /* value rb_stub returns */

static char g_tz_name[72];
static int  g_tz_set_ok = 1;

static char        g_core_cmd[64];
static char        g_core_param[2048];
static int         g_core_calls;
static int         g_core_rc;
static const char *g_core_reply = CORE_OK;

static int test_clock_gettime(clockid_t id, struct timespec *ts)
{
    int rc = clock_gettime(id, ts);

    if (rc == 0 && id == CLOCK_BOOTTIME) {
        ts->tv_sec += (time_t)g_up_off_s;
        g_up_off_s += g_up_step_s;
    }
    return rc;
}

static int test_adjtimex(struct timex *tx)
{
    (void)tx;
    return g_synced ? TIME_OK : TIME_ERROR;
}

int cfgwr_update_pools(const char *path, pool_config_update_fn update, void *ctx)
{
    return pool_config_update(path, update, ctx);
}

int cfgwr_read_pools(const char *path, pool_set *out)
{
    (void)path;
    g_read_calls++;
    if (g_read_rc != 0)
        return -1;
    *out = g_read_set;
    return 0;
}

static int disable_slot1(pool_set *ps, void *ctx)
{
    (void)ctx;
    for (int i = 0; i < ps->count; i++)
        if (ps->pools[i].slot == 1)
            ps->pools[i].enabled = 0;
    return 0;
}

static int rb_stub(void)
{
    g_rebooted = 1;
    return g_reboot_ok;
}

static int tz_get_stub(char *out, size_t cap)
{
    snprintf(out, cap, "Asia/Shanghai");
    return 0;
}
static int tz_set_stub(const char *tz)
{
    snprintf(g_tz_name, sizeof g_tz_name, "%s", tz);
    return g_tz_set_ok;
}

static int g_wifi_eth_set = 1;
static int g_wifi_eth_val;
static int wifi_eth_set_stub(int on)
{
    if (!g_wifi_eth_set)
        return 0;
    g_wifi_eth_val = on ? 1 : 0;
    return 1;
}

int core_api_call(const char *cmd, const char *param, char *out, size_t cap)
{
    g_core_calls++;
    snprintf(g_core_cmd, sizeof g_core_cmd, "%s", cmd);
    snprintf(g_core_param, sizeof g_core_param, "%s", param);
    snprintf(out, cap, "%s", g_core_reply);
    return g_core_rc < 0 ? g_core_rc : 0;
}

static int method_reply(const web_response *r, const char *out)
{
    return r->content_type && !strcmp(r->content_type, "application/json") &&
            r->body_len == strlen(out) && strstr(out, "{\"STATUS\":[{\"STATUS\":\"E\",") == out &&
            strstr(out, ",\"Code\":505,\"Msg\":\"Method not allowed\"}]") != NULL;
}

static int rest(web_method m, const char *path, const char *type, const char *body,
        const char *query, int authed, char *out, size_t cap, web_response *r)
{
    web_request req = { .method = m,
        .path                   = path,
        .body                   = body,
        .auth_cookie            = authed ? COOKIE : NULL,
        .content_type           = type,
        .query                  = query };

    g_core_calls    = 0;
    g_core_cmd[0]   = '\0';
    g_core_param[0] = '\0';
    return web_route(&req, &ctx, out, cap, r);
}

static int token_post(const char *type, const char *body, const char *addr, char *out, size_t cap,
        web_response *r)
{
    web_request req = { .method = WEB_POST,
        .path                   = WEB_TOKEN_URI,
        .body                   = body,
        .content_type           = type,
        .remote_addr            = addr,
        .tls                    = 1 };

    return web_route(&req, &ctx, out, cap, r);
}

static int token_req(web_method m, const char *path, const char *body, const char *token, char *out,
        size_t cap, web_response *r)
{
    web_request req = { .method = m,
        .path                   = path,
        .body                   = body,
        .content_type           = body ? "application/json" : NULL,
        .auth_token             = token,
        .tls                    = 1 };

    g_core_calls  = 0;
    g_core_cmd[0] = '\0';
    return web_route(&req, &ctx, out, cap, r);
}

static int token_from(const char *body, char tok[WEB_TOKEN_MAX])
{
    static const char jwt_chars[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";
    const char *p = strstr(body, "\"Token\":\"");
    size_t      n;

    if (!p)
        return -1;
    p += 9;
    n = strspn(p, jwt_chars);
    if (!n || n >= WEB_TOKEN_MAX || p[n] != '"')
        return -1;
    memcpy(tok, p, n);
    tok[n] = '\0';
    return 0;
}

static int stale_reply(const web_response *r, const char *out, char *body, size_t cap)
{
    const char *w = strstr(out, "\"When\":");
    size_t      n;

    body[0] = '\0';
    if (r->status != 401 || !r->content_type || strcmp(r->content_type, "application/json") ||
            r->extra_headers[0] || r->serve_file || g_core_calls || !w)
        return 0;
    w += 7;
    n = strspn(w, "0123456789");
    if (!n)
        return 0;
    snprintf(body, cap, "%.*s%s", (int)(w - out), out, w + n);
    return !strcmp(body, TOKEN_STALE);
}

static int status_is(const char *out, const char *want)
{
    char        body[512];
    const char *w = strstr(out, "\"When\":");
    size_t      n;

    if (!w)
        return 0;
    w += 7;
    n = strspn(w, "0123456789");
    if (!n)
        return 0;
    snprintf(body, sizeof body, "%.*s%s", (int)(w - out), out, w + n);
    return !strcmp(body, want);
}

static void boot_id_set(const char *id)
{
    FILE *f = fopen(WEB_TOKEN_BOOT_ID, "w");

    if (f) {
        fprintf(f, "%s\n", id);
        fclose(f);
    }
}

static size_t slurp(const char *path, char *buf, size_t cap)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static int route_cookie(web_method m, const char *path, const char *cookie, char *out, size_t cap,
        web_response *r)
{
    web_request req = { .method = m, .path = path, .body = NULL, .auth_cookie = cookie };
    return web_route(&req, &ctx, out, cap, r);
}

int main(void)
{
    strcpy(cred.user, "admin");
    strcpy(cred.pass, CRED_PASS);
    cred.loaded         = 1;
    tele.hwtype         = "Avalon Nano3s";
    tele.version        = "0.2.0";
    tele.pool[0].url    = "stratum+tcp://x:3333";
    tele.pool[0].worker = "w.001";
    ctx.creds           = &cred;
    ctx.telemetry       = &tele;

    char         out[2048];
    web_response r;

    mkdir("router_weblog", 0755);
    unlink(MINER_WEB_LOG_PATH);
    CHECK(web_log_web_fd() >= 0, "web.log stand-in opened");

    pool_init(&g_read_set, 0, 0);
    pool_add(&g_read_set, "stratum+tcp://x:3333", "w.001", "realpoolpass");
    pool_add(&g_read_set, "stratum+tcp://z:3333", "w.003", "thirdpass");
    g_read_set.pools[1].slot = 2;
    ctx.pool_conf            = ROUTER_CONF;

    g_read_calls = 0;
    route_cookie(WEB_GET, "/api/v1/dashboard", NULL, out, sizeof out, &r);
    CHECK(r.status == 200 && out[0] == '{', "GET /api/v1/dashboard -> 200 JSON (no login needed)");
    CHECK(!strcmp(r.extra_headers, NO_STORE), "GET /api/v1/dashboard -> Cache-Control: no-store");
    CHECK(strstr(out, "\"hwtype\":\"Avalon Nano3s\"") != NULL, "dashboard carries telemetry");
    CHECK(strstr(out, "realpoolpass") == NULL && strstr(out, "\"passwd1\":\"*\"") != NULL &&
                    strstr(out, "\"passwd2\":\"\"") != NULL && g_read_calls == 0,
            "unauthed dashboard masks a saved pool password, reads no pool set");

    route_cookie(WEB_GET, "/api/v1/dashboard", COOKIE, out, sizeof out, &r);
    CHECK(r.status == 200 && strstr(out, "\"passwd1\":\"realpoolpass\"") != NULL &&
                    strstr(out, "\"passwd2\":\"\"") != NULL &&
                    strstr(out, "\"passwd3\":\"thirdpass\"") != NULL && g_read_calls == 1,
            "authed dashboard carries the saved password of each slot");
    CHECK(!strcmp(r.extra_headers, NO_STORE), "authed dashboard -> Cache-Control: no-store");

    g_read_rc = -1;
    route_cookie(WEB_GET, "/api/v1/dashboard", COOKIE, out, sizeof out, &r);
    CHECK(r.status == 200 && strstr(out, "\"hwtype\":\"Avalon Nano3s\"") != NULL &&
                    strstr(out, "\"passwd1\":\"\"") != NULL &&
                    strstr(out, "\"passwd3\":\"\"") != NULL && g_read_calls == 2,
            "authed dashboard with the pool set unreadable -> 200, empty passwords");
    g_read_rc = 0;

    route_cookie(WEB_GET, "/api/v1/dashboard", "deadbeef", out, sizeof out, &r);
    CHECK(r.status == 200 && strstr(out, "\"passwd1\":\"*\"") != NULL && g_read_calls == 2,
            "a wrong cookie gets the masked dashboard");
    ctx.pool_conf = NULL;
    route_cookie(WEB_GET, "/api/v1/dashboard", COOKIE, out, sizeof out, &r);
    CHECK(r.status == 200 && strstr(out, "\"passwd1\":\"\"") != NULL && g_read_calls == 2,
            "authed dashboard without a pool config path -> empty passwords");

    ctx.auth_nonce = "4d43ac86";
    route_cookie(WEB_GET, "/api/v1/auth", NULL, out, sizeof out, &r);
    CHECK(r.status == 200 && out[0] == '{', "GET /api/v1/auth -> 200 JSON");
    CHECK(strstr(out, "\"auth\":\"4d43ac86\"") != NULL, "auth returns the challenge nonce");
    CHECK(strstr(out, "\"activated\":0") != NULL, "auth carries activated=0 when not activated");
    ctx.activated = 1;
    route_cookie(WEB_GET, "/api/v1/auth", NULL, out, sizeof out, &r);
    CHECK(strstr(out, "\"activated\":1") != NULL, "auth carries activated=1 when activated");
    ctx.activated = 0;

    ctx.dna = "0201000012345678";
    route_cookie(WEB_GET, "/api/v1/auth", NULL, out, sizeof out, &r);
    CHECK(strstr(out, "\"code\":\"681e3a30432837de8000edda\"") != NULL,
            "auth code is sha256(DNA)[:24]");
    ctx.dna = NULL;
    route_cookie(WEB_GET, "/api/v1/auth", NULL, out, sizeof out, &r);
    CHECK(r.status == 200 && strstr(out, "\"code\":\"\"") != NULL,
            "auth code stays empty when the DNA is unknown");

    ctx.activated   = 1;
    ctx.qr_approved = 1;
    {
        web_request tq = { .method = WEB_POST, .path = "/api/v1/qr-login" };

        web_route(&tq, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"auth\":\"4d43ac86\"") != NULL &&
                        strstr(out, "code") == NULL && strstr(out, CRED_PASS_PREFIX) == NULL,
                "QR login body carries the nonce only, no webpass fragment");
        tq.tls = 1;
        web_route(&tq, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(r.extra_headers, "; HttpOnly; SameSite=Strict; Secure\r\n"),
                "QR login over HTTPS marks the cookie Secure");
    }
    ctx.qr_approved = 0;
    ctx.activated   = 0;

    ctx.activated   = WEB_ACTIVATED_UNKNOWN;
    ctx.qr_approved = 1;
    route_cookie(WEB_GET, "/api/v1/auth", NULL, out, sizeof out, &r);
    CHECK(r.status == 200 && strstr(out, "\"activated\":0,\"starting\":1") != NULL,
            "auth while starting carries starting 1");
    {
        web_request sq = { .method = WEB_POST, .path = "/api/v1/session" };
        web_route(&sq, &ctx, out, sizeof out, &r);
        CHECK(r.status == 503 && !strcmp(out, "{\"error\":\"starting\"}"),
                "POST /api/v1/session while starting -> 503");
        sq.path = "/api/v1/qr-login";
        web_route(&sq, &ctx, out, sizeof out, &r);
        CHECK(r.status == 503 && !strcmp(out, "{\"error\":\"starting\"}") &&
                        r.extra_headers[0] == '\0',
                "POST /api/v1/qr-login while starting -> 503, no cookie");
        sq.method = WEB_GET;
        sq.path   = "/api/v1/activation";
        web_route(&sq, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && !strcmp(out, "{\"activated\":0,\"starting\":1}"),
                "GET /api/v1/activation while starting -> starting 1");
    }
    ctx.activated   = 1;
    ctx.qr_approved = 0;
    route_cookie(WEB_GET, "/api/v1/activation", NULL, out, sizeof out, &r);
    CHECK(r.status == 200 && !strcmp(out, "{\"activated\":1}"),
            "activation once known carries no starting field");
    ctx.activated = 0;

    {
        static const char *const removed[] = { "login", "logout", "qr_login", "is_login",
            "dashboard", "get_dashboard", "status", "get_activation", "get_auth", "poolconfig",
            "pools", "timezoneconf", "reboot" };
        static const char *const fmt[]     = { "/%s.cgi", "/cgi-bin/%s", "/cgi-bin/%s.cgi" };
        static const char *const other[]   = { "/cgi-bin/set_pools", "/cgi-bin/get_nonsense",
              "/cgi-bin/upload.aup", "/cgi-bin/upgrade", "/cgi-bin/pools.cgi", "/upload",
              WEB_UPGRADE_URI, "/no-such-route" };
        static const char        form[] = "pool1=stratum%2Btcp%3A%2F%2Fa%3A1&worker1=w&passwd1=x&"
                                          "timezone=Asia%2FTokyo&"
                                          "passwd=4d43ac86d30122d73dce746fd1ac3363";
        char                     path[64], msg[160];
        int                      ok = 1;
        size_t n = sizeof removed / sizeof removed[0] * (sizeof fmt / sizeof fmt[0]);

        ctx.pool_conf   = ROUTER_CONF;
        ctx.reboot      = rb_stub;
        ctx.tz_set      = tz_set_stub;
        ctx.activated   = 1;
        ctx.qr_approved = 1;
        g_rebooted      = 0;
        g_tz_name[0]    = '\0';
        unlink(ROUTER_CONF);
        for (size_t i = 0; i < n + sizeof other / sizeof other[0]; i++) {
            if (i < n)
                snprintf(path, sizeof path, fmt[i % 3], removed[i / 3]);
            else
                snprintf(path, sizeof path, "%s", other[i - n]);
            for (int k = 0; k < 4; k++) {
                web_request q = { .method = k & 1 ? WEB_POST : WEB_GET,
                    .path                 = path,
                    .body                 = k & 1 ? form : NULL,
                    .auth_cookie          = k & 2 ? COOKIE : NULL,
                    .tls                  = 1 };

                web_route(&q, &ctx, out, sizeof out, &r);
                if (r.status != 404 || strcmp(out, "Not found\n") || !r.content_type ||
                        strcmp(r.content_type, "text/plain") || r.extra_headers[0] ||
                        r.serve_file) {
                    snprintf(msg, sizeof msg, "%s %s%s -> 404 Not found", k & 1 ? "POST" : "GET",
                            path, k & 2 ? " logged in" : "");
                    CHECK(0, msg);
                    ok = 0;
                }
            }
        }
        CHECK(ok, "every removed .cgi and /cgi-bin/ path -> 404 Not found, logged in or not");
        CHECK(g_rebooted == 0 && g_tz_name[0] == '\0' && access(ROUTER_CONF, F_OK) != 0,
                "a removed path reboots nothing, sets no zone and saves no pool");
        ctx.qr_approved = 0;
        ctx.activated   = 0;
    }

    {
        web_request q = { .method = WEB_DELETE, .path = "/api/v1/session" };

        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 &&
                        !strcmp(r.extra_headers,
                                "Set-Cookie: auth=; Path=/; HttpOnly; Max-Age=0\r\n"),
                "DELETE /api/v1/session -> 200, expires the auth cookie");
        q.tls = 1;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 &&
                        strstr(r.extra_headers,
                                "Set-Cookie: auth=; Path=/; HttpOnly; Max-Age=0; Secure\r\n") !=
                                NULL,
                "DELETE /api/v1/session over HTTPS expires a Secure cookie");
        q.auth_cookie = COOKIE;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(r.extra_headers, "Max-Age=0") != NULL,
                "DELETE /api/v1/session with a live cookie -> same expiry");
        q.method = WEB_METHOD_OTHER;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 405 && method_reply(&r, out) && r.extra_headers[0] == '\0',
                "HEAD /api/v1/session -> 405 Code 505, the cookie is kept");
    }

    const char *pf = "pool1=stratum%2Btcp%3A%2F%2Fa%3A1&worker1=w&passwd1=x";
    {
        ctx.pool_conf = ROUTER_CONF;
        unlink(ROUTER_CONF);
        web_request q = { .method = WEB_POST, .path = "/api/v1/pools", .body = pf };
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401, "POST /api/v1/pools form unauthed -> 401");
        CHECK(access(ROUTER_CONF, F_OK) != 0,
                "unauthenticated pool save cannot create a config file");

        q.auth_cookie = COOKIE;
        ctx.pool_conf = NULL;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 500 && strstr(out, "\"result\":\"error\"") != NULL &&
                        strstr(out, "\"reason\":\"cannot save the pool settings\"") != NULL,
                "pool form with no config path -> 500 cannot save the pool settings");

        ctx.pool_conf = ROUTER_CONF;
        unlink(ROUTER_CONF);
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"pools\":\"1\"") != NULL,
                "pool form authed -> 200, parsed 1 pool");
        CHECK(strstr(out, "\"note\":\"saved; takes effect after the device restarts\"") != NULL,
                "pool form reply says the save takes effect after a restart");
        pool_set saved;
        pool_init(&saved, 1000, 60000);
        CHECK(pool_config_load(&saved, ROUTER_CONF) == 1 &&
                        !strcmp(saved.pools[0].url, "stratum+tcp://a:1") &&
                        !strcmp(saved.pools[0].user, "w") && !strcmp(saved.pools[0].pass, "x"),
                "pool form persisted the pool to pools.conf");

        char        before[4096];
        size_t      blen            = slurp(ROUTER_CONF, before, sizeof before);
        const char *invalid_forms[] = {
            "pool1=stratum%2Btcp://b:2&worker1=w&passwd1=x&"
            "pool2=stratum%2Btcp://c:3&worker2=a%22b&passwd2=x",
            "pool1=stratum%2Btcp://b:2&worker1=w&passwd1=x%00tail",
            "pool1=stratum%2Btcp://b:2&worker1=w&worker1=other&passwd1=x",
        };
        for (size_t i = 0; i < sizeof invalid_forms / sizeof invalid_forms[0]; i++) {
            q.body = invalid_forms[i];
            web_route(&q, &ctx, out, sizeof out, &r);
            CHECK(r.status == 400, "invalid pool batch rejected in full");
            char after[4096];
            CHECK(slurp(ROUTER_CONF, after, sizeof after) == blen && !memcmp(before, after, blen),
                    "invalid pool batch leaves existing file byte-identical");
        }
        static const struct {
            const char *body; /* /api/v1/pools form with one control byte */
            const char *name;
        } ctrl_forms[] = {
            { "pool1=stratum%2Btcp://b:2&worker1=w&passwd1=x%0Atail",
                    "pool password with %0A (LF) -> 400, file unchanged" },
            { "pool1=stratum%2Btcp://b:2&worker1=w%1Fb&passwd1=x",
                    "pool worker with %1F (US) -> 400, file unchanged" },
            { "pool1=stratum%2Btcp://b%7F:2&worker1=w&passwd1=x",
                    "pool URL with %7F (DEL) -> 400, file unchanged" },
        };
        for (size_t i = 0; i < sizeof ctrl_forms / sizeof ctrl_forms[0]; i++) {
            char after[4096];
            q.body = ctrl_forms[i].body;
            web_route(&q, &ctx, out, sizeof out, &r);
            CHECK(r.status == 400 && strstr(out, "\"result\":\"error\"") != NULL &&
                            slurp(ROUTER_CONF, after, sizeof after) == blen &&
                            !memcmp(before, after, blen),
                    ctrl_forms[i].name);
        }

        q.body                   = pf;
        const char *replacement  = "pool1=stratum%2Btcp%3A%2F%2Fb%3A2&worker1=other&passwd1=y";
        const char *bad_cookie[] = { NULL, "4d43ac86deadbeefdeadbeefdeadbeef", "invalid-cookie" };
        web_request unauth = { .method = WEB_POST, .path = "/api/v1/pools", .body = replacement };
        for (size_t credential = 0; credential < 3; credential++) {
            unauth.auth_cookie = bad_cookie[credential];
            web_route(&unauth, &ctx, out, sizeof out, &r);
            CHECK(r.status == 401, "the pool save rejects missing or invalid credentials");
            char   unchanged[4096];
            size_t length = slurp(ROUTER_CONF, unchanged, sizeof unchanged);
            CHECK(length == blen && !memcmp(before, unchanged, blen),
                    "a rejected pool save preserves existing config bytes");
        }
        q.body = "pool1=stratum%2Btcp://b:2&worker1=w&passwd1=x&pool2=stratum%2Btcp://c:3&worker2=";
        web_route(&q, &ctx, out, sizeof out, &r);
        pool_init(&saved, 1000, 60000);
        CHECK(r.status == 200 && pool_config_load(&saved, ROUTER_CONF) == 1 &&
                        !strcmp(saved.pools[0].url, "stratum+tcp://b:2") &&
                        saved.pools[0].slot == 0,
                "an incomplete row is saved as a free slot, the complete one kept");
        q.body = pf;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"pools\":\"1\"") != NULL,
                "the logged-in pool form saves one pool again");
        const char *bad = "pool1=stratum%2Btcp%3A%2F%2Fa%3A1&worker1=w&passwd1=x&"
                          "pool2=stratum%2Btcp%3A%2F%2Fb%3A2&worker2=w%222&passwd2=y";
        q.body          = bad;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 400 && strstr(out, "invalid or oversized pool fields") != NULL,
                "a refused pool form -> 400 with a reason");
        char   after[4096];
        size_t alen = slurp(ROUTER_CONF, after, sizeof after);
        CHECK(alen == blen && !memcmp(before, after, blen),
                "a refused pool form leaves pools.conf byte-identical");

        q.body = "pool1=stratum%2Btcp%3A%2F%2Fa%3A1&worker1=w&passwd1=x&"
                 "pool2=notaurl&worker2=w2&passwd2=y";
        web_route(&q, &ctx, out, sizeof out, &r);
        pool_init(&saved, 1000, 60000);
        CHECK(r.status == 200 && pool_config_load(&saved, ROUTER_CONF) == 2 &&
                        !strcmp(saved.pools[1].url, "notaurl"),
                "pool form saves an unparseable url unvalidated");

        q.body = "pool1=&worker1=&passwd1=";
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 400 && strstr(out, "no pool configured") != NULL,
                "pool form with no pool -> 400");

        q.body = "pool1=stratum%2Btcp%3A%2F%2Fa%3A1&worker1=w&passwd1=x&"
                 "pool2=stratum%2Btcp%3A%2F%2Fb%3A2&worker2=w2&passwd2=y";
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200, "pool form saves two pools");
        CHECK(pool_config_update(ROUTER_CONF, disable_slot1, NULL) == 0,
                "another writer disables pool 2 after the page was read");
        q.body = "pool1=stratum%2Btcp%3A%2F%2Fedited%3A1&worker1=w&passwd1=x&"
                 "pool2=stratum%2Btcp%3A%2F%2Fb%3A2&worker2=w2&passwd2=y";
        web_route(&q, &ctx, out, sizeof out, &r);
        pool_init(&saved, 1000, 60000);
        CHECK(r.status == 200 && pool_config_load(&saved, ROUTER_CONF) == 2 &&
                        !strcmp(saved.pools[0].url, "stratum+tcp://edited:1") &&
                        saved.pools[0].enabled &&
                        !strcmp(saved.pools[1].url, "stratum+tcp://b:2") && !saved.pools[1].enabled,
                "a web pool edit keeps a concurrent writer's disable of another pool");
        q.body = "pool1=stratum%2Btcp%3A%2F%2Fedited%3A1&worker1=w&passwd1=x&"
                 "pool2=stratum%2Btcp%3A%2F%2Fnew%3A3&worker2=w2&passwd2=y";
        web_route(&q, &ctx, out, sizeof out, &r);
        pool_init(&saved, 1000, 60000);
        CHECK(r.status == 200 && pool_config_load(&saved, ROUTER_CONF) == 2 &&
                        saved.pools[0].enabled &&
                        !strcmp(saved.pools[1].url, "stratum+tcp://new:3") &&
                        saved.pools[1].enabled,
                "a new url saved into a disabled slot enables it");

        q.body = "pool1=stratum%2Btcp%3A%2F%2Fedited%3A1&worker1=w9&passwd1=&"
                 "pool2=stratum%2Btcp%3A%2F%2Fother%3A4&worker2=w2";
        web_route(&q, &ctx, out, sizeof out, &r);
        pool_init(&saved, 1000, 60000);
        CHECK(r.status == 200 && strstr(out, "\"pools\":\"2\"") != NULL &&
                        pool_config_load(&saved, ROUTER_CONF) == 2 &&
                        !strcmp(saved.pools[0].user, "w9") && !strcmp(saved.pools[0].pass, "x") &&
                        !strcmp(saved.pools[1].url, "stratum+tcp://other:4") &&
                        !strcmp(saved.pools[1].pass, "y"),
                "an empty or missing password keeps the slot's saved password");

        blen   = slurp(ROUTER_CONF, before, sizeof before);
        q.body = "pool1=stratum%2Btcp%3A%2F%2Fedited%3A1&worker1=w9&passwd1=&"
                 "pool2=stratum%2Btcp%3A%2F%2Fother%3A4&worker2=w2&passwd2=&"
                 "pool3=stratum%2Btcp%3A%2F%2Fnew%3A5&worker3=w3&passwd3=";
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 400 && strstr(out, "pool 3 needs a password") != NULL &&
                        slurp(ROUTER_CONF, after, sizeof after) == blen &&
                        !memcmp(before, after, blen),
                "a new slot without a password -> 400, pools.conf unchanged");

        q.body = "pool1=stratum%2Btcp%3A%2F%2Fedited%3A1&worker1=w9&passwd1=&"
                 "pool2=&worker2=&passwd2=&"
                 "pool3=stratum%2Btcp%3A%2F%2Fnew%3A5&worker3=w3&passwd3=z";
        web_route(&q, &ctx, out, sizeof out, &r);
        pool_init(&saved, 1000, 60000);
        CHECK(r.status == 200 && pool_config_load(&saved, ROUTER_CONF) == 2 &&
                        !strcmp(saved.pools[0].pass, "x") && saved.pools[0].slot == 0 &&
                        !strcmp(saved.pools[1].url, "stratum+tcp://new:5") &&
                        !strcmp(saved.pools[1].pass, "z") && saved.pools[1].slot == 2,
                "an emptied row frees its slot, a new slot takes its password");

        q.body = "pool1=stratum%2Btcp%3A%2F%2Fedited%3A1&worker1=w9&passwd1=&"
                 "pool3=stratum%2Btcp%3A%2F%2Fnew%3A5&worker3=w3&passwd3=";
        web_route(&q, &ctx, out, sizeof out, &r);
        pool_init(&saved, 1000, 60000);
        CHECK(r.status == 200 && pool_config_load(&saved, ROUTER_CONF) == 2 &&
                        !strcmp(saved.pools[1].pass, "z"),
                "a slot saved without a password keeps the one set before");

        q.body = "pool1=stratum%2Btcp%3A%2F%2Fedited%3A1&worker1=w9&passwd1=*&"
                 "pool3=stratum%2Btcp%3A%2F%2Fnew%3A5&worker3=w3&passwd3=%2A";
        web_route(&q, &ctx, out, sizeof out, &r);
        pool_init(&saved, 1000, 60000);
        CHECK(r.status == 200 && pool_config_load(&saved, ROUTER_CONF) == 2 &&
                        !strcmp(saved.pools[0].pass, "x") && !strcmp(saved.pools[1].pass, "z"),
                "a password of \"*\" keeps the slot's saved password");

        blen   = slurp(ROUTER_CONF, before, sizeof before);
        q.body = "pool1=stratum%2Btcp%3A%2F%2Fedited%3A1&worker1=w9&passwd1=*&"
                 "pool2=stratum%2Btcp%3A%2F%2Fother%3A4&worker2=w2&passwd2=*";
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 400 && strstr(out, "pool 2 needs a password") != NULL &&
                        slurp(ROUTER_CONF, after, sizeof after) == blen &&
                        !memcmp(before, after, blen),
                "a new slot with the password \"*\" -> 400, pools.conf unchanged");
        unlink(ROUTER_CONF);
    }

    {
        static const char *const logs[] = {
            "router_log/miner.log",
            "router_log/miner.log.1",
            "router_log/miner_op.log",
            "router_log/miner_op.log.1",
            "router_log/web.log",
            "router_log/notes.txt",
        };
        static const char *const names[][2] = {
            { "miner.log", "router_log/miner.log" },
            { "miner_op.log", "router_log/miner_op.log" },
            { "web.log", "router_log/web.log" },
            { "miner.log\r\n", "router_log/miner.log" },
        };
        static const char *const unknown[] = {
            "bogus",
            "notes.txt",
            "../router_log/miner.log",
            "a..log",
            ".hidden.log",
            "sub/x.log",
            "a\nb\x80.log",
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.logx",
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.log",
        };
        char msg[320], errs[8192], warn[LOGEXPORT_NAME_MAX + 40];
        int  saved = dup(2), fd;

        mkdir("router_log", 0755);
        for (size_t i = 0; i < sizeof logs / sizeof logs[0]; i++) {
            FILE *lf = fopen(logs[i], "w");
            if (lf)
                fclose(lf);
        }
        fflush(stderr);
        fd = open("router_log/stderr.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(fd, 2);
        close(fd);

        char rtok[WEB_TOKEN_MAX];

        boot_id_set("boot-1");
        CHECK(web_token_issue(&cred, rtok, sizeof rtok) == 0, "a token for the log downloads");
        web_request q = { .method = WEB_POST, .path = "/runlog", .auth_token = rtok, .tls = 1 };
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
            q.body = names[i][0];
            web_route(&q, &ctx, out, sizeof out, &r);
            snprintf(msg, sizeof msg, "POST /runlog %.70s -> 200, serves %s.1 then %s", names[i][0],
                    names[i][1], names[i][1]);
            CHECK(r.status == 200 && r.serve_file && !strcmp(r.serve_file, names[i][1]) &&
                            r.serve_concat == 1,
                    msg);
            snprintf(msg, sizeof msg,
                    "POST /runlog %.70s -> serve_name is the posted log, "
                    "trimmed",
                    names[i][0]);
            CHECK(strlen(r.serve_name) == strcspn(names[i][0], "\r\n") &&
                            !strncmp(r.serve_name, names[i][0], strlen(r.serve_name)),
                    msg);
        }

        {
            char path[WEB_SERVE_PATH_MAX];
            int  concat = 0;

            CHECK(web_plugin_file("plugin-file", path, sizeof path, &concat) == -1 &&
                            web_plugin_file("miner.log", path, sizeof path, &concat) == -1,
                    "plugin stub maps no name");
            fclose(fopen("router_log/plugin-file", "w"));
            q.body = "plugin-file";
            web_route(&q, &ctx, out, sizeof out, &r);
            CHECK(r.status == 404 && r.serve_file == NULL && !strcmp(out, "Not found\n"),
                    "POST /runlog plugin-file with the plugin stub -> 404 Not found, no serve");
            unlink("router_log/plugin-file");
        }

        for (size_t i = 0; i < sizeof unknown / sizeof unknown[0]; i++) {
            q.body = unknown[i];
            web_route(&q, &ctx, out, sizeof out, &r);
            snprintf(msg, sizeof msg, "POST /runlog %.70s -> 404 Not found, no serve", unknown[i]);
            CHECK(r.status == 404 && r.serve_file == NULL && !strcmp(out, "Not found\n"), msg);
        }
        q.body = NULL;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 404 && r.serve_file == NULL, "POST /runlog without a body -> 404");
        q.body = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.log";
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 404 && r.serve_file == NULL && !strcmp(out, "Not found\n"),
                "POST /runlog name of LOGEXPORT_NAME_MAX - 1 bytes -> not a listed log -> 404");
        q.body = "\x80\xff.log";
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 404 && r.serve_file == NULL && !strcmp(out, "Not found\n"),
                "POST /runlog name of bytes >= 0x80 -> not a listed log -> 404");

        q.body = "web.log";
        unlink("router_log/web.log");
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 404 && r.serve_file == NULL && !strcmp(out, "Not found\n"),
                "POST /runlog missing file -> 404 Not found, no serve");
        fclose(fopen("router_log/web.log.1", "w"));
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 404 && r.serve_file == NULL && !strcmp(out, "Not found\n"),
                "POST /runlog only the .1 present -> 404 Not found, no serve");
        unlink("router_log/web.log.1");
        CHECK(symlink("miner.log", "router_log/web.log") == 0, "symlink fixture created");
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 404 && r.serve_file == NULL && !strcmp(out, "Not found\n"),
                "POST /runlog symlinked file -> 404 Not found, no serve");
        unlink("router_log/web.log");
        fclose(fopen("router_log/web.log", "w"));
        unlink("router_log/miner_op.log.1");
        CHECK(symlink("miner.log", "router_log/miner_op.log.1") == 0,
                "symlinked .1 fixture created");
        q.body = "miner_op.log";
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 404 && r.serve_file == NULL && !strcmp(out, "Not found\n"),
                "POST /runlog symlinked .1 -> 404 Not found, no serve");
        if (geteuid() != 0) {
            q.body = "web.log";
            CHECK(chmod("router_log/web.log", 0) == 0, "unreadable fixture created");
            web_route(&q, &ctx, out, sizeof out, &r);
            CHECK(r.status == 500 && r.serve_file == NULL,
                    "POST /runlog regular file that cannot be opened -> 500, no serve");
            chmod("router_log/web.log", 0644);
        } else {
            printf("SKIP: POST /runlog regular file that cannot be opened -> 500, no serve "
                   "(root)\n");
        }

        q.body = "miner.log";
        q.tls  = 0;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 403 && r.serve_file == NULL && strstr(out, "HTTPS required"),
                "POST /runlog over plain HTTP -> 403 HTTPS required, no serve");
        q.tls    = 1;
        q.method = WEB_GET;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 405 && r.serve_file == NULL, "GET /runlog -> 405, no serve");
        q.method     = WEB_POST;
        q.auth_token = NO_TOKEN;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && r.serve_file == NULL && r.extra_headers[0] == '\0',
                "POST /runlog forged token -> 401, no challenge, no serve");
        q.auth_token = NULL;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && r.serve_file == NULL && r.extra_headers[0] == '\0' &&
                        !strcmp(out, "unauthorized"),
                "POST /runlog no token -> 401 unauthorized, no challenge, no serve");
        q.auth_cookie = COOKIE;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && r.serve_file == NULL,
                "POST /runlog with a login cookie only -> 401, no serve");
        q.auth_cookie = NULL;
        q.path        = "/runlog/miner.log";
        q.auth_token  = rtok;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.serve_file == NULL, "POST /runlog/<name> is not a log download");
        q.path = "/runlogs";
        q.body = "miner.log";
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status != 200 && r.serve_file == NULL, "POST /runlogs is not a log download");
        rest(WEB_GET, "/api/v1/x\nINFO forged\x1b", NULL, NULL, NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 404, "GET /api/v1/ with control bytes in the resource -> 404");

        fflush(stderr);
        dup2(saved, 2);
        close(saved);
        size_t en = slurp("router_log/stderr.txt", errs, sizeof errs - 1);
        errs[en]  = '\0';
        int ops   = 0;
        for (const char *p = errs; (p = strstr(p, "[OP] ")) != NULL; p++)
            ops++;
        CHECK(ops == 0, "routing /runlog logs no OP line; the serve step logs it");
        CHECK(strstr(errs,
                      "web.log download refused: router_log/web.log is not a regular "
                      "file\n") != NULL &&
                        strstr(errs,
                                "miner_op.log download refused: router_log/miner_op.log.1 is not a "
                                "regular file\n") != NULL,
                "a symbolic link or its .1 logs WARN, not a regular file");
        CHECK(strstr(errs, "web.log download refused: router_log/web.log does not exist\n") != NULL,
                "a missing log logs WARN, does not exist");
        CHECK(strstr(errs,
                      "log download refused: no such log \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                      "aaaaaaaaaaaaaaaaaaaaaa.log\"\n") != NULL,
                "a name of LOGEXPORT_NAME_MAX - 1 bytes is logged whole as no such log");
        CHECK(strstr(errs, "log download refused: no such log \"bogus\"\n") != NULL,
                "an unknown name logs WARN with the quoted name");
        CHECK(strstr(errs, "no such log \"a?b?.log\"\n") != NULL && !strstr(errs, "a\nb") &&
                        !strstr(errs, "\x80"),
                "a newline and a byte >= 0x80 in the name are logged as '?', the line unbroken");
        CHECK(strstr(errs, "no such log \"??.log\"\n") != NULL && !strstr(errs, "\xff"),
                "a name of bytes >= 0x80 is logged as '?'");
        CHECK(strstr(errs, "REST /api/v1/x?INFO forged? refused: no such resource\n") != NULL &&
                        !strstr(errs, "\nINFO forged") && !strstr(errs, "\x1b"),
                "control bytes in a REST resource are logged as '?', the line unbroken");
        snprintf(warn, sizeof warn, "no such log \"%.*s...\"\n", LOGEXPORT_NAME_MAX - 4,
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        CHECK(strstr(errs, warn) != NULL,
                "a name too long for the buffer is logged cut, ending in ...");
        CHECK(strstr(errs, "[WARN] ") != NULL && (geteuid() == 0 || strstr(errs, "[ERR] ") != NULL),
                "refusals log WARN, a file that cannot be opened ERR");

        q.method = WEB_POST;
        q.path   = "/no-such-route";
        q.body   = "plugin-file";
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 404 && r.serve_file == NULL && !strcmp(out, "Not found\n"),
                "POST /no-such-route is an unknown path -> 404, no serve");

        unlink("router_log/stderr.txt");
        for (size_t i = 0; i < sizeof logs / sizeof logs[0]; i++)
            unlink(logs[i]);
        unlink(WEB_TOKEN_KEY);
        unlink(WEB_TOKEN_BOOT_ID);
        rmdir("router_log");
    }

    ctx.auth_nonce = "4d43ac86";
    {
        const char *ok_body  = "passwd=4d43ac86d30122d73dce746fd1ac3363";
        const char *bad_body = "passwd=4d43ac86deadbeefdeadbeefdeadbeef";
        ctx.creds            = &cred;

        ctx.activated = 0;
        web_request q = { .method = WEB_POST, .path = "/api/v1/session", .body = ok_body };
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 403 && r.extra_headers[0] == '\0' && strstr(out, "not_activated") != NULL,
                "POST /api/v1/session unactivated -> 403, no cookie");

        ctx.activated = 1;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 &&
                        strstr(r.extra_headers,
                                "Set-Cookie: auth=4d43ac86d30122d73dce746fd1ac3363") != NULL &&
                        strstr(r.extra_headers, "Location:") == NULL &&
                        strstr(out, "\"result\":\"ok\"") != NULL,
                "POST /api/v1/session valid -> 200 + Set-Cookie, no redirect");
        CHECK(strstr(r.extra_headers,
                      "Set-Cookie: auth=4d43ac86d30122d73dce746fd1ac3363; Path=/; HttpOnly; "
                      "SameSite=Strict\r\n") != NULL,
                "POST /api/v1/session cookie keeps Path/HttpOnly and adds SameSite=Strict");
        q.tls = 1;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 &&
                        strstr(r.extra_headers,
                                "Set-Cookie: auth=4d43ac86d30122d73dce746fd1ac3363; Path=/; "
                                "HttpOnly; SameSite=Strict; Secure\r\n") != NULL,
                "POST /api/v1/session over HTTPS marks the cookie Secure");
        q.tls = 0;

        q.body = bad_body;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && r.extra_headers[0] == '\0',
                "POST /api/v1/session bad proof -> 401, no cookie");

        q.body = "passwd=4d43ac86d30122d73dce746fd1ac3363%3B%20Domain%3Devil";
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && r.extra_headers[0] == '\0' &&
                        strstr(out, "invalid_credentials") != NULL,
                "POST /api/v1/session proof with cookie attributes -> 401, no cookie");
        q.body = "passwd=zzzzzzzzd30122d73dce746fd1ac3363";
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && r.extra_headers[0] == '\0',
                "POST /api/v1/session non-hex nonce -> 401, no cookie");
        {
            static const char *const forged[] = {
                "passwd=4d43ac86d30122d73dce746fd1ac3363;Max-Age=999999",
                "passwd=4d43ac86d30122d73dce746fd1ac3363,x",
                "passwd=4d43ac86d30122d73dce746fd1ac3363%20x",
                "passwd=4d43ac86d30122d73dce746fd1ac33630",
                "passwd=4d43ac86d30122d73dce746fd1ac336",
            };
            for (size_t i = 0; i < sizeof forged / sizeof forged[0]; i++) {
                q.body = forged[i];
                web_route(&q, &ctx, out, sizeof out, &r);
                CHECK(r.status == 401 && r.extra_headers[0] == '\0' &&
                                strstr(out, "invalid_credentials") != NULL,
                        "POST /api/v1/session proof not exactly 32 hex -> 401, no cookie");
            }
            q.body = "passwd=4D43AC86D30122D73DCE746FD1AC3363";
            web_route(&q, &ctx, out, sizeof out, &r);
            CHECK(r.status == 200 &&
                            strstr(r.extra_headers,
                                    "Set-Cookie: auth=4D43AC86D30122D73DCE746FD1AC3363;") != NULL,
                    "POST /api/v1/session upper-case hex proof -> 200 + Set-Cookie");
        }

        q.method = WEB_DELETE;
        q.body   = NULL;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(r.extra_headers, "Max-Age=0") != NULL,
                "DELETE /api/v1/session -> 200, clears cookie");

        q.method = WEB_GET;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 405, "GET /api/v1/session -> 405");

        /* dashboard: plain JSON object */
        q.path   = "/api/v1/dashboard";
        q.method = WEB_GET;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && out[0] == '{' && strstr(out, "dashboardCallback") == NULL &&
                        strstr(out, "\"hwtype\":\"Avalon Nano3s\"") != NULL &&
                        strstr(out, "realpoolpass") == NULL &&
                        strstr(out, "\"passwd1\":\"*\"") != NULL,
                "GET /api/v1/dashboard unauthed -> 200 plain JSON, passwd masked");
        CHECK(!strcmp(r.extra_headers, NO_STORE),
                "GET /api/v1/dashboard -> Cache-Control: no-store");

        web_request qa = { .method = WEB_GET, .path = "/api/v1/dashboard", .auth_cookie = COOKIE };
        const char *pool_conf = ctx.pool_conf;
        ctx.creds             = &cred;
        ctx.pool_conf         = ROUTER_CONF;
        web_route(&qa, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"passwd1\":\"realpoolpass\"") != NULL,
                "GET /api/v1/dashboard authed -> the saved password");
        CHECK(!strcmp(r.extra_headers, NO_STORE),
                "GET /api/v1/dashboard authed -> Cache-Control: no-store");
        ctx.creds     = &cred;
        ctx.pool_conf = pool_conf;

        q.path = "/api/v1/activation";
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"activated\":1") != NULL,
                "GET /api/v1/activation -> 200 {activated:1}");

        ctx.dna = "0201000012345678";
        q.path  = "/api/v1/auth";
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && out[0] == '{' && strstr(out, "getAuthCallback") == NULL &&
                        strstr(out, "\"auth\":\"4d43ac86\"") != NULL &&
                        strstr(out, "\"code\":\"") != NULL,
                "GET /api/v1/auth -> 200 plain JSON with auth + code");
        ctx.dna = NULL;

        ctx.qr_approved = 1;
        ctx.activated   = 1;
        web_request qp  = { .method = WEB_POST, .path = "/api/v1/qr-login" };
        web_route(&qp, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"auth\":\"4d43ac86\"") != NULL &&
                        strstr(out, "getCookieCallback") == NULL && strstr(out, "code") == NULL,
                "POST /api/v1/qr-login approved -> 200 plain JSON");
        CHECK(!strcmp(r.extra_headers,
                      "Set-Cookie: auth=4d43ac86d30122d73dce746fd1ac3363; Path=/; "
                      "Max-Age=86400; HttpOnly; SameSite=Strict\r\n"),
                "POST /api/v1/qr-login approved sets the HttpOnly cookie");
        ctx.qr_approved = 0;
        web_route(&qp, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && !strcmp(out, "{}"), "POST /api/v1/qr-login unapproved -> 200 {}");
        ctx.activated = 0;
        web_route(&qp, &ctx, out, sizeof out, &r);
        CHECK(r.status == 403 && !strcmp(out, "{\"error\":\"not_activated\"}") &&
                        r.extra_headers[0] == '\0',
                "POST /api/v1/qr-login unactivated -> 403 not_activated, no cookie");

        /* pools write: auth required */
        ctx.creds      = &cred;
        web_request qw = { .method = WEB_POST,
            .path                  = "/api/v1/pools",
            .body                  = "pool1=stratum%2Btcp%3A%2F%2Fa%3A1&worker1=w&passwd1=x" };
        web_route(&qw, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && strstr(out, "unauthorized") != NULL,
                "POST /api/v1/pools unauthed -> 401");
        qw.auth_cookie = COOKIE;
        web_route(&qw, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"pools\":\"1\"") != NULL,
                "POST /api/v1/pools authed -> 200, one pool saved");
        unlink(ROUTER_CONF);

        web_request qpg = { .method = WEB_DELETE, .path = "/api/v1/pools", .auth_cookie = COOKIE };
        web_route(&qpg, &ctx, out, sizeof out, &r);
        CHECK(r.status == 405, "DELETE /api/v1/pools -> 405");

        /* reboot: auth required */
        g_rebooted      = 0;
        g_reboot_ok     = 1;
        web_request qr2 = { .method = WEB_POST, .path = "/api/v1/system/reboot" };
        web_route(&qr2, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && g_rebooted == 0,
                "POST /api/v1/system/reboot unauthed -> 401, seam not fired");
        qr2.auth_cookie = COOKIE;
        web_route(&qr2, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && g_rebooted == 1,
                "POST /api/v1/system/reboot authed -> 200, reboot fired");
        g_reboot_ok = 0;
        web_route(&qr2, &ctx, out, sizeof out, &r);
        CHECK(r.status == 503, "POST /api/v1/system/reboot failed seam -> 503");
        g_reboot_ok = 1;

        /* timezone: GET is open, POST needs auth and applies without reboot */
        ctx.tz_get      = tz_get_stub;
        ctx.tz_set      = tz_set_stub;
        web_request qtg = { .method = WEB_GET, .path = "/api/v1/timezone" };
        web_route(&qtg, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"timezone\":\"Asia/Shanghai\"") != NULL,
                "GET /api/v1/timezone -> 200, current zone");

        g_tz_name[0]    = 0;
        g_tz_set_ok     = 1;
        web_request qts = {
            .method = WEB_POST, .path = "/api/v1/timezone", .body = "timezone=Asia/Tokyo"
        };
        web_route(&qts, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && g_tz_name[0] == 0,
                "POST /api/v1/timezone unauthed -> 401, seam not fired");
        qts.auth_cookie = COOKIE;
        web_route(&qts, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && !strcmp(g_tz_name, "Asia/Tokyo"),
                "POST /api/v1/timezone authed -> 200, zone applied");

        /* a client clock in the form does not take part */
        g_tz_name[0]    = 0;
        web_request qtm = { .method = WEB_POST,
            .path                   = "/api/v1/timezone",
            .body                   = "timezone=Asia/Seoul&epoch=1",
            .auth_cookie            = COOKIE };
        web_route(&qtm, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && !strcmp(g_tz_name, "Asia/Seoul"),
                "POST /api/v1/timezone with a client epoch -> 200, epoch ignored");

        g_tz_set_ok = 0;
        web_route(&qts, &ctx, out, sizeof out, &r);
        CHECK(r.status == 503, "POST /api/v1/timezone rejected by miner -> 503");
        g_tz_set_ok = -1;
        web_route(&qts, &ctx, out, sizeof out, &r);
        CHECK(r.status == 400 && strstr(out, "not available") != NULL,
                "POST /api/v1/timezone zone not installed -> 400");
        g_tz_set_ok = 1;
        ctx.tz_set  = NULL;
        web_route(&qts, &ctx, out, sizeof out, &r);
        CHECK(r.status == 500 && strstr(out, "\"reason\":\"cannot set the timezone\"") != NULL,
                "POST /api/v1/timezone without a timezone backend -> 500");
        ctx.tz_set = tz_set_stub;

        static const char *const bad_tz[] = {
            "timezone=..%2F..%2Fetc%2Fpasswd&epoch=1787102966",
            "timezone=%2Fetc%2Flocaltime&epoch=1787102966",
            "timezone=Asia%2F%2FTokyo&epoch=1787102966",
            "timezone=Asia%2F&epoch=1787102966",
            "timezone=Asia%2FTo%20kyo&epoch=1787102966",
            "timezone=Asia%2FTokyo%3Brm&epoch=1787102966",
            "timezone=Aaaaaaaaaaaaaaaaaaaaaaaa%2FBbbbbbbbbbbbbbbbbbbbbbbb&epoch=1787102966",
        };
        for (size_t i = 0; i < sizeof bad_tz / sizeof bad_tz[0]; i++) {
            g_tz_name[0]   = 0;
            web_request qb = { .method = WEB_POST,
                .path                  = "/api/v1/timezone",
                .body                  = bad_tz[i],
                .auth_cookie           = COOKIE };
            web_route(&qb, &ctx, out, sizeof out, &r);
            CHECK(r.status == 400 && g_tz_name[0] == 0,
                    "POST /api/v1/timezone malformed zone name -> 400, seam not fired");
        }

        g_tz_name[0]    = 0;
        web_request qtp = { .method = WEB_POST,
            .path                   = "/api/v1/timezone",
            .body                   = "timezone=Etc%2FGMT%2B8&epoch=1787102966",
            .auth_cookie            = COOKIE };
        web_route(&qtp, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && !strcmp(g_tz_name, "Etc/GMT+8"),
                "POST /api/v1/timezone Etc/GMT+8 -> 200");

        web_request qtd = { .method = WEB_DELETE, .path = "/api/v1/timezone" };
        web_route(&qtd, &ctx, out, sizeof out, &r);
        CHECK(r.status == 405, "DELETE /api/v1/timezone -> 405");

        ctx.lcd_pelish  = 0;
        g_core_calls    = 0;
        web_request qlg = { .method = WEB_GET, .path = "/api/v1/lcd-display" };
        web_route(&qlg, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && g_core_calls == 0, "GET /api/v1/lcd-display unauthed -> 401");
        qlg.auth_cookie = COOKIE;
        web_route(&qlg, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"pelish\":0") != NULL &&
                        strstr(out, "\"rotate_s\":180") != NULL && g_core_calls == 0,
                "GET /api/v1/lcd-display -> 200 stock-only");
        ctx.lcd_pelish = 1;
        web_route(&qlg, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"pelish\":1") != NULL,
                "GET /api/v1/lcd-display pelish on");

        web_request qlp = { .method = WEB_POST, .path = "/api/v1/lcd-display", .body = "pelish=1" };
        web_route(&qlp, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && g_core_calls == 0,
                "POST /api/v1/lcd-display unauthed -> 401, core not called");
        qlp.auth_cookie = COOKIE;
        web_route(&qlp, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"pelish\":1") != NULL && g_core_calls == 1 &&
                        !strcmp(g_core_cmd, "ascset") && !strcmp(g_core_param, "0,lcd,5:1"),
                "POST /api/v1/lcd-display pelish=1 -> ascset lcd 5:1");
        qlp.body = "pelish=0";
        web_route(&qlp, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && !strcmp(g_core_param, "0,lcd,5:0"),
                "POST /api/v1/lcd-display pelish=0 -> ascset lcd 5:0");
        qlp.body     = "pelish=2";
        g_core_calls = 0;
        web_route(&qlp, &ctx, out, sizeof out, &r);
        CHECK(r.status == 400 && g_core_calls == 0, "POST /api/v1/lcd-display pelish=2 -> 400");
        rest(WEB_POST, "/api/v1/lcd-display", "application/json", "{\"pelish\":1}", NULL, 1, out,
                sizeof out, &r);
        CHECK(r.status == 200 && !strcmp(g_core_param, "0,lcd,5:1"),
                "POST /api/v1/lcd-display JSON pelish 1");
        rest(WEB_POST, "/api/v1/lcd", "application/json",
                "{\"command\":\"lcd\",\"parameter\":\"1:50\"}", NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 200 && !strcmp(g_core_cmd, "ascset") &&
                        !strcmp(g_core_param, "0,lcd,1:50"),
                "POST /api/v1/lcd stock command is unchanged");

        ctx.wifi_eth     = 0;
        ctx.wifi_eth_set = wifi_eth_set_stub;
        g_wifi_eth_set   = 1;
        g_wifi_eth_val   = 0;
        g_core_calls     = 0;
        web_request qwg  = { .method = WEB_GET, .path = "/api/v1/wifi-eth" };
        web_route(&qwg, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && g_core_calls == 0, "GET /api/v1/wifi-eth unauthed -> 401");
        qwg.auth_cookie = COOKIE;
        web_route(&qwg, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"wifi_eth\":0") != NULL && g_core_calls == 0,
                "GET /api/v1/wifi-eth -> 200 default off");
        ctx.wifi_eth = 1;
        web_route(&qwg, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"wifi_eth\":1") != NULL,
                "GET /api/v1/wifi-eth keep-wifi on");
        web_request qwp = { .method = WEB_POST, .path = "/api/v1/wifi-eth", .body = "wifi_eth=1" };
        web_route(&qwp, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && g_wifi_eth_val == 0,
                "POST /api/v1/wifi-eth unauthed -> 401");
        qwp.auth_cookie = COOKIE;
        web_route(&qwp, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"wifi_eth\":1") != NULL && g_wifi_eth_val == 1 &&
                        g_core_calls == 0,
                "POST /api/v1/wifi-eth wifi_eth=1 -> persist, no ascset");
        qwp.body = "wifi_eth=0";
        web_route(&qwp, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && g_wifi_eth_val == 0, "POST /api/v1/wifi-eth wifi_eth=0");
        qwp.body = "wifi_eth=2";
        web_route(&qwp, &ctx, out, sizeof out, &r);
        CHECK(r.status == 400, "POST /api/v1/wifi-eth wifi_eth=2 -> 400");
        rest(WEB_POST, "/api/v1/wifi-eth", "application/json", "{\"wifi_eth\":1}", NULL, 1, out,
                sizeof out, &r);
        CHECK(r.status == 200 && g_wifi_eth_val == 1, "POST /api/v1/wifi-eth JSON 1");

        /* unknown REST resource */
        web_request qu = { .method = WEB_GET, .path = "/api/v1/bogus" };
        web_route(&qu, &ctx, out, sizeof out, &r);
        CHECK(r.status == 404 && strstr(out, "\"STATUS\":\"E\"") != NULL &&
                        strstr(out, "\"Code\":14,\"Msg\":\"Invalid command bogus\"") != NULL,
                "GET /api/v1/bogus -> 404 STATUS E Code 14 naming the resource");
        web_request qf = { .method = WEB_POST, .path = "/api/v1/firmware", .auth_cookie = COOKIE };
        web_route(&qf, &ctx, out, sizeof out, &r);
        CHECK(r.status == 404 &&
                        strstr(out, "\"Code\":14,\"Msg\":\"Invalid command firmware\"") != NULL,
                "POST /api/v1/firmware -> 404 STATUS E Code 14, no upload alias");

        static const struct {
            web_method  method; /* method the resource does not accept */
            const char *path;
            const char *name;
        } wrong_method[] = {
            { WEB_GET, "/api/v1/session", "GET /api/v1/session -> 405 Code 505" },
            { WEB_POST, "/api/v1/dashboard", "POST /api/v1/dashboard -> 405 Code 505" },
            { WEB_DELETE, "/api/v1/activation", "DELETE /api/v1/activation -> 405 Code 505" },
            { WEB_DELETE, "/api/v1/auth", "DELETE /api/v1/auth -> 405 Code 505" },
            { WEB_GET, "/api/v1/qr-login", "GET /api/v1/qr-login -> 405 Code 505" },
            { WEB_DELETE, "/api/v1/pools", "DELETE /api/v1/pools -> 405 Code 505" },
            { WEB_GET, "/api/v1/system", "GET /api/v1/system -> 405 Code 505" },
            { WEB_POST, "/api/v1/summary", "POST /api/v1/summary -> 405 Code 505" },
            { WEB_GET, "/api/v1/system/reboot", "GET /api/v1/system/reboot -> 405 Code 505" },
        };
        ctx.creds  = &cred;
        g_rebooted = 0;
        for (size_t i = 0; i < sizeof wrong_method / sizeof wrong_method[0]; i++) {
            web_request qm = { .method = wrong_method[i].method,
                .path                  = wrong_method[i].path,
                .auth_cookie           = COOKIE };
            r.content_type = "stale";
            r.body_len     = 99;
            web_route(&qm, &ctx, out, sizeof out, &r);
            CHECK(r.status == 405 && method_reply(&r, out) && r.serve_file == NULL &&
                            r.extra_headers[0] == '\0',
                    wrong_method[i].name);
        }
        CHECK(g_rebooted == 0, "GET /api/v1/system/reboot -> 405 does not fire the reboot seam");
        ctx.activated = 0;
    }

    {
        static const char *const reads[] = { "version", "summary", "pools", "stats", "estats",
            "devs", "edevs", "devdetails", "config", "coin", "litestats" };
        static const struct {
            const char *path;
            const char *cmd;
            int         ascset;
            int         no_login;
        } writes[] = {
            { "/api/v1/pools", "setpool", 0, 0 },
            { "/api/v1/pools", "addpool", 0, 0 },
            { "/api/v1/pools", "removepool", 0, 0 },
            { "/api/v1/pools", "enablepool", 0, 0 },
            { "/api/v1/pools", "disablepool", 0, 0 },
            { "/api/v1/pools", "switchpool", 0, 0 },
            { "/api/v1/pools", "poolpriority", 0, 0 },
            { "/api/v1/stats", "zero", 0, 0 },
            { "/api/v1/system", "softon", 0, 0 },
            { "/api/v1/system", "softoff", 0, 0 },
            { "/api/v1/system", "reboot", 1, 0 },
            { "/api/v1/system", "filter-clean", 1, 0 },
            { "/api/v1/system/reboot", "reboot", 1, 0 },
            { "/api/v1/time", "time", 0, 0 },
            { "/api/v1/mode", "workmode", 1, 0 },
            { "/api/v1/mode", "work_mode_lvl", 1, 0 },
            { "/api/v1/mode", "worklevel", 1, 0 },
            { "/api/v1/led", "ledmode", 1, 0 },
            { "/api/v1/led", "ledset", 1, 0 },
            { "/api/v1/led", "nightlamp", 1, 0 },
            { "/api/v1/lcd", "lcd", 1, 0 },
            { "/api/v1/password", "password", 1, 0 },
            { "/api/v1/activation", "activate", 1, 1 },
            { "/api/v1/qr-auth", "qr_auth", 1, 1 },
        };
        static const struct {
            const char *path;
            const char *cmd;
        } refused[] = {
            { "/api/v1/pools", "reboot" },
            { "/api/v1/pools", "summary" },
            { "/api/v1/stats", "softoff" },
            { "/api/v1/system", "ascset" },
            { "/api/v1/system", "workmode" },
            { "/api/v1/system/reboot", "softoff" },
            { "/api/v1/time", "zero" },
            { "/api/v1/mode", "ledmode" },
            { "/api/v1/led", "lcd" },
            { "/api/v1/lcd", "ledset" },
            { "/api/v1/password", "lcd" },
            { "/api/v1/activation", "qr_auth" },
            { "/api/v1/qr-auth", "activate" },
        };
        char body[256];
        char want[128];
        char msg[320];
        char big[1200 + 64];

        ctx.creds = &cred;
        for (size_t i = 0; i < sizeof reads / sizeof reads[0]; i++) {
            char path[64];

            snprintf(path, sizeof path, "/api/v1/%s", reads[i]);
            rest(WEB_GET, path, NULL, NULL, NULL, 1, out, sizeof out, &r);
            snprintf(msg, sizeof msg, "GET %s -> core %s, empty parameter, reply as is", path,
                    reads[i]);
            CHECK(r.status == 200 && g_core_calls == 1 && !strcmp(g_core_cmd, reads[i]) &&
                            g_core_param[0] == '\0' && !strcmp(out, CORE_OK) &&
                            r.body_len == strlen(CORE_OK) &&
                            !strcmp(r.content_type, "application/json"),
                    msg);
            rest(WEB_GET, path, NULL, NULL, NULL, 0, out, sizeof out, &r);
            snprintf(msg, sizeof msg, "GET %s unauthed -> 401 Code 602, core not called", path);
            CHECK(r.status == 401 && g_core_calls == 0 && status_is(out, REST_UNAUTHORIZED), msg);
        }
        rest(WEB_GET, "/api/v1/stats", NULL, NULL, "parameter=0%2C1", 1, out, sizeof out, &r);
        CHECK(r.status == 200 && !strcmp(g_core_cmd, "stats") && !strcmp(g_core_param, "0,1"),
                "GET /api/v1/stats?parameter=0%2C1 -> core stats \"0,1\"");
        rest(WEB_GET, "/api/v1/loop", NULL, NULL, NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 200 && g_core_calls == 1 && !strcmp(g_core_cmd, "ascset") &&
                        !strcmp(g_core_param, "0,loop") && !strcmp(out, CORE_OK) &&
                        r.body_len == strlen(CORE_OK) &&
                        !strcmp(r.content_type, "application/json"),
                "GET /api/v1/loop -> core ascset \"0,loop\", reply as is");
        rest(WEB_GET, "/api/v1/loop", NULL, NULL, "parameter=1", 1, out, sizeof out, &r);
        CHECK(r.status == 200 && g_core_calls == 1 && !strcmp(g_core_param, "0,loop"),
                "GET /api/v1/loop?parameter=1 -> query ignored, core ascset \"0,loop\"");
        rest(WEB_GET, "/api/v1/loop", NULL, NULL, NULL, 0, out, sizeof out, &r);
        CHECK(r.status == 401 && g_core_calls == 0 && status_is(out, REST_UNAUTHORIZED),
                "GET /api/v1/loop unauthed -> 401 Code 602, core not called");
        rest(WEB_POST, "/api/v1/loop", "application/json", "{\"command\":\"loop\"}", NULL, 1, out,
                sizeof out, &r);
        CHECK(r.status == 405 && g_core_calls == 0, "POST /api/v1/loop -> 405, core not called");
        rest(WEB_GET, "/api/v1/pools", NULL, NULL, "parameter=a&parameter=b", 1, out, sizeof out,
                &r);
        CHECK(r.status == 400 && g_core_calls == 0 &&
                        strstr(out, "\"Code\":501,\"Msg\":\"Invalid request\"") != NULL,
                "GET /api/v1/pools with a repeated parameter -> 400 Code 501, core not called");

        for (size_t i = 0; i < sizeof writes / sizeof writes[0]; i++) {
            snprintf(body, sizeof body, "{\"command\":\"%s\",\"parameter\":\"p,1\"}",
                    writes[i].cmd);
            if (writes[i].ascset)
                snprintf(want, sizeof want, "0,%s,p,1", writes[i].cmd);
            else
                snprintf(want, sizeof want, "p,1");
            rest(WEB_POST, writes[i].path, "application/json", body, NULL, 1, out, sizeof out, &r);
            snprintf(msg, sizeof msg, "POST %s %s -> core %s \"%s\"", writes[i].path, writes[i].cmd,
                    writes[i].ascset ? "ascset" : writes[i].cmd, want);
            CHECK(r.status == 200 && g_core_calls == 1 &&
                            !strcmp(g_core_cmd, writes[i].ascset ? "ascset" : writes[i].cmd) &&
                            !strcmp(g_core_param, want) && !strcmp(out, CORE_OK) &&
                            !strcmp(r.content_type, "application/json"),
                    msg);
            rest(WEB_POST, writes[i].path, "application/json", body, NULL, 0, out, sizeof out, &r);
            if (writes[i].no_login) {
                snprintf(msg, sizeof msg, "POST %s %s unauthed -> core ascset \"%s\"",
                        writes[i].path, writes[i].cmd, want);
                CHECK(r.status == 200 && g_core_calls == 1 && !strcmp(g_core_cmd, "ascset") &&
                                !strcmp(g_core_param, want) && !strcmp(out, CORE_OK),
                        msg);
                continue;
            }
            snprintf(msg, sizeof msg, "POST %s %s unauthed -> 401 Code 602, core not called",
                    writes[i].path, writes[i].cmd);
            CHECK(r.status == 401 && g_core_calls == 0 && status_is(out, REST_UNAUTHORIZED), msg);
        }

        for (size_t i = 0; i < sizeof refused / sizeof refused[0]; i++) {
            snprintf(body, sizeof body, "{\"command\":\"%s\",\"parameter\":\"1\"}", refused[i].cmd);
            rest(WEB_POST, refused[i].path, "application/json", body, NULL, 1, out, sizeof out, &r);
            snprintf(msg, sizeof msg, "POST %s %s -> 400 Code 14, core not called", refused[i].path,
                    refused[i].cmd);
            snprintf(want, sizeof want, "\"Code\":14,\"Msg\":\"Invalid command %s\"",
                    refused[i].cmd);
            CHECK(r.status == 400 && g_core_calls == 0 && strstr(out, want) != NULL, msg);
        }
        rest(WEB_POST, "/api/v1/led", "application/json",
                "{\"command\":\"led\\tmode0123456789012345678901234567890123\"}", NULL, 1, out,
                sizeof out, &r);
        CHECK(r.status == 400 && g_core_calls == 0 &&
                        strstr(out,
                                "\"Msg\":\"Invalid command ledmode0123456789012345678901234\"}"),
                "an unknown command is echoed without control characters, cut to 32 bytes");

        rest(WEB_POST, "/api/v1/system/reboot", "application/json", "{\"command\":\"reboot\"}",
                NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 200 && !strcmp(g_core_cmd, "ascset") && !strcmp(g_core_param, "0,reboot"),
                "POST /api/v1/system/reboot without parameter -> ascset \"0,reboot\"");
        rest(WEB_POST, "/api/v1/system/reboot", "application/json",
                "{\"command\":\"reboot\",\"parameter\":5}", NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 200 && !strcmp(g_core_param, "0,reboot,5"),
                "POST /api/v1/system/reboot numeric parameter 5 -> ascset \"0,reboot,5\"");
        rest(WEB_POST, "/api/v1/pools", "application/json",
                "{\"command\":\"switchpool\",\"parameter\":0}", NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 200 && !strcmp(g_core_cmd, "switchpool") && !strcmp(g_core_param, "0"),
                "POST /api/v1/pools switchpool parameter 0 -> core switchpool \"0\"");
        rest(WEB_POST, "/api/v1/time", "application/json",
                "{\"command\":\"time\",\"parameter\":\"set,t:1787102966,Asia/Tokyo\"}", NULL, 1,
                out, sizeof out, &r);
        CHECK(r.status == 200 && !strcmp(g_core_cmd, "time") &&
                        !strcmp(g_core_param, "set,t:1787102966,Asia/Tokyo"),
                "POST /api/v1/time -> core time with the parameter as is");

        static const char *const json_types[] = { "application/json; charset=utf-8",
            "Application/JSON" };
        for (size_t i = 0; i < sizeof json_types / sizeof json_types[0]; i++) {
            rest(WEB_POST, "/api/v1/stats", json_types[i], "{\"command\":\"zero\"}", NULL, 1, out,
                    sizeof out, &r);
            snprintf(msg, sizeof msg, "Content-Type \"%s\" takes the JSON path", json_types[i]);
            CHECK(r.status == 200 && g_core_calls == 1 && !strcmp(g_core_cmd, "zero"), msg);
        }

        static const char *const bad_bodies[] = { "not json", "[\"zero\"]", "{\"command\":1}",
            "{\"command\":null}", "{\"command\":[\"zero\"]}", "{\"command\":{}}",
            "{\"parameter\":\"1\"}", "{\"command\":\"zero\",\"parameter\":{}}",
            "{\"command\":\"zero\",\"parameter\":true}",
            "{\"command\":\"zero\",\"parameter\":null}", "{\"command\":\"zero\",\"parameter\":[1]}",
            "" };
        for (size_t i = 0; i < sizeof bad_bodies / sizeof bad_bodies[0]; i++) {
            rest(WEB_POST, "/api/v1/stats", "application/json", bad_bodies[i], NULL, 1, out,
                    sizeof out, &r);
            snprintf(msg, sizeof msg, "POST /api/v1/stats body '%s' -> 400 Code 501, no core call",
                    bad_bodies[i]);
            CHECK(r.status == 400 && g_core_calls == 0 && strstr(out, "\"Code\":501,") != NULL,
                    msg);
        }
        memset(big, 0, sizeof big);
        strcpy(big, "{\"command\":\"zero\",\"parameter\":\"");
        memset(big + strlen(big), 'x', 1100);
        strcat(big, "\"}");
        rest(WEB_POST, "/api/v1/stats", "application/json", big, NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 400 && g_core_calls == 0,
                "POST parameter longer than 1023 bytes -> 400, core not called");

        g_core_reply = CORE_ERR;
        rest(WEB_POST, "/api/v1/led", "application/json",
                "{\"command\":\"ledmode\",\"parameter\":\"1\"}", NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 400 && !strcmp(out, CORE_ERR) &&
                        !strcmp(r.content_type, "application/json"),
                "core STATUS E -> HTTP 400, core reply as is");
        rest(WEB_GET, "/api/v1/summary", NULL, NULL, NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 400 && !strcmp(out, CORE_ERR), "GET with core STATUS E -> HTTP 400");
        g_core_reply = CORE_OK;

        g_core_rc = -1;
        rest(WEB_POST, "/api/v1/system", "application/json", "{\"command\":\"softoff\"}", NULL, 1,
                out, sizeof out, &r);
        CHECK(r.status == 504 && strstr(out, "\"STATUS\":\"E\"") != NULL &&
                        strstr(out, "\"Code\":500,\"Msg\":\"Command timeout\"") != NULL &&
                        !strcmp(r.content_type, "application/json"),
                "core channel failure on a write -> 504 Code 500");
        rest(WEB_GET, "/api/v1/version", NULL, NULL, NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 504 && strstr(out, "\"Code\":500,") != NULL,
                "core channel failure on a read -> 504 Code 500");

        g_core_rc = CORE_API_TRUNCATED;
        rest(WEB_GET, "/api/v1/estats", NULL, NULL, NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 500 && strstr(out, "\"STATUS\":\"E\"") != NULL &&
                        strstr(out, "\"Code\":500,\"Msg\":\"Reply too long: not sent\"") != NULL &&
                        r.body_len == strlen(out) && !strcmp(r.content_type, "application/json"),
                "core reply larger than the buffer on a read -> 500 Code 500, never a partial 200");
        rest(WEB_POST, "/api/v1/system", "application/json", "{\"command\":\"softoff\"}", NULL, 1,
                out, sizeof out, &r);
        CHECK(r.status == 500 && strstr(out, "\"Code\":500,") != NULL,
                "core reply larger than the buffer on a write -> 500 Code 500");
        g_core_rc = 0;

        rest(WEB_POST, "/api/v1/pools", NULL,
                "pool1=stratum%2Btcp%3A%2F%2Fa%3A1&worker1=w&passwd1=x", NULL, 1, out, sizeof out,
                &r);
        CHECK(r.status == 200 && g_core_calls == 0 && strstr(out, "\"pools\":\"1\"") != NULL,
                "POST /api/v1/pools form without a JSON type -> form save, core not called");
        unlink(ROUTER_CONF);
        rest(WEB_POST, "/api/v1/pools", "application/x-www-form-urlencoded",
                "pool1=stratum%2Btcp%3A%2F%2Fa%3A1&worker1=w&passwd1=x", NULL, 1, out, sizeof out,
                &r);
        CHECK(r.status == 200 && g_core_calls == 0 && strstr(out, "\"pools\":\"1\"") != NULL,
                "POST /api/v1/pools urlencoded form -> form save, core not called");
        unlink(ROUTER_CONF);
        rest(WEB_POST, "/api/v1/pools", "application/jsonp", "{\"command\":\"addpool\"}", NULL, 1,
                out, sizeof out, &r);
        CHECK(r.status == 400 && g_core_calls == 0,
                "POST /api/v1/pools application/jsonp -> form path, core not called");

        g_rebooted  = 0;
        g_reboot_ok = 1;
        rest(WEB_POST, "/api/v1/system/reboot", NULL, NULL, NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 200 && g_rebooted == 1 && g_core_calls == 0,
                "POST /api/v1/system/reboot without JSON -> reboot seam, core not called");
        g_rebooted = 0;
        rest(WEB_POST, "/api/v1/system/reboot", "application/json",
                "{\"command\":\"reboot\",\"parameter\":\"3\"}", NULL, 1, out, sizeof out, &r);
        CHECK(g_rebooted == 0 && !strcmp(g_core_param, "0,reboot,3"),
                "POST /api/v1/system/reboot JSON -> core ascset, reboot seam not fired");

        rest(WEB_POST, "/api/v1/activation", NULL, "x=1", NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 400 && g_core_calls == 0 &&
                        strstr(out, "\"Code\":501,\"Msg\":\"JSON request body required\"") != NULL,
                "POST /api/v1/activation without JSON -> 400 Code 501");
        rest(WEB_POST, "/api/v1/activation", NULL, "x=1", NULL, 0, out, sizeof out, &r);
        CHECK(r.status == 400 && g_core_calls == 0 &&
                        strstr(out, "\"Code\":501,\"Msg\":\"JSON request body required\"") != NULL,
                "POST /api/v1/activation without JSON unauthed -> 400 Code 501, as qr-auth");
        rest(WEB_POST, "/api/v1/qr-auth", NULL, "x=1", NULL, 0, out, sizeof out, &r);
        CHECK(r.status == 400 && g_core_calls == 0 &&
                        strstr(out, "\"Code\":501,\"Msg\":\"JSON request body required\"") != NULL,
                "POST /api/v1/qr-auth without JSON unauthed -> 400 Code 501");
        rest(WEB_GET, "/api/v1/activation", NULL, NULL, NULL, 0, out, sizeof out, &r);
        CHECK(r.status == 200 && strstr(out, "\"activated\"") != NULL && g_core_calls == 0,
                "GET /api/v1/activation unchanged, core not called");

        {
            web_request qp = { .method = WEB_POST,
                .path                  = "/api/v1/activation",
                .body                  = "{\"command\":\"activate\",\"parameter\":{}}",
                .content_type          = "application/json",
                .remote_addr           = "198.51.100.9",
                .tls                   = 1 };

            for (int i = 0; i < 6; i++) {
                g_core_calls = 0;
                web_route(&qp, &ctx, out, sizeof out, &r);
            }
            CHECK(r.status == 400 && g_core_calls == 0 && strstr(out, "\"Code\":501,") != NULL,
                    "six bad activation bodies -> 400 Code 501, core not called");
            qp.body      = "{\"command\":\"activate\",\"parameter\":\"sig\"}";
            g_core_calls = 0;
            web_route(&qp, &ctx, out, sizeof out, &r);
            CHECK(r.status == 200 && g_core_calls == 1,
                    "refusals before the core call do not count towards the lock");

            g_core_reply   = CORE_ERR;
            qp.remote_addr = "198.51.100.7";
            for (int i = 0; i < 5; i++) {
                g_core_calls = 0;
                web_route(&qp, &ctx, out, sizeof out, &r);
                snprintf(msg, sizeof msg, "unauthed activation %d rejected by core -> 400 as is",
                        i + 1);
                CHECK(r.status == 400 && g_core_calls == 1 && !strcmp(out, CORE_ERR), msg);
            }
            g_core_calls = 0;
            web_route(&qp, &ctx, out, sizeof out, &r);
            CHECK(r.status == 429 && g_core_calls == 0 && status_is(out, REST_LOCKED) &&
                            !strcmp(r.content_type, "application/json"),
                    "the 6th activation after 5 core rejections -> 429 Code 506, core not called");
            g_core_reply = CORE_OK;
            g_core_calls = 0;
            web_route(&qp, &ctx, out, sizeof out, &r);
            CHECK(r.status == 429 && g_core_calls == 0,
                    "a locked address stays locked for a well-formed activation");
            qp.path      = "/api/v1/qr-auth";
            qp.body      = "{\"command\":\"qr_auth\",\"parameter\":\"proof\"}";
            g_core_calls = 0;
            web_route(&qp, &ctx, out, sizeof out, &r);
            CHECK(r.status == 429 && g_core_calls == 0 && status_is(out, REST_LOCKED),
                    "the lock covers POST /api/v1/qr-auth from the same address");
            qp.remote_addr = "198.51.100.8";
            g_core_calls   = 0;
            web_route(&qp, &ctx, out, sizeof out, &r);
            CHECK(r.status == 200 && g_core_calls == 1 && !strcmp(g_core_cmd, "ascset") &&
                            !strcmp(g_core_param, "0,qr_auth,proof"),
                    "another address is not locked: unauthed qr_auth -> core ascset");

            web_request qs = { .method = WEB_POST,
                .path                  = "/api/v1/stats",
                .body                  = "{\"command\":\"zero\"}",
                .content_type          = "application/json",
                .auth_cookie           = COOKIE,
                .remote_addr           = "198.51.100.7",
                .tls                   = 1 };
            g_core_calls   = 0;
            web_route(&qs, &ctx, out, sizeof out, &r);
            CHECK(r.status == 200 && g_core_calls == 1,
                    "the activation lock does not refuse a logged-in write from that address");
        }

        rest(WEB_POST, "/api/v1/led", NULL, "ledmode=1", NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 400 && g_core_calls == 0 &&
                        strstr(out, "\"Code\":501,\"Msg\":\"JSON request body required\"") != NULL,
                "POST /api/v1/led without JSON -> 400 Code 501");
        rest(WEB_POST, "/api/v1/led", NULL, "ledmode=1", NULL, 0, out, sizeof out, &r);
        CHECK(r.status == 401 && g_core_calls == 0 && status_is(out, REST_UNAUTHORIZED),
                "POST /api/v1/led without JSON unauthed -> 401 Code 602");

        rest(WEB_POST, "/api/v1/timezone", "application/json", "{\"command\":\"time\"}", NULL, 1,
                out, sizeof out, &r);
        CHECK(r.status == 400 && g_core_calls == 0,
                "POST /api/v1/timezone JSON -> timezone form path, core not called");

        rest(WEB_POST, "/api/v1/plugin-res", "application/json", "{\"command\":\"plugin-cmd\"}",
                NULL, 1, out, sizeof out, &r);
        CHECK(r.status == 404 && g_core_calls == 0 &&
                        strstr(out, "\"Code\":14,\"Msg\":\"Invalid command plugin-res\"") != NULL,
                "no plugin resources: POST /api/v1/plugin-res -> 404 Code 14");
        size_t nplug = 1;
        CHECK(web_plugin_rest(&nplug) == NULL && nplug == 0, "web_plugin_rest stub is empty");
    }

    {
        static const char nf[] = "Not found\n";
        char              small[64];
        web_request       qn = { .method = WEB_GET, .path = "/no-such-route" };

        memset(small, '#', sizeof small);
        CHECK(web_route(&qn, &ctx, small, sizeof nf, &r) == 404 && r.body_len == sizeof nf - 1 &&
                        !strcmp(small, nf) && small[sizeof nf] == '#',
                "body_len below cap: the reply is sent whole");

        memset(small, '#', sizeof small);
        CHECK(web_route(&qn, &ctx, small, sizeof nf - 1, &r) == 404 &&
                        r.body_len == sizeof nf - 2 && strlen(small) == sizeof nf - 2 &&
                        !memcmp(small, nf, sizeof nf - 2) && small[sizeof nf - 1] == '#',
                "body_len == cap: body_len clamps to cap - 1 and out stays NUL-terminated");

        memset(small, '#', sizeof small);
        CHECK(web_route(&qn, &ctx, small, 8, &r) == 404 && r.body_len == 7 &&
                        !strcmp(small, "Not fou") && small[8] == '#',
                "body_len > cap on a text reply: clamps to cap - 1, nothing written past cap");

        web_request qa = { .method = WEB_GET, .path = "/api/v1/auth" };
        ctx.auth_nonce = "4d43ac86";
        memset(small, '#', sizeof small);
        CHECK(web_route(&qa, &ctx, small, 10, &r) == 200 && r.body_len == 9 && strlen(small) == 9 &&
                        !strncmp(small, "{\"auth\":\"", 9) && small[10] == '#',
                "body_len > cap on a JSON reply: clamps to cap - 1, nothing written past cap");

        memset(small, '#', sizeof small);
        CHECK(web_route(&qn, &ctx, small, 1, &r) == 404 && r.body_len == 0 && small[0] == '\0' &&
                        small[1] == '#',
                "cap 1: empty body, out holds only the NUL");
    }

    {
        char self[256] = "";
        static const struct {
            const char *host, *origin, *referer;
            int         changes_state, refused;
            const char *name;
        } hc[] = {
            { "evil.example", NULL, NULL, 0, 1, "Host evil.example -> refused" },
            { "evil.example:80", NULL, NULL, 1, 1, "Host evil.example:80 -> refused" },
            { "192.168.1.5:80", NULL, NULL, 0, 0, "Host 192.168.1.5:80 -> allowed" },
            { "192.168.1.5", NULL, NULL, 1, 0, "Host 192.168.1.5 -> allowed" },
            { "[fe80::1]", NULL, NULL, 0, 0, "Host [fe80::1] -> allowed" },
            { "[fe80::1]:8080", NULL, NULL, 0, 0, "Host [fe80::1]:8080 -> allowed" },
            { "localhost", NULL, NULL, 0, 0, "Host localhost -> allowed" },
            { "LocalHost:80", NULL, NULL, 0, 0, "Host LocalHost:80 -> allowed" },
            { NULL, "http://evil.example", NULL, 1, 0, "no Host -> allowed" },
            { "", NULL, NULL, 0, 1, "empty Host -> refused" },
            { "192.168.1", NULL, NULL, 0, 1, "Host 192.168.1 -> refused" },
            { "192.168.1.5.evil.example", NULL, NULL, 0, 1,
                    "Host 192.168.1.5.evil.example -> refused" },
            { "192.168.1.5:", NULL, NULL, 0, 1, "Host with an empty port -> refused" },
            { "192.168.1.5:80x", NULL, NULL, 0, 1, "Host with a bad port -> refused" },
            { "fe80::1", NULL, NULL, 0, 1, "unbracketed IPv6 Host -> refused" },
            { "[fe80::1", NULL, NULL, 0, 1, "unclosed IPv6 bracket -> refused" },
            { "[evil.example]", NULL, NULL, 0, 1, "bracketed name -> refused" },
            { "192.168.1.5", "http://evil.example", NULL, 1, 1,
                    "Origin evil.example, Host 192.168.1.5 -> refused" },
            { "192.168.1.5", "http://192.168.1.5", NULL, 1, 0, "matching Origin -> allowed" },
            { "192.168.1.5:8080", "http://192.168.1.5:8080", NULL, 1, 0,
                    "matching Origin with port -> allowed" },
            { "192.168.1.5:8080", "http://192.168.1.5", NULL, 1, 1,
                    "Origin without the Host port -> refused" },
            { "[fe80::1]", "http://[fe80::1]", NULL, 1, 0, "matching IPv6 Origin -> allowed" },
            { "192.168.1.5", "null", NULL, 1, 1, "Origin null -> refused" },
            { "192.168.1.5", NULL, NULL, 1, 0, "no Origin, no Referer -> allowed" },
            { "192.168.1.5", NULL, "http://192.168.1.5/pools.html?x=1", 1, 0,
                    "matching Referer -> allowed" },
            { "192.168.1.5", NULL, "https://evil.example/a", 1, 1,
                    "Referer evil.example -> refused" },
            { "192.168.1.5", NULL, "http://192.168.1.5.evil.example/", 1, 1,
                    "Referer with the Host as a prefix -> refused" },
            { "192.168.1.5", "http://192.168.1.5", "http://evil.example/", 1, 0,
                    "Origin decides over Referer" },
            { "192.168.1.5", "http://evil.example", NULL, 0, 0,
                    "a GET is not checked against Origin" },
        };
        for (size_t i = 0; i < sizeof hc / sizeof hc[0]; i++)
            CHECK((web_request_refused(hc[i].host, hc[i].origin, hc[i].referer,
                           hc[i].changes_state) != NULL) == hc[i].refused,
                    hc[i].name);
        CHECK(gethostname(self, sizeof self - 1) == 0 && self[0] &&
                        !web_request_refused(self, NULL, NULL, 0),
                "Host equal to the device host name -> allowed");
    }

    {
        static const struct {
            const char      *method;
            const char      *path;
            const char      *token;
            web_plain_action want;
            const char      *name;
        } pc[] = {
            { "GET", "/", NULL, WEB_PLAIN_REDIRECT, "GET -> HTTPS" },
            { "HEAD", "/", NULL, WEB_PLAIN_REDIRECT, "HEAD -> HTTPS" },
            { "POST", "/", NULL, WEB_PLAIN_REFUSE, "POST refused on HTTP" },
            { "PUT", "/", NULL, WEB_PLAIN_REFUSE, "PUT refused on HTTP" },
            { "DELETE", "/", NULL, WEB_PLAIN_REFUSE, "DELETE refused on HTTP" },
            { "get", "/", NULL, WEB_PLAIN_REFUSE, "lower-case get refused on HTTP" },
            { NULL, "/", NULL, WEB_PLAIN_REFUSE, "no method -> refused" },
            { "GET", NULL, NULL, WEB_PLAIN_REDIRECT, "GET without a path -> HTTPS" },
            { "GET", "/api/v1/summary", NO_TOKEN, WEB_PLAIN_REFUSE,
                    "GET with a Token header refused on HTTP, not redirected" },
            { "GET", "/", "", WEB_PLAIN_REFUSE, "an empty Token header refused on HTTP" },
            { "POST", WEB_TOKEN_URI, NULL, WEB_PLAIN_REFUSE, "POST /api/v1/token refused on HTTP" },
            { "GET", WEB_TOKEN_URI, NULL, WEB_PLAIN_REFUSE,
                    "GET /api/v1/token refused on HTTP, not redirected" },
        };
        for (size_t i = 0; i < sizeof pc / sizeof pc[0]; i++)
            CHECK(web_plain_policy(pc[i].method, pc[i].path, pc[i].token) == pc[i].want,
                    pc[i].name);
    }

    {
        static const struct {
            const char *host, *path, *query, *want;
            const char *name;
        } lc[] = {
            { "192.168.1.5", "/dashboard.html", NULL, "https://192.168.1.5/dashboard.html",
                    "page path kept" },
            { "192.168.1.5:80", "/", NULL, "https://192.168.1.5/", "Host port dropped" },
            { "[fe80::1]:80", "/login.html", NULL, "https://[fe80::1]/login.html",
                    "IPv6 Host kept bracketed" },
            { "localhost", "/api/v1/dashboard", NULL, "https://localhost/api/v1/dashboard",
                    "localhost" },
            { "192.168.1.5", "/a\r\nSet-Cookie: x=1", NULL, "https://192.168.1.5/",
                    "CRLF in the path -> root" },
            { "192.168.1.5", "/a b", NULL, "https://192.168.1.5/", "space in the path -> root" },
            { "192.168.1.5", "//evil.example/", NULL, "https://192.168.1.5/",
                    "path starting // -> root" },
            { "192.168.1.5", "", NULL, "https://192.168.1.5/", "empty path -> root" },
            { "192.168.1.5", NULL, NULL, "https://192.168.1.5/", "no path -> root" },
            { NULL, "/", NULL, NULL, "no Host -> no redirect" },
            { "evil.example", "/", NULL, NULL, "Host not allowed -> no redirect" },
            { "192.168.1.5:80x", "/", NULL, NULL, "bad Host port -> no redirect" },
            { "192.168.1.5", "/pools.html", "a=1&b=%2F:@!$'()*+,;?/~",
                    "https://192.168.1.5/pools.html?a=1&b=%2F:@!$'()*+,;?/~", "query kept" },
            { "192.168.1.5", "/", "", "https://192.168.1.5/", "empty query dropped" },
            { "192.168.1.5", "/", "a=1\r\nSet-Cookie: x=1", "https://192.168.1.5/",
                    "CRLF in the query -> query dropped" },
            { "192.168.1.5", "/", "a=1 2", "https://192.168.1.5/",
                    "space in the query -> query dropped" },
            { "192.168.1.5", "/", "a=\"<x>\"", "https://192.168.1.5/",
                    "quote or angle bracket in the query -> query dropped" },
            { "192.168.1.5", "/a b", "a=1", "https://192.168.1.5/",
                    "a path sent to the root drops the query" },
        };
        char loc[128];
        for (size_t i = 0; i < sizeof lc / sizeof lc[0]; i++) {
            int rc = web_https_location(lc[i].host, lc[i].path, lc[i].query, loc, sizeof loc);
            CHECK(lc[i].want ? rc == 0 && !strcmp(loc, lc[i].want) : rc != 0, lc[i].name);
        }
        CHECK(web_https_location("192.168.1.5", "/dashboard.html", NULL, loc, 20) != 0,
                "location that does not fit -> no redirect");
        CHECK(web_https_location("192.168.1.5", "/dashboard.html", "a=12345678", loc, 40) == 0 &&
                        !strcmp(loc, "https://192.168.1.5/dashboard.html"),
                "a query that does not fit is dropped, the path kept");
    }

    {
        static const char *const want[] = {
            "] [INFO] test_router: pool settings saved from the web page (1 pool(s))\n",
            "] [WARN] test_router: saving pool settings from the web page refused: "
            "not logged in\n",
            "] [ERR] test_router: saving pool settings from the web page failed: "
            "invalid or oversized pool fields\n",
            "] [WARN] test_router: log download refused: not logged in\n",
            "] [WARN] test_router: log download refused: only POST is accepted\n",
            "] [WARN] test_router: log download refused: no such log \"bogus\"\n",
            "] [INFO] test_router: reboot requested from the web page\n",
            "] [WARN] test_router: reboot request from the web page refused: not logged in\n",
            "] [ERR] test_router: reboot request from the web page was not accepted by "
            "the miner\n",
            "] [INFO] test_router: web login\n",
            "] [WARN] test_router: login from the web page refused: invalid credentials\n",
            "] [WARN] test_router: login from the web page refused: device not activated\n",
            "] [INFO] test_router: timezone set from the web page: Asia/Tokyo\n",
            "] [WARN] test_router: setting the timezone from the web page refused: "
            "not logged in\n",
            "] [INFO] test_router: LCD Pelish rotation set from the web page: 1\n",
            "] [WARN] test_router: LCD display option from the web page refused: "
            "not logged in\n",
        };
        static char wl[262144];
        size_t      wn = slurp(MINER_WEB_LOG_PATH, wl, sizeof wl - 1);
        char        msg[160];

        wl[wn] = '\0';
        for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
            const char *text = strchr(want[i], ':') + 1;

            snprintf(msg, sizeof msg, "web.log holds%.*s", (int)strlen(text) - 1, text);
            CHECK(strstr(wl, want[i]) != NULL, msg);
        }
        CHECK(truncate(MINER_WEB_LOG_PATH, 0) == 0, "web.log truncated as mm_logrot does");
        g_rebooted    = 0;
        g_reboot_ok   = 1;
        web_request q = { .method = WEB_POST, .path = "/api/v1/system/reboot" };
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 401 && g_rebooted == 0, "POST /api/v1/system/reboot unauthed -> 401");
        q.auth_cookie = COOKIE;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 200 && g_rebooted == 1, "POST /api/v1/system/reboot authed -> 200");
        q.method = WEB_GET;
        web_route(&q, &ctx, out, sizeof out, &r);
        CHECK(r.status == 405, "GET /api/v1/system/reboot -> 405");
        wn     = slurp(MINER_WEB_LOG_PATH, wl, sizeof wl - 1);
        wl[wn] = '\0';
        CHECK(wn > 0 && wl[0] == '[' && strlen(wl) == wn,
                "after truncation web.log restarts at offset 0, no hole");
        const char *a = strstr(wl,
                "] [WARN] test_router: reboot request from the web page "
                "refused: not logged in\n");
        const char *b = strstr(wl, "] [INFO] test_router: reboot requested from the web page\n");
        const char *c = strstr(wl,
                "] [WARN] test_router: reboot request from the web page "
                "refused: only POST is accepted\n");
        CHECK(a && b && c && a < b && b < c,
                "web.log records the REST reboot refused (401), requested, refused (405)");
        CHECK(!strstr(wl, "not accepted by the miner"), "an accepted reboot logs no failure line");
    }

    {
        mc_webcred               saved               = cred;
        char                     tok[WEB_TOKEN_MAX]  = "";
        char                     tok2[WEB_TOKEN_MAX] = "";
        char                     bad[WEB_TOKEN_MAX]  = "";
        char                     stale[5][256];
        static const char *const bad_bodies[] = {
            "user=admin&password=tokpass",
            "{\"command\":\"login\",\"user\":\"admin\",\"password\":\"tokpass\"}",
            "{\"command\":\"token\",\"user\":\"admin\"}",
            "{\"command\":\"token\",\"password\":\"tokpass\"}",
            "{\"command\":\"token\",\"user\":\"admin\",\"password\":1234}",
            "[\"token\"]",
            NULL,
        };
        static char wl[262144];
        size_t      wn;
        int         ok = 1;

        mkdir("router_log", 0755);
        unlink(WEB_TOKEN_KEY);
        boot_id_set("boot-1");
        logexport_pass_digest("tokpass", cred.pass);
        ctx.activated = 0;
        token_post("application/json", TOKEN_BODY, "10.1.0.1", out, sizeof out, &r);
        CHECK(r.status == 200 && r.content_type && !strcmp(r.content_type, "application/json"),
                "POST /api/v1/token unactivated, right user and password -> 200 JSON");
        CHECK(!strncmp(out, "{\"STATUS\":[{\"STATUS\":\"S\",\"When\":", 30) &&
                        strstr(out, "\"Code\":610,\"Msg\":\"Token issued\"") &&
                        strstr(out, "\"Description\":\"0.2.0\"") &&
                        strstr(out, "\"Token\":[{\"Name\":\"admin\",\"When\":") &&
                        strstr(out, "}],\"id\":1}"),
                "the token reply has the STATUS, Token and id fields");
        CHECK(token_from(out, tok) == 0 && !strncmp(tok, JWT_HEADER ".", strlen(JWT_HEADER) + 1),
                "the reply carries an HS256 JWT");
        CHECK(!strstr(out, "tokpass") && !strstr(out, cred.pass),
                "the token reply carries neither the password nor its digest");

        token_req(WEB_GET, "/api/v1/summary", NULL, tok, out, sizeof out, &r);
        CHECK(r.status == 200 && g_core_calls == 1 && !strcmp(g_core_cmd, "summary"),
                "GET /api/v1/summary with the Token header -> 200, read from core");
        token_req(WEB_POST, "/api/v1/stats", "{\"command\":\"zero\"}", tok, out, sizeof out, &r);
        CHECK(r.status == 200 && g_core_calls == 1 && !strcmp(g_core_cmd, "zero"),
                "POST /api/v1/stats zero with the Token header -> 200, sent to core");
        token_req(WEB_POST, "/api/v1/stats", "{\"command\":\"zero\"}", NO_TOKEN, out, sizeof out,
                &r);
        CHECK(r.status == 401 && g_core_calls == 0, "a forged token -> 401, core not called");
        token_req(WEB_POST, "/api/v1/stats", "{\"command\":\"zero\"}", "xyz", out, sizeof out, &r);
        CHECK(r.status == 401 && g_core_calls == 0, "a malformed Token header -> 401");
        token_req(WEB_GET, "/api/v1/summary", NULL, "", out, sizeof out, &r);
        CHECK(r.status == 401 && g_core_calls == 0, "an empty Token header -> 401");
        snprintf(bad, sizeof bad, "%s", tok);
        bad[strlen(bad) - 2] = bad[strlen(bad) - 2] == 'A' ? 'B' : 'A';
        token_req(WEB_GET, "/api/v1/summary", NULL, bad, out, sizeof out, &r);
        CHECK(stale_reply(&r, out, stale[0], sizeof stale[0]),
                "a token with a wrong signature -> 401 Code 602, core not called");
        token_req(WEB_GET, "/api/v1/dashboard", NULL, NO_TOKEN, out, sizeof out, &r);
        CHECK(stale_reply(&r, out, stale[4], sizeof stale[4]),
                "a forged token on a page open without login -> 401 Code 602 as well");
        token_req(WEB_POST, "/api/v1/activation", "{\"command\":\"activate\",\"parameter\":\"s\"}",
                NO_TOKEN, out, sizeof out, &r);
        CHECK(stale_reply(&r, out, stale[4], sizeof stale[4]),
                "a forged token on POST /api/v1/activation -> 401 Code 602, core not called");
        token_req(WEB_POST, "/api/v1/qr-auth", "{\"command\":\"qr_auth\",\"parameter\":\"p\"}", bad,
                out, sizeof out, &r);
        CHECK(stale_reply(&r, out, stale[4], sizeof stale[4]),
                "a token with a wrong signature on POST /api/v1/qr-auth -> 401 Code 602");
        {
            char cookie[40];

            snprintf(cookie, sizeof cookie, "4d43ac86%.24s", cred.pass);
            g_core_calls  = 0;
            web_request q = { .method = WEB_GET,
                .path                 = "/api/v1/summary",
                .auth_cookie          = cookie,
                .auth_token           = NO_TOKEN,
                .tls                  = 1 };
            web_route(&q, &ctx, out, sizeof out, &r);
            CHECK(r.status == 401 && g_core_calls == 0,
                    "a Token header decides alone: valid cookie next to a forged token -> 401");
        }

        for (int i = 0; i < 6; i++) {
            web_request q = { .method = WEB_GET,
                .path                 = "/api/v1/summary",
                .auth_token           = NO_TOKEN,
                .remote_addr          = "10.1.0.2",
                .tls                  = 1 };
            web_route(&q, &ctx, out, sizeof out, &r);
        }
        token_post("application/json", TOKEN_BODY, "10.1.0.2", out, sizeof out, &r);
        CHECK(r.status == 200, "six forged tokens do not lock the address out of /api/v1/token");

        token_post("application/json", TOKEN_WRONG, "10.1.0.3", out, sizeof out, &r);
        CHECK(r.status == 401 && strstr(out, "\"STATUS\":\"E\"") && strstr(out, TOKEN_DENIED) &&
                        strstr(out, "],\"id\":1}") && !strstr(out, "\"Token\":"),
                "POST /api/v1/token with a wrong password -> 401 Code 601, no token");
        token_post("application/json",
                "{\"command\":\"token\",\"user\":\"root\",\"password\":\"tokpass\"}", "10.1.0.3",
                out, sizeof out, &r);
        CHECK(r.status == 401 && strstr(out, TOKEN_DENIED),
                "POST /api/v1/token with a wrong user -> 401 Code 601");
        cred.user[0] = '\0';
        token_post("application/json",
                "{\"command\":\"token\",\"user\":\"\",\"password\":\"tokpass\"}", "10.1.0.3", out,
                sizeof out, &r);
        CHECK(r.status == 401 && strstr(out, TOKEN_DENIED),
                "an empty user name never gets a token, even with webuser empty");
        snprintf(cred.user, sizeof cred.user, "%s", saved.user);

        for (int i = 0; i < 5; i++)
            token_post("application/json", TOKEN_WRONG, "10.1.0.4", out, sizeof out, &r);
        token_post("application/json", TOKEN_BODY, "10.1.0.4", out, sizeof out, &r);
        CHECK(r.status == 401 && strstr(out, TOKEN_DENIED) && !strstr(out, "\"Token\":"),
                "five wrong passwords lock /api/v1/token: the right one then -> 401");
        token_post("application/json", TOKEN_BODY, "10.1.0.5", out, sizeof out, &r);
        CHECK(r.status == 200, "the lock holds only the address that failed");

        for (size_t i = 0; i < sizeof bad_bodies / sizeof bad_bodies[0]; i++) {
            token_post("application/json", bad_bodies[i], "10.1.0.6", out, sizeof out, &r);
            ok &= r.status == 400 && strstr(out, TOKEN_BAD_REQ) != NULL;
        }
        token_post("text/plain", TOKEN_BODY, "10.1.0.6", out, sizeof out, &r);
        ok &= r.status == 400 && strstr(out, TOKEN_BAD_REQ) != NULL;
        CHECK(ok, "a malformed /api/v1/token body or a non-JSON request -> 400 Code 601");
        token_post("application/json", TOKEN_BODY, "10.1.0.6", out, sizeof out, &r);
        CHECK(r.status == 200, "malformed token requests do not count as failed logins");

        snprintf(cred.pass, sizeof cred.pass, "%064d", 0);
        token_post("application/json", TOKEN_BODY, "10.1.0.7", out, sizeof out, &r);
        CHECK(r.status == 403 && strstr(out, "\"Code\":603"),
                "POST /api/v1/token with webpass not set -> 403 Code 603");
        token_req(WEB_GET, "/api/v1/summary", NULL, tok, out, sizeof out, &r);
        CHECK(r.status == 401, "a token issued before webpass was cleared -> 401");
        logexport_pass_digest("tokpass", cred.pass);
        ctx.creds = NULL;
        token_post("application/json", TOKEN_BODY, "10.1.0.7", out, sizeof out, &r);
        CHECK(r.status == 503 && strstr(out, "\"Code\":604"),
                "POST /api/v1/token without loaded credentials -> 503 Code 604");
        token_req(WEB_GET, "/api/v1/summary", NULL, tok, out, sizeof out, &r);
        CHECK(r.status == 401, "a Token header without loaded credentials -> 401");
        ctx.creds = &cred;
        unlink(WEB_TOKEN_BOOT_ID);
        token_post("application/json", TOKEN_BODY, "10.1.0.7", out, sizeof out, &r);
        CHECK(r.status == 503 && strstr(out, "\"Code\":604") && !strstr(out, "\"Token\":"),
                "POST /api/v1/token when the token cannot be made -> 503 Code 604");
        boot_id_set("boot-1");

        logexport_pass_digest("newpass", cred.pass);
        token_req(WEB_GET, "/api/v1/summary", NULL, tok, out, sizeof out, &r);
        CHECK(stale_reply(&r, out, stale[1], sizeof stale[1]),
                "a token issued before a password change -> 401 Code 602");
        logexport_pass_digest("tokpass", cred.pass);
        snprintf(cred.user, sizeof cred.user, "root");
        token_req(WEB_GET, "/api/v1/summary", NULL, tok, out, sizeof out, &r);
        CHECK(r.status == 401, "a token issued before a user name change -> 401");
        snprintf(cred.user, sizeof cred.user, "%s", saved.user);
        token_post("application/json", TOKEN_BODY, "10.1.0.8", out, sizeof out, &r);
        CHECK(r.status == 200 && token_from(out, tok) == 0,
                "a new token once the credentials are back");

        g_up_off_s = WEB_TOKEN_TTL_S - 2;
        token_req(WEB_GET, "/api/v1/summary", NULL, tok, out, sizeof out, &r);
        CHECK(r.status == 200, "a token short of 24 h of uptime old -> 200");
        g_up_off_s = WEB_TOKEN_TTL_S;
        token_req(WEB_GET, "/api/v1/summary", NULL, tok, out, sizeof out, &r);
        CHECK(stale_reply(&r, out, stale[2], sizeof stale[2]),
                "a token 24 h of uptime old -> 401 Code 602, core not called");
        g_up_off_s  = WEB_TOKEN_TTL_S - 1;
        g_up_step_s = 2;
        token_req(WEB_GET, "/api/v1/summary", NULL, tok, out, sizeof out, &r);
        CHECK(r.status == 200 && g_core_calls == 1 && g_up_off_s == WEB_TOKEN_TTL_S + 1,
                "a token expiring during the request is checked once and passes");
        g_up_off_s = WEB_TOKEN_TTL_S - 1;
        token_req(WEB_POST, "/runlog", "miner.log", tok, out, sizeof out, &r);
        CHECK(r.status != 401 && !strstr(r.extra_headers, "WWW-Authenticate") &&
                        g_up_off_s == WEB_TOKEN_TTL_S + 1,
                "...on /runlog as well");
        g_up_step_s = 0;
        g_up_off_s  = 0;

        boot_id_set("boot-2");
        g_synced = 0;
        token_req(WEB_GET, "/api/v1/summary", NULL, tok, out, sizeof out, &r);
        CHECK(stale_reply(&r, out, stale[3], sizeof stale[3]),
                "a token from the previous boot before the clock is synchronized -> 401 Code 602");
        CHECK(!strcmp(stale[0], stale[1]) && !strcmp(stale[1], stale[2]) &&
                        !strcmp(stale[2], stale[3]),
                "wrong signature, password change, expiry and an untrusted clock after a reboot "
                "get the same reply but for When");
        token_req(WEB_POST, "/runlog", "miner.log", tok, out, sizeof out, &r);
        CHECK(stale_reply(&r, out, stale[4], sizeof stale[4]), "...on /runlog as well");
        token_post("application/json", TOKEN_BODY, "10.1.0.8", out, sizeof out, &r);
        CHECK(r.status == 200 && token_from(out, tok2) == 0,
                "a token is issued while the clock is not synchronized");
        token_req(WEB_GET, "/api/v1/summary", NULL, tok2, out, sizeof out, &r);
        CHECK(r.status == 200, "...and is accepted during the same boot");
        g_synced = 1;
        token_req(WEB_GET, "/api/v1/summary", NULL, tok, out, sizeof out, &r);
        CHECK(r.status == 200,
                "a token from the previous boot once the clock is synchronized -> 200");
        boot_id_set("boot-3");
        token_req(WEB_GET, "/api/v1/summary", NULL, tok2, out, sizeof out, &r);
        CHECK(r.status == 401,
                "a token issued with the clock not synchronized -> 401 after a reboot");
        g_synced = 0;
        token_req(WEB_GET, "/api/v1/summary", NULL, tok2, out, sizeof out, &r);
        CHECK(r.status == 401, "...also before the clock is synchronized");
        g_synced = 1;

        token_post("application/json", TOKEN_BODY, "10.1.0.8", out, sizeof out, &r);
        token_from(out, tok2);
        token_req(WEB_DELETE, WEB_TOKEN_URI, NULL, tok2, out, sizeof out, &r);
        CHECK(r.status == 405 && method_reply(&r, out), "DELETE /api/v1/token -> 405 Code 505");
        token_req(WEB_GET, WEB_TOKEN_URI, NULL, tok2, out, sizeof out, &r);
        CHECK(r.status == 405 && method_reply(&r, out), "GET /api/v1/token -> 405 Code 505");

        token_req(WEB_POST, "/runlog", "miner.log", tok2, out, sizeof out, &r);
        CHECK(r.status != 401 && !strstr(r.extra_headers, "WWW-Authenticate"),
                "POST /runlog with the Token header passes the login check");
        token_req(WEB_POST, "/runlog", "miner.log", NO_TOKEN, out, sizeof out, &r);
        CHECK(stale_reply(&r, out, stale[4], sizeof stale[4]),
                "POST /runlog with a forged token -> 401 Code 602");
        {
            web_request q = { .method = WEB_POST,
                .path                 = "/cgi-bin/api",
                .body                 = "{\"command\":\"set_pools\"}",
                .auth_token           = tok2 };
            web_route(&q, &ctx, out, sizeof out, &r);
            CHECK(r.status == 404 && !strcmp(out, "Not found\n"),
                    "an unknown path with a valid Token header -> 404");
        }

        wn     = slurp(MINER_WEB_LOG_PATH, wl, sizeof wl - 1);
        wl[wn] = '\0';
        CHECK(strstr(wl, "] [INFO] test_router: web token issued\n") &&
                        strstr(wl,
                                "] [WARN] test_router: web token refused: clock not "
                                "synchronized\n") &&
                        strstr(wl,
                                "] [WARN] test_router: web token refused: invalid, expired "
                                "or for other credentials\n") &&
                        strstr(wl,
                                "] [WARN] test_router: web token request refused: "
                                "invalid credentials\n"),
                "web.log records token issue, refusals and an unsynchronized clock");
        CHECK(!strstr(wl, tok + strlen(JWT_HEADER) + 1) &&
                        !strstr(wl, tok2 + strlen(JWT_HEADER) + 1) && !strstr(wl, "tokpass") &&
                        !strstr(wl, cred.pass),
                "web.log holds no token, password or password digest");
        unlink(WEB_TOKEN_KEY);
        unlink(WEB_TOKEN_BOOT_ID);
        cred = saved;
    }

    if (fails) {
        fprintf(stderr, "test_router: %d FAIL\n", fails);
        return 1;
    }
    printf("test_router: all pass\n");
    return geteuid() == 0 ? TST_SKIP : 0;
}
