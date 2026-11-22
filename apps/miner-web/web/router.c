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
#include "jwrite.h"
#include "jread.h"
#include "core_api.h"
#include "webform.h"
#include "pool_config.h"
#include "cfgwr_client.h"
#include "sha256.h"
#include "hex.h"
#include "logexport.h"
#include "web_logfmt.h"
#include "webcred.h"
#include "webtoken.h"
#include "web_plugin.h"
#include "webstatus.h"
#include <arpa/inet.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define CT_TEXT         "text/plain"
#define CT_JSON         "application/json"
#define LOGIN_PROOF_MAX 64 /* login proof buffer size, bytes including NUL */

#define QR_COOKIE_MAX_AGE_S 86400

#define TZ_NAME_CAP 48 /* zone name buffer incl. NUL, bytes; mm_miner's MM_TZ_NAME_CAP */

#define REST_PARAM_MAX 1024
#define TOKEN_PASS_MAX 128 /* plain-text password buffer, bytes including NUL */

typedef struct {
    pool_set want;
    int      no_pass;
} pools_form;

static pthread_once_t g_hostname_once = PTHREAD_ONCE_INIT;
static char           g_hostname[HOST_NAME_MAX + 1];

static const char *const rest_reads[] = { "version", "summary", "pools", "stats", "estats", "devs",
    "edevs", "devdetails", "config", "coin", "litestats", "loop", NULL };

static const char *const pools_top[]      = { "setpool", "addpool", "removepool", "enablepool",
         "disablepool", "switchpool", "poolpriority", NULL };
static const char *const stats_top[]      = { "zero", NULL };
static const char *const system_top[]     = { "softon", "softoff", NULL };
static const char *const system_asc[]     = { "reboot", "filter-clean", NULL };
static const char *const reboot_asc[]     = { "reboot", NULL };
static const char *const time_top[]       = { "time", NULL };
static const char *const mode_asc[]       = { "workmode", "work_mode_lvl", "worklevel", NULL };
static const char *const led_asc[]        = { "ledmode", "ledset", "nightlamp", NULL };
static const char *const lcd_asc[]        = { "lcd", NULL };
static const char *const password_asc[]   = { "password", NULL };
static const char *const activation_asc[] = { "activate", NULL };
static const char *const qr_auth_asc[]    = { "qr_auth", NULL };

static const web_rest_resource rest_writes[] = {
    { "pools", pools_top, NULL },
    { "stats", stats_top, NULL },
    { "system", system_top, system_asc },
    { "system/reboot", NULL, reboot_asc },
    { "time", time_top, NULL },
    { "mode", NULL, mode_asc },
    { "led", NULL, led_asc },
    { "lcd", NULL, lcd_asc },
    { "password", NULL, password_asc },
    { "activation", NULL, activation_asc },
    { "qr-auth", NULL, qr_auth_asc },
};

static const char *const rest_no_login[] = { "activation", "qr-auth", NULL };

static void hostname_read(void)
{
    if (gethostname(g_hostname, sizeof g_hostname - 1) != 0)
        g_hostname[0] = '\0';
}

static int port_ok(const char *p)
{
    size_t n = strspn(p + 1, "0123456789");

    return p[0] == ':' && n >= 1 && n <= 5 && p[1 + n] == '\0';
}

static int host_allowed(const char *host)
{
    char          name[256];
    unsigned char addr[16];
    const char   *end;
    size_t        n;

    if (host[0] == '[') {
        end = strchr(host, ']');
        if (!end || (end[1] && !port_ok(end + 1)))
            return 0;
        n = (size_t)(end - host - 1);
        if (n >= sizeof name)
            return 0;
        memcpy(name, host + 1, n);
        name[n] = '\0';
        return inet_pton(AF_INET6, name, addr) == 1;
    }
    end = strchr(host, ':');
    if (end && !port_ok(end))
        return 0;
    n = end ? (size_t)(end - host) : strlen(host);
    if (!n || n >= sizeof name)
        return 0;
    memcpy(name, host, n);
    name[n] = '\0';
    pthread_once(&g_hostname_once, hostname_read);
    return inet_pton(AF_INET, name, addr) == 1 || !strcasecmp(name, "localhost") ||
            (g_hostname[0] && !strcasecmp(name, g_hostname));
}

static int same_authority(const char *url, const char *host)
{
    size_t n;

    if (!strncasecmp(url, "http://", 7))
        url += 7;
    else if (!strncasecmp(url, "https://", 8))
        url += 8;
    else
        return 0;
    n = strcspn(url, "/?#");
    return n == strlen(host) && !strncasecmp(url, host, n);
}

const char *web_request_refused(const char *host, const char *origin, const char *referer,
        int changes_state)
{
    if (!host)
        return NULL;
    if (!host_allowed(host))
        return "Host not allowed";
    if (!changes_state)
        return NULL;
    if (origin ? !same_authority(origin, host) : referer && !same_authority(referer, host))
        return "cross-origin request refused";
    return NULL;
}

web_plain_action web_plain_policy(const char *method, const char *path, const char *token)
{
    if (token || (path && !strcmp(path, WEB_TOKEN_URI)))
        return WEB_PLAIN_REFUSE;
    if (method && (!strcmp(method, "GET") || !strcmp(method, "HEAD")))
        return WEB_PLAIN_REDIRECT;
    return WEB_PLAIN_REFUSE;
}

