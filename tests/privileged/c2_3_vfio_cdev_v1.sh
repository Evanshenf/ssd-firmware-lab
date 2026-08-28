#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

set -eu

module_path=${1:-kernel/vfio-cdev-v1/ssd_fwlab_vfio_v1.ko}
tool_path=${2:-tools/vfio-cdev-v1-c23/build/vfio_cdev_v1_c23}
module_name=ssd_fwlab_vfio_v1
platform_name=ssd-fwlab-vfio-v1
platform_path=/sys/bus/platform/devices/$platform_name
media_device=/dev/sdb
media_name=${media_device##*/}
expected_vfio_dma_rw_crc=0xaa22e02a
expected_module_sha256=8c9f1418dcdce7e0ebbd18d97adcc9e11a7a9f7e2bfe5b565f18f8db8714b072
loaded=0
vfio_name=
media_write_before=

media_write_counters()
{
	awk '{ print $5 ":" $7 }' "/sys/class/block/$media_name/stat"
}

media_is_safe()
{
	if [ ! -b "$media_device" ] ||
	   [ ! -r "/sys/class/block/$media_name/ro" ] ||
	   [ "$(cat "/sys/class/block/$media_name/ro")" != 1 ]; then
		echo "$media_device must exist and remain read-only" >&2
		return 1
	fi
	for block_path in /sys/class/block/"$media_name"*; do
		[ -e "$block_path" ] || continue
		block_name=${block_path##*/}
		block_device=/dev/$block_name
		if findmnt -rn -S "$block_device" >/dev/null; then
			echo "raw-media device is mounted: $block_device" >&2
			return 1
		fi
		for holder in /sys/class/block/"$block_name"/holders/*; do
			if [ -e "$holder" ]; then
				echo "raw-media device has a holder: $block_name -> ${holder##*/}" >&2
				return 1
			fi
		done
		if command -v swapon >/dev/null 2>&1 &&
		   swapon --noheadings --raw --output NAME 2>/dev/null |
			grep -Fxq "$block_device"; then
			echo "raw-media device is active swap: $block_device" >&2
			return 1
		fi
	done
	return 0
}

media_writes_unchanged()
{
	[ -n "$media_write_before" ] || return 0
	media_write_after=$(media_write_counters) || return 1
	if [ "$media_write_after" != "$media_write_before" ]; then
		echo "raw-media write counters changed: before=$media_write_before after=$media_write_after" >&2
		return 1
	fi
	return 0
}

cleanup()
{
	rc=$?
	module_present=0
	trap - EXIT INT TERM
	if grep -q "^${module_name} " /proc/modules; then
		module_present=1
	fi
	if [ "$loaded" -eq 1 ] || [ "$module_present" -eq 1 ]; then
		if [ "$module_present" -eq 1 ]; then
			if rmmod "$module_name"; then
				loaded=0
			elif grep -q "^${module_name} " /proc/modules; then
				echo "failed to unload $module_name" >&2
				rc=1
			else
				loaded=0
			fi
		else
			loaded=0
		fi
	fi
	count=0
	while { [ -e "$platform_path" ] ||
		{ [ -n "$vfio_name" ] &&
		  [ -e "/dev/vfio/devices/$vfio_name" ]; }; } &&
	      [ "$count" -lt 50 ]; do
		count=$((count + 1))
		sleep 0.1
	done
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
	if ! media_is_safe; then
		rc=1
	fi
	if ! media_writes_unchanged; then
		rc=1
	fi
	exit "$rc"
}

trap cleanup EXIT INT TERM

if [ "$(id -u)" -ne 0 ]; then
	echo "must run as root" >&2
	exit 2
fi
if [ ! -f "$module_path" ] || [ ! -x "$tool_path" ]; then
	echo "module or C2.3 tool missing" >&2
	exit 2
fi
if ! command -v timeout >/dev/null 2>&1; then
	echo "GNU timeout is required for the privileged C2.3 gate" >&2
	exit 2
fi
if grep -q "^${module_name} " /proc/modules || [ -e "$platform_path" ]; then
	echo "stale V1 module/platform device exists before load" >&2
	exit 1
fi
if ! media_is_safe; then
	exit 1
fi
media_write_before=$(media_write_counters)
if [ -z "$media_write_before" ]; then
	echo "could not read raw-media write counters" >&2
	exit 1
fi

module_vermagic=$(modinfo -F vermagic "$module_path")
case "$module_vermagic" in
	"$(uname -r) "*) ;;
	*)
		echo "module vermagic does not match running kernel: $module_vermagic" >&2
		exit 1
		;;
