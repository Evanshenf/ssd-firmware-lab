<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0027: capacity presets on the channel NAND construction

- Date: 2026-09-15
- Source: `4fe70514cfb298fceda46dbab1dee65d2eee2cd9`
- Refines [ADR-0015](0015-capacity-presets-and-mapped-budgets.md),
  [ADR-0025](0025-native-channel-construction.md) and
  [ADR-0026](0026-native-worker-lifetime.md).

## Decision

The existing MQ2 `channel-lab4k` option accepts the same logical capacity
choices as R0: `--namespace-mib 64|256|65536`. Default capacity remains64MiB;
default storage remains R0. The pure `scale_storage_profile_capacity_mib`
helper selects the construction geometry and LBA expectation together, checks
the ordinary FTL layout, then returns them. The old
`scale_storage_capacity_mib` is an exact R0 wrapper.

Channel presets retain4channels,1LUN/channel,2planes/LUN,64pages/block,
4096main+128OOB bytes/page, four request credits and the same synthetic timing.
Only blocks/plane change:40,160,40960. Increasing capacity does not add
parallel resources or imply more throughput. Cooperative and optional1/4worker
execution use the same mutableFTL3/NFC actors and physical-v2 shard engines.
No protocol/lifecycle/kernel ABI, core algorithm or media-format change occurs.

## Capacity and recovery authority

Native CLI validation, media geometry, format/expected LBA and worker enabling
all accept the selected preset. The recovered FTL volume remains the authority
for Identify and command range checks; a CLI number cannot resize it.
The four-shard manifest and child UUID/geometry identities are retained across
runtime reconstruction. Format is new-only; recovery is not conversion.
OldFTL2 single-file R0 media is not a channel volume, despite both using the
physical-v2 codec. There is no implicit fallback to that old construction.

Wrong-preset recovery is checked on clean, closed fixtures. Do not generalize
their unchanged-byte result to dirty media: physical redo recovery can occur
before native code rejects a geometry mismatch.

## Verification and limits

The existing native-progress fixture tests64MiB cooperative and256MiB
fourworker construction, Identify, tail I/O, Flush, past-end READ rejection,
retained locks, runtime reconstruction, cold recovery and actual joins.
Its Host ioctls are simulated; storage uses the real firmware stack. GCC and
Clang sanitizer cases ran on x86-64 and actual ARM64. See the
[exact-source result](../results/2026-09-15-channel-capacity.md).

`--capacity-plan` on that fixture checks all three literal presets, image
lengths and real arena-size queries without creating media. Cross-compiled
AArch64/RISC-V64/s390x plan execution is sizing evidence only.

The new64GiB channel profile has not passed native Linux device qualification.
Its four images plus manifest total91289093120bytes. Existing full-image
resource admission and60-second threaded operational deadlines remain;
successful small tests do not prove large startup/recovery meets them.
Large testing needs a separate resource/identity plan, not a VM reinstall or
automatic deletion of a retained dataset. No new test framework, capacity
campaign, NUMA-locality or performance claim is introduced.
