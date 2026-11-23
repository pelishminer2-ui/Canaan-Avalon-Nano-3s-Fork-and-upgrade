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

# do_install chowns messagebus:messagebus and root:messagebus. pseudo resolves
# those names only from the recipe sysroot (PSEUDO_PASSWD, no host fallback).
# base-passwd's sysroot postinst copies passwd.master from the dependent
# sysroot's datadir, and staging does not install those masters there, so the
# copy fails and useradd_sysroot exits without creating messagebus.
# Seed the same base files and useradd the recipe already declares, before
# do_install, outside fakeroot so the passwd file is real.

do_seed_messagebus() {
	sysroot="${STAGING_DIR_TARGET}"
	install -d "${sysroot}${sysconfdir}"
	if [ ! -s "${sysroot}${sysconfdir}/passwd" ]; then
		master=""
		for d in ${COMPONENTS_DIR}/*/base-passwd${datadir}/base-passwd; do
			if [ -f "$d/passwd.master" ] && [ -f "$d/group.master" ]; then
				master="$d"
				break
			fi
		done
		if [ -z "$master" ]; then
			echo "ERROR: base-passwd masters not found under ${COMPONENTS_DIR}" >&2
			exit 1
		fi
		install -m 644 "$master/passwd.master" "${sysroot}${sysconfdir}/passwd"
		install -m 644 "$master/group.master" "${sysroot}${sysconfdir}/group"
	fi
	if ! grep -q '^messagebus:' "${sysroot}${sysconfdir}/passwd"; then
		# --prefix writes the sysroot passwd without chroot. --root needs
		# CAP_SYS_CHROOT, which the container build user does not have.
		useradd --prefix "${sysroot}" --system \
			--home ${localstatedir}/lib/dbus \
			--no-create-home --shell /bin/false \
			--user-group messagebus
	fi
}

addtask do_seed_messagebus after do_prepare_recipe_sysroot before do_install
