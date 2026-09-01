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
pci_loaded=0
iommu_loaded=0
bound=0
bdf=

cleanup()
{
	rc=$?
	trap - EXIT INT TERM
	if [ "$bound" -eq 1 ] && [ -n "$bdf" ] &&
	   [ -e "/sys/bus/pci/drivers/vfio-pci/$bdf" ]; then
		if ! printf '%s' "$bdf" > /sys/bus/pci/drivers/vfio-pci/unbind; then
			rc=1
		fi
	fi
	if [ -n "$bdf" ] && [ -e "/sys/bus/pci/devices/$bdf/driver_override" ]; then
		printf '%s' none > "/sys/bus/pci/devices/$bdf/driver_override" || rc=1
	fi
	if [ "$pci_loaded" -eq 1 ]; then
		rmmod "$pci_module_name" || rc=1
	fi
	if [ "$iommu_loaded" -eq 1 ]; then
		rmmod "$iommu_module_name" || rc=1
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

dmesg_marker=$(dmesg | wc -l)
modprobe vfio-pci
if [ "$(cat /sys/module/vfio/parameters/enable_unsafe_noiommu_mode 2>/dev/null || echo N)" != N ]; then
	echo "VFIO unsafe no-IOMMU mode is enabled" >&2
	exit 1
fi
insmod "$iommu_module_path"
iommu_loaded=1
insmod "$pci_module_path"
pci_loaded=1

for candidate in /sys/bus/pci/devices/*; do
	[ -f "$candidate/vendor" ] || continue
	[ "$(cat "$candidate/vendor")" = "$vendor" ] || continue
	[ "$(cat "$candidate/device")" = "$device" ] || continue
	bdf=${candidate##*/}
done
test -n "$bdf"
fwlab_require_exact_poc_bdf "$bdf"
test -L "/sys/bus/pci/devices/$bdf/iommu_group"

printf '%s' vfio-pci > "/sys/bus/pci/devices/$bdf/driver_override"
printf '%s' "$bdf" > /sys/bus/pci/drivers_probe
test "$(basename "$(readlink -f "/sys/bus/pci/devices/$bdf/driver")")" = vfio-pci
bound=1

group_path=$(readlink -f "/sys/bus/pci/devices/$bdf/iommu_group")
group_id=${group_path##*/}
test -e "$group_path/devices/$bdf"
test -e "/dev/vfio/$group_id" || test -e /dev/vfio/devices

printf 'BDF=%s IOMMU_GROUP=%s DRIVER=vfio-pci\n' "$bdf" "$group_id"
lspci -D -s "$bdf" -nnk

printf '%s' "$bdf" > /sys/bus/pci/drivers/vfio-pci/unbind
bound=0
printf '%s' none > "/sys/bus/pci/devices/$bdf/driver_override"
rmmod "$pci_module_name"
pci_loaded=0
rmmod "$iommu_module_name"
iommu_loaded=0

new_log=$(dmesg | tail -n "+$((dmesg_marker + 1))")
printf '%s\n' "$new_log"
if printf '%s\n' "$new_log" | grep -Eiq \
	'BUG:|WARNING:|Oops:|KASAN:|lockdep|general protection fault'; then
	echo "fatal kernel diagnostic detected" >&2
	exit 1
fi

echo "M4 official vfio-pci bind/unbind: PASS"
