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

SUMMARY = "Avalon miner standalone web app (civetweb)"
DESCRIPTION = "Device web UI and HTTP API served by civetweb as a separate process."
HOMEPAGE = "https://www.canaan.io"

LICENSE = "MIT"
LIC_FILES_CHKSUM = " \
    file://third_party/js-sha256/sha256.min.js;beginline=1;endline=8;md5=7037aa0210f62e4797eaa6286017db6f \
    file://${CANAAN_SDK_ROOT}/NOTICE;beginline=120;endline=125;md5=cce0f937c00c031581a3ba8aff31cc9b \
    file://${CANAAN_SDK_ROOT}/LICENSE;beginline=1;endline=18;md5=255f95baaf5ed8b6a4a1b241f082b682 \
"

# openssl creates the device HTTPS certificate.
DEPENDS = "civetweb openssl"

inherit externalsrc cmake
EXTERNALSRC = "${CANAAN_SDK_ROOT}/apps/miner-web"
EXTERNALSRC_BUILD = "${WORKDIR}/build"

MINER_SRC = "${CANAAN_SDK_ROOT}/apps/miner"
MINER_WEB_SHARED_SRC = " \
    ${MINER_SRC}/common/cfgwr:True \
    ${MINER_SRC}/common/ini:True \
    ${MINER_SRC}/common/json:True \
    ${MINER_SRC}/common/pool:True \
    ${MINER_SRC}/common/proto:True \
    ${MINER_SRC}/common/sha:True \
    ${MINER_SRC}/common/util:True \
    ${MINER_SRC}/miner_mgmt/api:True \
    ${MINER_SRC}/miner_mgmt/ota:True \
    ${MINER_SRC}/osif/*.h:True \
"
python () {
    d.appendVarFlag('do_compile', 'file-checksums', ' ${MINER_WEB_SHARED_SRC}')
}

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${B}/miner_web ${D}${sbindir}/miner_web

    # civetweb document_root; on the rootfs because the app volume mounts over /mntapp/release.
    # assets/ includes the LAN UI pages and brand logos (pelish-logo.png, canaan-logo.png).
    install -d ${D}${datadir}/miner-web/www
    cp -r ${S}/assets/* ${D}${datadir}/miner-web/www/
    cp ${S}/third_party/js-sha256/sha256.min.js ${S}/third_party/qrcodejs/qrcode.min.js \
        ${D}${datadir}/miner-web/www/
    find ${D}${datadir}/miner-web/www -type f -exec chmod 0644 {} +

    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${S}/services/miner-web.init ${D}${sysconfdir}/init.d/miner-web
}

FILES:${PN} += "${datadir}/miner-web"

inherit update-rc.d
INITSCRIPT_NAME = "miner-web"
INITSCRIPT_PARAMS = "defaults 91 09"
