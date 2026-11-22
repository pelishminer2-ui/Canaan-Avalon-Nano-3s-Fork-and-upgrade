# Avalon miner software

Architecture and external interfaces of the miner (`apps/miner`) and its web server
(`apps/miner-web`). Paths are relative to `apps/miner/` unless they start with `apps/`, `docs/`,
`product/` or `test/`. Cross-layer rules: [CROSS-LAYER-RULES.md](../CROSS-LAYER-RULES.md); build:
[BUILD.md](../BUILD.md); host tests: [TESTING.md](../TESTING.md).

## Overview

The miner runs as four Linux processes over `AF_UNIX`:

| process | role |
|---|---|
| `mm_miner` | supervisor of the other two; rail, fan, watchdog, config writes, OTA |
| `core_miner` | stratum client and the AvalonMiner API on `:4028` and the trusted socket |
| `asic_miner` | ASIC UART, work dispatch, nonce collection, chip telemetry |
| `miner_web` | web UI on `:443`, REST on `:443` and `:8443` (`:80` redirects), firmware upload |

**Layers.** lay1 product config (`hal/board/` and `product/k230/miner/<line>/<product>`) · lay2
`osif` (OS interface) · lay3 `miner_core` (data path) and `miner_mgmt` (control rules and
services) · lay4 `hal` (hardware). lay3 calls only `osif`, `hal` and `common/`; `hal` calls `osif`,
never lay3.

**Processes.**
- `/etc/init.d/avalon-miner` starts `mm_miner`, which starts `core_miner` from
  `/mntapp/release/linux/app` and `asic_miner` from `/mntapp/release/asic`. Stop the miner through
  the init script, or kill `mm_miner`, never a child. `/etc/init.d/miner-web` runs `miner_web` on
  its own.
- The processes exchange native structs (`common/proto/`, `common/cfgwr/`, `common/pool/pool.h`):
  build all four binaries from one tree and deploy them together.

**Logs** in `/data/userdata/log/`: `miner.log`, `miner_op.log` (key events) and `web.log`, each
rotated to one `<log>.1`. `$MINER_LOG_LEVEL` (`ERR|WARN|INFO|DBG`, default `INFO`) sets the level.

**Storage.** `/data` is a separate UBI volume, outside the OTA package, so config, calibration and
logs survive an upgrade.

## OTA