int web_https_location(const char *host, const char *path, const char *query, char *out, size_t cap)
{
    static const char path_chars[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~/";
    static const char query_chars[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~/?%!$&'()*+,;=:@";
    size_t n;
    int    len;

    if (!host || !host_allowed(host))
        return -1;
    n = host[0] == '[' ? (size_t)(strchr(host, ']') - host + 1) : strcspn(host, ":");
    if (!path || path[0] != '/' || path[1] == '/' || path[strspn(path, path_chars)]) {
        path  = "/";
        query = NULL;
    }
    if (query && (!query[0] || query[strspn(query, query_chars)]))
        query = NULL;
    len = snprintf(out, cap, "https://%.*s%s%s%s", (int)n, host, path, query ? "?" : "",
            query ? query : "");
    if (query && len >= 0 && (size_t)len >= cap)
        len = snprintf(out, cap, "https://%.*s%s", (int)n, host, path);
    return len < 0 || (size_t)len >= cap ? -1 : 0;
}

static size_t emit_text(char *out, size_t cap, const char *s)
{
    size_t n = strlen(s);
    if (cap) {
        size_t k = n < cap - 1 ? n : cap - 1;
        memcpy(out, s, k);
        out[k] = '\0';
    }
    return n;
}

static int reply(web_response *resp, int status, const char *type, char *out, size_t cap,
        const char *text)
{
    resp->content_type  = type;
    resp->body_len      = emit_text(out, cap, text);
    return resp->status = status;
}

static const char *cookie_secure(const web_request *req)
{
    return req->tls ? "; Secure" : "";
}

static const char *token_fault(const web_request *req, const web_ctx *ctx)
{
    if (!ctx || !ctx->creds)
        return "credentials not loaded";
    if (mc_api_webpass_unset(ctx->creds))
        return "web password not set";
    switch (web_token_check(ctx->creds, req->auth_token)) {
    case WEB_TOKEN_OK:
        return NULL;
    case WEB_TOKEN_CLOCK:
        return "clock not synchronized";
    default:
        return "invalid, expired or for other credentials";
    }
}

static int request_authed(const web_request *req, const web_ctx *ctx, int token_ok)
{
    if (req->auth_token)
        return token_ok;
    return ctx && ctx->creds && req->auth_cookie &&
            mc_api_check_cookie(ctx->creds, req->auth_cookie);
}

static int login_proof_ok(const web_request *req, const web_ctx *ctx, char proof[LOGIN_PROOF_MAX])
{
    return web_form_get(req->body ? req->body : "", "passwd", proof, LOGIN_PROOF_MAX) == 1 &&
            strlen(proof) == 32 && strspn(proof, "0123456789abcdefABCDEF") == 32 && ctx &&
            ctx->creds && web_auth_login(ctx->creds, proof, req->remote_addr);
}

static const mc_dashboard *dashboard_view(const web_ctx *ctx, int authed, mc_dashboard *view,
        pool_set *ps)
{
    *view = *ctx->telemetry;
    for (int i = 0; i < 3; i++)
        view->pool[i].passwd = !authed && view->pool[i].url && view->pool[i].url[0] ? "*" : NULL;
    pool_init(ps, 0, 0);
    if (!authed || !ctx->pool_conf || cfgwr_read_pools(ctx->pool_conf, ps) != 0)
        return view;
    for (int i = 0; i < ps->count; i++)
        if (ps->pools[i].slot >= 0 && ps->pools[i].slot < 3)
            view->pool[ps->pools[i].slot].passwd = ps->pools[i].pass;
    return view;
}

static size_t dashboard_out(const web_ctx *ctx, int authed, char *out, size_t cap)
{
    mc_dashboard        view;
    pool_set            ps;
    const mc_dashboard *d = dashboard_view(ctx, authed, &view, &ps);
    size_t              n = mc_status_dashboard_json(d, out, cap);

    explicit_bzero(&ps, sizeof ps);
    return n;
}

static int op_refused(web_response *resp, int status, const char *op, const char *why)
{
    web_logf(stderr, "WARN", "%s from the web page refused: %s\n", op, why);
    return resp->status = status;
}

static int pools_keep_pass(pool_set *ps, void *ctx)
{
    pools_form *f = ctx;

    for (int i = 0; i < f->want.count; i++) {
        pool_t *p = &f->want.pools[i];

        if (!strcmp(p->pass, "*"))
            p->pass[0] = '\0';
        for (int j = 0; !p->pass[0] && j < ps->count; j++)
            if (ps->pools[j].slot == p->slot)
                snprintf(p->pass, sizeof p->pass, "%s", ps->pools[j].pass);
        if (!p->pass[0]) {
            f->no_pass = p->slot + 1;
            return -1;
        }
    }
    return pool_config_apply_slots(ps, &f->want);
}

static int pools_write(const web_request *req, const web_ctx *ctx, char *out, size_t cap,
        web_response *resp)
{
    pools_form f                         = { .no_pass = 0 };
    int        n                         = web_pools_from_form(req->body ? req->body : "", &f.want);
    char       reason[POOL_URL_MAX + 64] = "";
    int        status                    = 200;

    if (n <= 0) {
        snprintf(reason, sizeof reason, "%s",
                n < 0 ? "invalid or oversized pool fields" : "no pool configured");
        status = 400;
    } else if (!ctx || !ctx->pool_conf) {
        snprintf(reason, sizeof reason, "cannot save the pool settings");
        status = 500;
    } else if (cfgwr_update_pools(ctx->pool_conf, pools_keep_pass, &f) != 0) {
        if (f.no_pass) {
            snprintf(reason, sizeof reason, "pool %d needs a password", f.no_pass);
            status = 400;
        } else {
            snprintf(reason, sizeof reason, "cannot save the pool settings");
            status = 500;
        }
    }
    explicit_bzero(&f, sizeof f);

    if (status == 200)
        web_logf(stderr, "INFO",
                "pool settings saved from the web page "
                "(%d pool(s))\n",
                n);
    else
        web_logf(stderr, "ERR",
                "saving pool settings from the web page "
                "failed: %s\n",
                reason);

    jwriter w;
    jw_init(&w, out, cap);
    jw_obj_open(&w);
    if (status == 200) {
        char cnt[12];
        snprintf(cnt, sizeof cnt, "%d", n);
        jw_kv_str(&w, "result", "ok");
        jw_kv_str(&w, "pools", cnt);
        jw_kv_str(&w, "note", "saved; takes effect after the device restarts");
    } else {
        jw_kv_str(&w, "result", "error");
        jw_kv_str(&w, "reason", reason);
    }
    jw_obj_close(&w);
    resp->content_type = CT_JSON;
    resp->body_len     = jw_len(&w);
    resp->status       = status;
    return resp->status;
}

static int tz_name_ok(const char *name)
{
    size_t n = strlen(name);
    if (!n || n >= TZ_NAME_CAP || name[0] == '/' || name[n - 1] == '/')
        return 0;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (c == '/') {
            if (name[i + 1] == '/')
                return 0;
            continue;
        }
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') &&
                c != '_' && c != '+' && c != '-')
            return 0;
    }
    return 1;
}

