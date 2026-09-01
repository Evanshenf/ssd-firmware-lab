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
kernel_image=${4:-/boot/vmlinuz-$(uname -r)}
initramfs_image=${5:-$script_dir/build/fwlab-l2-initramfs.cpio.gz}
iommu_module_name=ssd_fwlab_sw_iommu_poc
pci_module_name=ssd_fwlab_synth_pci_poc
iommu_loaded=0
pci_loaded=0
bdf=
controller=
namespace=
backend=
host_pattern=
guest_pattern=
observed=
qemu_log=
sdb_stat_before=

cleanup()
{
	rc=$?
	trap - EXIT INT TERM
	if [ -n "$bdf" ] && [ -e "/sys/bus/pci/devices/$bdf" ]; then
		driver=
		if [ -L "/sys/bus/pci/devices/$bdf/driver" ]; then
			driver=$(basename "$(readlink -f \
				"/sys/bus/pci/devices/$bdf/driver")")
		fi
		case "$driver" in
			nvme|vfio-pci)
				printf '%s' "$bdf" > "/sys/bus/pci/drivers/$driver/unbind" ||
					rc=1
				;;
		esac
		printf '%s' none > "/sys/bus/pci/devices/$bdf/driver_override" ||
			rc=1
	fi
	if [ "$pci_loaded" -eq 1 ]; then
		rmmod "$pci_module_name" || rc=1
	fi
	if [ "$iommu_loaded" -eq 1 ]; then
		rmmod "$iommu_module_name" || rc=1
	fi
	rm -f "$backend" "$host_pattern" "$guest_pattern" "$observed" \
		"$qemu_log"
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

bind_vfio()
{
	printf '%s' "$bdf" > /sys/bus/pci/drivers/nvme/unbind
	remaining=200
	while [ "$remaining" -gt 0 ] && [ -e "$namespace" ]; do
		remaining=$((remaining - 1))
		sleep 0.05
	done
	test ! -e "$namespace"
	printf 1 > "/sys/bus/pci/devices/$bdf/reset"
	printf '%s' vfio-pci > "/sys/bus/pci/devices/$bdf/driver_override"
	printf '%s' "$bdf" > /sys/bus/pci/drivers_probe
	test "$(basename "$(readlink -f \
		"/sys/bus/pci/devices/$bdf/driver")")" = vfio-pci
}

vfio_admission()
{
	cdev=
	for node in "/sys/bus/pci/devices/$bdf/vfio-dev"/*; do
		[ -e "$node" ] || continue
		cdev=/dev/vfio/devices/${node##*/}
	done
	test -n "$cdev"
	test -c "$cdev"
	"$probe_path" "$cdev" --nvme-admission
}

restore_native()
{
	printf '%s' "$bdf" > /sys/bus/pci/drivers/vfio-pci/unbind
	printf '%s' none > "/sys/bus/pci/devices/$bdf/driver_override"
	printf 1 > "/sys/bus/pci/devices/$bdf/reset"
	printf '%s' nvme > "/sys/bus/pci/devices/$bdf/driver_override"
	printf '%s' "$bdf" > /sys/bus/pci/drivers_probe
	wait_for_namespace
}

if [ "$(id -u)" -ne 0 ]; then
	echo "must run as root" >&2
	exit 2
fi
for artifact in "$iommu_module_path" "$pci_module_path" "$probe_path" \
	"$kernel_image" "$initramfs_image"; do
	[ -e "$artifact" ] || { echo "artifact not found: $artifact" >&2; exit 2; }
done
test -x "$probe_path"
test -c /dev/kvm
if ! grep -Fq 'memmap=16M$0x70000000' /proc/cmdline; then
	echo "fixed BAR reservation is absent from the running kernel" >&2
	exit 1
fi
if grep -q "^${pci_module_name} " /proc/modules ||
   grep -q "^${iommu_module_name} " /proc/modules; then
	echo "stale PoC module exists before L2 smoke" >&2
	exit 2
fi

backend=$(mktemp /var/tmp/ssd-fwlab-m5-media.XXXXXX)
host_pattern=$(mktemp /tmp/fwlab-m5-host.XXXXXX)
guest_pattern=$(mktemp /tmp/fwlab-m5-guest.XXXXXX)
observed=$(mktemp /tmp/fwlab-m5-observed.XXXXXX)
qemu_log=$(mktemp /tmp/fwlab-m5-qemu.XXXXXX)
truncate -s 1048576 "$backend"
case "$(findmnt -n -o SOURCE -T "$backend")" in
	/dev/sdb*) echo "refusing a backend on reserved /dev/sdb" >&2; exit 1 ;;
