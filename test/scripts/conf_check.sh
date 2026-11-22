#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026, Canaan Creative Co., Ltd.
#
# For the licensing terms, see the LICENSE file (MIT License).
#
# NO WARRANTY. The Software is provided "AS IS", without warranty of any kind, express or
# implied, including but not limited to the warranties of merchantability, fitness for a
# particular purpose and non-infringement. In no event shall the authors or copyright
# holders be liable for any claim, damages or other liability arising from, out of or in
# connection with the Software or the use or other dealings in the Software.

# build.conf fields — every product build.conf declares non-empty MACHINE / DISTRO / IMAGE.
# Hardware-free. Checks ALL products (cheap guard) so a
# broken sibling build.conf is caught.
# The recipe checks read product/ and the meta-canaan layer.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SDK_ROOT="$(cd "$HERE/../.." && pwd)"
source "$HERE/lib/assert.sh"
tap_init "conf-check" "$SDK_ROOT/test/results/conf-check.xml"
META_CANAAN="$SDK_ROOT/build/yocto/meta-canaan"
if [[ -d "$META_CANAAN" ]]; then
    assert_pass "meta-canaan layer present"
else
    assert_fail "meta-canaan layer present" "missing: $META_CANAAN"
fi

while IFS= read -r conf; do
    name="$(basename "$(dirname "$conf")")"
    m="$(grep -E '^MACHINE=' "$conf" | head -1 | cut -d= -f2-)"
    d="$(grep -E '^DISTRO=' "$conf" | head -1 | cut -d= -f2-)"
    i="$(grep -E '^IMAGE=' "$conf" | head -1 | cut -d= -f2-)"
    if [[ -n "$m" && -n "$d" && -n "$i" ]]; then
        assert_pass "$name: MACHINE/DISTRO/IMAGE set ($m / $d / $i)"
    else
        assert_fail "$name: build.conf missing MACHINE/DISTRO/IMAGE" "M='$m' D='$d' I='$i'"
    fi
done < <(find "$SDK_ROOT/product" -name build.conf | sort)

tmpfs_tmp_hook_reason() { # $1=recipe; echoes nothing when wired, else the reason
    awk '
        /^[ \t]*#/ { next }
        /ROOTFS_POSTPROCESS_COMMAND:remove/ { next }
        /ROOTFS_POSTPROCESS_COMMAND/ { ppc = ppc " " $0 }
        /^[A-Za-z_][A-Za-z0-9_]*\(\)[ \t]*{/ { fn = $0; sub(/\(\).*/, "", fn); next }
        fn != "" && /^}/ { fn = ""; next }
        fn != "" && $1 == "tmpfs" && $2 == "/tmp" && $3 == "tmpfs" { seen[fn] = $0 }
        END {
            for (f in seen) {
                if (index(ppc, f) == 0) { unrun = unrun " " f; continue }
                if (seen[f] !~ /mode=1777/) { nomode = nomode " " f; continue }
                exit 0
            }
            if (unrun != "") {
                printf "fstab \"tmpfs /tmp\" line written by%s, which no " \
                    "ROOTFS_POSTPROCESS_COMMAND runs\n", unrun
                exit 0
            }
            if (nomode != "") {
                printf "fstab \"tmpfs /tmp\" line in%s carries no mode=1777\n", nomode
                exit 0
            }
            print "no ROOTFS_POSTPROCESS_COMMAND function appends a \"tmpfs /tmp tmpfs\" line" \
                " to /etc/fstab"
        }' "$1"
}

while IFS= read -r json; do
    pdir="$(cd "$(dirname "$json")/.." && pwd)"
    name="$(basename "$pdir")"
    img="$(grep -E '^IMAGE=' "$pdir/build.conf" 2>/dev/null | head -1 | cut -d= -f2-)"
    recipe="$(find "$pdir" "$META_CANAAN" -name "${img:-__none__}.bb" 2>/dev/null | head -1)"
    if [[ -z "$img" || -z "$recipe" ]]; then
        assert_fail "$name: OTA product resolves its rootfs image recipe" \
            "build.conf IMAGE='$img', no ${img:-?}.bb under $pdir or $META_CANAAN"
        continue
    fi
    why="$(tmpfs_tmp_hook_reason "$recipe")"
    if [[ -z "$why" ]]; then
        assert_pass "$name: $(basename "$recipe") mounts tmpfs on /tmp (OTA latch)"
    else
        detail="$why — without it the board accepts exactly one OTA, silently"
        detail+=" (docs/CROSS-LAYER-RULES.md)"
        assert_fail "$name: $(basename "$recipe") must mount tmpfs on /tmp (OTA latch)" "$detail"
    fi
done < <(find "$SDK_ROOT/product" -name aup-ota.json | sort)