static int tz_write(const web_request *req, const web_ctx *ctx, char *out, size_t cap,
        web_response *resp)
{
    char        tz[72]  = "";
    const char *body    = req->body ? req->body : "";
    int         have_tz = web_form_get(body, "timezone", tz, sizeof tz) == 1 && tz[0];
    const char *reason  = "";
    int         status  = 200;
    int         rc      = 0;

    if (!have_tz) {
        reason = "timezone is missing";
        status = 400;
    } else if (!tz_name_ok(tz)) {
        reason = "invalid timezone name";
        status = 400;
    } else if (!ctx || !ctx->tz_set) {
        reason = "cannot set the timezone";
        status = 500;
    } else if ((rc = ctx->tz_set(tz)) < 0) {
        reason = "this timezone is not available on the device";
        status = 400;
    } else if (rc == 0) {
        reason = "the miner did not accept the timezone";
        status = 503;
    }

    if (status == 200)
        web_logf(stderr, "INFO", "timezone set from the web page: %s\n", tz);
    else
        web_logf(stderr, "ERR",
                "setting the timezone from the web page "
                "failed: %s\n",
                reason);

    jwriter w;
    jw_init(&w, out, cap);
    jw_obj_open(&w);
    if (status == 200) {
        jw_kv_str(&w, "result", "ok");
        jw_kv_str(&w, "timezone", tz);
    } else {
        jw_kv_str(&w, "result", "error");
        jw_kv_str(&w, "reason", reason);
    }
    jw_obj_close(&w);
    resp->content_type = CT_JSON;
    resp->body_len     = jw_len(&w);
    resp->status       = status;
    return status;
}

static int web_starting(const web_ctx *ctx)
{
    return ctx && ctx->activated == WEB_ACTIVATED_UNKNOWN;
}

static const char *activation_json(const web_ctx *ctx)
{
    if (web_starting(ctx))
        return "{\"activated\":0,\"starting\":1}";
    return ctx && ctx->activated > 0 ? "{\"activated\":1}" : "{\"activated\":0}";
}

static void auth_fields(jwriter *w, const web_ctx *ctx)
{
    char code[25] = "";
    if (ctx && ctx->dna && *ctx->dna) {
        unsigned char dg[SHA256_DIGEST_SIZE];
        sha256(ctx->dna, strlen(ctx->dna), dg);
        hex_encode(code, dg, 12);
    }
    jw_kv_str(w, "auth", ctx && ctx->auth_nonce ? ctx->auth_nonce : "");
    jw_kv_str(w, "code", code);
    jw_kv_str(w, "session", ctx && ctx->qr_session ? ctx->qr_session : "");
    jw_kv_raw(w, "activated", ctx && ctx->activated > 0 ? "1" : "0");
    if (web_starting(ctx))
        jw_kv_raw(w, "starting", "1");
}

static void qr_login_fields(jwriter *w, const web_request *req, const web_ctx *ctx,
        web_response *resp)
{
    const char *nonce = ctx->auth_nonce ? ctx->auth_nonce : "";

    snprintf(resp->extra_headers, sizeof resp->extra_headers,
            "Set-Cookie: auth=%s%.24s; Path=/; Max-Age=%d; HttpOnly; SameSite=Strict%s\r\n", nonce,
            ctx->creds ? ctx->creds->pass : "", QR_COOKIE_MAX_AGE_S, cookie_secure(req));
    jw_kv_str(w, "auth", nonce);
}

static const char *list_find(const char *const *list, const char *name)
{
    for (; list && *list; list++)
        if (!strcmp(*list, name))
            return *list;
    return NULL;
}

static const web_rest_resource *rest_find(const web_rest_resource *tab, size_t n, const char *r)
{
    for (size_t i = 0; tab && i < n; i++)
        if (!strcmp(tab[i].path, r))
            return &tab[i];
    return NULL;
}

static const web_rest_resource *rest_resource(const char *r)
{
    size_t                   n   = sizeof rest_writes / sizeof rest_writes[0];
    const web_rest_resource *res = rest_find(rest_writes, n, r);

    if (res)
        return res;
    res = web_plugin_rest(&n);
    return rest_find(res, n, r);
}

