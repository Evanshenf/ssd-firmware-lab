#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output=${1:-$script_dir/../build/fwlab-l2-initramfs.cpio.gz}
kernel_release=${2:-$(uname -r)}
root=$(mktemp -d /tmp/fwlab-l2-initramfs.XXXXXX)

cleanup()
{
	rc=$?
	trap - EXIT INT TERM
	rm -rf "$root"
	exit "$rc"
}
trap cleanup EXIT INT TERM

test -x /usr/bin/busybox
test -r "/lib/modules/$kernel_release/modules.dep"
mkdir -p "$root/bin" "$root/dev" "$root/modules" "$root/proc" \
	"$root/sys" "$root/tmp" "$(dirname -- "$output")"
cp /usr/bin/busybox "$root/bin/busybox"
ln -s busybox "$root/bin/sh"
for applet in cat cmp dd head insmod mount poweroff sleep sync yes; do
	ln -s busybox "$root/bin/$applet"
done
cp "$script_dir/l2-init" "$root/init"
chmod 0755 "$root/init"

index=0
modprobe --set-version "$kernel_release" --show-depends nvme |
while read -r action module; do
	[ "$action" = insmod ] || continue
	index=$((index + 1))
	base=$(basename -- "$module")
	case "$base" in
		*.zst)
			base=${base%.zst}
			zstd -q -d -c "$module" > \
				"$root/modules/$(printf '%02d' "$index")-$base"
			;;
		*.xz)
			base=${base%.xz}
			xz -d -c "$module" > \
				"$root/modules/$(printf '%02d' "$index")-$base"
			;;
		*.gz)
			base=${base%.gz}
			gzip -d -c "$module" > \
				"$root/modules/$(printf '%02d' "$index")-$base"
			;;
		*) cp "$module" "$root/modules/$(printf '%02d' "$index")-$base" ;;
	esac
done

(cd "$root" && find . -print0 | cpio --null -o --format=newc --quiet) |
	gzip -9 > "$output"
echo "$output"