initenv_hits=""
while IFS= read -r f; do
    hits="$(grep -nE 'UBOOT_INITIAL_ENV|u-boot-initial-env' "$f" 2>/dev/null || true)"
    [[ -n "$hits" ]] && initenv_hits+="$f: $hits"$'\n'
done < <(find "$SDK_ROOT/product" "$META_CANAAN" \
    \( -name '*.bb' -o -name '*.bbappend' -o -name '*.inc' -o -name '*.conf' \) 2>/dev/null | sort)
if [[ -z "$initenv_hits" ]]; then
    assert_pass "no recipe ships /etc/u-boot-initial-env (UBOOT_INITIAL_ENV unset)"
else
    detail="$initenv_hits — a defaults file lets the first OTA persist defaults+one key"
    detail+=" with no HW/SW (docs/CROSS-LAYER-RULES.md)"
    assert_fail "no recipe may ship /etc/u-boot-initial-env" "$detail"
fi

# check_env_seed.py is the only thing that verifies the seeded U-Boot env, which
# is the only source of HW / SW / quick_boot / autoload / the four *_partition
# keys (docs/CROSS-LAYER-RULES.md). Delete the call from a gen-spinand.sh and nothing
# else notices: the pack still succeeds and every image after it ships an
# unverified seed. This is a SOURCE-TEXT check — it reads the packager, it does
# not run it. What it can see: the call exists outside a comment, is top-level
# and unindented, starts with python3 (so no `if`/`$(...)` wrapper eats the
# status), carries --product and --env-image, no top-level `exit` precedes it,
# and the script sets -e so a non-zero status actually stops the pack. What it
# CANNOT see: control flow. A call left at column 0 inside an `if false; then`
# block, or one whose --env-image points at a file the pack never stages, passes
# this check. Only running the packager would catch those.
env_seed_call_reason() { # $1=packager $2=product; echoes nothing when wired, else the reason
    awk -v product="$2" '
        /^[ \t]*#/ { next }
        joining {
            cmd = cmd " " $0
            if ($0 !~ /\\[ \t]*$/) joining = 0
            next
        }
        /^[ \t]*set[ \t]+-[a-zA-Z]*e/ { set_e = 1 }
        /^[ \t]*[A-Za-z_][A-Za-z0-9_]*=[^ \t]*check_env_seed\.py/ {
            name = substr($0, 1, index($0, "=") - 1); gsub(/[ \t]/, "", name)
            var[name] = 1; next
        }
        cmd == "" && /^exit([ \t]|$)/ { dead_exit = FNR }
        cmd == "" {
            hit = ($0 ~ /check_env_seed\.py/)
            for (v in var) if (index($0, "$" v) || index($0, "${" v "}")) hit = 1
            if (!hit || $0 !~ /python3/) next # skips e.g. check_file "$CHECK_ENV_SEED"
            call = FNR; indented = ($0 ~ /^[ \t]/); first = $1; cmd = $0
            if ($0 ~ /\\[ \t]*$/) joining = 1
        }
        END {
            if (cmd == "") {
                print "no line runs check_env_seed.py (looked for a python3 invocation," \
                    " directly or through a variable holding its path)"
                exit
            }
            if (!set_e) {
                printf "the packager does not set -e, so the check at line %d would print" \
                    " and the pack would carry on\n", call
                exit
            }
            if (indented) {
                printf "the call at line %d is indented, so it sits inside a function or" \
                    " a conditional and is not run unconditionally\n", call
                exit
            }
            if (first != "python3") {
                printf "the call at line %d starts with %s, not python3, so something" \
                    " consumes its exit status instead of propagating it\n", call, first
                exit
            }
            if (index(cmd, "|") || index(cmd, "&")) {
                printf "the call at line %d chains or pipes its exit status away\n", call
                exit
            }
            if (cmd !~ ("--product[ \t]+" product "([ \t]|$)")) {
                printf "the call at line %d does not pass --product %s, so it checks" \
                    " some other product\n", call, product
                exit
            }
            if (cmd !~ /--env-image[ \t]/) {
                printf "the call at line %d passes no --env-image\n", call
                exit
            }
            if (dead_exit && dead_exit < call) {
                printf "a top-level exit at line %d precedes the call at line %d," \
                    " which is therefore unreachable\n", dead_exit, call
                exit
            }
        }' "$1"
}

