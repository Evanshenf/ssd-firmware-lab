#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_cfg="$script_dir/99-ssd-fwlab-bar.cfg"
target_cfg=/etc/default/grub.d/99-ssd-fwlab-bar.cfg

if [ "$(id -u)" -ne 0 ]; then
	echo "must run as root" >&2
	exit 2
fi
if [ ! -e "$target_cfg" ]; then
	echo "reservation config is already absent"
	exit 0
fi
if ! cmp -s "$source_cfg" "$target_cfg"; then
	echo "refusing to remove modified or unrelated $target_cfg" >&2
	exit 1
fi

rm "$target_cfg"
update-grub
echo "reservation removed from boot configuration; reboot required"