static int rest_json(const web_request *req)
{
    const char *t = req->content_type;
    size_t      n = strlen(CT_JSON);

    return t && !strncasecmp(t, CT_JSON, n) && (t[n] == '\0' || t[n] == ';' || t[n] == ' ');
}

static int rest_status(web_response *resp, int status, int code, const char *msg, char *out,
        size_t cap)
{
    resp->content_type  = CT_JSON;
    resp->body_len      = web_status_json("E", code, msg, out, cap);
    return resp->status = status;
}

static int rest_method(web_response *resp, char *out, size_t cap)
{
    return rest_status(resp, 405, WEB_STATUS_METHOD_CODE, WEB_STATUS_METHOD_MSG, out, cap);
}

static int rest_refused(web_response *resp, int status, const char *r, const char *why, char *out,
        size_t cap, int code, const char *msg)
{
    char safe[LOGEXPORT_NAME_MAX];

    logexport_safe_name(r, safe, sizeof safe);
    web_logf(stderr, "WARN", "REST /api/v1/%s refused: %s\n", safe, why);
    return rest_status(resp, status, code, msg, out, cap);
}

static int rest_bad_cmd(web_response *resp, int status, const char *r, const char *why,
        const char *name, char *out, size_t cap)
{
    char safe[LOGEXPORT_NAME_MAX];

    logexport_safe_name(r, safe, sizeof safe);
    web_logf(stderr, "WARN", "REST /api/v1/%s refused: %s\n", safe, why);
    resp->content_type  = CT_JSON;
    resp->body_len      = web_status_bad_cmd(name, out, cap);
    return resp->status = status;
}

static int rest_param(const json_value *v, char *out, size_t cap)
{
    int n;

    if (!v) {
        out[0] = '\0';
        return 0;
    }
    if (json_is(v, JSON_STRING)) {
        n = snprintf(out, cap, "%s", json_string(v));
    } else if (json_is(v, JSON_NUMBER) && json_number(v) == (double)json_int(v)) {
        n = snprintf(out, cap, "%lld", json_int(v));
    } else if (json_is(v, JSON_NUMBER)) {
        n = snprintf(out, cap, "%.17g", json_number(v));
    } else {
        return -1;
    }
    return n < 0 || (size_t)n >= cap ? -1 : 0;
}

static int core_reply(const char *cmd, const char *param, char *out, size_t cap, web_response *resp)
{
    json_value       *v;
    const json_value *st;
    int               status = 200;
    int               rc     = core_api_call(cmd, param, out, cap);

    if (rc == CORE_API_TRUNCATED) {
        web_logf(stderr, "ERR", "REST %s failed: core_miner reply too large\n", cmd);
        return rest_status(resp, 500, WEB_STATUS_FAULT_CODE, WEB_STATUS_TOO_LONG_MSG, out, cap);
    }
    if (rc < 0) {
        web_logf(stderr, "ERR", "REST %s failed: core_miner did not answer\n", cmd);
        return rest_status(resp, 504, WEB_STATUS_FAULT_CODE, WEB_STATUS_NO_CORE_MSG, out, cap);
    }
    v  = json_parse(out);
    st = json_object_get(json_array_get(json_object_get(v, "STATUS"), 0), "STATUS");
    if (!strcmp(json_string(st), "E"))
        status = 400;
    json_free(v);
    resp->content_type  = CT_JSON;
    resp->body_len      = strlen(out);
    return resp->status = status;
}

static int rest_read(const web_request *req, const char *cmd, char *out, size_t cap,
        web_response *resp)
{
    char param[REST_PARAM_MAX];

    if (!strcmp(cmd, "loop"))
        return core_reply("ascset", "0,loop", out, cap, resp);
    if (web_form_get(req->query, "parameter", param, sizeof param) < 0)
        return rest_refused(resp, 400, cmd, "invalid parameter", out, cap, WEB_STATUS_BAD_REQ_CODE,
                WEB_STATUS_INVALID_MSG);
    return core_reply(cmd, param, out, cap, resp);
}

static int rest_write(const web_request *req, const char *r, const web_rest_resource *res,
        char *out, size_t cap, web_response *resp, int *rejected)
{
    char              param[REST_PARAM_MAX];
    char              arg[REST_PARAM_MAX + 64];
    json_value       *v   = json_parse(req->body);
    const json_value *pv  = json_object_get(v, "parameter");
    const char       *cmd = json_string(json_object_get(v, "command"));
    const char       *top = list_find(res->top, cmd);
    const char       *op  = top ? NULL : list_find(res->ascset, cmd);
    int               bad;
    int               status;

    bad = !json_is(v, JSON_OBJECT) || !json_is(json_object_get(v, "command"), JSON_STRING) ||
            rest_param(pv, param, sizeof param) != 0;
    if (json_is(pv, JSON_STRING))
        explicit_bzero((char *)json_string(pv), strlen(json_string(pv)));
    if (bad) {
        explicit_bzero(param, sizeof param);
        json_free(v);
        return rest_refused(resp, 400, r, "invalid request body", out, cap, WEB_STATUS_BAD_REQ_CODE,
                WEB_STATUS_INVALID_MSG);
    }
    if (!top && !op) {
        explicit_bzero(param, sizeof param);
        status = rest_bad_cmd(resp, 400, r, "command not allowed", cmd, out, cap);
        json_free(v);
        return status;
    }
    json_free(v);
    web_logf(stderr, "INFO", "REST %s requested from /api/v1/%s\n", top ? top : op, r);
    if (top) {
        status = core_reply(top, param, out, cap, resp);
    } else {
        snprintf(arg, sizeof arg, "0,%s%s%s", op, param[0] ? "," : "", param);
        status = core_reply("ascset", arg, out, cap, resp);
        explicit_bzero(arg, sizeof arg);
    }
    explicit_bzero(param, sizeof param);
    *rejected = status == 400;
    return status;
}