while IFS= read -r json; do
    pdir="$(cd "$(dirname "$json")/.." && pwd)"
    name="$(basename "$pdir")"
    pack="$(grep -E '^PACK_SCRIPT=' "$pdir/build.conf" 2>/dev/null | head -1 | cut -d= -f2-)"
    if [[ -z "$pack" || ! -f "$pdir/$pack" ]]; then
        assert_fail "$name: OTA product resolves its packager" \
            "build.conf PACK_SCRIPT='$pack', no such script under $pdir"
        continue
    fi
    why="$(env_seed_call_reason "$pdir/$pack" "$name")"
    if [[ -z "$why" ]]; then
        assert_pass "$name: $pack runs check_env_seed.py and propagates its status"
    else
        detail="$why — unverified, a bad seed ships and the board refuses every OTA with"
        detail+=" UPGRADE_SW_ERR, pinned to slot A until reflashed (docs/CROSS-LAYER-RULES.md)"
        assert_fail "$name: $pack must run check_env_seed.py on the env seed it stages" "$detail"
    fi
done < <(find "$SDK_ROOT/product" -name aup-ota.json | sort)

ble_gatt_table_reason() { # $1=product dir; echoes nothing when wired, else the reason
    local pdir="$1" app code files rel f u
    app="$(find "$pdir" -path '*recipes-connectivity/bluez5/*.bbappend' | sort | head -1)"
    if [[ -z "$app" ]]; then
        echo "ships no bluez5 bbappend, so bluetoothd registers its stock local GATT table"
        return
    fi
    code="$(grep -v '^[[:space:]]*#' "$app")"
    if ! grep -qE 'DeviceID[[:space:]]*=[[:space:]]*false' <<<"$code"; then
        echo "$(basename "$app") does not write \"DeviceID = false\" into main.conf," \
            "so DIS 0x180a/PnP 0x2a50 is registered"
        return
    fi
    if ! grep -qE '\[GATT\]' <<<"$code" ||
        ! grep -qE 'Channels[[:space:]]*=[[:space:]]*1' <<<"$code"; then
        echo "$(basename "$app") does not write \"[GATT] Channels = 1\" into main.conf," \
            "so EATT is enabled and 0x2b3a Server Supported Features is registered"
        return
    fi
    rel="$(grep -oE 'file://[A-Za-z0-9._+-]*gatt-database[A-Za-z0-9._+-]*\.patch' <<<"$code" |
        head -1)"
    if [[ -z "$rel" ]]; then
        echo "$(basename "$app") adds no gatt-database patch to SRC_URI"
        return
    fi
    rel="${rel#file://}"
    files="$(dirname "$app")"
    f="$(find "$files" -name "$rel" | sort | head -1)"
    if [[ -z "$f" ]]; then
        echo "SRC_URI names $rel but no such file exists under $files"
        return
    fi
    for u in GATT_CHARAC_APPEARANCE GATT_CHARAC_CAR GATT_CHARAC_CLI_FEAT GATT_CHARAC_DB_HASH; do
        if ! grep -qE "^-.*${u}\b" "$f"; then
            echo "$rel does not remove the ${u} characteristic registration"
            return
        fi
    done
}

while IFS= read -r desc; do
    pdir="$(cd "$(dirname "$desc")/../.." && pwd)"
    name="$(basename "$pdir")"
    why="$(ble_gatt_table_reason "$pdir")"
    if [[ -z "$why" ]]; then
        title="$name: bluetoothd publishes one readable chrc ahead of 0xffe1 (BLE provisioning)"
        assert_pass "$title"
    else
        title="$name: bluetoothd must publish only GAP 0x2a00 ahead of the 0xffff provisioning"
        title+=" service"
        detail="$why — the phone app addresses the scan list by INDEX into the"
        detail+=" readable-characteristic list, so an extra one makes it read the wrong"
        detail+=" characteristic and show an empty Wi-Fi list, silently"
        assert_fail "$title" "$detail"
    fi
done < <(grep -rlE '^[[:space:]]*SUPPORT_BLE[[:space:]]*=[[:space:]]*1([^0-9]|$)' \
    "$SDK_ROOT/product" --include='boardconf.ini' | sort)

ble_le_only_reason() { # $1=product dir; echoes nothing when LE-only, else the reason
    local pdir="$1" app code removed p
    app="$(find "$pdir" -path '*recipes-connectivity/bluez5/*.bbappend' | sort | head -1)"
    if [[ -z "$app" ]]; then
        echo "ships no bluez5 bbappend, so bluetoothd runs dual-mode with every stock profile"
        return
    fi
    code="$(grep -v '^[[:space:]]*#' "$app")"
    if ! grep -qE 'ControllerMode[[:space:]]*=[[:space:]]*le' <<<"$code"; then
        echo "$(basename "$app") does not write \"ControllerMode = le\" into main.conf," \
            "so BR/EDR stays on"
        return
    fi
    removed="$(sed -n '/^PACKAGECONFIG:remove/,/"$/p' <<<"$code")"
    for p in a2dp-profiles avrcp-profiles network-profiles hid-profiles; do
        if ! grep -qE "^[[:space:]]*${p}\b" <<<"$removed"; then
            echo "$(basename "$app") does not remove ${p} from PACKAGECONFIG"
            return
        fi
    done
}

