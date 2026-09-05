#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

# Uses installed packages as runtime artifacts; no third-party code is vendored.
set -eu
[ "$#" = 1 ] || [ "$#" = 2 ] || {
    echo "usage: $0 NEW_OUTPUT.cpio.gz [STATIC_NATIVE_CLIENT]" >&2; exit 2;
}
output=$1
[ ! -e "$output" ] && [ ! -L "$output" ] || {
    echo "refusing existing output: $output" >&2; exit 2;
}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
kernel_release=$(uname -r)
stage=$(mktemp -d /var/tmp/fwlab-j2-initramfs.XXXXXX)
case "$stage" in /var/tmp/fwlab-j2-initramfs.*) ;; *) exit 2 ;; esac
cleanup()
{
    status=$?
    trap - EXIT INT TERM
    rm -rf -- "$stage"
    exit "$status"
}
trap cleanup EXIT INT TERM

test -x /usr/bin/busybox
test -r "/lib/modules/$kernel_release/modules.dep"
mkdir -p "$stage/bin" "$stage/dev" "$stage/modules" "$stage/proc" \
    "$stage/sys" "$stage/tmp"
cp /usr/bin/busybox "$stage/bin/busybox"
if [ "$#" = 2 ]; then
    test -x "$2"
    cp "$2" "$stage/bin/j1_native_io"
fi
ln -s busybox "$stage/bin/sh"
for applet in cat insmod mount poweroff sleep uname; do
    ln -s busybox "$stage/bin/$applet"
done
cp "$script_dir/l2-init" "$stage/init"
chmod 0755 "$stage/init"

# The dependency file is a generated package-tool output, not an instruction.
modprobe --set-version "$kernel_release" --show-depends nvme > "$stage/dependencies"
index=0
while read -r action module; do
    [ "$action" = insmod ] || continue
    index=$((index + 1))
    name=$(basename -- "$module")
    number=$(printf '%02d' "$index")
    case "$name" in
        *.zst) zstd -q -d -c "$module" > "$stage/modules/$number-${name%.zst}" ;;
        *.xz) xz -d -c "$module" > "$stage/modules/$number-${name%.xz}" ;;
        *.gz) gzip -d -c "$module" > "$stage/modules/$number-${name%.gz}" ;;
        *) cp "$module" "$stage/modules/$number-$name" ;;
    esac
done < "$stage/dependencies"
[ "$index" -gt 0 ] || { echo "no nvme modules resolved" >&2; exit 1; }

# Separate producers preserve errors without relying on a pipeline's last code.
(cd "$stage" && find . -print0 > entries)
(cd "$stage" && cpio --null -o --format=newc --quiet < entries) > "$stage/archive.cpio"
(set -C; gzip -9 -c "$stage/archive.cpio" > "$output")
printf 'J2_INITRAMFS_BUILT kernel=%s modules=%s output=%s\n' "$kernel_release" "$index" "$output"