Invariants: [CROSS-LAYER-RULES.md](../CROSS-LAYER-RULES.md#miner-aup-ota). Packaging:
[BUILD.md](../BUILD.md#output-images).

- **Package:** the bins of the product's `aup-ota.json`; bin names are the MTD label stems.
- **Security:** a correctly signed package whose header `HW` and `SW` match the U-Boot env is
  accepted, including an older version; a successful upload reboots the machine unattended.

## Watchdog and reboots

Invariants: [CROSS-LAYER-RULES.md](../CROSS-LAYER-RULES.md#miner-watchdog).

| leg | reports while | when it stops |
|---|---|---|
| SUPERVISOR | `mm_miner` main loop and V/F thread run | board reboots |
| ASIC | `asic_miner` telemetry arrives with `asic_health` OK | board reboots |
| CORE | `core_miner` stats: main loop runs, no API request in service for over 30 s | silent for 60 s: killed and respawned; board reboots when three respawns in a row never report |

A board whose hashboards never enumerate latches a fault softoff instead of reset-looping.

**Reboot reasons** are stored in `systemcfg.ini` `[syscfg] reason/subreason` and logged at start-up:
0x01 PowerOn, 0x02 WEB, 0x03 API, 0x04 WDT, 0x06 System, 0x07 CommFail, 0x08 PoolInactive, 0x09
FacRestore, 0x0A RTSysFail, 0x0B NetdevOut, 0x0C PowErr, 0x0D Upgrade, 0x11 NOShare, 0x12
LowHashrate, 0x13 HashLost, else Unknown. `System` subreasons: a fatal signal number, 15 for an
init-script stop, 0xF3 kernel panic, 0xF1 other hardware watchdog reset. `WDT`
subreason: bits 0–2 are the legs SUPERVISOR, ASIC, CORE that had reported, bits 16–18 the legs
the gate waits for; a leg set in the high half and clear in the low half had not reported.

**Reboot ownership.** Only `mm_miner` reboots; `ascset 0,reboot,<0..300>` and
`ascset 0,reboot_web` (answered only on the trusted socket) are forwarded to it.

## IPC

`AF_UNIX` `SOCK_SEQPACKET` sockets, mode 0700 (`common/proto/miner_ipc.h`):

| path | server | client | carries |
|---|---|---|---|
| `/run/avalon_miner_ipcmsg.sock` | `core_miner` | `asic_miner` | jobs, nonces, telemetry |
| `/run/avalon_miner_ctrl.sock` | `mm_miner` | `core_miner` | control commands, status, stats |
| `/run/avalon_miner_asicctrl.sock` | `mm_miner` | `asic_miner` | V/F, telemetry, efuse |
| `/tmp/sockcfgwr` (`SOCK_STREAM`, 0600) | `mm_miner` | `core_miner`, `miner_web` | config writes, pool read |
| `/run/avalon_miner_api.sock` (`SOCK_STREAM`, 0600) | `core_miner` | `miner_web` | API requests (trusted socket) |
| `/tmp/sockupctrl` (`SOCK_STREAM`, 0600) | `mm_miner` | `miner_web` | OTA |

## `:4028` API

- `core_miner` serves the API on TCP `:4028`, all interfaces, without authentication, and on the
  trusted socket `MC_API_SOCK_PATH` (`common/proto/api_sock.h`), one request per connection, same
  commands and replies.
- Each listener runs in its own thread and serves one connection at a time. On `:4028` a request
  must arrive within 300 ms and the reply leave within 500 ms, on the trusted socket within 2 s
  each; past either limit the connection is closed. A request that does not arrive in time is
  logged with one line at a fixed level, `DBG` on `:4028` and `WARN` on the trusted socket.
- `:4028`, loopback included, only reads. A write answers `E`/45 `Access denied to '<cmd>'
  command`, and `check` answers `Exists=Y`, `Access=N` for it. Writes are the commands registered
  through `mc_api_command_register`, `qrlogin`, `time set`, and `ascset` other than `help` and the
  requests a setting declares as reads (the read check of `mc_api_setter_declare()`).
- The trusted socket takes every command, and `check` answers `Access=Y` there. `qrlogin` and
  `ascset 0,reboot_web` answer only there. `miner_web` authenticates a write before it sends it,
  except `ascset` `activate` and `qr_auth`, whose signature or proof `core_miner` checks;
  `ascset password` also checks the old password. Writes from the network reach it through the
  HTTPS REST resources of `miner_web` on `:443` and `:8443`
  ([MINER-WEB.md](../miner-web/MINER-WEB.md#rest)).
- A request starting with `{` is JSON (`"command"`, `"parameter"`); any other is
  `command|parameter`. The `parameter` of a JSON `ascset` is decoded as a JSON string, escapes
  included. A request starting with an HTTP method answers `Invalid command`. A reply
  larger than `MC_API_REPLY_CAP` is `E`/14 `Reply too long: not sent`.
- Identity `AvalonMiner`. `AM_VER` is `major.minor.patch` of `apps/miner/VERSION`. The keys of the
  `version` reply, their order and count are fixed (`VERSION_KEYS[]`,
  `miner_core/tests/test_api_cmds.c`).
- The build version ([BUILD.md](../BUILD.md#output-images)) is `FWVERSION` in `version`,
  `Ver`/`LVer` in estats and the firmware version on the dashboard.
- A text request to estats, stats or litestats answers `MM ID0` as one `Key[value]` list, parsed
  by field name.
- A JSON request answers with `MMID0Summary`, a JSON object holding the same keys and values.
  estats and stats split it into `Sysinfo` (versions, system state, control-board temperatures,
  fans, power, error and pool state) and `Hashinfo` (hash rate, work, hashboard temperatures and
  per-board counters), followed by `HBinfo` with one `HB_<b>_Info` object of per-chip lists per
  hashboard, then `MM Count` and `Nonce Mask`. The parameter `all` (`estats|all`) adds the
  per-chip `CF`, `PLLCNT`, `ERATIO`, `C_` and `GHSmm` lists to each `HB_<b>_Info`, as it adds them
  to the text list. litestats puts its keys flat in `MMID0Summary`.
- In `MMID0Summary` a numeric value is a JSON number without its `%` unit, a list of values an
  array, and text or a value joined with `-` or `.` (`Ver`, `LED`, `BOOTBY`, `ATA<n>`) a string.
  Keys keep their text-reply order within a block:

  ```
  "MMID0Summary":{"Sysinfo":{"Ver":"<model>-<version>","LVer":"<version>","FW":"Debug",
  "PVer":"<major.minor>",...},"Hashinfo":{"LW":<n>,"MH":[<n>],...}},
  "HBinfo":{"HB_0_Info":{"PVT_T0":[<n>,...],"PVT_V0":[<n>,...],"MW0":[<n>,...]}},
  "MM Count":1,"Nonce Mask":25
  ```

- The protocol version `PVer` is `1.5`.
- The `pools` reply carries each pool's URL, user and status; the pool passwords reach only a
  logged-in web dashboard, over `/tmp/sockcfgwr`.
- Five wrong passwords within 60 s refuse every password check for 60 s, and five wrong QR login
  proofs within 60 s every proof check for 60 s; each check has its own lock.
- The phone app matches the `password` reply by substring, in order: `Auth Failed`, `failed`,
  `new password success`.
- `ascset` replies: applied `S`/119 `ASC <n> set OK`; info `I`/118 `ASC <n> set info: <reason>`;
  refused `E`/120 `ASC <n> set failed: <reason>`, an unknown subcommand with the reason
  `Unknown option: <subcmd>`. `ascset 0,help` lists the offered settings. `workmode,get`,
  `worklevel,get` and `work_mode_lvl,get` answer `I`/118 with `workmode <n>`, `worklevel 0` and
  `workmode <n> worklevel 0`, `<n>` the running mode; they are reads and answer on `:4028`.
- `ascset` logs each setting with its arguments, except `password`, `qr_auth`, `activate` and the
  settings a plugin declares secret (`MC_API_SETTER_SECRET`).
- `time|set,t:<utc-epoch>,<tz>` sets timezone and clock once `mm_miner` has applied the zone, else
  answers `E`/120 `Time set failed: <reason>` and sets neither: an epoch outside 2020–2099, a zone
  the image does not ship, or no answer from `mm_miner` within 2 s. The phone app takes a reply
  containing `failed` as a failure. On `:4028` it answers `E`/45; it is set over the trusted
  socket or REST. `time|get` reads the timezone; `time` without a parameter answers `I`/130
  `Time parameter is not available`.
- `softon`/`softoff` (`now`, `<epoch>`, `<0|1>,<epoch>`) answer `S`/119 once `mm_miner` has accepted
  the command, else `E`/120 with the reason: a scheduled time already past, an immediate `softon`
  during the over-temperature cool-down, a `softon` alone during a fault stop, or no answer from
  `mm_miner` within 2 s.
- `wallpaper` takes an image in hex chunks; chunk 0 carries its SHA-256 and signature, checked
  against the API key. `none` clears it.

**Pool commands.** Pool ids are positions in the `pools` reply.

| command | success | errors |
|---|---|---|
| `enablepool N` | `S,47` | `E,8` no pools, `E,25` missing id, `E,26` invalid id, `I,49` already enabled |
| `disablepool N` | `S,48` | `E,8`, `E,25`, `E,26`, `I,50` already disabled, `E,51` last active |
| `switchpool N` | `S,27` | `E,8`, `E,25`, `E,26` |
| `poolpriority N,…` | `S,73` | `E,8`, `E,25`, `E,26`, `E,74` duplicate; `E,25`/`E,26`/`E,74` change nothing |
| `addpool` | `S,55` | `E,52` missing details, `E,53` already exists, invalid details, no free slot or save failed |
| `removepool N` | `S,68` | `E,8`, `E,25`, `E,26`, `E,67` active pool, `E,66` no other enabled pool, `E,53` save failed |
| `setpool` | `S,133` | `E,132` |

`enablepool`, `disablepool`, `switchpool` and `poolpriority` act at once (`E,53` when the save
fails); `addpool`, `removepool` and `setpool` take effect when `core_miner` restarts, or within 10 s
while it has no pool.

**Phone app.**
- Provisioning reads `DNA=` and `MODEL=` from a text-mode `version`; `MODEL` must be `Nano3s`.
- LAN discovery: UDP 10001 `get_ip$$$$` → JSON `{"device","ip"}`, answered only within the device's
  own subnets.
- BLE: service `0xffff`; `0xffe1` scan read, `0xffe2` write (`$$$$` framing), `0xffe3` status; no
  pairing, and bluetoothd runs no GATT client on the phone (`ReverseServiceDiscovery = false`): a
  read the phone refuses would make it ask for pairing and stall provisioning. The app addresses
  characteristics by position, so only GAP `0x2a00` may precede `0xffe1`
  (`test/scripts/conf_check.sh`).

## Web

Ports, authentication and routes: [MINER-WEB.md](../miner-web/MINER-WEB.md).

## Configuration

- `mm_miner` is the only runtime writer of `pools.conf`, `systemcfg.ini`, `interfaces` and the
  wallpaper directory under `/data/userconfig/`. A missing file is created from the factory
  defaults by the data mount, by `mm_miner` at start-up and, for `systemcfg.ini`, by `miner_web`
  ([CROSS-LAYER-RULES.md](../CROSS-LAYER-RULES.md#miner-factory-defaults)).
- Network: nano3s takes its address from DHCP only. `mm_miner` runs udhcpc on `wlan0` and, when a
  USB Ethernet adapter (or hub) is present, on `eth0` and/or `usb0`. When USB Ethernet has carrier
  and an IPv4 address, Wi-Fi is taken down (`wlan0` down, no udhcpc) so the radio is idle; pulling
  the cable brings Wi-Fi back. The LAN dashboard checkbox **Keep Wi-Fi on with Ethernet**
  (`[net] wifi_eth`, default `0`) keeps both up. LCD, UDP discovery, `:4028` and the LAN UI report
  a wired address when one exists. `interfaces` is seeded and restored with the other factory
  defaults; nothing in `mm_miner` reads it. The device name is `nan3s_` and the last four hex
  digits of the DNA.
- Time: chronyd or the phone app sets the clock; the timezone is `[syscfg] timezone`.
- Factory recovery (TP1 press of at least 5 s) restores `systemcfg.ini`, `pools.conf`,
  `interfaces` and, with BLE, `wpa_supplicant.conf` from the factory defaults, keeps the filter
  run time, deletes the wallpapers, sets `Etc/UTC` and reboots.

## Runtime behaviour

- **Thermal softoff:** above the chip PVT maximum (113 °C) `asic_miner` stops, the rail powers off
  and the fan is pinned for 180 s; `core_miner` keeps serving `:4028`.
- **Faults** stop `asic_miner`, power the rail off and stay latched until a softoff then softon, or
  a restart of `mm_miner`: bad PSU output, output power over 155 W, fan below 200 RPM under load,
  `asic_miner` failing to start, or aging finding no valid V/F point. estats `SoftOFF` reports the
  reason, `ECMM`/`ECHU` the error bits; an aging failure sets `ECMM` bit 13.
- **Network errors:** `ECMM` bit 1 is set while no network device exists, bit 4 while the board
  has no IP; neither stops mining.
- **Pool inactivity:** counted from the first job of the running `core_miner`. With an IP,
  `ECMM` flags a job gap over 120 s and the LCD shows the check-pool screen; after 360 s without a
  job the board reboots, with or without an IP. With an IP and an enabled pool, a `core_miner`
  that receives no first job within 120 s also shows the check-pool screen and keeps running.
- **Pool failover:** `core_miner` mines on the best-ranked enabled pool not marked Dead. A failed
  connect or handshake, or no usable job within 90 s of connecting, marks a pool Dead (`pools`
  `Status`) until a session on it delivers a usable job. With every pool Dead all are retried,
  1 s apart doubling to 60 s, and stay Dead meanwhile. A Dead pool ranked above the active one is
  probed every 30 s and switched back to once it has answered for 120 s.
- **No pool:** the chain hashes a default work at full work-mode V/F.
- **Work mode:** modes `0..2`; the running mode is the saved mode capped by the USB-PD power budget
  read at each rail power-on.
- **Buttons:** TP2 switches a dark LED back to day mode, else cycles screens, a TP2 double-click
  steps and saves the running work mode (ignored when the power budget allows only one mode), TP1
  starts provisioning, a TP1 press of at least 5 s runs factory recovery, holding the power key
  for about 3 s powers the board off.

**LCD faults:** no network dongle, power adapter failure and thermal hold each show a full-screen
image. E-04 (fan controller cannot be opened) shows on the error-code screen as the code alone;
the fault screen shows a code and its description: E-06 hashboard fault, E-07 fan stopped, E-08
aging failed, E-09 power fault.

## Hardware

- **a3197s PLL:** `a3197s_set_pll_seq()` programs the PLLs in an order pinned by
  `hal/tests/test_a3197s_pll_seq*.c` and its chip-select writes by
  `hal/tests/test_a3197s_pll_cs.c`; a wrong order or chip select can hang or damage the chips.
- **Power:** the DC-DC output readback is unreliable, so the rail voltage is the last commanded
  value; the INA226 measures the input bus.
- **GPIO:** the miner depends on the sysfs GPIO base equalling the pin number.
