#!/bin/sh
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

# wpa_cli action script: starts or renews the DHCP client when the interface
# associates. Invoked by wpa_cli as: wpa_action.sh <ifname> <event>.

IFNAME=$1
CMD=$2
# The pidfile and flags match the udhcpc that mm_miner starts, so this hook and
# the miner's Wi-Fi repair path drive one client and never start a second one.
PIDF=/var/run/udhcpc.$IFNAME.pid
if [ "$CMD" = "CONNECTED" ]; then
    echo "WiFi connected ($IFNAME), requesting an IP address" > /dev/console
    # Renew a live client, otherwise start one: after a replug the old udhcpc is
    # gone and a renew signal alone would leave the interface without a lease.
    if [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; then
        kill -USR1 "$(cat "$PIDF")"
    else
        udhcpc -i "$IFNAME" -b -T 1 -A 2 -s /etc/network/udhcpc-metric.script -p "$PIDF"
    fi
fi
if [ "$CMD" = "DISCONNECTED" ]; then
    echo "WiFi disconnected ($IFNAME)" > /dev/console
fi
