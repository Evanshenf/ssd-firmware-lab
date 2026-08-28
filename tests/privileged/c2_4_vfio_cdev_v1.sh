#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

set -eu

module_path=${1:-kernel/vfio-cdev-v1/ssd_fwlab_vfio_v1.ko}
tool_path=${2:-tools/vfio-cdev-v1-c24/build/vfio_cdev_v1_c24}
module_name=ssd_fwlab_vfio_v1
platform_name=ssd-fwlab-vfio-v1
platform_path=/sys/bus/platform/devices/$platform_name
media_device=/dev/sdb
media_name=${media_device##*/}
expected_vfio_dma_rw_crc=0xaa22e02a
expected_module_sha256=8c9f1418dcdce7e0ebbd18d97adcc9e11a7a9f7e2bfe5b565f18f8db8714b072
expected_duplicate_log='engine detach rejected: -22; forcing VFIO detach'
module_owned=0
load_armed=0
media_checks_armed=0
vfio_name=
media_write_before=
module_srcversion=
hold_pid=
hold_dir=
hold_fifo=
hold_log=
hold_rmmod_log=
hold_writer_open=0
hold_reaped=0
hold_rc=
gate_lock_dir=/run/ssd-fwlab
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
	grep -q "^${module_name} " /proc/modules
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
		# The bounded poll observed exit or Z state, so this wait only reaps.
		if wait "$hold_pid"; then
			hold_rc=0
		else
			hold_rc=$?
		fi
		hold_reaped=1
	fi
	return 0
}

release_hold_helper()
{
	if [ "$hold_writer_open" -eq 1 ]; then
		exec 9>&-
		hold_writer_open=0
	fi
	if [ -z "$hold_pid" ] || [ "$hold_reaped" -eq 1 ]; then
		return 0
	fi
	if wait_hold_helper_bounded 50; then
		return 0
	fi
	if ! kill -TERM "$hold_pid" 2>/dev/null && hold_helper_running; then
		echo "could not send TERM to hold-open helper: pid=$hold_pid" >&2
	fi
	if wait_hold_helper_bounded 20; then
		return 0
	fi
	if ! kill -KILL "$hold_pid" 2>/dev/null && hold_helper_running; then
		echo "could not send KILL to hold-open helper: pid=$hold_pid" >&2
	fi
	if wait_hold_helper_bounded 20; then
		return 0
	fi
	echo "hold-open helper did not exit after bounded TERM/KILL: pid=$hold_pid" >&2
	return 1
}

remove_hold_files()
{
	remove_rc=0
	for hold_path in "$hold_fifo" "$hold_log" "$hold_rmmod_log"; do
		[ -n "$hold_path" ] || continue
		if { [ -e "$hold_path" ] || [ -L "$hold_path" ]; } &&
		   ! rm -f -- "$hold_path"; then
			echo "failed to remove hold-open temporary file: $hold_path" >&2
			remove_rc=1
		fi
	done
	if [ -n "$hold_dir" ] &&
	   { [ -e "$hold_dir" ] || [ -L "$hold_dir" ]; }; then
		if ! rmdir "$hold_dir"; then
			echo "failed to remove hold-open temporary directory: $hold_dir" >&2
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
		echo "hold-open helper remains live: pid=$hold_pid" >&2
		absent_rc=1
	fi
	for hold_path in "$hold_fifo" "$hold_log" "$hold_rmmod_log"; do
		[ -n "$hold_path" ] || continue
		if [ -e "$hold_path" ] || [ -L "$hold_path" ]; then
			echo "hold-open temporary file remains: $hold_path" >&2
			absent_rc=1
		fi
	done
	if [ -n "$hold_dir" ] &&
	   { [ -e "$hold_dir" ] || [ -L "$hold_dir" ]; }; then
		echo "hold-open temporary directory remains: $hold_dir" >&2
		absent_rc=1
	fi
	return "$absent_rc"
}

