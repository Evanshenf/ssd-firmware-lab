#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

fwlab_require_profile_ack()
{
	expected=I_ACCEPT_DESTRUCTIVE_PROFILE_NESTED_POC
	if [ "${FWLAB_POC_ACK:-}" != "$expected" ]; then
		echo "refusing privileged PoC without FWLAB_POC_ACK=$expected" >&2
		exit 2
	fi
	if [ "$(uname -r)" != 7.0.0-30-generic ]; then
		echo "refusing runtime outside pinned kernel 7.0.0-30-generic" >&2
		exit 2
	fi
}

fwlab_require_exact_poc_bdf()
{
	bdf=$1
	found=
	count=0
	for candidate in /sys/bus/pci/devices/*; do
		[ -f "$candidate/vendor" ] || continue
		[ "$(cat "$candidate/vendor")" = 0xfffa ] || continue
		[ "$(cat "$candidate/device")" = 0x0002 ] || continue
		found=${candidate##*/}
		count=$((count + 1))
	done
	case "$bdf" in
		7[0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]:00:00.0) ;;
		*) echo "unexpected PoC BDF: $bdf" >&2; exit 2 ;;
	esac
	if [ "$count" -ne 1 ] || [ "$found" != "$bdf" ]; then
		echo "PoC function identity is ambiguous" >&2
		exit 2
	fi
}