static int rest_write_no_login(const web_request *req, const char *r, const web_rest_resource *res,
        char *out, size_t cap, web_response *resp)
{
    int rejected = 0;
    int status;

    if (!web_proof_guard_open(req->remote_addr))
        return rest_refused(resp, 429, r, "too many failed attempts", out, cap,
                WEB_STATUS_LOCKED_CODE, WEB_STATUS_LOCKED_MSG);
    status = rest_write(req, r, res, out, cap, resp, &rejected);
    if (rejected)
        web_proof_guard_fail(req->remote_addr);
    return status;
}

static void token_status_obj(char *out, size_t cap, const char *st, int code, const char *msg,
        long long when, const char *desc)
{
    char    num[24];
    jwriter w;

    jw_init(&w, out, cap);
    jw_obj_open(&w);
    jw_kv_str(&w, "STATUS", st);
    snprintf(num, sizeof num, "%lld", when);
    jw_kv_raw(&w, "When", num);
    snprintf(num, sizeof num, "%d", code);
    jw_kv_raw(&w, "Code", num);
    jw_kv_str(&w, "Msg", msg);
    if (desc && desc[0])
        jw_kv_str(&w, "Description", desc);
    jw_obj_close(&w);
}

static int token_status(web_response *resp, int status, const char *st, int code, const char *msg,
        char *out, size_t cap)
{
    resp->content_type  = CT_JSON;
    resp->body_len      = web_status_json(st, code, msg, out, cap);
    return resp->status = status;
}

static int token_refused(web_response *resp, int status, int code, const char *msg, const char *why,
        char *out, size_t cap)
{
    web_logf(stderr, "WARN", "web token request refused: %s\n", why);
    return token_status(resp, status, "E", code, msg, out, cap);
}

static int token_issue(const web_request *req, const web_ctx *ctx, char *out, size_t cap,
        web_response *resp)
{
    char              user[MC_WEBUSER_MAX]  = "";
    char              plain[TOKEN_PASS_MAX] = "";
    char              digest[WEBPASS_HEX_LEN + 1];
    char              token[WEB_TOKEN_MAX];
    char              st[512], tk[WEB_TOKEN_MAX + 256], num[24];
    long long         when;
    jwriter           w;
    json_value       *v = rest_json(req) ? json_parse(req->body) : NULL;
    const json_value *u = json_object_get(v, "user");
    const json_value *p = json_object_get(v, "password");
    int               ok;

    ok = !strcmp(json_string(json_object_get(v, "command")), "token") && json_is(u, JSON_STRING) &&
            json_is(p, JSON_STRING) &&
            (size_t)snprintf(user, sizeof user, "%s", json_string(u)) < sizeof user &&
            (size_t)snprintf(plain, sizeof plain, "%s", json_string(p)) < sizeof plain;
    if (json_is(p, JSON_STRING))
        explicit_bzero((char *)json_string(p), strlen(json_string(p)));
    json_free(v);
    if (!ok) {
        explicit_bzero(plain, sizeof plain);
        return token_refused(resp, 400, WEB_STATUS_TOKEN_REQ_CODE, "Invalid token request",
                "invalid request body", out, cap);
    }
    if (!ctx || !ctx->creds) {
        explicit_bzero(plain, sizeof plain);
        return token_refused(resp, 503, WEB_STATUS_TOKEN_UNAVAIL_CODE, "Token service unavailable",
                "credentials not loaded", out, cap);
    }
    if (mc_api_webpass_unset(ctx->creds)) {
        explicit_bzero(plain, sizeof plain);
        return token_refused(resp, 403, WEB_STATUS_TOKEN_UNSET_CODE, "Web password not set",
                "web password not set", out, cap);
    }
    logexport_pass_digest(plain, digest);
    explicit_bzero(plain, sizeof plain);
    ok = user[0] && web_auth_password(ctx->creds, user, digest, req->remote_addr);
    explicit_bzero(digest, sizeof digest);
    if (!ok)
        return token_refused(resp, 401, WEB_STATUS_TOKEN_REQ_CODE, "Invalid user or password",
                "invalid credentials", out, cap);
    if (web_token_issue(ctx->creds, token, sizeof token) != 0)
        return token_refused(resp, 503, WEB_STATUS_TOKEN_UNAVAIL_CODE, "Token service unavailable",
                "token not signed", out, cap);

    when = (long long)time(NULL);
    token_status_obj(st, sizeof st, "S", WEB_STATUS_TOKEN_OK_CODE, "Token issued", when,
            ctx->telemetry ? ctx->telemetry->version : NULL);
    jw_init(&w, tk, sizeof tk);
    jw_obj_open(&w);
    jw_kv_str(&w, "Name", user);
    snprintf(num, sizeof num, "%lld", when);
    jw_kv_raw(&w, "When", num);
    jw_kv_str(&w, "Token", token);
    jw_obj_close(&w);
    snprintf(out, cap, "{\"STATUS\":[%s],\"Token\":[%s],\"id\":1}", st, tk);
    explicit_bzero(token, sizeof token);
    explicit_bzero(tk, sizeof tk);
    web_logf(stderr, "INFO", "web token issued\n");
    resp->content_type  = CT_JSON;
    resp->body_len      = strlen(out);
    return resp->status = 200;
}

