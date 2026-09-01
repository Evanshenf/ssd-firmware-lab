#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/poc-safety.sh"
fwlab_require_profile_ack
kernel_module_dir=${FWLAB_M4_KERNEL_MODULE_DIR:-$script_dir/../../kernel/m4-synthetic-pci-poc}
module_path=${1:-$kernel_module_dir/ssd_fwlab_bar_aperture_probe.ko}
module_name=ssd_fwlab_bar_aperture_probe
loaded=0

cleanup()
{
	rc=$?
	trap - EXIT INT TERM
	if [ "$loaded" -eq 1 ]; then
		rmmod "$module_name" || rc=1
	fi
	exit "$rc"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
	echo "must run as root" >&2
	exit 2
fi
if [ ! -f "$module_path" ]; then
	echo "module not found: $module_path" >&2
	exit 2
fi
if ! grep -Fq 'memmap=16M$0x70000000' /proc/cmdline; then
	echo "fixed BAR reservation is absent from the running kernel" >&2
	exit 1
fi

dmesg_marker=$(dmesg | wc -l)
insmod "$module_path"
loaded=1
rmmod "$module_name"
loaded=0

new_log=$(dmesg | tail -n "+$((dmesg_marker + 1))")
printf '%s\n' "$new_log"
printf '%s\n' "$new_log" | grep -Fq \
	'ssd_fwlab_bar_aperture_probe: PASS'
if printf '%s\n' "$new_log" | grep -Eiq \
	'BUG:|WARNING:|Oops:|KASAN:|lockdep|general protection fault'; then
	echo "fatal kernel diagnostic detected" >&2
	exit 1
fi

echo "M4 fixed BAR aperture claim/map round trip: PASS"
