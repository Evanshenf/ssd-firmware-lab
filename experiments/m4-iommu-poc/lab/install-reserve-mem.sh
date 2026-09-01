#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/../poc-safety.sh"
fwlab_require_profile_ack
source_cfg="$script_dir/99-ssd-fwlab-bar.cfg"
target_cfg=/etc/default/grub.d/99-ssd-fwlab-bar.cfg
installed=0

rollback_on_error()
{
	rc=$?
	trap - EXIT INT TERM
	if [ "$rc" -ne 0 ] && [ "$installed" -eq 1 ]; then
		rm -f "$target_cfg"
		update-grub >/dev/null 2>&1 || true
	fi
	exit "$rc"
}

trap rollback_on_error EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
	echo "must run as root" >&2
	exit 2
fi
if [ ! -f "$source_cfg" ]; then
	echo "source config missing: $source_cfg" >&2
	exit 2
fi
if [ -e "$target_cfg" ]; then
	if cmp -s "$source_cfg" "$target_cfg"; then
		echo "reservation config already installed"
	else
		echo "refusing to replace unrelated $target_cfg" >&2
		exit 1
	fi
else
	install -m 0644 "$source_cfg" "$target_cfg"
	installed=1
fi

update-grub
if ! grep -Fq 'memmap=16M$0x70000000' /boot/grub/grub.cfg; then
	echo "generated grub.cfg does not contain the reservation" >&2
	exit 1
fi

installed=0
trap - EXIT INT TERM
echo "reservation installed; reboot required"