static int rest_route(const web_request *req, const web_ctx *ctx, int token_ok, char *out,
        size_t cap, web_response *resp)
{
    const char              *r      = req->path + 8; /* past "/api/v1/" */
    int                      authed = request_authed(req, ctx, token_ok);
    const char              *rd     = list_find(rest_reads, r);
    const web_rest_resource *res    = rest_resource(r);
    int                      open   = list_find(rest_no_login, r) != NULL;
    int                      rejected;

    if ((rd && req->method == WEB_GET) || (res && req->method == WEB_POST && rest_json(req))) {
        if (rd && req->method == WEB_GET) {
            if (!authed)
                return rest_refused(resp, 401, r, "not logged in", out, cap, WEB_STATUS_AUTH_CODE,
                        WEB_STATUS_NO_LOGIN_MSG);
            return rest_read(req, rd, out, cap, resp);
        }
        if (open)
            return rest_write_no_login(req, r, res, out, cap, resp);
        if (!authed)
            return rest_refused(resp, 401, r, "not logged in", out, cap, WEB_STATUS_AUTH_CODE,
                    WEB_STATUS_NO_LOGIN_MSG);
        return rest_write(req, r, res, out, cap, resp, &rejected);
    }

    if (!strcmp(r, "session")) {
        if (req->method == WEB_POST) {
            if (web_starting(ctx)) {
                op_refused(resp, 503, "login", "starting, retry");
                return reply(resp, 503, CT_JSON, out, cap, "{\"error\":\"starting\"}");
            }
            if (ctx && ctx->activated == 0) {
                op_refused(resp, 403, "login", "device not activated");
                return reply(resp, 403, CT_JSON, out, cap, "{\"error\":\"not_activated\"}");
            }
            char proof[LOGIN_PROOF_MAX];
            if (login_proof_ok(req, ctx, proof)) {
                snprintf(resp->extra_headers, sizeof resp->extra_headers,
                        "Set-Cookie: auth=%s; Path=/; HttpOnly; SameSite=Strict%s\r\n", proof,
                        cookie_secure(req));
                explicit_bzero(proof, sizeof proof);
                web_logf(stderr, "INFO", "web login\n");
                return reply(resp, 200, CT_JSON, out, cap, "{\"result\":\"ok\"}");
            }
            explicit_bzero(proof, sizeof proof);
            op_refused(resp, 401, "login", "invalid credentials");
            return reply(resp, 401, CT_JSON, out, cap, "{\"error\":\"invalid_credentials\"}");
        }
        if (req->method == WEB_DELETE) {
            web_logf(stderr, "INFO", "web logout\n");
            snprintf(resp->extra_headers, sizeof resp->extra_headers,
                    "Set-Cookie: auth=; Path=/; HttpOnly; Max-Age=0%s\r\n", cookie_secure(req));
            return reply(resp, 200, CT_JSON, out, cap, "{\"result\":\"ok\"}");
        }
        return rest_method(resp, out, cap);
    }

    if (!strcmp(r, "dashboard")) {
        if (req->method != WEB_GET)
            return rest_method(resp, out, cap);
        snprintf(resp->extra_headers, sizeof resp->extra_headers, "Cache-Control: no-store\r\n");
        if (!ctx || !ctx->telemetry)
            return reply(resp, 200, CT_JSON, out, cap, "{}");
        resp->content_type  = CT_JSON;
        resp->body_len      = dashboard_out(ctx, authed, out, cap);
        return resp->status = 200;
    }

    if (!strcmp(r, "activation") && req->method == WEB_GET)
        return reply(resp, 200, CT_JSON, out, cap, activation_json(ctx));

    if (!strcmp(r, "auth")) {
        if (req->method != WEB_GET)
            return rest_method(resp, out, cap);
        jwriter w;
        jw_init(&w, out, cap);
        jw_obj_open(&w);
        auth_fields(&w, ctx);
        jw_obj_close(&w);
        resp->content_type  = CT_JSON;
        resp->body_len      = jw_len(&w);
        return resp->status = 200;
    }

    if (!strcmp(r, "qr-login")) {
        if (req->method != WEB_POST)
            return rest_method(resp, out, cap);
        if (web_starting(ctx))
            return reply(resp, 503, CT_JSON, out, cap, "{\"error\":\"starting\"}");
        if (ctx && ctx->activated == 0)
            return reply(resp, 403, CT_JSON, out, cap, "{\"error\":\"not_activated\"}");
        if (!ctx || !ctx->qr_approved)
            return reply(resp, 200, CT_JSON, out, cap, "{}");
        jwriter w;
        jw_init(&w, out, cap);
        jw_obj_open(&w);
        qr_login_fields(&w, req, ctx, resp);
        jw_obj_close(&w);
        resp->content_type  = CT_JSON;
        resp->body_len      = jw_len(&w);
        return resp->status = 200;
    }

    if (!strcmp(r, "pools")) {
        if (req->method != WEB_POST) {
            op_refused(resp, 405, "saving pool settings", "only POST is accepted");
            return rest_method(resp, out, cap);
        }
        if (!authed) {
            op_refused(resp, 401, "saving pool settings", "not logged in");
            return reply(resp, 401, CT_JSON, out, cap, "{\"error\":\"unauthorized\"}");
        }
        return pools_write(req, ctx, out, cap, resp);
    }

    if (!strcmp(r, "system/reboot")) {
        if (req->method != WEB_POST) {
            op_refused(resp, 405, "reboot request", "only POST is accepted");
            return rest_method(resp, out, cap);
        }
        if (!authed) {
            op_refused(resp, 401, "reboot request", "not logged in");
            return reply(resp, 401, CT_JSON, out, cap, "{\"error\":\"unauthorized\"}");
        }
        web_logf(stderr, "INFO", "reboot requested from the web page\n");
        if (!ctx || !ctx->reboot || !ctx->reboot()) {
            web_logf(stderr, "ERR",
                    "reboot request from the web page was "
                    "not accepted by the miner\n");
            return reply(resp, 503, CT_JSON, out, cap, "{\"error\":\"reboot_failed\"}");
        }
        return reply(resp, 200, CT_JSON, out, cap, "{\"result\":\"ok\"}");
    }

    if (!strcmp(r, "lcd-display")) {
        if (req->method == WEB_GET) {
            if (!authed)
                return rest_refused(resp, 401, r, "not logged in", out, cap, WEB_STATUS_AUTH_CODE,
                        WEB_STATUS_NO_LOGIN_MSG);
            jwriter w;
            jw_init(&w, out, cap);
            jw_obj_open(&w);
            jw_kv_raw(&w, "pelish", ctx && ctx->lcd_pelish ? "1" : "0");
            jw_kv_raw(&w, "rotate_s", "180");
            jw_obj_close(&w);
            resp->content_type  = CT_JSON;
            resp->body_len      = jw_len(&w);
            return resp->status = 200;
        }
        if (req->method != WEB_POST) {
            op_refused(resp, 405, "LCD display option", "only GET and POST are accepted");
            return rest_method(resp, out, cap);
        }
        if (!authed) {
            op_refused(resp, 401, "LCD display option", "not logged in");
            return reply(resp, 401, CT_JSON, out, cap, "{\"error\":\"unauthorized\"}");
        }

        char        raw[12] = "";
        int         pelish  = -1;
        const char *body    = req->body ? req->body : "";
        if (web_form_get(body, "pelish", raw, sizeof raw) == 1) {
            if (!strcmp(raw, "1") || !strcasecmp(raw, "on") || !strcasecmp(raw, "true"))
                pelish = 1;
            else if (!strcmp(raw, "0") || !strcasecmp(raw, "off") || !strcasecmp(raw, "false"))
                pelish = 0;
        } else if (rest_json(req)) {
            json_value       *jv = json_parse(body);
            const json_value *pv = json_object_get(jv, "pelish");
            if (json_is(pv, JSON_NUMBER) && (json_int(pv) == 0 || json_int(pv) == 1))
                pelish = (int)json_int(pv);
            json_free(jv);
        }
        if (pelish < 0)
            return reply(resp, 400, CT_JSON, out, cap, "{\"error\":\"invalid_pelish\"}");

        char arg[32];
        int  status;
        snprintf(arg, sizeof arg, "0,lcd,5:%d", pelish);
        status = core_reply("ascset", arg, out, cap, resp);
        if (status == 200) {
            jwriter w;
            jw_init(&w, out, cap);
            jw_obj_open(&w);
            jw_kv_str(&w, "result", "ok");
            jw_kv_raw(&w, "pelish", pelish ? "1" : "0");
            jw_obj_close(&w);
            resp->content_type  = CT_JSON;
            resp->body_len      = jw_len(&w);
            web_logf(stderr, "INFO", "LCD Pelish rotation set from the web page: %d\n", pelish);
        } else {
            web_logf(stderr, "ERR", "LCD Pelish rotation from the web page failed\n");
        }
        return resp->status;
    }

    if (!strcmp(r, "wifi-eth")) {
        if (req->method == WEB_GET) {
            if (!authed)
                return rest_refused(resp, 401, r, "not logged in", out, cap, WEB_STATUS_AUTH_CODE,
                        WEB_STATUS_NO_LOGIN_MSG);
            jwriter w;
            jw_init(&w, out, cap);
            jw_obj_open(&w);
            jw_kv_raw(&w, "wifi_eth", ctx && ctx->wifi_eth ? "1" : "0");
            jw_obj_close(&w);
            resp->content_type  = CT_JSON;
            resp->body_len      = jw_len(&w);
            return resp->status = 200;
        }
        if (req->method != WEB_POST) {
            op_refused(resp, 405, "Wi-Fi with Ethernet option", "only GET and POST are accepted");
            return rest_method(resp, out, cap);
        }
        if (!authed) {
            op_refused(resp, 401, "Wi-Fi with Ethernet option", "not logged in");
            return reply(resp, 401, CT_JSON, out, cap, "{\"error\":\"unauthorized\"}");
        }

        char        raw[12] = "";
        int         keep    = -1;
        const char *body    = req->body ? req->body : "";
        if (web_form_get(body, "wifi_eth", raw, sizeof raw) == 1) {
            if (!strcmp(raw, "1") || !strcasecmp(raw, "on") || !strcasecmp(raw, "true"))
                keep = 1;
            else if (!strcmp(raw, "0") || !strcasecmp(raw, "off") || !strcasecmp(raw, "false"))
                keep = 0;
        } else if (rest_json(req)) {
            json_value       *jv = json_parse(body);
            const json_value *pv = json_object_get(jv, "wifi_eth");
            if (json_is(pv, JSON_NUMBER) && (json_int(pv) == 0 || json_int(pv) == 1))
                keep = (int)json_int(pv);
            json_free(jv);
        }
        if (keep < 0)
            return reply(resp, 400, CT_JSON, out, cap, "{\"error\":\"invalid_wifi_eth\"}");
        if (!ctx || !ctx->wifi_eth_set || !ctx->wifi_eth_set(keep)) {
            web_logf(stderr, "ERR", "Wi-Fi with Ethernet option from the web page failed\n");
            return reply(resp, 503, CT_JSON, out, cap, "{\"error\":\"wifi_eth_failed\"}");
        }
        jwriter w;
        jw_init(&w, out, cap);
        jw_obj_open(&w);
        jw_kv_str(&w, "result", "ok");
        jw_kv_raw(&w, "wifi_eth", keep ? "1" : "0");
        jw_obj_close(&w);
        resp->content_type  = CT_JSON;
        resp->body_len      = jw_len(&w);
        web_logf(stderr, "INFO", "Wi-Fi with Ethernet set from the web page: %d\n", keep);
        return resp->status = 200;
    }

    if (!strcmp(r, "timezone")) {
        if (req->method == WEB_GET) {
            char tz[72] = "";
            if (ctx && ctx->tz_get)
                ctx->tz_get(tz, sizeof tz);
            jwriter w;
            jw_init(&w, out, cap);
            jw_obj_open(&w);
            jw_kv_str(&w, "timezone", tz);
            jw_obj_close(&w);
            resp->content_type  = CT_JSON;
            resp->body_len      = jw_len(&w);
            return resp->status = 200;
        }
        if (req->method != WEB_POST) {
            op_refused(resp, 405, "setting the timezone", "only POST is accepted");
            return rest_method(resp, out, cap);
        }
        if (!authed) {
            op_refused(resp, 401, "setting the timezone", "not logged in");
            return reply(resp, 401, CT_JSON, out, cap, "{\"error\":\"unauthorized\"}");
        }
        return tz_write(req, ctx, out, cap, resp);
    }

    if (res && req->method == WEB_POST) {
        if (!authed && !open)
            return rest_refused(resp, 401, r, "not logged in", out, cap, WEB_STATUS_AUTH_CODE,
                    WEB_STATUS_NO_LOGIN_MSG);
        return rest_refused(resp, 400, r, "not a JSON request", out, cap, WEB_STATUS_BAD_REQ_CODE,
                WEB_STATUS_JSON_MSG);
    }
    if (rd || res)
        return rest_method(resp, out, cap);

    return rest_bad_cmd(resp, 404, r, "no such resource", r, out, cap);
}

