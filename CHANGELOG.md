<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Development history

## 2026-09-10 — shared capacity and current 64-GiB qualification

- Share 64/256/65536-MiB construction presets between headless and native scaled
  workers; add explicit matching-capacity creation/recovery and native client
  expectations without resizing existing images or changing PCI/HIF policy.
- Retain the default 600-MiB mapped budget and permit explicit budgets up to
  90 GiB; NAND format, checksums, synchronization and exclusive ownership remain.
- Make the current 64-GiB entry select PAGE2/window-v2/mapped physical-v2;
  preserve the old C3/v1 entry under an explicit reference name.
- Complete current-path ARM64 64-GiB fill/overwrite/GC/recovery/readback and
  native Linux 256-MiB reset/rebind/cold-recovery/ext4 checks. Native 64-GiB
  reset timing, ARM PCI and SSD performance remain unqualified.
- Record the construction amendment in ADR-0015 and exact capacity evidence
  separately from the earlier performance and C3/v1 campaigns.

## 2026-09-10 — current userspace cross-architecture coverage

- Extend the existing current-spine cross job to AArch64, RISC-V64 and
  big-endian s390x, retaining x86-64 GCC/Clang execution and the frozen C4 lane.
- Compile the actual current MQ2 userspace worker and check ELF class,
  endianness and machine. Reuse existing small PAGE2/physical-media emulator
  tests and retain the ARM-only explicit CRC check.
- Distinguish compile coverage from whole-worker execution and native kernel
  support; label the current 64-GiB entry as the C3/compact-v1 reference route.
  No firmware algorithm, media-format or large-capacity qualification change.

## 2026-09-10 — bounded maintenance after publication

- Track compiler/build flags in the three unfrozen current-layer Makefiles;
  unchanged configuration keeps cached outputs, changed configuration rebuilds.
- Select and assert actual MQ2 profile 3 in the existing progress fixture,
  including reset/recovery; retain its fake-Q1/real-storage evidence boundary.
- Reuse the existing CRC oracle with required x86 SSE4.2 and ARM CRC backends
  in hosted CI; do not infer ISA coverage from a generic fast-named target.
- Align live architecture/requirements and directory/result navigation with
  current production/reference/test roles, without moving or rewriting frozen
  files. Correct the FTL cuts command's missing environment inheritance.
- Publish the existing ARM sample/timing records and complete the rejected
  experiment's executable identities; no performance campaign rerun and no
  independent benchmark-harness portability claim.

This maintenance changes build/test/documentation, not firmware algorithms,
on-media formats or the existing preview tag.

## 2026-09-10 — adopted scalable storage and serial-credit MQ2

Development publication, not a new frozen release. The 29 source commits from
`efe304b` through `a6ee009` remain independently auditable; no squash or rewrite
of historical evidence identities was performed.

- Capacity comes from a recovered ready FTL volume. Add scalable mapping,
  streamed checkpoints, dual journal rails, private foreground GC and retained
  Block parents up to 1 MiB.
- Add physical NAND v2 with reservation-protected direct homes and explicit
  recovery, PAGE2-R0 functional batching, bounded mapped-tmpfs construction and
  opt-in exclusive host-file validation. Old formats remain separate.
- Preserve CRC bytes while adding portable/explicit-ISA fast paths; improve
  owned NFC window alignment and remove measured redundant validation work
  without dropping persistent checks, locks or synchronization.
- Bind the scalable path to explicit native media/producer/profile identities.
  Large and MQ2 constructions expose 64 MiB, up to 1-MiB I/O; MQ2 has two I/O
  queue pairs and three MSI-X vectors but **one global I/O frame**.
- Retain captured queue incarnations, accepted deletion/retry and per-vector
  route lifetime. Drive the existing HIF from one firmware PUMP loop and report
  actual execution progress for idle backoff.
- Add ADR-0012–0014, separated [performance samples](docs/results/2026-09-10-throughput.md),
  [capability/support matrix](docs/current-status.md), native operator sequence
  and selected current-path CI entries using bounded tmpfs and ordinary users.

The rejected combined-PUMP/STATUS experiment is excluded. All existing
disk/tmpfs/native/ARM evidence retains its source and scope; a historical
64-GiB headless campaign does not establish current native 64-GiB support.
No 10-GB/s end-to-end, parallel FTL, raw-block, RTOS, real NAND or production
readiness claim. See the [development evidence index](docs/results/2026-09-10-scaled-storage-mq2.md).

## v0.1.0-spine-preview.1

The existing immutable tag points to `4b2a562`. It remains the reviewed fixed
1-MiB/8-KiB, one-I/O-queue software preview. Its
[results and limitations](docs/results/2026-09-05-vertical-spine-preview.md)
have not been rewritten to cover the newer development profile.