esac
vfio_dma_rw_crc=$(modprobe --dump-modversions "$module_path" |
	awk '$2 == "vfio_dma_rw" { print $1 }')
if [ "$vfio_dma_rw_crc" != "$expected_vfio_dma_rw_crc" ]; then
	echo "unexpected vfio_dma_rw modversion: $vfio_dma_rw_crc" >&2
	exit 1
fi
module_sha256=$(sha256sum "$module_path" | awk '{ print $1 }')
if [ "$module_sha256" != "$expected_module_sha256" ]; then
	echo "module is not the frozen C2.2 artifact: $module_sha256" >&2
	exit 1
fi
tool_sha256=$(sha256sum "$tool_path" | awk '{ print $1 }')
script_sha256=$(sha256sum "$0" | awk '{ print $1 }')
module_srcversion=$(modinfo -F srcversion "$module_path")
echo "phase=artifact-identity module_sha256=$module_sha256 tool_sha256=$tool_sha256 script_sha256=$script_sha256 srcversion=$module_srcversion vfio_dma_rw_crc=$vfio_dma_rw_crc"
if ! "$tool_path" --selftest; then
	echo "C2.3 pure LE/interval selftest failed" >&2
	exit 1
fi
echo "phase=c2.3-selftest-passed"

dmesg_marker=$(dmesg | wc -l)
taint_before=$(cat /proc/sys/kernel/tainted)

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
if [ ! -c /dev/iommu ]; then
	echo "/dev/iommu was not created" >&2
	exit 1
fi
echo "phase=vfio-ready unsafe_noiommu=$unsafe_noiommu"

insmod "$module_path"
loaded=1
echo "phase=module-loaded"

if [ ! -d "$platform_path" ] || [ ! -L "$platform_path/driver" ]; then
	echo "V1 platform device is not bound" >&2
	exit 1
fi
for candidate in "$platform_path"/vfio-dev/vfio*; do
	[ -e "$candidate" ] || continue
	if [ -n "$vfio_name" ]; then
		echo "more than one V1 VFIO cdev discovered" >&2
		exit 1
	fi
	vfio_name=${candidate##*/}
done
if [ -z "$vfio_name" ]; then
	echo "V1 VFIO cdev was not created" >&2
	exit 1
fi
count=0
while [ ! -c "/dev/vfio/devices/$vfio_name" ] && [ "$count" -lt 50 ]; do
	count=$((count + 1))
	sleep 0.1
done
if [ ! -c "/dev/vfio/devices/$vfio_name" ]; then
	echo "V1 VFIO cdev node was not created: $vfio_name" >&2
	exit 1
fi
echo "phase=cdev-ready node=$vfio_name"

set +e
timeout -k 5s 90s "$tool_path" "/dev/vfio/devices/$vfio_name"
tool_rc=$?
set -e
if [ "$tool_rc" -ne 0 ]; then
	echo "C2.3 negative oracle failed or timed out: rc=$tool_rc" >&2
	exit 1
fi
echo "phase=c2.3-userspace-passed"

rmmod "$module_name"
loaded=0
count=0
while { [ -e "$platform_path" ] ||
	[ -e "/dev/vfio/devices/$vfio_name" ]; } && [ "$count" -lt 50 ]; do
	count=$((count + 1))
	sleep 0.1
done
if [ -e "$platform_path" ] ||
   [ -e "/dev/vfio/devices/$vfio_name" ]; then
	echo "V1 module objects did not disappear within 5 seconds" >&2
	exit 1
fi
echo "phase=module-unloaded"

if ! media_is_safe; then
	exit 1
fi
if ! media_writes_unchanged; then
	exit 1
fi
taint_after=$(cat /proc/sys/kernel/tainted)
allowed_taint=$((taint_before | 4096 | 8192))
unexpected_taint=$((taint_after & ~allowed_taint))
if [ "$unexpected_taint" -ne 0 ]; then
	echo "unexpected kernel taint: before=$taint_before after=$taint_after" >&2
	exit 1
fi

new_log=$(dmesg | tail -n "+$((dmesg_marker + 1))")
printf '%s\n' "$new_log"
if printf '%s\n' "$new_log" | grep -Eiq \
	'BUG:|WARNING:|Oops:|KASAN:|UBSAN:|lockdep|refcount_t:|use-after-free|scheduling while atomic|general protection fault|kernel NULL pointer|hung task'; then
	echo "fatal kernel diagnostic detected" >&2
	exit 1
fi

echo "C2.3 V1 independent negative IOAS-copy privileged gate: PASS"
