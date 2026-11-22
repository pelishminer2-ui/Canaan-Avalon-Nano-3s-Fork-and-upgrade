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

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:k230-nano3s = " \
    file://udhcpc-eth0.script \
    file://udhcpc-metric.script \
"

do_install:append:k230-nano3s() {
    install -m 0755 ${WORKDIR}/udhcpc-metric.script ${D}${sysconfdir}/network/udhcpc-metric.script
    install -m 0755 ${WORKDIR}/udhcpc-eth0.script ${D}${sysconfdir}/network/udhcpc-eth0.script
    if [ -f ${D}${sysconfdir}/network/udhcpc-eth0.script ]; then
        sed -i 's|/usr/share/udhcpc/default.script|/etc/network/udhcpc-metric.script|' \
            ${D}${sysconfdir}/network/udhcpc-eth0.script
    fi
}
