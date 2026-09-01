#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

set -eu

output_dir=${1:-/tmp/ssd-fwlab-memmap-probe}
kernel=$(readlink -f /boot/vmlinuz)
initrd=$(readlink -f /boot/initrd.img)
dollar='$'

mkdir -p "$output_dir"

run_case()
{
	name=$1
	extra=$2
	log="$output_dir/$name.log"

	set +e
	timeout 20s qemu-system-x86_64 \
		-enable-kvm \
		-machine q35,accel=kvm \
		-cpu host \
		-smp 2 \
		-m 2048 \
		-nodefaults \
		-no-reboot \
		-nographic \
		-monitor none \
		-serial stdio \
		-kernel "$kernel" \
		-initrd "$initrd" \
		-append "console=ttyS0 root=/dev/does-not-exist rd.shell=0 $extra" \
		>"$log" 2>&1
	rc=$?
	set -e
	if [ "$rc" -ne 0 ] && [ "$rc" -ne 124 ]; then
		echo "$name QEMU failed: rc=$rc" >&2
		return "$rc"
	fi
	grep -E 'Command line:|BIOS-provided physical RAM map|user-defined physical RAM map|Memory:|Kernel panic|VFS:|ALERT!' "$log" || true
}

run_case baseline ""
run_case memmap "memmap=16M${dollar}0x70000000"

echo "logs: $output_dir"
