<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Cooperative channel domains with real NAND shards and serial FTL

Source: `647fd732034a1b6e7c66a6989f16d26551125af1`, baseline
`2065379f4876027c7f2e9075d5584970a9273800`. Separate commits preserve NFC
(`ffdb7ab`), physical assembly (`a128f70`) and integration (`647fd73`).
Profile: **WAVE4-LAB4K-A**, explicitly selected cooperative execution.
This is additive development evidence, not a release, native deployment or
multi-threaded/multi-head performance qualification.

Design: [ADR-0020](../adr/0020-cooperative-nand-channel-domains.md).
One independent read-only exact-source/evidence confirmation returned
**NoRequired / STOP** for the four A obligations. It verified all 18 source
identities, selected logs and executable identities without another test run.
Earlier design consultation is separate and is not the source-approval basis.

## Actual path

`scale_storage_channel_lab_factory_init()` selects the same Linux-profile,
lifecycle, aggregate Block and ordinary serial format-2 FTL. PAGE2 requests go
through the cooperative hub to independently owned timed NFC/physical-v2
channels. Global PPA routing preserves FTL main/OOB identity. Each shard uses
the existing v2 codec, CRC, health/generation and ordered synchronization.
There is no direct logical block backend or second NVMe command executor.

The native worker continues to select R0. Its userspace source list receives
the hub object because shared storage construction references it; no new
native factory is selected and no worker is started for this qualification.

## Four bounded evidence groups

| Group | Observed result |
|---|---|
| Assembly/recovery | Two independent real shards preserve global/local PPA, opaque OOB and local transaction sequences; exact reopen and exclusive OFD ownership pass. Wrong global UUID, missing/swapped shards and interrupted manifest publication reject without creating/resizing replacements. A physically complete assembly with uninitialized FTL rejects metadata recovery without binding a namespace or changing its physical content hash. |
| Resource/time | Four real PROGRAM requests across two channels and two LUNs per channel finish at 113449 synthetic ns, with both LUN arrays busy and exact payload/OOB. A later request on previously idle channel 2 admits no earlier than that JOIN. Core fixture also checks sealed ingress and no early result visibility. |
| Ownership/progress | Changing caller input after ACCEPTED does not change programmed bytes. All four result credits remain held until take/discard and retirement ACK; a new submission gets BP before those ACKs. Final close progresses the last ACK without requiring another request. Accepted hub PROGRAM drains even when close precedes child admission; unaccepted FTL DATA stops. Core fixture retains actual sibling facts on one child failure. |
| Existing serial journey | Linux-profile/J0 RMW across partial pages, FUA/Flush, durable frontier and exact recovery pass. Close before DATA acceptance preserves old data/frontier; close after hub acceptance, before child admission, recovers completed data and its final frontier. Both finish with existing J0 all-zero close certificates. |

The hub's adjacent fixture uses controlled physical callbacks. Separate assembly
and hub/J0 fixtures use real files. Blank-FTL rejection is executed in a child
process with normal lock release on exit; it is not a simulated machine power
loss. Payload/OOB, effect and persistent mapping checks stay in their actual
firmware layers, not in a replacement test backend.

GCC and Clang ASan/UBSan pass the new core, assembly and real J0 fixtures.
AArch64, RISC-V64 and big-endian s390x J0 executables were statically linked,
their ELF identities checked, and **actually run under QEMU user mode**. These
are local cross executions, not hosted CI or native cross-architecture PCI.
One affected existing parallel READ J0 regression passed with unchanged
assertions. The ordinary MQ2 userspace target compiled/linked only.

## Media and measurement limits

All real files use a separately provisioned 1-GiB local tmpfs, serial execution
and the ordinary POSIX adapter. No mapped-byte profile switch, skipped sync,
slow-disk fallback, raw media or existing image is involved.

The assembly fixture uses two shards totaling 2261504 bytes. Each hub/J0 set
uses four shards of 2245120 bytes, totaling 8980480 bytes, for geometry
4 channels × 2 LUNs × 1 plane × 4 blocks × 64 pages, 4096 main/128 OOB bytes.
The J0 namespace is 1 MiB by fixture choice; this does not shrink or requalify
the existing native 64-GiB construction. Successful disposable sets are closed
and removed by verified ownership; unexpected failure artifacts are retained.

Synthetic costs remain explicit: 1-us load/confirm/status command, 10-us read
array, 100-us program array, 1-ms erase, 1-byte status and 1 decimal GB/s per
channel. No vendor preset, whole-drive rate limiter or wall-clock pacing exists.

Final small J0 test durations and process peak RSS are listed only for execution
budget transparency, **not NAND/SSD bandwidth or architecture speed comparisons**:

| Execution | Whole fixture wall time | Peak RSS KiB |
|---|---:|---:|
| GCC x86-64 | 0.04 s | 10856 |
| Clang ASan/UBSan x86-64 | 0.12 s | 85888 |
| AArch64 QEMU user | 0.18 s | 21072 |
| RISC-V64 QEMU user | 0.17 s | 20044 |
| s390x QEMU user | 0.19 s | 19744 |

PROGRAM currently copies caller→hub→child. More worker threads have not yet
been implemented or measured. The cooperative hub holds a closed batch until
JOIN; it is deliberately not a fully pipelined controller.

## Reproduction and STOP

Provision the existing isolated, capped tmpfs as described in the
[FTL guide](../../core/ftl-scale/README.md). Run media tests serially as the
ordinary lab user (the assembly fixture expects UID 1000):

```sh
make -C core/nfc-page-v2 check-channel
make -C media/file-nand-v2 check-channel-volume
make -C frontends/headless-scale -f ftl.mk check-channel-j0
```

The last two reject missing/non-tmpfs/insufficient backing space. Executables and
logs can remain on disk; only new physical media belongs on tmpfs. These checks
do not run the old large-capacity suites or deploy a native controller.

A stops at the four groups plus affected checks and one source confirmation.
B multi-head format 3, C real threaded channel transport and D independent-plane
READ remain separate unimplemented work. No 16-KiB, 4-TB, TLC/pSLC, 8-GB/s,
hardware NAND migration, RTOS or bare-metal claim follows from A.
