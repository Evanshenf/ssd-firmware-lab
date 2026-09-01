#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/poc-safety.sh"
fwlab_require_profile_ack
kernel_module_dir=${FWLAB_M4_KERNEL_MODULE_DIR:-$script_dir/../../kernel/m4-synthetic-pci-poc}
iommu_module_path=${1:-$kernel_module_dir/ssd_fwlab_sw_iommu_poc.ko}
pci_module_path=${2:-$kernel_module_dir/ssd_fwlab_synth_pci_poc.ko}
unaligned_probe_path=${3:-$script_dir/build/nvme_unaligned_probe}
iommu_module_name=ssd_fwlab_sw_iommu_poc
pci_module_name=ssd_fwlab_synth_pci_poc
iommu_loaded=0
pci_loaded=0
bdf=
controller=
namespace=
pattern_a=
pattern_b=
pattern_8k=
observed=
sdb_stat_before=

cleanup()
{
	rc=$?
	trap - EXIT INT TERM
	rm -f "$pattern_a" "$pattern_b" "$pattern_8k" "$observed"
	if [ "$pci_loaded" -eq 1 ]; then
		rmmod "$pci_module_name" || rc=1
	fi
	if [ "$iommu_loaded" -eq 1 ]; then
		rmmod "$iommu_module_name" || rc=1
	fi
	exit "$rc"
}

trap cleanup EXIT INT TERM

wait_for_namespace()
{
	remaining=200
	controller=
	namespace=
	while [ "$remaining" -gt 0 ]; do
		for candidate in "/sys/bus/pci/devices/$bdf/nvme"/nvme*; do
			[ -d "$candidate" ] || continue
			controller=${candidate##*/}
			namespace=/dev/${controller}n1
			[ -b "$namespace" ] && return 0
		done
		remaining=$((remaining - 1))
		sleep 0.05
	done
	return 1
}

if [ "$(id -u)" -ne 0 ]; then
	echo "must run as root" >&2
	exit 2
fi
if [ ! -f "$iommu_module_path" ] || [ ! -f "$pci_module_path" ] ||
   [ ! -x "$unaligned_probe_path" ]; then
	echo "PoC module not found" >&2
	exit 2
fi
if ! grep -Fq 'memmap=16M$0x70000000' /proc/cmdline; then
	echo "fixed BAR reservation is absent from the running kernel" >&2
	exit 1
fi
if grep -q "^${pci_module_name} " /proc/modules ||
   grep -q "^${iommu_module_name} " /proc/modules; then
	echo "stale PoC module exists before native-NVMe smoke" >&2
	exit 2
fi
if [ -b /dev/sdb ]; then
	sdb_stat_before=$(cat /sys/block/sdb/stat)
fi

pattern_a=$(mktemp /tmp/fwlab-native-a.XXXXXX)
pattern_b=$(mktemp /tmp/fwlab-native-b.XXXXXX)
pattern_8k=$(mktemp /tmp/fwlab-native-8k.XXXXXX)
observed=$(mktemp /tmp/fwlab-native-observed.XXXXXX)
dd if=/dev/urandom of="$pattern_a" bs=4096 count=1 status=none
dd if=/dev/urandom of="$pattern_b" bs=4096 count=1 status=none
dd if=/dev/urandom of="$pattern_8k" bs=8192 count=1 status=none

modprobe nvme
dmesg_marker=$(dmesg | wc -l)
insmod "$iommu_module_path"
iommu_loaded=1
insmod "$pci_module_path" nvme_mode=1
pci_loaded=1

for candidate in /sys/bus/pci/devices/*; do
	[ -f "$candidate/vendor" ] || continue
	[ "$(cat "$candidate/vendor")" = 0xfffa ] || continue
	[ "$(cat "$candidate/device")" = 0x0002 ] || continue
	bdf=${candidate##*/}
done
test -n "$bdf"
fwlab_require_exact_poc_bdf "$bdf"
wait_for_namespace
test "$(basename "$(readlink -f "/sys/bus/pci/devices/$bdf/driver")")" = nvme
test "$(blockdev --getsize64 "$namespace")" = 1048576
nvme id-ctrl "/dev/$controller" | grep -Fq \
	'SSD Firmware Lab M4 Native NVMe PoC'

dd if="$pattern_a" of="$namespace" bs=4096 count=1 \
	oflag=direct conv=fsync,notrunc status=none
dd if="$namespace" of="$observed" bs=4096 count=1 \
	iflag=direct status=none
cmp "$pattern_a" "$observed"
nvme flush "$namespace" >/dev/null

dd if="$pattern_8k" of="$namespace" bs=8192 count=1 seek=4 \
	oflag=direct conv=fsync,notrunc status=none
dd if="$namespace" of="$observed" bs=8192 count=1 skip=4 \
	iflag=direct status=none
cmp "$pattern_8k" "$observed"
"$unaligned_probe_path" "$namespace"

nvme reset "/dev/$controller" >/dev/null
wait_for_namespace
dd if="$namespace" of="$observed" bs=4096 count=1 \
	iflag=direct status=none
cmp "$pattern_a" "$observed"

dd if="$pattern_b" of="$namespace" bs=4096 count=1 seek=1 \
	oflag=direct conv=fsync,notrunc status=none
printf '%s' "$bdf" > /sys/bus/pci/drivers/nvme/unbind
test ! -e "$namespace"
printf '%s' "$bdf" > /sys/bus/pci/drivers_probe
wait_for_namespace
dd if="$namespace" of="$observed" bs=4096 count=1 skip=1 \
	iflag=direct status=none
cmp "$pattern_b" "$observed"

if [ -n "$sdb_stat_before" ]; then
	test "$(cat /sys/block/sdb/stat)" = "$sdb_stat_before"
fi

rmmod "$pci_module_name"
pci_loaded=0
rmmod "$iommu_module_name"
iommu_loaded=0
test ! -e "/sys/bus/pci/devices/$bdf"

new_log=$(dmesg | tail -n "+$((dmesg_marker + 1))")
printf '%s\n' "$new_log"
if printf '%s\n' "$new_log" | grep -Eiq \
	'BUG:|WARNING:|Oops:|KASAN:|lockdep|general protection fault|I/O error|failed to'; then
	echo "fatal kernel diagnostic detected" >&2
	exit 1
fi

echo "M4 native nvme-pci write/read/flush/reset/rebind: PASS"
