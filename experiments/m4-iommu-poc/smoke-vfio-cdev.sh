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
probe_path=${3:-$script_dir/build/vfio_handoff_probe}
iommu_module_name=ssd_fwlab_sw_iommu_poc
pci_module_name=ssd_fwlab_synth_pci_poc
iommu_loaded=0
pci_loaded=0
bound=0
bdf=
cdev=

cleanup()
{
	rc=$?
	trap - EXIT INT TERM
	if [ "$bound" -eq 1 ] && [ -n "$bdf" ] &&
	   [ -e "/sys/bus/pci/drivers/vfio-pci/$bdf" ]; then
		printf '%s' "$bdf" > /sys/bus/pci/drivers/vfio-pci/unbind || rc=1
	fi
	if [ -n "$bdf" ] &&
	   [ -e "/sys/bus/pci/devices/$bdf/driver_override" ]; then
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
if [ ! -f "$iommu_module_path" ] || [ ! -f "$pci_module_path" ] ||
   [ ! -x "$probe_path" ]; then
	echo "PoC artifact not found" >&2
	exit 2
fi
if ! grep -Fq 'memmap=16M$0x70000000' /proc/cmdline; then
	echo "fixed BAR reservation is absent from the running kernel" >&2
	exit 1
fi

modprobe vfio-pci
test "$(cat /sys/module/vfio/parameters/enable_unsafe_noiommu_mode)" = N
test "$(cat /sys/module/iommufd/parameters/allow_unsafe_interrupts)" = N

dmesg_marker=$(dmesg | wc -l)
insmod "$iommu_module_path"
iommu_loaded=1
insmod "$pci_module_path"
pci_loaded=1

for candidate in /sys/bus/pci/devices/*; do
	[ -f "$candidate/vendor" ] || continue
	[ "$(cat "$candidate/vendor")" = 0xfffa ] || continue
	[ "$(cat "$candidate/device")" = 0x0002 ] || continue
	bdf=${candidate##*/}
done
test -n "$bdf"
fwlab_require_exact_poc_bdf "$bdf"

printf '%s' vfio-pci > "/sys/bus/pci/devices/$bdf/driver_override"
printf '%s' "$bdf" > /sys/bus/pci/drivers_probe
test "$(basename "$(readlink -f "/sys/bus/pci/devices/$bdf/driver")")" = \
	vfio-pci
bound=1

for node in "/sys/bus/pci/devices/$bdf/vfio-dev"/*; do
	[ -e "$node" ] || continue
	cdev=/dev/vfio/devices/${node##*/}
done
test -n "$cdev"
test -c "$cdev"

"$probe_path" "$cdev"

printf '%s' "$bdf" > /sys/bus/pci/drivers/vfio-pci/unbind
bound=0
printf '%s' none > "/sys/bus/pci/devices/$bdf/driver_override"
rmmod "$pci_module_name"
pci_loaded=0
rmmod "$iommu_module_name"
iommu_loaded=0

new_log=$(dmesg | tail -n "+$((dmesg_marker + 1))")
printf '%s\n' "$new_log"
printf '%s\n' "$new_log" | grep -Fq 'map iova=0x40000000'
printf '%s\n' "$new_log" | grep -Fq 'unmap iova=0x40000000'
if printf '%s\n' "$new_log" | grep -Eiq \
	'BUG:|WARNING:|Oops:|KASAN:|lockdep|general protection fault'; then
	echo "fatal kernel diagnostic detected" >&2
	exit 1
fi

echo "M4-C secure upstream VFIO cdev handoff: PASS"
