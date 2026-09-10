#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

# Current semantic execution, not a re-approval of historical leaf manifests.
# Explicit targets retain the archives/objects used as real digest arguments.
set -eu
cd "$(dirname "$0")/.."
spine_cc=${CC:-cc}
spine_started=$(date +%s)
export FWLAB_TEST_MEDIA_DIR="${FWLAB_TEST_MEDIA_DIR:-/run/fwlab-test-media}"

# The selected scaled/POSIX test images live in this bounded mount. Executables
# and logs remain on the checkout/runner filesystem; there is no disk fallback.
# Historical J0 fixtures retain their original, separately scoped byte stores.
if [ "$(id -u)" -eq 0 ]; then
  echo 'CURRENT_SPINE_PREFLIGHT_ERROR|run_tests_as_ordinary_user' >&2
  exit 1
fi
case "$FWLAB_TEST_MEDIA_DIR" in
  /*) ;;
  *) echo 'CURRENT_SPINE_PREFLIGHT_ERROR|absolute_media_directory_required' >&2; exit 1 ;;
esac
if [ ! -d "$FWLAB_TEST_MEDIA_DIR" ] || [ ! -w "$FWLAB_TEST_MEDIA_DIR" ]; then
  echo 'CURRENT_SPINE_PREFLIGHT_ERROR|missing_or_unwritable_media_directory|no_disk_fallback=1' >&2
  exit 1
fi
# stat emits filesystem type, block size, total blocks and available blocks.
read -r spine_type spine_block_bytes spine_total_blocks spine_free_blocks <<EOF
$(stat -f -c '%T %S %b %a' -- "$FWLAB_TEST_MEDIA_DIR")
EOF
if [ "$spine_type" != tmpfs ] ||
   [ "$((spine_block_bytes * spine_total_blocks))" -gt 1073741824 ] ||
   [ "$((spine_block_bytes * spine_free_blocks))" -lt 268435456 ]; then
  echo 'CURRENT_SPINE_PREFLIGHT_ERROR|tmpfs_cap_1GiB_and_free_256MiB_required|no_disk_fallback=1' >&2
  exit 1
fi
stage() {
  printf 'CURRENT_SPINE_STAGE|name=%s|elapsed_seconds=%s|scaled_medium=tmpfs\n' \
    "$1" "$(( $(date +%s) - spine_started ))"
  df -B1 -- "$FWLAB_TEST_MEDIA_DIR"
  awk '/^MemAvailable:/ { print "CURRENT_SPINE_MEMORY|available_KiB=" $2 }' /proc/meminfo
}
sha() { sha256sum "$1" | cut -d' ' -f1; }
git rev-parse HEAD
"$spine_cc" --version
stage legacy-lifecycle-and-J0
make -j1 -B -C core/command-spine CC="$spine_cc" build/s0b/spine_lifecycle.o build/s0b/s0b_profile_matrix
core/command-spine/build/s0b/s0b_profile_matrix "$(sha core/command-spine/build/s0b/spine_lifecycle.o)"
make -j1 -B -C frontends/headless-j0 CC="$spine_cc" build/j0a/libfwlab_m3p_v0.a build/j0a/libfwlab_nfc_v1.a build/j0a/libfwlab_file_nand_v0.a build/j0a/j0a_lower_matrix
frontends/headless-j0/build/j0a/j0a_lower_matrix \
  --m3p-sha "$(sha frontends/headless-j0/build/j0a/libfwlab_m3p_v0.a)" \
  --nfc-sha "$(sha frontends/headless-j0/build/j0a/libfwlab_nfc_v1.a)" \
  --file-sha "$(sha frontends/headless-j0/build/j0a/libfwlab_file_nand_v0.a)" \
  --elf-sha "$(sha frontends/headless-j0/build/j0a/j0a_lower_matrix)"
make -j1 -B -C frontends/headless-j0 CC="$spine_cc" build/j0b/j0_host_data.o build/j0b/libfwlab_m3p_v0.a build/j0b/libfwlab_nfc_v1.a build/j0b/libfwlab_file_nand_v0.a build/j0b/libfwlab_spine_lifecycle_v0.a build/j0b/j0b_profile_matrix
frontends/headless-j0/build/j0b/j0b_profile_matrix \
  --lifecycle-sha "$(sha frontends/headless-j0/build/j0b/libfwlab_spine_lifecycle_v0.a)" \
  --host-sha "$(sha frontends/headless-j0/build/j0b/j0_host_data.o)" \
  --m3p-sha "$(sha frontends/headless-j0/build/j0b/libfwlab_m3p_v0.a)" \
  --nfc-sha "$(sha frontends/headless-j0/build/j0b/libfwlab_nfc_v1.a)" \
  --file-sha "$(sha frontends/headless-j0/build/j0b/libfwlab_file_nand_v0.a)" \
  --elf-sha "$(sha frontends/headless-j0/build/j0b/j0b_profile_matrix)"
make -j1 -B -C frontends/linux-m4 CC="$spine_cc" worker native-io check-runtime

# Existing bounded fixtures, not the completed 64/256 MiB full campaigns or a
# performance gate. Byte-fixture crash semantics are separate from tmpfs POSIX
# process recovery; neither is physical-disk or host-power-loss evidence.
stage PAGE2-and-physical-media
make -j1 -B -C core/nfc-page-v2 CC="$spine_cc" check
make -j1 -B -C media/file-nand-v2 CC="$spine_cc" FWLAB_MEDIA_EXCLUSIVE=0 \
  check check-operation check-mapped
make -j1 -B -C media/file-nand-v2 CC="$spine_cc" \
  BUILD_DIR=build/ci-exclusive FWLAB_MEDIA_EXCLUSIVE=1 check-mapped

stage retained-Block-parent
make -j1 -B -C frontends/headless-scale -f ftl.mk CC="$spine_cc" \
  FWLAB_CRC_NATIVE=0 FWLAB_MEDIA_EXCLUSIVE=0 \
  check-crc check-crc-fast check-parent-window-v2-mapped

# Compiles the selected production worker and executes its actual host loop
# over real FTL/PAGE2/physical-v2 storage with a fake ioctl Host. This does NOT
# load the kernel transport or establish native IRQ/M5/multi-queue concurrency.
stage serial-credit-MQ2-userspace
make -j1 -B -C frontends/linux-m4 CC="$spine_cc" \
  mq2-worker profile-check attach-check check-progress-runtime
stage complete
