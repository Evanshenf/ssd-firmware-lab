<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# N1: parallel physical READ through the existing headless command spine

Source/test identity: `1a605b8fff135c4424edf1bdf26b31be59fa06a0`.
Design: [ADR-0018](../adr/0018-resource-scheduled-nand-read-lab.md).
The finite N1 READ slice passes; timed writes, real NAND calibration, native
qualification and large-capacity algorithms are not included in this result.

## What is actually connected

```text
existing Linux-profile-v1 -> existing lifecycle -> one aggregate Block parent
-> generic FTL four-run READ pool -> token-keyed PAGE2 provider
-> LAB resource events -> physical NAND v2 main/OOB -> validated logical prefix
-> existing controller buffer/data mover -> one completion intent/publication
```

FTL receives the ordinary PAGE2 provider, not a LAB object, clock or timing table.
Only the headless storage composition knows the concrete LAB construction.
It checks J0 admissions, FTL work and lower live-idle before changing both phases.
Non-READ Block admission is rejected before parent/metadata mutation. Ordinary
constructors remain serial and write-capable; their on-media formats are unchanged.

The new engine fills eligible runs before advancing NFC, collects by token and
writes actual controller-buffer spans in logical-prefix order. Accepted lower
ownership clears only after successful result consumption. Failure/cancellation
stops new issue/publication but continues sibling advancement and collection.
Unusable control/API errors quarantine instead of falsely reporting drain.

## Fixed headless cases

The public fixture reuses the existing J0/profile/lifecycle driver and creates
seven fresh 8931328-byte physical-v2 images for six bounded obligation groups.
Every image uses a1-MiB LAB namespace, 4-KiB main/128-byte OOB, 64pages/block,
16blocks per resource and two resources. This does not replace the ordinary
64/256/65536-MiB capacity presets.

| Obligation | Executed evidence |
| --- | --- |
| Real cross-resource path | Ordinary FTL fill/overwrite prepares mapped pages on different channels; one legal8-KiB Linux-profile READ returns exact bytes through the real lifecycle and NAND |
| Resource contention | Same-channel/two-LUN and two-channel arrangements produce their declared event schedules, without changing data semantics |
| Out-of-order/partial/hole | A later child finishes before the middle child; a forwarding observer confirms contiguous actual buffer writes; separate partial/unmapped-hole case returns exact data/zeros |
| Real read error | A fresh-medium bad-block preparation causes a legal READ to fail at NAND; no logical prefix is published and all sibling work drains |
| Queue/started close | Both an actually owned FTL parent before NAND issue and an array-started request are closed; the existing complete zero-reference certificate and no-late-publication checks pass |
| Reopen and neighboring semantics | Same-format recovery occurs in PREP under a new construction identity, then exact timed readback; ordinary Writes, FUA and Flush remain in preparation |

The error case preserves a valid Host transfer description: it is not a malformed
request rejected before reaching NAND. The forwarding buffer observer changes no
payload or return result. No map is edited to manufacture parallel placement.
Close is checked not to perform a hidden unmeasured metadata transaction.

## Results and performance limits

All six groups passed with x86-64 GCC and Clang ASan/UBSan. The same fixture was
cross-compiled and actually executed sequentially under AArch64, RISC-V64 and
s390x QEMU user mode; all passed. This is not native PCI evidence on those ISAs.

With explicitly synthetic command1000ns, array-read10000ns and channel rate
1000000000B/s, the two-page headless request takes:

| LAB wiring | Model completion time |
| --- | --- |
| Two independent channels | 15224ns |
| One shared channel, two LUNs | 19448ns |

This illustrates real arbitration in the model, not calibrated NAND bandwidth.
The full seven-image x86-64 suite took0.11s with GCC and0.20s with sanitizers;
peak process RSS was6968KiB and61696KiB respectively, excluding tmpfs storage.
Those short functional-suite times are not a throughput benchmark.

A separate adjacent Block-level control reused a1-MiB fill and64-KiB overwrite.
Its128-KiB READ returned the same bytes with two physical runs and continuous
UIDs:487168ns through the ordinary serial FTL window,243584ns through the new
parallel reader. These are model times, not native Host rates.

Another bounded adjacent control connected the **same parallel FTL constructor
to the real single-slot PAGE2-R0**, without a LAB object or clock. One1-MiB READ
returned the new64KiB prefix and original960KiB suffix via five runs
`16,48,64,64,64` pages. Six actual backpressure responses, continuous UIDs63..67,
and complete lower/buffer retirement were observed. This proves that particular
generic binding, not an unrestricted hardware-port or full Rule-of-Two claim.

The unchanged ordinary PAGE2 parent suite passed with Clang sanitizers, including
GC, partial windows, close and uncertain DATA/MAP-A recovery. The original C3
parent suite passed with GCC. The ordinary native MQ2 userspace worker linked
successfully; it was not loaded or tested on a native controller in this slice.

## Reproduce

Use the existing capped, user-writable local tmpfs media directory. The entry
rejects a missing/wrong backend or insufficient resources; it never falls back
to the slow disk or formats an existing image.

```sh
FWLAB_TEST_MEDIA_DIR=/run/fwlab-test-media \
  make -C frontends/headless-scale -f ftl.mk check-parallel-read-j0
```

Entry: [test_parallel_read_j0.c](../../frontends/headless-scale/test_parallel_read_j0.c).
All successful temporary images are closed, identity-checked and removed.
Prior NAND images, virtual machines and raw devices are untouched.

Exact-hash independent source confirmation returned NoRequired for N1/A2. It
was read-only and did not execute tests. Raw logs and supplemental private
Block probes are retained in the maintainer archive; the command above is the
public headless reproduction, not a packaged runner for every private control.

N1 stops here. Next is a separately specified timed PROGRAM/ERASE and multi-head
write/recovery slice. Native pacing, real geometry, pSLC/folding, 4-TB algorithms
and the physical erase-generation ownership gap remain explicitly unqualified.
