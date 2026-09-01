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
iommu_module_name=ssd_fwlab_sw_iommu_poc
pci_module_name=ssd_fwlab_synth_pci_poc
vendor=0xfffa
device=0x0002
pci_root_device=/sys/devices/ssd_fwlab_synth_pci_poc
iommu_root_device=/sys/devices/ssd_fwlab_sw_iommu_poc
pci_loaded=0
iommu_loaded=0
bdf=

cleanup()
{
	rc=$?
	trap - EXIT INT TERM
	if [ "$pci_loaded" -eq 1 ]; then
		if ! rmmod "$pci_module_name"; then
			rc=1
			if [ -n "$bdf" ] && [ -e "/sys/bus/pci/devices/$bdf/remove" ]; then
				printf 1 > "/sys/bus/pci/devices/$bdf/remove" || true
				rmmod "$pci_module_name" || true
			fi
		fi
	fi
	if [ "$iommu_loaded" -eq 1 ]; then
		if ! rmmod "$iommu_module_name"; then
			rc=1
		fi
	fi
	if grep -q "^${pci_module_name} " /proc/modules ||
	   grep -q "^${iommu_module_name} " /proc/modules; then
		echo "PoC module remains loaded" >&2
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
if [ ! -f "$iommu_module_path" ] || [ ! -f "$pci_module_path" ]; then
	echo "PoC module not found" >&2
	exit 2
fi
if grep -q "^${pci_module_name} " /proc/modules ||
   grep -q "^${iommu_module_name} " /proc/modules ||
   [ -e "$pci_root_device" ] || [ -e "$iommu_root_device" ]; then
	echo "stale PoC state exists before load" >&2
	exit 2
fi

dmesg_marker=$(dmesg | wc -l)
insmod "$iommu_module_path"
iommu_loaded=1
test -d "$iommu_root_device"
insmod "$pci_module_path"
pci_loaded=1
test -d "$pci_root_device"

for candidate in /sys/bus/pci/devices/*; do
	[ -f "$candidate/vendor" ] || continue
	[ "$(cat "$candidate/vendor")" = "$vendor" ] || continue
	[ "$(cat "$candidate/device")" = "$device" ] || continue
	if [ -n "$bdf" ]; then
		echo "more than one PoC function found" >&2
		exit 1
	fi
	bdf=${candidate##*/}
done

if [ -z "$bdf" ]; then
	echo "synthetic PCI function was not enumerated" >&2
	exit 1
fi
fwlab_require_exact_poc_bdf "$bdf"
test ! -e "/sys/bus/pci/devices/$bdf/driver"
test "$(cat "/sys/bus/pci/devices/$bdf/driver_override")" = "none"
test -L "/sys/bus/pci/devices/$bdf/iommu_group"

group_path=$(readlink -f "/sys/bus/pci/devices/$bdf/iommu_group")
group_id=${group_path##*/}
test -e "$group_path/devices/$bdf"

printf 'BDF=%s IOMMU_GROUP=%s\n' "$bdf" "$group_id"
lspci -D -s "$bdf" -nnvv

rmmod "$pci_module_name"
pci_loaded=0
test ! -e "/sys/bus/pci/devices/$bdf"
test ! -e "$pci_root_device"
rmmod "$iommu_module_name"
iommu_loaded=0
test ! -e "$iommu_root_device"

new_log=$(dmesg | tail -n "+$((dmesg_marker + 1))")
printf '%s\n' "$new_log"
if printf '%s\n' "$new_log" | grep -Eiq \
	'BUG:|WARNING:|Oops:|KASAN:|lockdep|general protection fault'; then
	echo "fatal kernel diagnostic detected" >&2
	exit 1
fi

echo "M4 synthetic PCI + kernel IOMMU enumerate/unload: PASS"
