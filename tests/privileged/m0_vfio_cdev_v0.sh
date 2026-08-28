#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

set -eu

module_path=${1:-kernel/vfio-cdev-v0/ssd_fwlab_vfio_v0.ko}
smoke_path=${2:-tools/vfio-cdev-v0/vfio_cdev_v0_smoke}
module_name=ssd_fwlab_vfio_v0
platform_name=ssd-fwlab-vfio-v0
platform_path=/sys/bus/platform/devices/$platform_name
loaded=0
vfio_name=

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
	if [ -e "$platform_path" ]; then
		echo "platform device remains after cleanup" >&2
		rc=1
	fi
	if [ -n "$vfio_name" ] &&
	   [ -e "/dev/vfio/devices/$vfio_name" ]; then
		echo "VFIO cdev remains after cleanup: $vfio_name" >&2
		rc=1
	fi
	exit "$rc"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
	echo "must run as root" >&2
	exit 2
fi
if [ ! -f "$module_path" ] || [ ! -x "$smoke_path" ]; then
	echo "module or smoke binary missing" >&2
	exit 2
fi
if grep -q "^${module_name} " /proc/modules || [ -e "$platform_path" ]; then
	echo "stale V0 device/module exists before load" >&2
	exit 1
fi

modprobe vfio
unsafe_noiommu=$(cat /sys/module/vfio/parameters/enable_unsafe_noiommu_mode)
case "$unsafe_noiommu" in
	N|n|0) ;;
	*)
		echo "unsafe no-IOMMU mode must remain disabled" >&2
		exit 1
		;;
esac
count=0
while [ ! -c /dev/iommu ] && [ "$count" -lt 50 ]; do
	count=$((count + 1))
	sleep 0.1
done
test -c /dev/iommu
echo "phase=vfio-ready unsafe_noiommu=$unsafe_noiommu"

dmesg_marker=$(dmesg | wc -l)
taint_before=$(cat /proc/sys/kernel/tainted)
insmod "$module_path"
loaded=1
echo "phase=module-loaded"

test -d "$platform_path"
test -L "$platform_path/driver"

for candidate in "$platform_path"/vfio-dev/vfio*; do
	[ -e "$candidate" ] || continue
	if [ -n "$vfio_name" ]; then
		echo "more than one VFIO cdev discovered" >&2
		exit 1
	fi
	vfio_name=${candidate##*/}
done
if [ -z "$vfio_name" ]; then
	echo "VFIO cdev was not created" >&2
	exit 1
fi

count=0
while [ ! -c "/dev/vfio/devices/$vfio_name" ] && [ "$count" -lt 50 ]; do
	count=$((count + 1))
	sleep 0.1
done
test -c "/dev/vfio/devices/$vfio_name"
echo "phase=cdev-ready node=$vfio_name"

"$smoke_path" "/dev/vfio/devices/$vfio_name"
echo "phase=userspace-smoke-passed"

rmmod "$module_name"
loaded=0
test ! -e "$platform_path"
count=0
while [ -e "/dev/vfio/devices/$vfio_name" ] && [ "$count" -lt 50 ]; do
	count=$((count + 1))
	sleep 0.1
done
test ! -e "/dev/vfio/devices/$vfio_name"
echo "phase=module-unloaded"

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
	'BUG:|WARNING:|Oops:|KASAN:|UBSAN:|lockdep|refcount_t:|use-after-free|scheduling while atomic|general protection fault|kernel NULL pointer'; then
	echo "fatal kernel diagnostic detected" >&2
	exit 1
fi

echo "V0 emulated VFIO cdev/iommufd contract: PASS"
