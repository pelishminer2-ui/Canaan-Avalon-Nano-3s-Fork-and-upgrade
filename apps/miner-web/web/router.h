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

#ifndef ROUTER_H
#define ROUTER_H

#include <stddef.h>

#include "logexport.h"
#include "webauth.h"
#include "status.h"

#define WEB_SERVE_PATH_MAX    256 /* serve_path buffer, bytes including NUL */
#define WEB_RUNLOG_URI        "/runlog"
#define WEB_TOKEN_URI         "/api/v1/token"
#define WEB_UPGRADE_URI       "/api/v1/upgrade"
#define WEB_ACTIVATED_UNKNOWN (-1)

typedef enum { WEB_GET, WEB_POST, WEB_DELETE, WEB_METHOD_OTHER } web_method;

/* What a request on the plain-HTTP port gets */
typedef enum {
    WEB_PLAIN_REDIRECT, /* GET or HEAD, sent to the same path and query over HTTPS */
    WEB_PLAIN_REFUSE    /* anything else, a Token header or WEB_TOKEN_URI, 403 */
} web_plain_action;

typedef struct {
    web_method  method;
    const char *path;
    const char *body;        /* NUL-terminated request body, NULL when empty */
    const char *auth_cookie; /* "auth" cookie value, NULL when absent */
    const char *auth_token;  /* "Token" header value, NULL when absent */
    int         tls;         /* 1 when the request arrived over HTTPS */
    const char *remote_addr;
    const char *content_type;
    const char *query;
} web_request;

typedef struct {
    int         status;
    const char *content_type;       /* Content-Type value, NULL for text/plain */
    size_t      body_len;           /* reply body length in the output buffer, bytes */
    char        extra_headers[256]; /* extra header lines, each ending in CRLF */
    const char *serve_file;         /* file sent as the body instead of the buffer, NULL for none */
    int         serve_concat;       /* 1 to send the rotated "<serve_file>.1" before serve_file */
    char        serve_path[WEB_SERVE_PATH_MAX]; /* storage serve_file may point at */
    char        serve_name[LOGEXPORT_NAME_MAX];
} web_response;

typedef struct {
    const mc_webcred   *creds;     /* web login credentials, NULL when not loaded */
    const mc_dashboard *telemetry; /* dashboard telemetry, NULL when unavailable */
    const char         *pool_conf;
    const char         *auth_nonce; /* QR login nonce, 8 hex chars, or empty */
    /* QR login session token, 32 hex chars, NULL when not issued */
    const char *qr_session;
    int         qr_approved;
    int         activated;
    const char *dna;                      /* device DNA string, NULL when unknown */
    int (*reboot)(void);                  /* requests a reboot, non-zero when accepted */
    int (*tz_get)(char *out, size_t cap); /* read current zone, 0 ok */
    int (*tz_set)(const char *tz);        /* set zone: 1 ok, 0 refused,
                                             -1 zone not installed */
    int lcd_pelish;                       /* 1: LAN UI Pelish LCD rotation */
} web_ctx;

int              web_route(const web_request *req, const web_ctx *ctx, char *out, size_t cap,
                     web_response *resp);
void             web_log(const char *lvl, const char *fmt, ...);
const char      *web_request_refused(const char *host, const char *origin, const char *referer,
             int changes_state);
web_plain_action web_plain_policy(const char *method, const char *path, const char *token);
int web_https_location(const char *host, const char *path, const char *query, char *out,
        size_t cap);

#endif /* ROUTER_H */
