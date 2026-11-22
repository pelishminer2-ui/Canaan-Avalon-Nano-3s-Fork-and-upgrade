# Nano3s how-to

Set up the Nano3s Wi-Fi, set its pools, reboot it and check that it mines. API details:
[MINER.md](miner/MINER.md#4028-api) and [MINER-WEB.md](miner-web/MINER-WEB.md#rest).

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
interface that is up; Ethernet is preferred when both are connected. The home screen shows the
preferred IP, and the round button next to the display (`TP2`) cycles the screens.

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
