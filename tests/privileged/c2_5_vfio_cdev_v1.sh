#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

set -eu

if [ "$#" -ne 3 ]; then
	echo "usage: $0 FROZEN_V1_KO PEER_FIXTURE_KO C2_5_TOOL" >&2
	exit 2
fi

module_path=$1
peer_module_path=$2
tool_path=$3
module_name=ssd_fwlab_vfio_v1
peer_module_name=ssd_fwlab_v1_peer_fixture
h0_module_name=ssd_fwlab_host_h0
v0_module_name=ssd_fwlab_vfio_v0
platform_name=ssd-fwlab-vfio-v1
base_platform_path=/sys/bus/platform/devices/$platform_name
peer_platform_path=/sys/bus/platform/devices/$platform_name.0
h0_platform_path=/sys/devices/ssd_fwlab_host_h0
v0_platform_path=/sys/bus/platform/devices/ssd-fwlab-vfio-v0
media_device=/dev/sdb
media_name=${media_device##*/}
expected_vfio_dma_rw_crc=0xaa22e02a
expected_module_sha256=8c9f1418dcdce7e0ebbd18d97adcc9e11a7a9f7e2bfe5b565f18f8db8714b072
expected_module_srcversion=FFE4E0F87FA9FA275C67192

module_owned=0
peer_owned=0
media_checks_armed=0
base_vfio_name=
peer_vfio_name=
base_vfio_rdev=
peer_vfio_rdev=
media_write_before=
module_srcversion=
peer_module_srcversion=
boot_id_before=
dmesg_marker=
taint_before=
load_signal_deferred=0
pending_signal=0
hold_pid=
hold_dir=
hold_fifo=
hold_log=
hold_writer_open=0
hold_reaped=0
hold_rc=
gate_lock_dir=/run/ssd-fwlab
# C2.4 froze this pathname. Reusing it is the only way to make the frozen
# C2.4 wrapper and C2.5 mutually exclusive without changing C2.4 evidence.
gate_lock_path=$gate_lock_dir/c2-4-vfio-v1.lock

media_write_counters()
{
	awk '{ print $5 ":" $7 }' "/sys/class/block/$media_name/stat"
}

media_is_safe()
{
	if ! active_swaps=$(swapon --show=NAME --noheadings --raw 2>/dev/null); then
		echo "could not enumerate active swap devices" >&2
		return 1
	fi
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
		if findmnt -rn -S "$block_device" >/dev/null 2>&1; then
			echo "raw-media device is mounted: $block_device" >&2
			return 1
		else
			findmnt_rc=$?
			if [ "$findmnt_rc" -ne 1 ]; then
				echo "could not inspect mounts for $block_device: rc=$findmnt_rc" >&2
				return 1
			fi
		fi
		for holder in /sys/class/block/"$block_name"/holders/*; do
			if [ -e "$holder" ]; then
				echo "raw-media device has a holder: $block_name -> ${holder##*/}" >&2
				return 1
			fi
		done
		if printf '%s\n' "$active_swaps" | grep -Fxq "$block_device"; then
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

module_is_loaded()
{
	loaded_name=$1
	grep -q "^${loaded_name} " /proc/modules
}

loaded_module_matches_artifact()
{
	if [ -z "$module_srcversion" ] ||
	   [ ! -r "/sys/module/$module_name/srcversion" ]; then
		echo "loaded $module_name srcversion is unavailable" >&2
		return 1
	fi
	if ! loaded_srcversion=$(cat "/sys/module/$module_name/srcversion"); then
		echo "could not read loaded $module_name srcversion" >&2
		return 1
	fi
	if [ "$loaded_srcversion" != "$module_srcversion" ]; then
		echo "loaded $module_name srcversion mismatch: expected=$module_srcversion actual=$loaded_srcversion" >&2
		return 1
	fi
	return 0
}

loaded_peer_matches_artifact()
{
	if [ -z "$peer_module_srcversion" ] ||
	   [ ! -r "/sys/module/$peer_module_name/srcversion" ]; then
		echo "loaded $peer_module_name srcversion is unavailable" >&2
		return 1
	fi
	if ! loaded_peer_srcversion=$(cat "/sys/module/$peer_module_name/srcversion"); then
		echo "could not read loaded $peer_module_name srcversion" >&2
		return 1
	fi
	if [ "$loaded_peer_srcversion" != "$peer_module_srcversion" ]; then
		echo "loaded $peer_module_name srcversion mismatch: expected=$peer_module_srcversion actual=$loaded_peer_srcversion" >&2
		return 1
	fi
	return 0
}

v1_platform_count()
{
	count=0
	for candidate in /sys/bus/platform/devices/"$platform_name"*; do
		[ -e "$candidate" ] || continue
		candidate_name=${candidate##*/}
		case "$candidate_name" in
			"$platform_name"|"$platform_name".*)
				count=$((count + 1))
				;;
		esac
	done
	printf '%s\n' "$count"
}

discover_single_cdev()
{
	discover_platform=$1
	discover_name=
	for candidate in "$discover_platform"/vfio-dev/vfio*; do
		[ -e "$candidate" ] || continue
		if [ -n "$discover_name" ]; then
			echo "more than one VFIO cdev below $discover_platform" >&2
			return 1
		fi
		discover_name=${candidate##*/}
	done
	if [ -z "$discover_name" ]; then
		echo "no VFIO cdev below $discover_platform" >&2
		return 1
	fi
	printf '%s\n' "$discover_name"
}

wait_cdev_node()
{
	wait_name=$1
	wait_count=0
	while [ ! -c "/dev/vfio/devices/$wait_name" ] && [ "$wait_count" -lt 50 ]; do
		wait_count=$((wait_count + 1))
		sleep 0.1
	done
	[ -c "/dev/vfio/devices/$wait_name" ]
}

cdev_open_count()
{
	cdev_node=$1
	if ! cdev_rdev=$(stat -Lc '%t:%T' -- "$cdev_node"); then
		echo "could not resolve cdev identity: $cdev_node" >&2
		return 1
	fi
	open_count=0
	for descriptor in /proc/[0-9]*/fd/*; do
		[ -L "$descriptor" ] || continue
		if descriptor_rdev=$(stat -Lc '%t:%T' -- "$descriptor" 2>/dev/null) &&
		   [ "$descriptor_rdev" = "$cdev_rdev" ]; then
			open_count=$((open_count + 1))
		fi
	done
	printf '%s\n' "$open_count"
}

hold_helper_running()
{
	if [ -z "$hold_pid" ] || [ "$hold_reaped" -eq 1 ]; then
		return 1
	fi
	if ! kill -0 "$hold_pid" 2>/dev/null; then
		return 1
	fi
	if ! hold_state=$(awk '$1 == "State:" { print $2 }' \
		"/proc/$hold_pid/status" 2>/dev/null); then
		return 0
	fi
	[ -n "$hold_state" ] || return 0
	[ "$hold_state" != Z ]
}

wait_hold_helper_bounded()
{
	wait_limit=$1
	wait_count=0
	while hold_helper_running && [ "$wait_count" -lt "$wait_limit" ]; do
		wait_count=$((wait_count + 1))
		sleep 0.1
	done
	if hold_helper_running; then
		return 1
	fi
	if [ "$hold_reaped" -eq 0 ]; then
		if wait "$hold_pid"; then
			hold_rc=0
		else
			hold_rc=$?
		fi
		hold_reaped=1
	fi
	return 0
}

release_hold_helper_cleanup()
{
	if [ "$hold_writer_open" -eq 1 ]; then
		exec 9>&-
		hold_writer_open=0
	fi
	if [ -z "$hold_pid" ] || [ "$hold_reaped" -eq 1 ]; then
		return 0
	fi
	if wait_hold_helper_bounded 30; then
		return 0
	fi
	if ! kill -TERM "$hold_pid" 2>/dev/null && hold_helper_running; then
		echo "could not send TERM to survivor helper: pid=$hold_pid" >&2
	fi
	if wait_hold_helper_bounded 20; then
		return 0
	fi
	if ! kill -KILL "$hold_pid" 2>/dev/null && hold_helper_running; then
		echo "could not send KILL to survivor helper: pid=$hold_pid" >&2
	fi
	if wait_hold_helper_bounded 20; then
		return 0
	fi
	echo "survivor helper did not exit after bounded TERM/KILL: pid=$hold_pid" >&2
	return 1
}

remove_hold_files()
{
	remove_rc=0
	for hold_path in "$hold_fifo" "$hold_log"; do
		[ -n "$hold_path" ] || continue
		if { [ -e "$hold_path" ] || [ -L "$hold_path" ]; } &&
		   ! rm -f -- "$hold_path"; then
			echo "failed to remove survivor temporary file: $hold_path" >&2
			remove_rc=1
		fi
	done
	if [ -n "$hold_dir" ] &&
	   { [ -e "$hold_dir" ] || [ -L "$hold_dir" ]; }; then
		if ! rmdir "$hold_dir"; then
			echo "failed to remove survivor temporary directory: $hold_dir" >&2
			remove_rc=1
		fi
	fi
	return "$remove_rc"
}

hold_resources_absent()
{
	absent_rc=0
	if [ -n "$hold_pid" ] &&
	   { [ "$hold_reaped" -ne 1 ] || kill -0 "$hold_pid" 2>/dev/null; }; then
		echo "survivor helper remains live: pid=$hold_pid" >&2
		absent_rc=1
	fi
	for hold_path in "$hold_fifo" "$hold_log"; do
		[ -n "$hold_path" ] || continue
		if [ -e "$hold_path" ] || [ -L "$hold_path" ]; then
			echo "survivor temporary file remains: $hold_path" >&2
			absent_rc=1
		fi
	done
	if [ -n "$hold_dir" ] &&
	   { [ -e "$hold_dir" ] || [ -L "$hold_dir" ]; }; then
		echo "survivor temporary directory remains: $hold_dir" >&2
		absent_rc=1
	fi
	return "$absent_rc"
}

cleanup()
{
	cleanup_rc=$1
	cleanup_scope=0
	trap - EXIT HUP INT QUIT TERM
	load_signal_deferred=0
	pending_signal=0
	if [ "$cleanup_rc" -ne 0 ] && [ -n "$dmesg_marker" ]; then
		cleanup_boot=$(cat /proc/sys/kernel/random/boot_id 2>/dev/null || :)
		cleanup_taint=$(cat /proc/sys/kernel/tainted 2>/dev/null || :)
		cleanup_dmesg_end=$(dmesg 2>/dev/null | wc -l || :)
		echo "phase=cleanup-diagnostics rc=$cleanup_rc boot=${cleanup_boot:-unavailable} taint=${cleanup_taint:-unavailable} log_start=$dmesg_marker log_end=${cleanup_dmesg_end:-unavailable}" >&2
		if cleanup_log=$(dmesg 2>/dev/null |
			tail -n "+$((dmesg_marker + 1))" | tail -n 200); then
			printf '%s\n' "$cleanup_log" >&2
		fi
	fi

	if ! release_hold_helper_cleanup; then
		[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
	fi
	if ! remove_hold_files; then
		[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
	fi
	if ! hold_resources_absent; then
		[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
	fi

	if [ "$peer_owned" -eq 1 ]; then
		cleanup_scope=1
		if module_is_loaded "$peer_module_name"; then
			if loaded_peer_matches_artifact; then
				if timeout -k 2s 5s rmmod "$peer_module_name"; then
					:
				else
					peer_cleanup_rc=$?
					echo "bounded cleanup peer rmmod failed: module=$peer_module_name rc=$peer_cleanup_rc" >&2
					[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
				fi
			else
				echo "refusing to unload peer module with mismatched identity" >&2
				[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
			fi
		fi
		peer_owned=0
	fi

	if [ "$module_owned" -eq 1 ]; then
		cleanup_scope=1
		if module_is_loaded "$module_name"; then
			if loaded_module_matches_artifact; then
				if timeout -k 2s 5s rmmod "$module_name"; then
					:
				else
					module_cleanup_rc=$?
					echo "bounded cleanup V1 rmmod failed: module=$module_name rc=$module_cleanup_rc" >&2
					[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
				fi
			else
				echo "refusing to unload V1 module with mismatched identity" >&2
				[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
			fi
		fi
		module_owned=0
	fi

	if [ "$cleanup_scope" -eq 1 ]; then
		cleanup_count=0
		while { module_is_loaded "$module_name" ||
			module_is_loaded "$peer_module_name" ||
			[ -e "$base_platform_path" ] ||
			[ -e "$peer_platform_path" ] ||
			{ [ -n "$base_vfio_name" ] &&
			  [ -e "/dev/vfio/devices/$base_vfio_name" ]; } ||
			{ [ -n "$peer_vfio_name" ] &&
			  [ -e "/dev/vfio/devices/$peer_vfio_name" ]; }; } &&
		      [ "$cleanup_count" -lt 50 ]; do
			cleanup_count=$((cleanup_count + 1))
			sleep 0.1
		done
		if module_is_loaded "$module_name" ||
		   module_is_loaded "$peer_module_name" ||
		   [ -e "$base_platform_path" ] || [ -e "$peer_platform_path" ] ||
		   { [ -n "$base_vfio_name" ] &&
		     [ -e "/dev/vfio/devices/$base_vfio_name" ]; } ||
		   { [ -n "$peer_vfio_name" ] &&
		     [ -e "/dev/vfio/devices/$peer_vfio_name" ]; }; then
			echo "V1/peer resources remain after cleanup" >&2
			[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
		fi
	fi
	if [ "$media_checks_armed" -eq 1 ]; then
		if ! media_is_safe; then
			[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
		fi
		if ! media_writes_unchanged; then
			[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
		fi
	fi
	exit "$cleanup_rc"
}

handle_signal()
{
	signal_rc=$1
	if [ "$load_signal_deferred" -eq 1 ]; then
		if [ "$pending_signal" -eq 0 ]; then
			pending_signal=$signal_rc
		fi
		return 0
	fi
	cleanup "$signal_rc"
}

finish_deferred_signal()
{
	load_signal_deferred=0
	if [ "$pending_signal" -ne 0 ]; then
		deferred_signal_rc=$pending_signal
		pending_signal=0
		cleanup "$deferred_signal_rc"
	fi
}

trap 'cleanup $?' EXIT
trap 'handle_signal 129' HUP
trap 'handle_signal 130' INT
trap 'handle_signal 131' QUIT
trap 'handle_signal 143' TERM

for required_command in awk cat dmesg findmnt flock grep id insmod install \
	kill mkfifo mktemp modinfo modprobe readlink rm rmdir rmmod sha256sum \
	sleep stat swapon tail timeout uname wc; do
	if ! command -v "$required_command" >/dev/null 2>&1; then
		echo "required command is missing: $required_command" >&2
		exit 2
	fi
done
if [ "$(id -u)" -ne 0 ]; then
	echo "must run as root" >&2
	exit 2
fi
if [ ! -f "$module_path" ] || [ ! -f "$peer_module_path" ] ||
   [ ! -x "$tool_path" ]; then
	echo "frozen V1 module, peer fixture, or C2.5 tool is missing" >&2
	exit 2
fi
if [ "$(modinfo -F name "$module_path")" != "$module_name" ]; then
	echo "first artifact is not the frozen V1 module" >&2
	exit 1
fi
if [ "$(modinfo -F name "$peer_module_path")" != "$peer_module_name" ]; then
	echo "second artifact is not the C2.5 peer fixture" >&2
	exit 1
fi

media_checks_armed=1
if [ -L "$gate_lock_dir" ] ||
   { [ -e "$gate_lock_dir" ] && [ ! -d "$gate_lock_dir" ]; }; then
	echo "unsafe C2.5 gate lock directory: $gate_lock_dir" >&2
	exit 1
fi
if [ ! -d "$gate_lock_dir" ]; then
	if ! install -d -m 0700 -o root -g root -- "$gate_lock_dir"; then
		echo "could not create private C2.5 gate lock directory: $gate_lock_dir" >&2
		exit 1
	fi
fi
gate_lock_dir_uid=$(stat -c %u -- "$gate_lock_dir")
gate_lock_dir_mode=$(stat -c %a -- "$gate_lock_dir")
if [ -L "$gate_lock_dir" ] || [ ! -d "$gate_lock_dir" ] ||
   [ "$gate_lock_dir_uid" -ne 0 ] || [ "$gate_lock_dir_mode" != 700 ]; then
	echo "C2.5 gate lock directory must be root-owned mode 0700: path=$gate_lock_dir uid=$gate_lock_dir_uid mode=$gate_lock_dir_mode" >&2
	exit 1
fi
if [ -L "$gate_lock_path" ] ||
   { [ -e "$gate_lock_path" ] && [ ! -f "$gate_lock_path" ]; }; then
	echo "unsafe C2.5 gate lock file: $gate_lock_path" >&2
	exit 1
fi
exec 8>"$gate_lock_path"
if [ -L "$gate_lock_path" ] || [ ! -f "$gate_lock_path" ] ||
   [ "$(stat -c %u -- "$gate_lock_path")" -ne 0 ]; then
	echo "C2.5 gate lock file is not a root-owned regular file: $gate_lock_path" >&2
	exit 1
fi
if ! flock -n 8; then
	echo "another C2.4/C2.5 V1 gate owns $gate_lock_path" >&2
	exit 1
fi

for stale_module in "$h0_module_name" "$v0_module_name" "$module_name" \
	"$peer_module_name"; do
	if module_is_loaded "$stale_module"; then
		echo "stale project module exists before C2.5: $stale_module" >&2
		exit 1
	fi
done
if [ -e "$h0_platform_path" ] || [ -e "$v0_platform_path" ] ||
   [ "$(v1_platform_count)" -ne 0 ]; then
	echo "stale H0/V0/V1 platform object exists before C2.5" >&2
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
peer_module_vermagic=$(modinfo -F vermagic "$peer_module_path")
kernel_release=$(uname -r)
case "$module_vermagic" in
	"$kernel_release "*) ;;
	*)
		echo "V1 module vermagic does not match running kernel: $module_vermagic" >&2
		exit 1
		;;
esac
case "$peer_module_vermagic" in
	"$kernel_release "*) ;;
	*)
		echo "peer fixture vermagic does not match running kernel: $peer_module_vermagic" >&2
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
module_srcversion=$(modinfo -F srcversion "$module_path")
if [ "$module_srcversion" != "$expected_module_srcversion" ]; then
	echo "unexpected frozen V1 srcversion: $module_srcversion" >&2
	exit 1
fi
peer_module_sha256=$(sha256sum "$peer_module_path" | awk '{ print $1 }')
peer_module_srcversion=$(modinfo -F srcversion "$peer_module_path")
if [ -z "$peer_module_srcversion" ]; then
	echo "peer fixture srcversion is empty" >&2
	exit 1
fi
tool_sha256=$(sha256sum "$tool_path" | awk '{ print $1 }')
script_sha256=$(sha256sum "$0" | awk '{ print $1 }')
echo "phase=artifact-identity kernel_release=$kernel_release module_vermagic=$module_vermagic peer_vermagic=$peer_module_vermagic module_sha256=$module_sha256 peer_sha256=$peer_module_sha256 tool_sha256=$tool_sha256 script_sha256=$script_sha256 srcversion=$module_srcversion peer_srcversion=$peer_module_srcversion vfio_dma_rw_crc=$vfio_dma_rw_crc"

if timeout -k 2s 10s "$tool_path" --selftest; then
	:
else
	selftest_rc=$?
	echo "C2.5 pure selftest failed or timed out: rc=$selftest_rc" >&2
	exit 1
fi
echo "phase=c2.5-selftest-passed"

boot_id_before=$(cat /proc/sys/kernel/random/boot_id)
dmesg >/dev/null
dmesg_marker=$(dmesg | wc -l)
taint_before=$(cat /proc/sys/kernel/tainted)
if [ "$taint_before" -ne 0 ]; then
	echo "kernel must be untainted before the C2.5 gate: taint=$taint_before" >&2
	exit 1
fi
echo "phase=kernel-log-cursor-open boot_id=$boot_id_before line=$dmesg_marker isolation=exclusive-gate"

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

preload_module_sha256=$(sha256sum "$module_path" | awk '{ print $1 }')
if [ "$preload_module_sha256" != "$expected_module_sha256" ]; then
	echo "frozen V1 module changed immediately before insmod: $preload_module_sha256" >&2
	exit 1
fi
load_signal_deferred=1
pending_signal=0
if insmod "$module_path"; then
	module_load_rc=0
	module_owned=1
else
	module_insmod_rc=$?
	module_load_rc=$module_insmod_rc
	module_owned=0
fi
finish_deferred_signal
if [ "$module_load_rc" -ne 0 ]; then
	echo "frozen V1 insmod failed; ownership was not acquired: rc=$module_load_rc" >&2
	exit 1
fi
postload_module_sha256=$(sha256sum "$module_path" | awk '{ print $1 }')
if [ "$postload_module_sha256" != "$preload_module_sha256" ]; then
	echo "V1 module path changed across insmod: before=$preload_module_sha256 after=$postload_module_sha256" >&2
	exit 1
fi
if ! loaded_module_matches_artifact; then
	exit 1
fi
if [ ! -d "$base_platform_path" ] || [ ! -L "$base_platform_path/driver" ] ||
   [ "$(v1_platform_count)" -ne 1 ]; then
	echo "frozen V1 did not create exactly its base platform device" >&2
	exit 1
fi
base_driver_target=$(readlink "$base_platform_path/driver")
if [ "${base_driver_target##*/}" != "$platform_name" ]; then
	echo "base platform device is bound to the wrong driver: $base_driver_target" >&2
	exit 1
fi
base_vfio_name=$(discover_single_cdev "$base_platform_path")
if ! wait_cdev_node "$base_vfio_name"; then
	echo "base VFIO cdev node was not created: $base_vfio_name" >&2
	exit 1
fi
base_vfio_rdev=$(stat -Lc '%t:%T' -- "/dev/vfio/devices/$base_vfio_name")
echo "phase=base-cdev-ready sysfs=$base_platform_path node=/dev/vfio/devices/$base_vfio_name"

preload_peer_sha256=$(sha256sum "$peer_module_path" | awk '{ print $1 }')
if [ "$preload_peer_sha256" != "$peer_module_sha256" ]; then
	echo "peer fixture changed immediately before insmod: $preload_peer_sha256" >&2
	exit 1
fi
load_signal_deferred=1
pending_signal=0
if insmod "$peer_module_path"; then
	peer_load_rc=0
	peer_owned=1
else
	peer_insmod_rc=$?
	peer_load_rc=$peer_insmod_rc
	peer_owned=0
fi
finish_deferred_signal
if [ "$peer_load_rc" -ne 0 ]; then
	echo "peer fixture insmod failed; ownership was not acquired: rc=$peer_load_rc" >&2
	exit 1
fi
postload_peer_sha256=$(sha256sum "$peer_module_path" | awk '{ print $1 }')
if [ "$postload_peer_sha256" != "$preload_peer_sha256" ]; then
	echo "peer fixture path changed across insmod: before=$preload_peer_sha256 after=$postload_peer_sha256" >&2
	exit 1
fi
if ! loaded_peer_matches_artifact; then
	exit 1
fi
if [ ! -d "$peer_platform_path" ] || [ ! -L "$peer_platform_path/driver" ] ||
   [ "$(v1_platform_count)" -ne 2 ]; then
	echo "peer fixture did not create exactly one additional V1 platform device" >&2
	exit 1
fi
peer_driver_target=$(readlink "$peer_platform_path/driver")
if [ "${peer_driver_target##*/}" != "$platform_name" ]; then
	echo "peer platform device is bound to the wrong driver: $peer_driver_target" >&2
	exit 1
fi
peer_vfio_name=$(discover_single_cdev "$peer_platform_path")
if ! wait_cdev_node "$peer_vfio_name"; then
	echo "peer VFIO cdev node was not created: $peer_vfio_name" >&2
	exit 1
fi
if [ "$base_vfio_name" = "$peer_vfio_name" ]; then
	echo "base and peer resolved to the same VFIO cdev: $base_vfio_name" >&2
	exit 1
fi
peer_vfio_rdev=$(stat -Lc '%t:%T' -- "/dev/vfio/devices/$peer_vfio_name")
if [ "$base_vfio_rdev" = "$peer_vfio_rdev" ]; then
	echo "base and peer have the same character-device identity: $base_vfio_rdev" >&2
	exit 1
fi
echo "phase=two-cdev-topology base_sysfs=$base_platform_path base_cdev=/dev/vfio/devices/$base_vfio_name peer_sysfs=$peer_platform_path peer_cdev=/dev/vfio/devices/$peer_vfio_name"

if module_is_loaded "$h0_module_name" || module_is_loaded "$v0_module_name" ||
   [ -e "$h0_platform_path" ] || [ -e "$v0_platform_path" ]; then
	echo "H0 or V0 appeared during the isolated V1 gate" >&2
	exit 1
fi
current_tool_sha256=$(sha256sum "$tool_path" | awk '{ print $1 }')
if [ "$current_tool_sha256" != "$tool_sha256" ]; then
	echo "C2.5 tool changed before the main oracle: $current_tool_sha256" >&2
	exit 1
fi
set +e
timeout -k 5s 240s "$tool_path" \
	"/dev/vfio/devices/$base_vfio_name" \
	"/dev/vfio/devices/$peer_vfio_name"
tool_rc=$?
set -e
if [ "$tool_rc" -ne 0 ]; then
	echo "C2.5 two-instance oracle failed or timed out: rc=$tool_rc" >&2
	exit 1
fi
echo "phase=c2.5-two-instance-oracle-passed"

if ! peer_open_count=$(cdev_open_count "/dev/vfio/devices/$peer_vfio_name"); then
	echo "could not scan peer cdev users after the main oracle" >&2
	exit 1
fi
if [ "$peer_open_count" -ne 0 ]; then
	echo "peer cdev still has an open fd after the main oracle" >&2
	exit 1
fi
current_tool_sha256=$(sha256sum "$tool_path" | awk '{ print $1 }')
if [ "$current_tool_sha256" != "$tool_sha256" ]; then
	echo "C2.5 tool changed before the survivor oracle: $current_tool_sha256" >&2
	exit 1
fi
hold_dir=$(mktemp -d /tmp/ssd-fwlab-c25-survivor.XXXXXX)
hold_fifo=$hold_dir/control
hold_log=$hold_dir/helper.log
mkfifo "$hold_fifo"
exec 9<>"$hold_fifo"
hold_writer_open=1
"$tool_path" --hold-survivor "/dev/vfio/devices/$base_vfio_name" \
	9>&- <"$hold_fifo" >"$hold_log" 2>&1 &
hold_pid=$!
count=0
while ! grep -Fxq 'C2.5 survivor READY' "$hold_log" 2>/dev/null &&
	[ "$count" -lt 50 ]; do
	if ! kill -0 "$hold_pid" 2>/dev/null; then
		cat "$hold_log" >&2
		echo "survivor helper exited before READY" >&2
		exit 1
	fi
	count=$((count + 1))
	sleep 0.1
done
if ! grep -Fxq 'C2.5 survivor READY' "$hold_log"; then
	echo "survivor helper did not become ready" >&2
	exit 1
fi
if ! base_open_count=$(cdev_open_count "/dev/vfio/devices/$base_vfio_name"); then
	echo "could not scan base cdev users for the survivor helper" >&2
	exit 1
fi
if [ "$base_open_count" -lt 1 ]; then
	echo "survivor helper did not retain the base cdev" >&2
	exit 1
fi
base_module_refcnt=$(cat "/sys/module/$module_name/refcnt")
if [ "$base_module_refcnt" -lt 1 ]; then
	echo "live base cdev did not retain the frozen V1 module: refcnt=$base_module_refcnt" >&2
	exit 1
fi
if ! peer_open_count=$(cdev_open_count "/dev/vfio/devices/$peer_vfio_name"); then
	echo "could not rescan peer cdev users before fixture removal" >&2
	exit 1
fi
if [ "$peer_open_count" -ne 0 ]; then
	echo "peer cdev is still open before fixture removal" >&2
	exit 1
fi
if [ "$(cat "/sys/module/$peer_module_name/refcnt")" -ne 0 ]; then
	echo "peer fixture module has an unexpected live reference" >&2
	exit 1
fi
current_base_rdev=$(stat -Lc '%t:%T' -- "/dev/vfio/devices/$base_vfio_name")
current_peer_rdev=$(stat -Lc '%t:%T' -- "/dev/vfio/devices/$peer_vfio_name")
if [ "$current_base_rdev" != "$base_vfio_rdev" ] ||
   [ "$current_peer_rdev" != "$peer_vfio_rdev" ]; then
	echo "base or peer cdev identity changed before fixture removal" >&2
	exit 1
fi
base_driver_target=$(readlink "$base_platform_path/driver")
peer_driver_target=$(readlink "$peer_platform_path/driver")
if [ "${base_driver_target##*/}" != "$platform_name" ] ||
   [ "${peer_driver_target##*/}" != "$platform_name" ]; then
	echo "base or peer driver binding changed before fixture removal" >&2
	exit 1
fi
echo "phase=survivor-ready base_open=yes peer_open=no"

if timeout -k 2s 5s rmmod "$peer_module_name"; then
	:
else
	peer_rmmod_rc=$?
	echo "bounded peer fixture rmmod failed: module=$peer_module_name rc=$peer_rmmod_rc" >&2
	exit 1
fi
peer_owned=0
count=0
while { [ -e "$peer_platform_path" ] ||
	[ -e "/dev/vfio/devices/$peer_vfio_name" ]; } && [ "$count" -lt 50 ]; do
	count=$((count + 1))
	sleep 0.1
done
if module_is_loaded "$peer_module_name" || [ -e "$peer_platform_path" ] ||
   [ -e "/dev/vfio/devices/$peer_vfio_name" ]; then
	echo "peer fixture/platform/cdev did not disappear within 5 seconds" >&2
	exit 1
fi
if ! module_is_loaded "$module_name" || [ ! -e "$base_platform_path" ] ||
   [ ! -c "/dev/vfio/devices/$base_vfio_name" ] ||
   ! hold_helper_running; then
	echo "peer removal disturbed the live base survivor" >&2
	exit 1
fi
base_module_refcnt=$(cat "/sys/module/$module_name/refcnt")
if [ "$base_module_refcnt" -lt 1 ]; then
	echo "peer removal lost the live frozen V1 module reference: refcnt=$base_module_refcnt" >&2
	exit 1
fi
if [ "$(v1_platform_count)" -ne 1 ]; then
	echo "peer removal did not leave exactly one base platform instance" >&2
	exit 1
fi
echo "phase=peer-removed-base-survives"

if ! printf 'x' >&9; then
	echo "could not release survivor helper with one byte" >&2
	exit 1
fi
exec 9>&-
hold_writer_open=0
if ! wait_hold_helper_bounded 100; then
	echo "survivor helper did not complete its post-removal copy" >&2
	exit 1
fi
if [ "$hold_rc" -ne 0 ]; then
	cat "$hold_log" >&2
	echo "survivor helper failed after peer removal: rc=$hold_rc" >&2
	exit 1
fi
cat "$hold_log"
echo "phase=post-peer-removal-copy-passed"
if ! remove_hold_files || ! hold_resources_absent; then
	echo "survivor temporary resources were not completely removed" >&2
	exit 1
fi

if timeout -k 2s 5s rmmod "$module_name"; then
	:
else
	final_rmmod_rc=$?
	echo "bounded final V1 rmmod failed: module=$module_name rc=$final_rmmod_rc" >&2
	exit 1
fi
module_owned=0
count=0
while { [ -e "$base_platform_path" ] ||
	[ -e "/dev/vfio/devices/$base_vfio_name" ]; } && [ "$count" -lt 50 ]; do
	count=$((count + 1))
	sleep 0.1
done
if module_is_loaded "$module_name" || [ -e "$base_platform_path" ] ||
   [ -e "/dev/vfio/devices/$base_vfio_name" ] ||
   [ "$(v1_platform_count)" -ne 0 ]; then
	echo "V1 module/platform/cdev did not disappear within 5 seconds" >&2
	exit 1
fi
echo "phase=peer-then-v1-unload-passed"

postrun_peer_sha256=$(sha256sum "$peer_module_path" | awk '{ print $1 }')
if [ "$postrun_peer_sha256" != "$peer_module_sha256" ]; then
	echo "peer fixture artifact changed across the run: before=$peer_module_sha256 after=$postrun_peer_sha256" >&2
	exit 1
fi
if ! media_is_safe || ! media_writes_unchanged; then
	exit 1
fi
for residual_module in "$h0_module_name" "$v0_module_name" "$module_name" \
	"$peer_module_name"; do
	if module_is_loaded "$residual_module"; then
		echo "project module remains after C2.5: $residual_module" >&2
		exit 1
	fi
done
if [ -e "$h0_platform_path" ] || [ -e "$v0_platform_path" ] ||
   [ -e "$base_platform_path" ] || [ -e "$peer_platform_path" ]; then
	echo "project platform object remains after C2.5" >&2
	exit 1
fi

taint_after=$(cat /proc/sys/kernel/tainted)
allowed_taint=$((taint_before | 4096 | 8192))
unexpected_taint=$((taint_after & ~allowed_taint))
if [ "$unexpected_taint" -ne 0 ]; then
	echo "unexpected kernel taint: before=$taint_before after=$taint_after" >&2
	exit 1
fi
boot_id_after=$(cat /proc/sys/kernel/random/boot_id)
if [ "$boot_id_after" != "$boot_id_before" ]; then
	echo "boot ID changed during C2.5 gate: before=$boot_id_before after=$boot_id_after" >&2
	exit 1
fi
dmesg >/dev/null
dmesg_end=$(dmesg | wc -l)
if [ "$dmesg_end" -lt "$dmesg_marker" ]; then
	echo "kernel log cursor moved backwards: start=$dmesg_marker end=$dmesg_end" >&2
	exit 1
fi
echo "phase=kernel-log-cursor-close boot_id=$boot_id_after start=$dmesg_marker end=$dmesg_end"
new_log=$(dmesg | tail -n "+$((dmesg_marker + 1))")
printf '%s\n' "$new_log"
if printf '%s\n' "$new_log" | grep -Eiq \
	'engine close failed|engine detach rejected|detach commit failed|generation exhausted'; then
	echo "forbidden V1 engine lifecycle diagnostic detected" >&2
	exit 1
fi
if printf '%s\n' "$new_log" | grep -Eiq \
	'BUG:|kernel BUG at|WARNING:|Oops:|KASAN:|UBSAN:|KFENCE:|lockdep|refcount_t:|use-after-free|scheduling while atomic|general protection fault|kernel NULL pointer|hung task|hung_task|blocked for more than|soft lockup|hard lockup|RCU.*stall'; then
	echo "fatal kernel diagnostic detected" >&2
	exit 1
fi
if ! hold_resources_absent; then
	echo "survivor resources reappeared before final PASS" >&2
	exit 1
fi

echo "phase=final-health taint_before=$taint_before taint_after=$taint_after media_writes=$media_write_before boot_id=$boot_id_after"
echo "C2.5 V1 two-instance and survivor-removal privileged gate: PASS"