esac
if [ -b /dev/sdb ]; then
	sdb_stat_before=$(cat /sys/block/sdb/stat)
fi
yes FWLAB-L1-M4-HOST | head -c 4096 > "$host_pattern"

modprobe nvme
modprobe vfio-pci
test "$(cat /sys/module/vfio/parameters/enable_unsafe_noiommu_mode)" = N
test "$(cat /sys/module/iommufd/parameters/allow_unsafe_interrupts)" = N
dmesg_marker=$(dmesg | wc -l)
insmod "$iommu_module_path"
iommu_loaded=1
insmod "$pci_module_path" nvme_mode=1 backend_path="$backend"
pci_loaded=1

for candidate in /sys/bus/pci/devices/*; do
	[ -f "$candidate/vendor" ] || continue
	[ "$(cat "$candidate/vendor")" = 0xfffa ] || continue
	[ "$(cat "$candidate/device")" = 0x0002 ] || continue
	bdf=${candidate##*/}
done
test -n "$bdf"
fwlab_require_exact_poc_bdf "$bdf"
test -e "/sys/bus/pci/devices/$bdf/reset"
wait_for_namespace
dd if="$host_pattern" of="$namespace" bs=4096 count=1 seek=7 \
	oflag=direct conv=fsync,notrunc status=none
nvme flush "$namespace" >/dev/null

for cycle in 1 2; do
	bind_vfio
	vfio_admission
	: > "$qemu_log"
	timeout --signal=TERM 60 qemu-system-x86_64 \
		-enable-kvm -machine q35,accel=kvm -cpu host -smp 1 -m 256M \
		-nodefaults -display none -serial stdio -monitor none -no-reboot \
		-kernel "$kernel_image" -initrd "$initramfs_image" \
		-append "console=ttyS0 rdinit=/init panic=-1 fwlab_cycle=$cycle" \
		-object iommufd,id=iommufd0 \
		-device "vfio-pci,host=$bdf,iommufd=iommufd0,x-no-mmap=on" \
		> "$qemu_log" 2>&1
	grep -Fq "FWLAB-L2-NVME-CYCLE-$cycle: PASS" "$qemu_log"
	if grep -Eiq 'qemu-system.*(warning|error)|FWLAB-L2-NVME: FAIL|BUG:|Oops:|KASAN:|Kernel panic' "$qemu_log"; then
		echo "fatal L2/QEMU diagnostic detected in cycle $cycle" >&2
		exit 1
	fi
	grep -F "FWLAB-L2-NVME-CYCLE-$cycle: PASS" "$qemu_log"

	restore_native
	yes "FWLAB-L2-P5-CYCLE-$cycle" | head -c 4096 > "$guest_pattern"
	dd if="$namespace" of="$observed" bs=4096 count=1 \
		skip=$((8 + cycle)) iflag=direct status=none
	cmp "$guest_pattern" "$observed"
	dd if="$namespace" of="$observed" bs=4096 count=1 skip=7 \
		iflag=direct status=none
	cmp "$host_pattern" "$observed"
done

rmmod "$pci_module_name"
pci_loaded=0
rmmod "$iommu_module_name"
iommu_loaded=0
test ! -e "/sys/bus/pci/devices/$bdf"

for cycle in 1 2; do
	yes "FWLAB-L2-P5-CYCLE-$cycle" | head -c 4096 > "$guest_pattern"
	dd if="$backend" of="$observed" bs=4096 count=1 \
		skip=$((8 + cycle)) status=none
	cmp "$guest_pattern" "$observed"
done
if [ -n "$sdb_stat_before" ]; then
	test "$(cat /sys/block/sdb/stat)" = "$sdb_stat_before"
fi

new_log=$(dmesg | tail -n "+$((dmesg_marker + 1))")
if printf '%s\n' "$new_log" | grep -Eiq \
	'BUG:|WARNING:|Oops:|KASAN:|lockdep|general protection fault|I/O error'; then
	echo "fatal L1 kernel diagnostic detected" >&2
	exit 1
fi

echo "M5/P6 upstream VFIO L2 two-cycle owner restoration: PASS"
