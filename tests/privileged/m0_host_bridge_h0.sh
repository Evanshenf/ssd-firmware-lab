#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

set -eu

module_path=${1:-kernel/host-pci-h0/ssd_fwlab_host_h0.ko}
module_name=ssd_fwlab_host_h0
vendor=0xfffa
device=0x0001
class=0xff0000
loaded=0
root_device=/sys/devices/ssd_fwlab_host_h0
bdf=

cleanup()
{
	rc=$?
	trap - EXIT INT TERM
	if [ "$loaded" -eq 1 ]; then
		if ! rmmod "$module_name"; then
			echo "failed to unload $module_name" >&2
			rc=1
		fi
	fi
	if grep -q "^${module_name} " /proc/modules; then
		echo "module remains loaded after cleanup" >&2
		rc=1
	fi
	if [ -e "$root_device" ]; then
		echo "root device remains after cleanup" >&2
		rc=1
	fi
	if [ -n "$bdf" ] && [ -e "/sys/bus/pci/devices/$bdf" ]; then
		echo "PCI function remains after cleanup: $bdf" >&2
		rc=1
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
if grep -q "^${module_name} " /proc/modules; then
	echo "module is already loaded" >&2
	exit 2
fi
if [ -e "$root_device" ]; then
	echo "stale H0 root device exists before load" >&2
	exit 1
fi

dmesg_marker=$(dmesg | wc -l)
taint_before=$(cat /proc/sys/kernel/tainted)
insmod "$module_path"
loaded=1
test -d "$root_device"

for candidate in /sys/bus/pci/devices/*; do
	[ -f "$candidate/vendor" ] || continue
	[ "$(cat "$candidate/vendor")" = "$vendor" ] || continue
	[ "$(cat "$candidate/device")" = "$device" ] || continue
	if [ -n "$bdf" ]; then
		echo "more than one H0 function found" >&2
		exit 1
	fi
	bdf=${candidate##*/}
done

if [ -z "$bdf" ]; then
	echo "H0 function was not enumerated" >&2
	exit 1
fi

test "$(cat "/sys/bus/pci/devices/$bdf/class")" = "$class"
test ! -e "/sys/bus/pci/devices/$bdf/driver"
test "$(cat "/sys/bus/pci/devices/$bdf/driver_override")" = "none"

if awk '($1 != "0x0000000000000000" || $2 != "0x0000000000000000") { exit 1 }' \
	"/sys/bus/pci/devices/$bdf/resource"; then
	:
else
	echo "H0 unexpectedly exposes a PCI resource" >&2
	exit 1
fi

lspci -s "$bdf" -nnvv
rmmod "$module_name"
loaded=0
test ! -e "/sys/bus/pci/devices/$bdf"
test ! -e "$root_device"

taint_after=$(cat /proc/sys/kernel/tainted)
allowed_taint=$((taint_before | 4096 | 8192))
unexpected_taint=$((taint_after & ~allowed_taint))
if [ "$unexpected_taint" -ne 0 ]; then
	echo "unexpected kernel taint bits: before=$taint_before after=$taint_after" >&2
	exit 1
fi

new_log=$(dmesg | tail -n "+$((dmesg_marker + 1))")
printf '%s\n' "$new_log"
if printf '%s\n' "$new_log" | grep -Eiq \
	'BUG:|WARNING:|Oops:|KASAN:|lockdep|general protection fault'; then
	echo "fatal kernel diagnostic detected" >&2
	exit 1
fi

echo "H0 synthetic PCI load/enumerate/unload: PASS"