while IFS= read -r desc; do
    pdir="$(cd "$(dirname "$desc")/../.." && pwd)"
    name="$(basename "$pdir")"
    why="$(ble_le_only_reason "$pdir")"
    if [[ -z "$why" ]]; then
        assert_pass "$name: bluetoothd is LE-only without BR/EDR profiles (BLE provisioning)"
    else
        assert_fail "$name: bluetoothd must run LE-only without BR/EDR profiles" \
            "$why — BR/EDR would be connectable and its profiles reachable by any nearby device"
    fi
done < <(grep -rlE '^[[:space:]]*SUPPORT_BLE[[:space:]]*=[[:space:]]*1([^0-9]|$)' \
    "$SDK_ROOT/product" --include='boardconf.ini' | sort)

ble_no_gatt_client_reason() { # $1=product dir; echoes nothing when off, else the reason
    local pdir="$1" app
    app="$(find "$pdir" -path '*recipes-connectivity/bluez5/*.bbappend' | sort | head -1)"
    if [[ -z "$app" ]]; then
        echo "ships no bluez5 bbappend, so bluetoothd keeps its default main.conf"
        return
    fi
    if ! grep -v '^[[:space:]]*#' "$app" |
            grep -qE 'ReverseServiceDiscovery[[:space:]]*=[[:space:]]*false'; then
        echo "$(basename "$app") does not write \"ReverseServiceDiscovery = false\" into main.conf"
    fi
}

while IFS= read -r desc; do
    pdir="$(cd "$(dirname "$desc")/../.." && pwd)"
    name="$(basename "$pdir")"
    why="$(ble_no_gatt_client_reason "$pdir")"
    if [[ -z "$why" ]]; then
        assert_pass "$name: bluetoothd runs no GATT client on the phone (BLE provisioning)"
    else
        detail="$why — the phone refuses a read with Insufficient Authentication, bluetoothd"
        detail+=" then asks for pairing, the adapter refuses it and the provisioning reads stall"
        detail+=" until the link times out"
        assert_fail "$name: bluetoothd must not run a GATT client on the phone" "$detail"
    fi
done < <(grep -rlE '^[[:space:]]*SUPPORT_BLE[[:space:]]*=[[:space:]]*1([^0-9]|$)' \
    "$SDK_ROOT/product" --include='boardconf.ini' | sort)

while IFS= read -r json; do
    name="$(basename "$(cd "$(dirname "$json")/.." && pwd)")"
    data="$(python3 -c '
import json, re, sys
for f in json.load(open(sys.argv[1]))["files"]:
    if f.get("name") == "data" or re.search(r"(^|/)data(\.ubi(fs)?)?(/|$)", f.get("path", "")):
        print(f.get("name"), f.get("path"))
' "$json" 2>&1)"
    if [[ -z "$data" ]]; then
        assert_pass "$name: $(basename "$json") carries no data volume"
    else
        assert_fail "$name: $(basename "$json") must not carry the data volume" \
            "$data — an OTA would overwrite the unit's configuration and keys"
    fi
done < <(find "$SDK_ROOT/product" -name aup-ota.json | sort)

nano3s_usbnet_reason() {
    local cfg="$SDK_ROOT/product/k230/miner/avalon-home/nano3s/recipes-kernel/linux/files/canaan-nano3s.cfg"
    local k
    if [[ ! -f "$cfg" ]]; then
        echo "missing $cfg"
        return
    fi
    for k in CONFIG_USB_USBNET CONFIG_MII CONFIG_PHYLIB CONFIG_USB_NET_CDCETHER \
            CONFIG_USB_NET_CDC_NCM CONFIG_USB_NET_CDC_EEM CONFIG_USB_NET_RNDIS_HOST \
            CONFIG_USB_NET_AX8817X CONFIG_USB_NET_AX88179_178A CONFIG_USB_RTL8152 \
            CONFIG_USB_RTL8150 CONFIG_USB_NET_SMSC95XX CONFIG_USB_NET_SMSC75XX \
            CONFIG_USB_LAN78XX CONFIG_USB_NET_DM9601 CONFIG_USB_NET_PLUSB \
            CONFIG_USB_DWC2; do
        if ! grep -qE "^${k}=y" "$cfg"; then
            echo "$k is not =y in canaan-nano3s.cfg"
            return
        fi
    done
}

why="$(nano3s_usbnet_reason)"
if [[ -z "$why" ]]; then
    assert_pass "nano3s kernel cfg enables USB Ethernet class and chipset drivers"
else
    assert_fail "nano3s kernel cfg must enable USB Ethernet (CDC/RNDIS/ASIX/RTL/SMSC/LAN78xx)" \
        "$why — a USB-to-Cat5 adapter on the host port would not enumerate"
fi

tap_done