cleanup()
{
	cleanup_rc=$1
	cleanup_scope=0
	cleanup_can_unload=0
	trap - EXIT HUP INT TERM
	if ! release_hold_helper; then
		[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
	fi
	if ! remove_hold_files; then
		[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
	fi
	if ! hold_resources_absent; then
		[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
	fi

	if [ "$module_owned" -eq 1 ]; then
		if module_is_loaded; then
			if loaded_module_matches_artifact; then
				cleanup_scope=1
				cleanup_can_unload=1
			else
				module_owned=0
				[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
			fi
		else
			cleanup_scope=1
		fi
	elif [ "$load_armed" -eq 1 ]; then
		if module_is_loaded; then
			if loaded_module_matches_artifact; then
				module_owned=1
				cleanup_scope=1
				cleanup_can_unload=1
			else
				[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
			fi
		else
			cleanup_scope=1
		fi
	fi
	load_armed=0
	if [ "$cleanup_can_unload" -eq 1 ]; then
		if timeout -k 2s 5s rmmod "$module_name"; then
			:
		else
			cleanup_rmmod_rc=$?
			echo "bounded cleanup rmmod failed: module=$module_name rc=$cleanup_rmmod_rc" >&2
			[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
		fi
	fi
	if [ "$cleanup_scope" -eq 1 ]; then
		count=0
		while { [ -e "$platform_path" ] ||
			{ [ -n "$vfio_name" ] &&
			  [ -e "/dev/vfio/devices/$vfio_name" ]; }; } &&
		      [ "$count" -lt 50 ]; do
			count=$((count + 1))
			sleep 0.1
		done
		if module_is_loaded; then
			echo "module remains loaded after cleanup" >&2
			[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
		else
			module_owned=0
		fi
		if [ -e "$platform_path" ]; then
			echo "platform device remains after cleanup" >&2
			[ "$cleanup_rc" -ne 0 ] || cleanup_rc=1
		fi
		if [ -n "$vfio_name" ] &&
		   [ -e "/dev/vfio/devices/$vfio_name" ]; then
			echo "VFIO cdev remains after cleanup: $vfio_name" >&2
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

trap 'cleanup $?' EXIT
trap 'cleanup 129' HUP
trap 'cleanup 130' INT
trap 'cleanup 143' TERM

for required_command in awk cat dmesg findmnt flock grep id insmod install \
	kill mkfifo mktemp modinfo modprobe rm rmdir rmmod sha256sum sleep stat \
	swapon tail timeout uname wc; do
	if ! command -v "$required_command" >/dev/null 2>&1; then
		echo "required command is missing: $required_command" >&2
		exit 2
	fi
done
if [ "$(id -u)" -ne 0 ]; then
	echo "must run as root" >&2
	exit 2
fi
media_checks_armed=1
if [ -L "$gate_lock_dir" ] ||
   { [ -e "$gate_lock_dir" ] && [ ! -d "$gate_lock_dir" ]; }; then
	echo "unsafe C2.4 gate lock directory: $gate_lock_dir" >&2
	exit 1
fi
if [ ! -d "$gate_lock_dir" ]; then
	if ! install -d -m 0700 -o root -g root -- "$gate_lock_dir"; then
		echo "could not create private C2.4 gate lock directory: $gate_lock_dir" >&2
		exit 1
	fi
fi
gate_lock_dir_uid=$(stat -c %u -- "$gate_lock_dir")
gate_lock_dir_mode=$(stat -c %a -- "$gate_lock_dir")
if [ -L "$gate_lock_dir" ] || [ ! -d "$gate_lock_dir" ] ||
   [ "$gate_lock_dir_uid" -ne 0 ] || [ "$gate_lock_dir_mode" != 700 ]; then
	echo "C2.4 gate lock directory must be root-owned mode 0700: path=$gate_lock_dir uid=$gate_lock_dir_uid mode=$gate_lock_dir_mode" >&2
	exit 1
fi
if [ -L "$gate_lock_path" ] ||
   { [ -e "$gate_lock_path" ] && [ ! -f "$gate_lock_path" ]; }; then
	echo "unsafe C2.4 gate lock file: $gate_lock_path" >&2
	exit 1
fi
# The verified root-only directory prevents an unprivileged path swap between
# this last type check and the shell redirection/open.
exec 8>"$gate_lock_path"
if [ -L "$gate_lock_path" ] || [ ! -f "$gate_lock_path" ] ||
   [ "$(stat -c %u -- "$gate_lock_path")" -ne 0 ]; then
	echo "C2.4 gate lock file is not a root-owned regular file: $gate_lock_path" >&2
	exit 1
fi
if ! flock -n 8; then
	echo "another C2.4 privileged gate owns $gate_lock_path" >&2
	exit 1
fi
if [ ! -f "$module_path" ] || [ ! -x "$tool_path" ]; then
	echo "module or C2.4 tool missing" >&2
	exit 2
fi
if module_is_loaded || [ -e "$platform_path" ]; then
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
if [ -z "$module_srcversion" ]; then
	echo "module srcversion is empty" >&2
	exit 1
fi
echo "phase=artifact-identity module_sha256=$module_sha256 tool_sha256=$tool_sha256 script_sha256=$script_sha256 srcversion=$module_srcversion vfio_dma_rw_crc=$vfio_dma_rw_crc"
if timeout -k 2s 10s "$tool_path" --selftest; then
	:
else
	selftest_rc=$?
	echo "C2.4 pure LE/record selftest failed or timed out: rc=$selftest_rc" >&2
	exit 1
fi
echo "phase=c2.4-selftest-passed"

boot_id_before=$(cat /proc/sys/kernel/random/boot_id)
dmesg >/dev/null
dmesg_marker=$(dmesg | wc -l)
taint_before=$(cat /proc/sys/kernel/tainted)
if [ "$taint_before" -ne 0 ]; then
	echo "kernel must be untainted before the C2.4 gate: taint=$taint_before" >&2
	exit 1
fi
# P1 scope: the exclusive gate and stable boot ID bound this line cursor; the
# wrapper intentionally does not claim journal-grade isolation from other logs.
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
	echo "module changed immediately before insmod: $preload_module_sha256" >&2
	exit 1
fi
load_armed=1
insmod "$module_path"
postload_module_sha256=$(sha256sum "$module_path" | awk '{ print $1 }')
if [ "$postload_module_sha256" != "$preload_module_sha256" ]; then
	echo "module path changed across insmod: before=$preload_module_sha256 after=$postload_module_sha256" >&2
	exit 1
fi
if ! loaded_module_matches_artifact; then
	exit 1
fi
module_owned=1
load_armed=0
echo "phase=module-loaded srcversion=$loaded_srcversion path_sha256=$postload_module_sha256"

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

hold_dir=$(mktemp -d /tmp/ssd-fwlab-c24-hold.XXXXXX)
hold_fifo=$hold_dir/control
hold_log=$hold_dir/helper.log
hold_rmmod_log=$hold_dir/rmmod.log
mkfifo "$hold_fifo"
exec 9<>"$hold_fifo"
hold_writer_open=1
"$tool_path" --hold-open "/dev/vfio/devices/$vfio_name" \
	9>&- <"$hold_fifo" >"$hold_log" 2>&1 &
hold_pid=$!
count=0
while ! grep -Fxq 'C2.4 hold-open READY' "$hold_log" 2>/dev/null &&
	[ "$count" -lt 50 ]; do
	if ! kill -0 "$hold_pid" 2>/dev/null; then
		cat "$hold_log" >&2
		echo "hold-open helper exited before READY" >&2
		exit 1
	fi
	count=$((count + 1))
	sleep 0.1
done
if ! grep -Fxq 'C2.4 hold-open READY' "$hold_log"; then
	echo "hold-open helper did not become ready" >&2
	exit 1
fi
module_refcnt=$(cat "/sys/module/$module_name/refcnt")
if [ "$module_refcnt" -lt 1 ]; then
	echo "open cdev did not hold a module reference: $module_refcnt" >&2
	exit 1
fi
set +e
timeout -k 2s 5s rmmod "$module_name" >"$hold_rmmod_log" 2>&1
blocked_rmmod_rc=$?
set -e
if [ "$blocked_rmmod_rc" -eq 0 ] || [ "$blocked_rmmod_rc" -ge 124 ]; then
	cat "$hold_rmmod_log" >&2
	echo "ordinary rmmod did not return a normal kmod failure while cdev was open: rc=$blocked_rmmod_rc" >&2
	exit 1
fi
if ! grep -Fqi 'is in use' "$hold_rmmod_log"; then
	cat "$hold_rmmod_log" >&2
	echo "blocked rmmod did not report the expected in-use error" >&2
	exit 1
fi
if ! module_is_loaded ||
	[ ! -e "$platform_path" ] ||
	[ ! -c "/dev/vfio/devices/$vfio_name" ]; then
	echo "failed rmmod disturbed live module/platform/cdev state" >&2
	exit 1
fi
module_refcnt_after_block=$(cat "/sys/module/$module_name/refcnt")
if [ "$module_refcnt_after_block" -lt 1 ]; then
	echo "failed rmmod lost the live module reference: $module_refcnt_after_block" >&2
	exit 1
fi
echo "phase=open-owner-blocked-rmmod rc=$blocked_rmmod_rc refcnt=$module_refcnt_after_block error=in-use"

exec 9>&-
hold_writer_open=0
if ! release_hold_helper; then
	echo "hold-open helper could not be released within its bounded deadline" >&2
	exit 1
fi
if [ "$hold_rc" -ne 0 ]; then
	cat "$hold_log" >&2
	echo "hold-open helper failed during serial release: rc=$hold_rc" >&2
	exit 1
fi
module_refcnt=$(cat "/sys/module/$module_name/refcnt")
if [ "$module_refcnt" -ne 0 ]; then
	echo "module reference did not return to zero: $module_refcnt" >&2
	exit 1
fi
echo "phase=open-owner-released refcnt=$module_refcnt"
if ! remove_hold_files || ! hold_resources_absent; then
	echo "hold-open temporary resources were not completely removed" >&2
	exit 1
fi

set +e
timeout -k 5s 180s "$tool_path" "/dev/vfio/devices/$vfio_name"
tool_rc=$?
set -e
if [ "$tool_rc" -ne 0 ]; then
	echo "C2.4 lifecycle/race oracle failed or timed out: rc=$tool_rc" >&2
	exit 1
fi
echo "phase=c2.4-userspace-passed"

if timeout -k 2s 5s rmmod "$module_name"; then
	:
else
	final_rmmod_rc=$?
	echo "bounded final rmmod failed: module=$module_name rc=$final_rmmod_rc" >&2
	exit 1
fi
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
if module_is_loaded; then
	echo "V1 module remains loaded after final rmmod" >&2
	exit 1
fi
module_owned=0
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

boot_id_after=$(cat /proc/sys/kernel/random/boot_id)
if [ "$boot_id_after" != "$boot_id_before" ]; then
	echo "boot ID changed during C2.4 gate: before=$boot_id_before after=$boot_id_after" >&2
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
duplicate_log_count=$(printf '%s\n' "$new_log" | awk -v exact="$expected_duplicate_log" '
	index($0, exact) > 0 &&
	index($0, exact) == length($0) - length(exact) + 1 { count++ }
	END { print count + 0 }')
detach_rejected_count=$(printf '%s\n' "$new_log" |
	grep -F -c 'engine detach rejected:' || true)
if [ "$duplicate_log_count" -ne 1 ] || [ "$detach_rejected_count" -ne 1 ]; then
	echo "expected one exact frozen-adapter duplicate-detach diagnostic; exact=$duplicate_log_count all_detach_rejected=$detach_rejected_count" >&2
	exit 1
fi
if printf '%s\n' "$new_log" | grep -Eiq \
	'engine close failed|detach commit failed|generation exhausted'; then
	echo "forbidden engine close/detach-commit/generation diagnostic detected" >&2
	exit 1
fi
echo "phase=expected-frozen-adapter-diagnostic count=$duplicate_log_count errno=-22"
if printf '%s\n' "$new_log" | grep -Eiq \
	'BUG:|kernel BUG at|WARNING:|Oops:|KASAN:|UBSAN:|KFENCE:|lockdep|refcount_t:|use-after-free|scheduling while atomic|general protection fault|kernel NULL pointer|hung task|hung_task|blocked for more than|soft lockup|hard lockup|RCU.*stall'; then
	echo "fatal kernel diagnostic detected" >&2
	exit 1
fi
if ! hold_resources_absent; then
	echo "hold-open resources reappeared before final PASS" >&2
	exit 1
fi

echo "C2.4 V1 lifecycle, unload-open, and bounded real-race privileged gate: PASS"
