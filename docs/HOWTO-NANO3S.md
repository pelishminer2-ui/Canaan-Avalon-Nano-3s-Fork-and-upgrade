# Nano3s how-to

**Embedded mode.** This fork is on-board firmware for the Avalon Nano 3S control board
(Canaan K230) only. Yocto Linux and the miner apps are packed into one `.kdimg` and written
to that board's SPI-NAND with `kburn`. After boot, a USB-RJ45 adapter in the same dongle USB
hole is the board's USB host. This is not a PC sidecar and not an SSH-only `mm_miner` swap.

**EMBEDDED MODE:** write this image onto the Nano 3S control board (BOOT + kburn SPI NAND).
Same dongle USB hole. Not Wi-Fi OTA from factory. Not a Windows service.

This `.kdimg` does **not** install on Antminer S19, JPro, KPro, or AM Logic. This tree is a
Canaan Nano 3s SDK fork. Nano 3s only, not S19. The first write is factory kburn. Do not flash
other custom images.

Set up the Nano3s Wi-Fi, set its pools, reboot it and check that it mines. API details:
[MINER.md](miner/MINER.md#4028-api) and [MINER-WEB.md](miner-web/MINER-WEB.md#rest).

## Factory-first install (stock unit)

A factory Avalon Nano 3s will **reject** this SDK's `.aup` over OTA: the factory image uses a
different OTA key, `SW`/`HW`, and flash layout. Do not try OTA until the board is already running
this tree.

1. Build `./build.sh nano3s` and wait for `build/images/*.kdimg.gz` plus the matching `.aup`.
2. `gunzip -k` the `.kdimg.gz`. Keep a copy of that `.kdimg` as the kburn recovery image.
3. Hold **BOOT** (recessed button) while applying power so the ROM enters Canaan burn mode.
4. Flash **only this SDK's nano3s `.kdimg`** to **SPI-NAND** with Canaan `kburn -m nand` or the
   K230 Burning Tool (`Medium` = SPI NAND, image part `all`). This file does not install on
   Antminer S19. Nano 3s only, not S19. Do not flash other custom images.
5. Do not pull power until the tool reports success. A cut flash is recovered only by factory
   kburn of this SDK's `.kdimg` again, not by OTA and not by another custom image.
6. After the first successful kburn, later pelish builds are **OTA only**: upload the new `.aup`
   on the LAN Upgrade page (or `mmupgrade`). The miner writes the inactive A/B slot and reboots
   into it. Keep the last known-good `.kdimg` for kburn if an OTA slot will not boot.

First-boot network: provision Wi-Fi with the Avalon app over BLE (`TP1`), or plug a USB-to-Cat5
adapter (hub + Wi-Fi dongle is fine). With Ethernet link and an IPv4 address, Wi-Fi turns off
unless **Keep Wi-Fi on with Ethernet** is checked. The Avalon app stays on this firmware.

This tree is a **Canaan Nano 3s SDK fork**. The first write is factory `kburn` of this SDK's
nano3s `.kdimg`. Do not flash other custom images. Nano 3s only, not S19. S19 JPro, KPro, and
AM Logic are not this tree. Keep this SDK's `.kdimg` as the recovery image. Another custom image
is not recovery firmware and must not be flashed over factory or this SDK.

## Current limitation

Phone app support is in progress. Wi-Fi setup over BLE works.

- the app cannot manage the miner.

Use the API below to configure the miner and read its state, or read the state on the screen.

## API

| use | how | credential |
|---|---|---|
| read | TCP `:4028`, JSON `{"command":"<name>","parameter":"<value>"}` | none |
| write | HTTPS `https://<ip>:8443/api/v1/<resource>`, `POST` JSON | `Token` header |

`:4028` only reads; a write sent there answers `Access denied`. The certificate is self-signed,
hence `curl -k`. The reads below use this helper; the reply ends with a NUL byte, which `tr` drops:

```sh
IP=192.168.1.100   # replace with your miner's IP (shown on the home screen)
api() { echo -n "{\"command\":\"$1\"}" | nc -w 2 "$IP" 4028 | tr -d '\0'; }
```

## 1. Set up Wi-Fi

Press the pinhole button (`TP1` on the board) with a pin to start provisioning, then set the
Wi-Fi network in the Avalon app over BLE. A USB Wi-Fi dongle, a USB-to-Cat5 Ethernet adapter,
or a hub with both, can share the USB host port. The miner takes addresses by DHCP on every
interface that is up. When USB Ethernet has a link and an IPv4 address, Wi-Fi is turned off so
the radio stays idle; unplug the cable and Wi-Fi comes back. The LAN dashboard checkbox **Keep
Wi-Fi on with Ethernet** (off by default) keeps both up. The home screen shows the preferred
IP, and the round button next to the display (`TP2`) cycles the screens. The Avalon app and
LAN UI stay reachable on the Ethernet IP while Wi-Fi is down.

## 2. Activate

Open `https://<ip>/`; the page shows a QR code. Update the Avalon app to the latest version and
scan the code. Skip this step until the app supports this firmware.

## 3. Get a token

```sh
TOKEN=$(curl -sk -X POST https://$IP:8443/api/v1/token \
  -H 'Content-Type: application/json' \
  -d '{"command":"token","user":"admin","password":"admin"}' | jq -r '.Token[0].Token')
```

The factory login is `admin` / `admin`. A token works on a device that is not activated, and
holds for 24 h.

## 4. Set the pools

Three slots, `0` to `2`; the parameter is `<slot>,<url>,<worker>,<password>`:

```sh
curl -sk -X POST https://$IP:8443/api/v1/pools -H "Token: $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"command":"setpool","parameter":"0,stratum+tcp://pool.example.com:3333,myworker.001,x"}'
```

Success is `STATUS` `S`, code 133. The new pools take effect after a reboot (within 10 s when
the miner had no pool).

To set the pools of several miners at once, use
[`apps/miner/tools/mmpool`](miner-web/MINER-WEB.md#mmpool).

## 5. Reboot

```sh
curl -sk -X POST https://$IP:8443/api/v1/system/reboot -H "Token: $TOKEN" \
  -H 'Content-Type: application/json' -d '{"command":"reboot","parameter":"0"}'
```

The parameter is the delay in seconds, `0` to `300`.

## 6. Check mining

```sh
api pools | jq '.POOLS[] | {POOL, URL, User, Status, "Stratum Active", Accepted, Rejected}'
```

Mining is normal when the active pool shows `Status` `Alive`, `Stratum Active` `true` and a
growing `Accepted`. `Dead` means the pool did not answer; check the URL, worker and network.

## 7. Hash rate

```sh
api summary | jq '.SUMMARY[0] | {Elapsed, "MHS av", "MHS 5s", "MHS 1m", Accepted, Rejected}'
api estats | jq '.STATS[0].MMID0Summary.Hashinfo | {GHSspd, GHSmm, GHSavg}'
```

| field | command | meaning |
|---|---|---|
| `MHS av` | `summary` | average since start, MH/s |
| `MHS 5s`, `MHS 1m` | `summary` | last 5 s / 1 min, MH/s |
| `GHSspd` | `estats` | real-time rate, GH/s |
| `GHSmm` | `estats` | nominal rate, GH/s |
| `GHSavg` | `estats` | average since start, from the work done, GH/s |

The screen's home page also shows hash rate, power and IP.

## Errors

| reply | cause | fix |
|---|---|---|
| `E`, code 45 `Access denied` | write sent to `:4028` | use HTTPS `:8443` |
| `401`, code 602 | token missing or expired | get a new token |
| `401`, code 601 | wrong user or password | check the login |
| `429`, code 506 | five failures within 60 s | wait 60 s |
| login `403` | device not activated | use the API |
