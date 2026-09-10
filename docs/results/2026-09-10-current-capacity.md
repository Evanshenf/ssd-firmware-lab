<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Current PAGE2 capacity qualification and native namespace construction

Date:2026-09-10. Source:`11cb8a80bafa02db960cab8ef31d356888166cc1`;
native test client:`8acc238948a5ec477d544654323b1c7bea90ecf8`.
These commits share the same production implementation; the later change adds
only explicit expected capacity to the existing native test client and its docs.
This is functional evidence, not whole-SSD performance or power-loss evidence.

## Separate results

| Scope | Result |
|---|---|
| Local GCC mapped-budget checks and headless current64/256MiB full journeys | PASS |
| Actual MQ2 worker with fake Host,64/256MiB | PASS: Identify, tail I/O, Flush, reset/recovery, wrong-capacity restart rejection |
| Clang ASan/UBSan mapped fixture and native256MiB fake-Host journey | PASS |
| Current worker cross compile: AArch64/RISC-V64/big-endian s390x | PASS; not cross-target worker execution or kernel qualification |
| ARM64 current-path64/256MiB full journeys | PASS, native GCC15 with CRC-native/exclusive-mapped options |
| ARM64 current-path64GiB | PASS:64GiB fill,32GiB overwrite, GC/checkpoint, cold close/reopen recovery,64GiB readback and final close; exit0 |
| Native x86-64 Linux256MiB | PASS, including real driver and filesystem episode below |

## Native256MiB driver/filesystem episode

The installed Linux7.0.0-30-generic driver reported NSZE/NCAP/NUSE524288 with
512-byte LBAs:268435456bytes. The selected MQ2 worker used the shared capacity
preset and unchanged PCI/HIF sources. Six existing data shapes, including an
8KiB extent ending at LBA524287 and aligned/offset1MiB workloads, passed exact
readback. Linux split1MiB workloads at its128KiB wire limit; these are not
unsplit1MiB L1-command evidence.

Controller reset, driver unbind/rebind and a cold worker/media restart all
preserved the written data without reformat. On the same freshly created test
namespace, ext4 formatting used `nodiscard` and synchronous initialization;
mount, a4MiB random file, fsync, unmount, controller reset, remount and exact
SHA256 verification passed. This is not an arbitrary-filesystem qualification.
The original reference controller was restored unbound; its existing media and
the unrelated read-only backend disk were unchanged. The fresh test media was
closed and retained separately; no existing image was expanded or converted.

Exact identities:

- worker ELF:`cf167530cbb8a651dd5dc536a87c744449db68b3efa86df07be8da4e342b88c9`
- client ELF:`a6f04741bb1e0f8cf8a748afc5944508818ce23a9a7dafb8131bc32c7ea9f352`
- PCI module:`418da477332696990389b852779bd24f5ff13c19410570ba86f8ac0fa05e3943`
- IOMMU module:`d0268d62167c5c70da07e8cc22cc58bc00d13ec922e92f63bca68537d966c433`
- episode log:`270fab6baf39197ba191a673acadcbadd69542fd0d9fca8875b0a26e482883e0`
- Identify JSON:`874833a99204e50f157e66076d4fa3216c18a92721d89fa6395e0a2614e608c7`
- retained private evidence archive:`b9d58c6d8f7b32f2f5ba0bb506910d3a7ec0fe4cbe188d36287b305b7570c9c9`

## ARM64 large execution boundary

The current `check-64g` selects Linux-profile/headless lifecycle -> scalable
FTL format2 -> PAGE2-R0 -> mapped physical-v2, not the historical C3/v1 route.
Geometry provides80GiB main area plus OOB/page/block metadata; image size is
91289042944bytes on a fresh90GiB tmpfs. Logs/executable stay on the system disk.
The single test process uses CPU7, a92GiB memory ceiling and no swap/restart.
All modeled checksums, synchronization and locks remain. Exclusive-mapped mode
changes only the already documented external-file validation interval.

Source is the earlier exact11cb8a8 commit; AArch64 ELF:
`d87a633b3655d80910c58916df2d6c0de4576e06f9e90187bc15e33f234e114f`.
The complete run passed with68719476736bytes filled,34359738368bytes overwritten
and68719476736bytes read back after cold close/reopen recovery. Before recovery
it recorded143447GC cycles and213checkpoints. Final close/early-close checks
passed, GNU time and the systemd main process both reported exit0.

Elapsed wall time was20m34.25s (user1102.25s, system131.74s,99% CPU). Peak process
RSS was89497968KiB, including the resident whole-image mapping; it is not all
FTL metadata. The test-owned tmpfs extent peaked at91289042944bytes. No swap or
major faults were reported; sampled memory pressure/limit events stayed zero.
The successful fixture removed only its own temporary image; the dedicated
mount returned to0bytes used. This timing includes initialization, GC,
checkpoints, verification, recovery and cleanup; it is not a bandwidth benchmark.
The64GiB recovery leg recreates storage/runtime objects in the same process,
not a process-kill or host-power-loss test.

Complete artifact identities:

- full log:`4e293e1fb8f32dbd0c2f172da1e1d95778b7fe0f0826c94e542bcf2c689cb45a`
- time report:`58745ce6258d40bd50d2fe94249ee0be98f463549e7f8be8cfe3cd1f9d1bec03`
- small full-journey log:`f4f9f047eeb378d9433f49f42807a5e9addb091eaeff18df36d17252a250b874`
- retained private ARM evidence archive:`4d157196e647af76680b196c81943b0a3f003b5834304578332daf63e7be1217`

Neither this run nor cross compilation establishes native64GiB reset timing,
ARM synthetic PCI/M5, online resize, real NAND, disk power durability or SSD
bandwidth. The old C3/v1 64GiB result remains a separately versioned record.
