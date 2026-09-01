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
lab_module_path=${3:-$kernel_module_dir/ssd_fwlab_bar_lab.ko}
iommu_module_name=ssd_fwlab_sw_iommu_poc
pci_module_name=ssd_fwlab_synth_pci_poc
lab_module_name=ssd_fwlab_bar_lab
lab_driver_name=ssd_fwlab_bar_lab
vendor=0xfffa
device=0x0002
iommu_loaded=0
pci_loaded=0
lab_loaded=0
bdf=

cleanup()
{
	rc=$?
	trap - EXIT INT TERM
	if [ "$lab_loaded" -eq 1 ]; then
		rmmod "$lab_module_name" || rc=1
	fi
	if [ -n "$bdf" ] &&
	   [ -e "/sys/bus/pci/devices/$bdf/driver_override" ]; then
		printf '%s' none > "/sys/bus/pci/devices/$bdf/driver_override" || rc=1
	fi
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
   [ ! -f "$lab_module_path" ]; then
	echo "PoC module not found" >&2
	exit 2
fi
if ! grep -Fq 'memmap=16M$0x70000000' /proc/cmdline; then
	echo "fixed BAR reservation is absent from the running kernel" >&2
	exit 1
fi

dmesg_marker=$(dmesg | wc -l)
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

set -- $(sed -n '1p' "/sys/bus/pci/devices/$bdf/resource")
test "$1" = 0x0000000070000000
test "$2" = 0x0000000070ffffff

insmod "$lab_module_path"
lab_loaded=1
printf '%s' "$lab_driver_name" > "/sys/bus/pci/devices/$bdf/driver_override"
printf '%s' "$bdf" > /sys/bus/pci/drivers_probe
test "$(basename "$(readlink -f "/sys/bus/pci/devices/$bdf/driver")")" = \
	"$lab_driver_name"

printf 'BDF=%s DRIVER=%s\n' "$bdf" "$lab_driver_name"
lspci -D -s "$bdf" -nnvv
grep -A5 -B2 -F '70000000-70ffffff' /proc/iomem || true

rmmod "$lab_module_name"
lab_loaded=0
printf '%s' none > "/sys/bus/pci/devices/$bdf/driver_override"
rmmod "$pci_module_name"
pci_loaded=0
rmmod "$iommu_module_name"
iommu_loaded=0

test ! -e "/sys/bus/pci/devices/$bdf"
new_log=$(dmesg | tail -n "+$((dmesg_marker + 1))")
printf '%s\n' "$new_log"
printf '%s\n' "$new_log" | grep -Fq \
	'M4-A BAR doorbell/ack and two cold FLR cycles: PASS'
if printf '%s\n' "$new_log" | grep -Eiq \
	'BUG:|WARNING:|Oops:|KASAN:|lockdep|general protection fault'; then
	echo "fatal kernel diagnostic detected" >&2
	exit 1
fi

echo "M4-A synthetic BAR/lab-driver/reset: PASS"
