# Canaan SDK (canaan_sdk_for_linux)

## What this firmware is

This repository is the Pelish fork of the Canaan Avalon Nano 3s Linux SDK.

- Embedded firmware for the Nano 3s K230 control board. The first write is `kburn` of this tree's `.kdimg` to SPI-NAND. Factory firmware cannot accept this image over OTA.
- One dongle USB port. A USB-to-RJ45 adapter uses that same hole after the Wi-Fi stick comes out.
- LAN UI in the Avalon Overview style, with Pelish and Canaan logos and light and dark modes.
- Optional and off by default: LCD Pelish rotation, keep Wi-Fi on with Ethernet, mild autotune, and a 150 W aftermarket PSU cap. The stock software cap stays 140 W.
- Canaan Nano 3s SDK fork. Nano 3s only, not S19. The first write is factory kburn. Do not flash other custom images. This image does not install on Antminer S19, JPro, KPro, or AM Logic.

Canaan SDK (canaan_sdk_for_linux) is a Yocto/OpenEmbedded SDK that builds the Linux firmware of the
Avalon Miners on the Canaan K230 RISC-V SoC. It holds the Canaan Yocto layers with the K230 patches,
U-Boot, OpenSBI and Linux as Git submodules, the miner applications and the host-side tests.
The board boots from SPI-NAND with A/B slots: ROM → SPL → U-Boot 2025.07 → OpenSBI 1.7 →
Linux 6.6.36 → root file system → miner applications.

Supported products: Avalon Nano3s (`./build.sh nano3s`).

## Build

```sh
git clone https://github.com/Canaan-Creative/canaan_sdk_for_linux.git
cd canaan_sdk_for_linux
./build.sh setup             # once
./build.sh nano3s
```

The build writes the flash image `build/images/<name>.kdimg.gz` and the OTA package
`build/images/<name>.aup`; `<name>` is described in
[docs/BUILD.md](docs/BUILD.md#output-images). Build options:
[docs/BUILD.md](docs/BUILD.md).

The default build is a debug image with root login `root`/`root` and sshd.
The committed OTA signing key is public; production firmware needs a key pair of its own
([docs/miner/KEYS.md](docs/miner/KEYS.md)).

## Flash

Run `gunzip -k build/images/<name>.kdimg.gz`, hold the BOOT button while powering the board, and
write `build/images/<name>.kdimg` to SPI-NAND with `kburn -m nand` or the K230 burning tool,
as [Avalon_Nano3s](https://github.com/Canaan-Creative/Avalon_Nano3s#31-flash-image) describes.
Flashing replaces the factory firmware; a unit that does not boot is recovered only by reflashing.

## Upgrade

Upload `build/images/<name>.aup` on the Upgrade page of `https://<miner_ip>/` (login
`admin`/`admin`), or run `apps/miner/tools/mmupgrade build/images/<name>.aup <miner_ip> [password]`
(the password preferably in `$MMUPGRADE_PASS`), which takes a token from
`https://<miner_ip>:8443/api/v1/token` and uploads to `https://<miner_ip>:8443/api/v1/upgrade`
([docs/miner-web/MINER-WEB.md](docs/miner-web/MINER-WEB.md#mmupgrade)). The miner writes the
inactive A/B slot and reboots into it.

The `:4028` API accepts connections without authentication and only reads. Settings change on the
web pages or through the HTTPS REST API on ports 443 and 8443, which needs the web login or a token
([docs/miner-web/MINER-WEB.md](docs/miner-web/MINER-WEB.md#rest)). Keep miners on a trusted network.

## Documentation

| Document | Contents |
|---|---|
| [docs/BUILD.md](docs/BUILD.md) | build flow, Docker, output images |
| [docs/BOOT.md](docs/BOOT.md) | boot sequence, SPI-NAND A/B layout, slot selection, image header check |
| [docs/CROSS-LAYER-RULES.md](docs/CROSS-LAYER-RULES.md) | A/B layout, OTA, U-Boot environment, miner API, factory defaults, watchdog |
| [docs/TESTING.md](docs/TESTING.md) | host tests, their requirements and results |
| [docs/miner/KEYS.md](docs/miner/KEYS.md) | OTA and API signing keys |
| [docs/miner/MINER.md](docs/miner/MINER.md) | miner architecture and external interfaces |
| [docs/miner-web/MINER-WEB.md](docs/miner-web/MINER-WEB.md) | web server: ports, authentication, routes, REST API |
| [CHANGELOG.md](CHANGELOG.md) | changes per version |
| [CONTRIBUTING.md](CONTRIBUTING.md) | how to contribute |
| [docs/HOWTO-NANO3S.md](docs/HOWTO-NANO3S.md) | Nano3s how-to: activation, pools, reboot, mining state and hash rate over the API |

## Support

- Questions, bug reports and other inquiries: the repository's issue tracker, or e-mail
  linux-sdk@canaan-creative.com.
- Official website: https://www.canaan.io

## License

Unless a file states otherwise, the Canaan-authored software in this repository is licensed under
the [MIT License](LICENSE).

- Third-party components and their licenses are listed in [NOTICE](NOTICE); their license texts are
  in [third-party-license/](third-party-license/).
- Copyright licenses do not grant trademark rights; see [TRADEMARKS.md](TRADEMARKS.md).
- Whoever redistributes a built image must provide the corresponding source of its GPL and LGPL
  components, or a written offer for it, and keep the license texts in
  `/usr/share/common-licenses/`.