static int route(const web_request *req, const web_ctx *ctx, char *out, size_t cap,
        web_response *resp)
{
    const char *why;
    int         token_ok = 0;

    if (req->path && !strcmp(req->path, WEB_TOKEN_URI)) {
        if (req->method != WEB_POST)
            return rest_method(resp, out, cap);
        return token_issue(req, ctx, out, cap, resp);
    }
    if (req->auth_token) {
        if ((why = token_fault(req, ctx))) {
            web_logf(stderr, "WARN", "web token refused: %s\n", why);
            return token_status(resp, 401, "E", WEB_STATUS_AUTH_CODE, WEB_STATUS_TOKEN_STALE_MSG,
                    out, cap);
        }
        token_ok = 1;
    }

    /* REST surface; firmware upload keeps the dedicated streaming handler */
    if (req->path && !strncmp(req->path, "/api/v1/", 8) && strcmp(req->path, WEB_UPGRADE_URI))
        return rest_route(req, ctx, token_ok, out, cap, resp);

    if (req->path && !strcmp(req->path, WEB_RUNLOG_URI)) {
        char name[LOGEXPORT_NAME_MAX];
        int  concat = 1;
        int  st;

        if (!req->tls) {
            web_logf(stderr, "WARN", "log download refused: HTTPS required\n");
            return reply(resp, 403, CT_TEXT, out, cap, "HTTPS required");
        }
        if (req->method != WEB_POST) {
            web_logf(stderr, "WARN", "log download refused: only POST is accepted\n");
            return resp->status = 405;
        }
        if (!token_ok) {
            web_logf(stderr, "WARN", "log download refused: not logged in\n");
            return reply(resp, 401, CT_TEXT, out, cap, "unauthorized");
        }
        if (logexport_body_name(req->body, name, sizeof name) != 0 ||
                (web_plugin_file(name, resp->serve_path, sizeof resp->serve_path, &concat) != 0 &&
                        logexport_lookup(name, resp->serve_path, sizeof resp->serve_path) != 0)) {
            logexport_safe_name(req->body, name, sizeof name);
            web_logf(stderr, "WARN", "log download refused: no such log \"%s\"\n", name);
            return reply(resp, 404, CT_TEXT, out, cap, "Not found\n");
        }
        st = logexport_serve_check(name, resp->serve_path, concat);
        if (st != 200)
            return reply(resp, st, CT_TEXT, out, cap,
                    st == 404 ? "Not found\n" : "cannot read the log");
        logexport_safe_name(name, resp->serve_name, sizeof resp->serve_name);
        resp->serve_file    = resp->serve_path;
        resp->serve_concat  = concat;
        resp->content_type  = "application/octet-stream";
        return resp->status = 200;
    }

    return reply(resp, 404, CT_TEXT, out, cap, "Not found\n");
}

int web_route(const web_request *req, const web_ctx *ctx, char *out, size_t cap, web_response *resp)
{
    resp->content_type     = NULL;
    resp->body_len         = 0;
    resp->extra_headers[0] = '\0';
    resp->serve_file       = NULL;
    resp->serve_concat     = 0;
    resp->serve_name[0]    = '\0';

    int status = route(req, ctx, out, cap, resp);
    if (resp->body_len >= cap)
        resp->body_len = cap ? cap - 1 : 0;
    return status;
}
