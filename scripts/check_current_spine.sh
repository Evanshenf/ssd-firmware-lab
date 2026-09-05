#!/bin/sh
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

# Current semantic execution, not a re-approval of historical leaf manifests.
# Explicit targets retain the archives/objects used as real digest arguments.
set -eu
cd "$(dirname "$0")/.."
spine_cc=${CC:-cc}
sha() { sha256sum "$1" | cut -d' ' -f1; }
git rev-parse HEAD
"$spine_cc" --version
make -B -C core/command-spine CC="$spine_cc" build/s0b/spine_lifecycle.o build/s0b/s0b_profile_matrix
core/command-spine/build/s0b/s0b_profile_matrix "$(sha core/command-spine/build/s0b/spine_lifecycle.o)"
make -B -C frontends/headless-j0 CC="$spine_cc" build/j0a/libfwlab_m3p_v0.a build/j0a/libfwlab_nfc_v1.a build/j0a/libfwlab_file_nand_v0.a build/j0a/j0a_lower_matrix
frontends/headless-j0/build/j0a/j0a_lower_matrix \
  --m3p-sha "$(sha frontends/headless-j0/build/j0a/libfwlab_m3p_v0.a)" \
  --nfc-sha "$(sha frontends/headless-j0/build/j0a/libfwlab_nfc_v1.a)" \
  --file-sha "$(sha frontends/headless-j0/build/j0a/libfwlab_file_nand_v0.a)" \
  --elf-sha "$(sha frontends/headless-j0/build/j0a/j0a_lower_matrix)"
make -B -C frontends/headless-j0 CC="$spine_cc" build/j0b/j0_host_data.o build/j0b/libfwlab_m3p_v0.a build/j0b/libfwlab_nfc_v1.a build/j0b/libfwlab_file_nand_v0.a build/j0b/libfwlab_spine_lifecycle_v0.a build/j0b/j0b_profile_matrix
frontends/headless-j0/build/j0b/j0b_profile_matrix \
  --lifecycle-sha "$(sha frontends/headless-j0/build/j0b/libfwlab_spine_lifecycle_v0.a)" \
  --host-sha "$(sha frontends/headless-j0/build/j0b/j0_host_data.o)" \
  --m3p-sha "$(sha frontends/headless-j0/build/j0b/libfwlab_m3p_v0.a)" \
  --nfc-sha "$(sha frontends/headless-j0/build/j0b/libfwlab_nfc_v1.a)" \
  --file-sha "$(sha frontends/headless-j0/build/j0b/libfwlab_file_nand_v0.a)" \
  --elf-sha "$(sha frontends/headless-j0/build/j0b/j0b_profile_matrix)"
make -B -C frontends/linux-m4 CC="$spine_cc" worker native-io check-runtime
