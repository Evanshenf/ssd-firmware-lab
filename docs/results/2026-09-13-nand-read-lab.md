<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# N0: resource-scheduled NAND READ, real physical-v2 binding

## Identity and scope

- Source and test commit: `a8c07f17c9d30d6d36605f98b9016513d7a1b979`.
- Core implementation commit: `dd5740a`.
- Design: [ADR-0018](../adr/0018-resource-scheduled-nand-read-lab.md).
- Profile: private `LAB-READ-R1`, four slots, one worker, synthetic integer time.
- Result: the finite **N0 lower-binding** cases pass. No parallel FTL, native
  NVMe, vendor NAND timing, 4-TB capacity or sustained-write result is claimed.

The existing PAGE2-R0 tests are unchanged and still pass. R0 keeps its one-slot
behavior and 279040-byte arena. LAB preparation delegates to those same R0
semantics; timed reads use the shared request and physical-page fact helpers.
Physical NAND bytes, format, CRC, barriers and POSIX exclusion are unchanged.

## Executed checks

| Lane | Actual execution | Result |
| --- | --- | --- |
| x86-64 GCC | Original R0 and new LAB unit suites | Pass |
| x86-64 Clang ASan/UBSan | Both unit suites | Pass |
| AArch64 | Cross-compiled both suites, executed with QEMU user mode | Pass |
| RISC-V64 | Cross-compiled both suites, executed with QEMU user mode | Pass |
| s390x | Cross-compiled both suites, executed with QEMU user mode | Pass |
| x86-64 GCC, real physical-v2 | Ordinary-user POSIX file on bounded local tmpfs | Pass |
| x86-64 Clang ASan/UBSan, real physical-v2 | Same finite real-media scenario on a fresh image | Pass |

Cross/user-mode results do not qualify native PCI, a different kernel platform
or the full NVMe stack on those architectures. This is not a hosted-CI attestation.

Unit cases cover command/data bus exclusion, array overlap, register retention,
serial pages within a group, four retained slots/backpressure, UID continuity,
one-way phase transition, media errors with no output, queued/started cancellation,
reset/drain, immutable terminal results after late cancellation, and explicit
time/trace bounds. A separate bounded read-only source confirmation returned
NoRequired for this N0 candidate; it did not execute tests or approve later slices.

## Real-media scenario

The test uses a newly created 2245120-byte physical-v2 image, with:

- 2 channels, 2 LUNs/channel, 1 plane/LUN, 2 blocks/plane, 64 pages/block;
- 4096-byte main and 128-byte OOB per page;
- four ordinary PREP program transactions, two pages in each selected LUN;
- a one-way transition on the same provider and media into TIMED_READ;
- four accepted two-page reads, retained results and reverse-order consumption;
- exact independent main/OOB pattern checks, queued and started cancellation,
  reset with a retained result, and same-format close/reopen under a fresh epoch.

The test inspects the actual resource event sequence. It observes two concurrent
channel owners and four overlapping array reads, while rejecting overlapping
transfers on the same channel. Hardware resources release before retained result
slots are reused. A cancelled started group materializes/drains only its current
page, not the next page. Timed mutation attempts are rejected and physical media
sequence stays4 during the read interval.

Declared synthetic inputs are command1000ns, array-read10000ns and channel byte
rate1000000000B/s; each page's data-out includes4224 bytes. For this fixed scenario,
the model completes8 physical page reads in38896ns. **This number is a model
timeline check, not a measured SSD bandwidth or a calibrated NAND specification.**
The short wall-clock test is unsuitable for throughput conclusions. Maximum
process RSS was3092KiB for GCC and10112KiB with sanitizers; these values exclude
tmpfs page-cache storage and are not a full-system memory budget.

Each successful test closed its instances and removed only its own fresh image
and temporary directory. Existing media and previously completed capacity tests
were not reopened. Tmpfs reopen verifies recovery while that filesystem survives,
not disk durability or whole-machine power loss.

## Reproduction

The first command needs no physical image. The second requires an already
provisioned, user-writable local tmpfs directory, at most1GiB with at least64MiB
free; a missing/wrong filesystem or insufficient space is an error, not a disk
fallback. Programs and logs should stay on the ordinary filesystem.

```sh
make -C core/nfc-page-v2 check
FWLAB_TEST_MEDIA_DIR=/run/fwlab-test-media \
  make -C media/file-nand-v2 check-nfc-lab
```

The real entry is
[test_nfc_lab_media.c](../../frontends/headless-scale/test_nfc_lab_media.c).
It creates a new private file, never accepts a raw device and never formats an
existing image. The production native constructor is not changed by these tests.

## Evidence identity

| Artifact | SHA-256 |
| --- | --- |
| LAB implementation | `8e989c664cf783c3117d02601c41c01d644cdea0b369de26567ab917185e6282` |
| Shared R0/LAB helpers | `1f70f498621098a229ad6b49e12a0c3cef6e1874ed3d5c290ead933b06922b94` |
| Real-media test source | `63df41c29c12f6dc2648950242401c870c8d9608c5bfcd596f282528a5b4eb2c` |
| GCC real-media ELF | `29fb9d1df7cecb41b0b54ad8bc61abbfef92478f713f7dc1a7dd69fd8214b7fa` |
| Clang sanitizer real-media ELF | `58ee68ff643d3c4751a786482f30741ea53c2615fdd8c17edbbc27eb8ee5f842` |

Raw execution logs are retained in the maintainer's private archive; local path
details are not published here. The source-based commands above reproduce the
finite checks, not necessarily byte-identical binaries from other toolchains.

N0 stops here. Next is N1: prepare a cross-resource layout through real serial
FTL writes, then issue multiple physical runs within one existing Block parent.
The simple capacity product and this lower-level overlap do not prove that the
current FTL already produces such parallel work.
