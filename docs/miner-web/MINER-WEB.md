# Miner web server (`apps/miner-web`)

`miner_web` serves the miner's web pages and firmware upload. Paths are relative to
`apps/miner-web/` unless they start with `apps/miner/`, `build/`, `docs/` or `product/`.
Processes, layers and deployment: [MINER.md](../miner/MINER.md#overview).

## Layout

| path | content |
|---|---|
| `app/miner_web_main.c` | entry: `miner_web [port] [systemcfg.ini]`, plain-HTTP port `80` by default |
| `web/` | the router, the civetweb glue, firmware and wallpaper upload, tokens, log download, the HTTPS certificate |
| `assets/` | the pages (`login.html`, `dashboard.html`, `pools.html`, `qr_login.html`, `upgrade.html`, `network.html`, `log.html`, `password.html`, `reset.html`), shared chrome (`chrome.css`, `chrome.js`), and brand logos (`pelish-logo.png`, `canaan-logo.png`) |
| `third_party/` | `js-sha256` and `qrcodejs`, loaded by the pages |
| `services/miner-web.init` | starts `miner_web` on port 80 with `/data/userconfig/systemcfg.ini` and restarts it when it exits |
| `tests/` | host tests: `docs/TESTING.md` |

The recipe `product/k230/miner/common/recipes-apps/miner-web/miner-web_0.1.bb` builds it against
civetweb and OpenSSL `libcrypto` and installs the pages to `/usr/share/miner-web/www`. civetweb
comes from meta-openembedded;
`product/k230/miner/common/recipes-connectivity/civetweb/civetweb_%.bbappend` pins its revision,
builds `ssl` against the OpenSSL 3 API and turns off `caching` and the `civetweb` executable.

## Interface to the miner

| what | how |
|---|---|
| dashboard, REST, wallpaper, reboot, time zone, QR login | `core_miner` trusted API socket `/run/avalon_miner_api.sock` (`web/core_api.c`) |
| login, dashboard JSON | `apps/miner/miner_mgmt/api` (`status.c`, `webauth.c`), compiled in |
| pool save, pool passwords for the dashboard | config writer `/tmp/sockcfgwr` (`apps/miner/common/cfgwr`) |
| firmware upload | `mm_miner` OTA socket `/tmp/sockupctrl` |

## Ports

| port | serves |
|---|---|
| `443` | every route, HTTPS |
| `8443` | `/api/v1/*` only, HTTPS; any other path answers `404`. When it cannot be opened, `/api/v1` stays on `443` |
| `80` | redirects `GET` and `HEAD` to the same path on HTTPS (`302`); answers `403` to other methods, to a request with a `Token` header and to `/api/v1/token`, as the `STATUS` JSON `503` `HTTPS required` under `/api/v1/` |

- HTTPS is TLS 1.2 or later.
- The certificate is self-signed EC P-256, made at first start in `/data/userconfig/miner-web.pem`
  and kept across reboot, OTA and factory reset, or in `/tmp/miner-web.pem` until reboot when
  `/data` is not writable. Browsers warn on it.

## Request checks

- **Host:** `Host`, when the request carries one, must be an IP literal (IPv6 in brackets),
  `localhost` or the device host name; otherwise `403`. A request without it passes.
- **Origin:** on methods other than `GET`/`HEAD` and on firmware and wallpaper upload, `Origin` (or
  `Referer`), when the request carries one, must name the same host and port as `Host`; otherwise
  `403`. A request with neither passes.
- **Static files:** only the pages, the brand logos and the two scripts listed in `g_static_files`
  (`web/civetweb_glue.c`); any other path answers `404`. A new page goes into that list; a new
  third-party script also into the recipe's `do_install`. Static responses carry
  `Cache-Control: public, max-age=300`. Dashboard and other API replies stay `no-store`.

## Authentication

- The pages use the `auth` cookie set by login (`HttpOnly; SameSite=Strict`, `Secure` over HTTPS),
  checked against the first 24 hex digits of `webpass` (the password SHA-256) in `systemcfg.ini`;
  tools use a `Token` header. The nano3s factory login is `admin` / `admin`.
- Five failed logins or token requests from one address within 60 s refuse that address for 60 s.
  The lockout tracks 16 addresses and frees the least recently failed slot that is not locked;
  while all 16 are locked, a new address is refused as locked.
- The REST resources other than `activation` and `qr-auth`, saving pools, reboot, setting the time
  zone, firmware upload and wallpaper need the cookie or a token. `/runlog` takes only a token,
  on a `POST` over HTTPS; otherwise it answers `401`, `403` or `405`.
- The dashboard needs neither and answers with `Cache-Control: no-store`. Without a login a
  configured pool shows the password `*`; with one the dashboard carries the saved passwords.
- Login and QR login answer `503` `{"error":"starting"}` while the activation state is unknown and
  `403` when the device is not activated; a token also works on a device that is not activated. A
  QR approval is taken only on an activated device whose `webpass` has a non-zero digit.

**Token.**
- `POST /api/v1/token` with `Content-Type: application/json` and
  `{"command":"token","user":"<u>","password":"<p>"}` returns an HS256 JWT valid for 24 h:

  ```json
  {"STATUS":[{"STATUS":"S","When":<t>,"Code":610,"Msg":"Token issued","Description":"<version>"}],
   "Token":[{"Name":"<u>","When":<t>,"Token":"<jwt>"}],"id":1}
  ```

  A refused request answers `E` with code 601 (`400` malformed, `401` wrong user or password),
  603 `webpass` not set (`403`) or 604 token service unavailable (`503`).
- The signing key comes from a random secret in `/data/factory/miner-web.tokenkey`, or
  `/tmp/miner-web.tokenkey` when `/data` cannot hold it, and from the web user name and password.
  The secret file must be a regular file owned by the user `miner_web` runs as, with mode 0600 or
  narrower; otherwise a new secret replaces it. Changing the user name or password, by any path,
  or a factory reset, which deletes the file on `/data`, refuses every token issued before.
- A token holds for 24 h within its boot; after a reboot only when the clock was synchronized
  both when it was issued and when it is checked.
- A `Token` header is the only credential of its request, on every route. A refused token answers
  `401` with code 602 `Token expired, request a new one` and does not count toward the lockout.

## Status codes

Refusals by `miner_web` on the [REST](#rest) resources, the token reply, and the wallpaper and
firmware upload replies are a `STATUS` JSON,
`{"STATUS":[{"STATUS":"E","When":<t>,"Code":<code>,"Msg":"<msg>"}],"id":1}`, with the codes of
`web/webstatus.h`. The form routes `session`, `pools` with a form body, `system/reboot` and
`timezone` answer `{"error":"<reason>"}` or `{"result":"error","reason":"<reason>"}`, and a method
they do not take `405` with code 505.

| code | HTTP | `Msg` |
|---|---|---|
| 14 | `400`, `404` | `Invalid command <name>`: a command not on the resource, or an unknown resource |
| 200 | `200`, `202` | firmware upload result, `STATUS` `S` |
| 500 | `200`, `500`, `504`, `409`, `503` | `Reply too long: not sent` (`500`), `Command timeout` (`504`, `core_miner` did not answer in time), `Internal error`, wallpaper and firmware upload failures; `200` when `mm_miner` judged the upgrade failed |
| 501 | `400`, `411`, `413` | `Invalid request`, `JSON request body required` (a `POST` without JSON to a REST resource, `activation` and `qr-auth` included), an invalid upload |
| 503 | `403` | `HTTPS required` |
| 504 | `403` | `Request refused`: `Host` or `Origin` on upload and wallpaper |
| 505 | `405` | `Method not allowed` |
| 506 | `429` | `Too many failed attempts, try again later` |
| 601 | `400`, `401` | `Invalid token request`, `Invalid user or password` |
| 602 | `401` | `Not logged in`, `Token expired, request a new one` |
| 603 | `403` | `Web password not set` |
| 604 | `503` | `Token service unavailable` |
| 610 | `200` | `Token issued`, `STATUS` `S` |

## Routes

`/api/v1` answers JSON.

| method | path |
|---|---|
| `POST` / `DELETE` | `/api/v1/session` (login / logout) |
| `POST` | `/api/v1/token` |
| `GET` | `/api/v1/dashboard` |
| `GET` | `/api/v1/activation` |
| `GET` | `/api/v1/auth` |
| `POST` | `/api/v1/qr-login` |
| `POST` | `/api/v1/pools` (form) |
| `POST` | `/api/v1/system/reboot` (form) |
| `GET` / `POST` | `/api/v1/timezone` (`timezone=<IANA name>`) |
| `GET` / `POST` | `/api/v1/wifi-eth` (`wifi_eth=0` auto-disable Wi-Fi when USB Ethernet has an IPv4 address; `1` keep Wi-Fi up) |
| `GET` / `POST` | the [REST](#rest) resources |
| `POST` / `DELETE` | `/api/v1/wallpaper` |
| `POST` | `/api/v1/upgrade` |
| `POST` | `/runlog` |

A method a `/api/v1` route does not take answers `405` with code 505.

- **Firmware upload:** `POST /api/v1/upgrade`, HTTPS only, `multipart/form-data` with the image in
  the first file field, one at a time. The reply is `Upgrade ok(<err>), percent(<n>)` (`S`, 200) or
  `Upgrade failed(<err>), percent(<n>)` (`E`, 500), HTTP `200` in both cases; without a result from
  `mm_miner` it is HTTP `202` `Upgrade status unknown, device reported no upgrade result`. When the
  OTA socket breaks off it is HTTP `500` `Upgrade failed(<err>), percent(<n>)`, or `Upgrade status
  unknown, the device stopped receiving the image` without a result. Refusals: `401` 602, `400`
  501 (no multipart body, no image, truncated body), `409` 500 `Another firmware upgrade is in
  progress`, `503` 500 `Device not ready for an upgrade, try again shortly`. `upgrade.html` shows
  the progress of the upload.
- **`/runlog`:** the body names `miner.log`, `miner_op.log` or `web.log` in `/data/userdata/log`,
  or a file `web_plugin_file()` (`web/web_plugin.h`) adds; the reply is `<file>.1` followed by
  `<file>`. Any other name answers `404`.
- **Pools:** form row i is config slot i-1. A row with an empty URL or worker leaves its slot free;
  a row with an empty password or `*` keeps the password saved in its slot, and answers `400`
  `pool <n> needs a password` when the slot holds none, so a form cannot set the password `*`. An
  invalid field, or a form that leaves every slot free, answers `400` and changes nothing. A slot
  keeps its enabled flag while its URL is unchanged; a new URL enables it. Saved pools take effect
  when `core_miner` restarts. With a login the dashboard carries the saved passwords in
  `passwd1`..`passwd3`, read from `mm_miner` over `/tmp/sockcfgwr`, and the Pools page shows them.
- **Time zone:** `POST` changes the zone only, not the clock.
- **Wi-Fi with Ethernet:** `GET`/`POST /api/v1/wifi-eth` (login required) reads or writes
  `[net] wifi_eth` in `systemcfg.ini`. Default `0` takes `wlan0` down when USB Ethernet has an
  IPv4 address; `1` keeps Wi-Fi up. The dashboard checkbox **Keep Wi-Fi on with Ethernet** posts
  the form field `wifi_eth`. No extra settings page.
- **QR login:** the QR code carries a public challenge. The App sends `qr_auth` with
  `<challenge>,<proof>` to `POST /api/v1/qr-auth`, and the page polls `POST /api/v1/qr-login`
  with its private token. A session lasts five minutes and is approved once.

## REST

Every request needs a login, except the writes to `activation` and `qr-auth`: their signature or
proof is checked by `core_miner`, and five replies with `STATUS` `E` to one address within 60 s
refuse that address for 60 s (`429`, code 506). The reply is the cgminer JSON of `core_miner`
(`STATUS` array, data sections, `id`), HTTP `200`, or `400` when its `STATUS` is `E`. Refusals by
`miner_web` follow [Status codes](#status-codes).

- **Reads:** `GET /api/v1/<command>` with the query value `parameter` runs `version`, `summary`,
  `pools`, `stats`, `estats`, `devs`, `edevs`, `devdetails`, `config`, `coin` or `litestats`;
  `GET /api/v1/loop` reads the chip count.
- **Writes:** `POST` with `Content-Type: application/json` and the body
  `{"command":"<name>","parameter":"<value>"}`; `command` is a string, `parameter` a string or a
  number. A top-level command goes as it is, an `ascset` setting as `ascset`
  `0,<name>[,<value>]`. A `POST` to `pools` or `system/reboot` without JSON takes the page form.

| resource | top-level commands | `ascset` settings |
|---|---|---|
| `pools` | `setpool`, `addpool`, `removepool`, `enablepool`, `disablepool`, `switchpool`, `poolpriority` | — |
| `stats` | `zero` | — |
| `system` | `softon`, `softoff` | `reboot`, `filter-clean` |
| `system/reboot` | — | `reboot` |
| `time` | `time` | — |
| `mode` | — | `workmode`, `work_mode_lvl`, `worklevel` |
| `led` | — | `ledmode`, `ledset`, `nightlamp` |
| `lcd` | — | `lcd` |
| `password` | — | `password` |
| `activation` | — | `activate` |
| `qr-auth` | — | `qr_auth` |

`web_plugin_rest()` (`web/web_plugin.h`) adds resources of the same shape.

**Wallpaper.** `POST /api/v1/wallpaper?format=png|gif&sha256=<hex>&signature=<hex>` takes the image
as the raw body, 1 KiB to 300 KiB, and passes it to `core_miner` as `ascset` `wallpaper` chunks;
`core_miner` checks the signature over `sha256` and the image against it. `DELETE` clears the
wallpaper. Both need a login and HTTPS. One request runs at a time, and none during a firmware
upload (`409`, code 500). An invalid `format`, `sha256`, `signature` or a truncated body answers
`400`, no `Content-Length` `411` and a size out of range `413`, all with code 501.

## `mmupgrade`

`apps/miner/tools/mmupgrade <file.aup> <ip> [password]` takes a token from
`https://<ip>:8443/api/v1/token` as `$MMUPGRADE_USER` (default `admin`) and uploads the file to
`https://<ip>:8443/api/v1/upgrade` with the `Token` header. The password comes from the argument
or from `$MMUPGRADE_PASS`; prefer the variable, as an argument shows in `ps`. `curl -k` does not
check or pin the device certificate, and a failed request is not retried. It exits 0 only when
the reply carries `Upgrade ok(`. A device without `8443` and `/api/v1/token` cannot be upgraded
with it.

## `mmpool`

`apps/miner/tools/mmpool -c <pools.conf> [options] <ip>... [--hosts <file>]` sets the pools of
one or more miners with `setpool` on `https://<ip>:8443/api/v1/pools`, reboots them and checks
on `:4028` that they mine. `-j N` handles N devices at once (default 8); a table of the results
follows. `mmpool -h` lists every option.

```sh
MMPOOL_PASS=admin apps/miner/tools/mmpool -c pools.conf 192.168.1.100
MMPOOL_PASS=admin apps/miner/tools/mmpool -c pools.conf --hosts farm.txt -j 16
```

**Pool file.** One pool per line, at most three; line n goes into slot n-1, `#` starts a
comment. A line without a password takes `$MMPOOL_POOL_PASS`. Keep the file mode 0600.

```
# <url>                               <worker>  [<password>]
stratum+tcp://pool.example.com:3333   acct.001  x
stratum+tcp://backup.example.com:3333 acct.001  x
```

Before sending anything, `mmpool` refuses an empty field, a field with a space, `,`, `"`, `|`,
`\` or a control character, a URL not ending in `:<port>` with a port 1 to 65535, and fields
over 256 (URL) or 128 (worker, password) bytes. The hosts file holds one address per line.

**Run.** Per device:
1. Reads the running pools from `:4028`. When their URL and worker equal the file, and
   `--force` is not given, the device is left as it is (`unchanged`).
2. Takes a token as `$MMPOOL_USER` (else `$MMUPGRADE_USER`, default `admin`) with the password
   from `$MMPOOL_PASS` (else `$MMUPGRADE_PASS`, else asked on the terminal). Passwords are never
   taken from the command line.
3. Writes each line into its slot and clears the slots past the last line, unless
   `--keep-unused`.
4. Reboots (`--reboot-delay N`). A miner that ran no pool loads the new ones within 10 s and is
   not rebooted. `--no-reboot` skips the reboot and the check; the pools wait for the next
   reboot (`pending`).
5. Polls `:4028` every `--verify-interval` s (default 10) for up to `--verify-timeout` s
   (default 300); `--no-verify` skips this.

**Check.** It passes when the miner has restarted (`Elapsed` of `summary` fell), runs every
pool of the file, by URL and worker, and no other (unless `--keep-unused`), and a pool of the
file that is `Alive` and `Stratum Active` raises `Accepted` between two polls. A pool of the
file running `Disabled` (a slot rewritten with the same URL keeps the flag) is only reported;
`--enable-disabled` sends it `enablepool`.

**Secrets.** The token and the request bodies reach `curl` on its standard input (`curl -K -`),
never in its arguments, and the passwords are removed from the environment of the processes it
starts. The `setpool` success reply repeats the pool password; `mmpool` prints only `Code` and,
for a refusal, `Msg` with any password masked, never a reply itself.

**TLS.** Without `--pin`, `curl -k` checks no certificate: on an untrusted network, someone in
the path can take the web password and the token and change the pools. `--pin sha256//<base64>`
checks the device public key with `curl --pinnedpubkey`; for several devices, `--pin <file>`
holds `<ip> sha256//<base64>` lines. The key is kept across reboot, OTA and factory reset (see
[Ports](#ports)). Its pin:

```sh
openssl s_client -connect <ip>:8443 </dev/null 2>/dev/null | openssl x509 -pubkey -noout |
  openssl pkey -pubin -outform der | openssl dgst -sha256 -binary | base64
```

**Failures.** A wrong login (code 601) is not retried, so a run adds one failure per device to
the [lockout](#authentication). A locked login (`429`, code 506) fails the device unless
`--wait-lock`, which waits 61 s (`$MMPOOL_LOCK_WAIT`) and tries once more. A `setpool` request is
retried twice on a network error, `503` or `504`, and once with a new token on `401`; a device
left with some slots written reports `partial`, and a new run rewrites it. When `8443` refuses
the connection, `--fallback-port 443` uses `443`.

| exit | meaning |
|---|---|
| 0 | every device `ok`, `unchanged` or `pending` |
| 1 | a device failed: unreachable, login, lockout, pin, `setpool` or reboot |
| 2 | usage or pool file error; nothing was sent |
| 3 | the pools were set, but the check failed on a device |

**Limits.** The miner reports only its running pools, without passwords, and no saved pool not
yet in effect. A change of password alone, or saved pools that a reboot has not applied yet,
need `--force`. A single pool file serves every device, so each device gets the same worker.

## Logging

General messages go to `miner.log` and `web.log` in `/data/userdata/log/`, upload detail only to
`web.log`, the upload summary only to `miner.log`; `OP` lines also to `miner_op.log`.
`$MINER_LOG_LEVEL` sets the level.
