<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Single-core performance and multi-queue roadmap

This is the implementation roadmap with historical diagnostic checkpoints, not
a claim of 10 GB/s throughput, PCIe compliance or production readiness. The
adopted source now includes serial-credit MQ2, not parallel FTL. Use the
[current matrix](current-status.md) and [dated performance report](results/2026-09-10-throughput.md)
for current claims. The fixed-profile tag and its evidence remain unchanged.

## Objective and measurement boundary

The objective is at least 10 decimal GB/s for large sequential reads and writes,
with one software data-path executor/core and real protocol, lifecycle, FTL,
NFC and physical NAND media participation. A proposed1–3% loss against a matched
substrate is an unproved acceptance target, not a consequence of using RAM.

Report8 KiB QD1 synchronous I/O separately from1 MiB sequential I/O. Match block
size, queue depth, allocation/cache state, completion guarantees and CPU/time
accounting. Count actual completed Host bytes; retain GC, checkpoint, recovery
and drain costs required by the named workload. Do not count a sparse file's
logical size as completed I/O or multi-core aggregate throughput as single-core.

Native tests additionally report Host producer/driver, firmware syscall and IRQ
CPU costs. Two Host queues do not require two firmware threads. Conversely,
multiple Host producers used to exercise two real Linux queues are not a
whole-system single-thread benchmark. PCIe 4.0 ×4's bandwidth is below10 GB/s; a
native10 GB/s-class comparison needsPCIe 5.0 ×4-class bandwidth or sufficient lane width.

## What the measurements establish

On one ARM64 VM, a single synchronous fio worker at QD1, with the entire process
bound to one vCPU, measured these short tmpfs repetitions over a512 MiB file:

| Workload | Initial observation | Later repetitions |
|---|---:|---:|
| 1 MiB sequential read | 8.31 GB/s | 13.09–13.37 GB/s |
| 1 MiB sequential write | 5.55 GB/s | 10.30–10.54 GB/s |
| 8 KiB write with periodic fdatasync | 3.11 GB/s | 4.51–4.58 GB/s |

The last two repetitions also used an explicit final fsync; syscall tracing
confirmed that sync operations actually executed. Lower initial results remain
part of the record. These are short file-substrate measurements, not isolated
hardware peaks, a1% precision certificate or SSD persistence evidence.

The earlier1 MiB write figures use terminal sync only and therefore are not the
denominator for SELF-per-command firmware writes. An additional matched trial
with fdatasync after each 1 MiB write and final fsync measured10.43,6.46,6.26 and
5.80 GB/s across four short runs. This variability is preserved; its cause was
not isolated. It is not evidence of a stable <=3% overhead budget.

T1M write acceptance requires per-command SELF in the DUT and matched raw
pwrite+fdatasync, including final required drain. Use five paired measurements
with alternating order and predeclared working-set/preconditioning/intervals;
report every ratio. Require the absolute target and min(DUT/raw)>=0.97 across
the pairs. A raw-baseline spread above 1% of its median makes the1–3% claim
inconclusive under this operational reproducibility guard, not a statistical
confidence interval. Real DUT/GC variability is retained rather than discarded.

The unoptimized full firmware path was around3 MiB/s. Source inspection shows
that an aligned8 KiB Write, excluding maintenance, performs two DATA and two
mapping-journal programs. Each compact-v1 program performs ten 4 KiB writes and
five barriers: at least160 KiB substrate writes and20 barriers per8 KiB Host write.
Repeated checksums, copies, metadata reads and per-call file validation add cost.
The native bridge has further fixed sleeps and one-command polling limits;
those native costs are separate from the headless measurement.

## First implemented repair: equivalent CRC32C

Commit9dd42595e4a9c0e539de852b682ec655cd34b36f replaces the bit-at-a-time FTL
and compact-media CRC32C implementations with a shared immutable byte table.
It preserves checksum bytes, the four-byte zero-substitution rule, formats,
FNV, synchronization, scheduling and queue behavior.

- 17,278 finite equivalence cases pass against the retained bitwise reference.
- Existing real media/NFC and64/256 MiB full fill, overwrite, GC, checkpoint,
  reopen and readback pass. Existing ten interruption cases pass with sanitizers.
- The ARM64 full small-capacity workload took 75.10 s versus an earlier 176.68 s;
  the x86 observation was 41.61 s versus 110.13 s. These historical timing ratios
  are not controlled paired GB/s benchmarks and do not establish10 GB/s.

Sources: [CRC utility](../include/fwlab/portable/crc32c.h),
[equivalence test](../frontends/headless-scale/test_crc.c),
[scaled FTL](../core/ftl-scale/README.md),
[compact media](../media/file-nand-v1/README.md).

The primitive check runs without a NAND file:

```sh
make -C frontends/headless-scale -f ftl.mk check-crc
```

Actual full-path tests use the explicitly provisioned, capped tmpfs described
by the scaled FTL documentation. Missing media configuration must not silently
fall back to a slow system-disk test.

## Measured cost and bounded P1 repair

The existing headless-scale entry now has an opt-in `--cost` mode. It forwards
the real byte-substrate calls unchanged and reports calls/bytes, NAND commits,
NFC children and maintenance. It is instrumented diagnosis, not a paired
throughput-acceptance benchmark. The test uses a fresh small tmpfs image and
checks SELF, reopen/readback and close; it does not run on an existing image.

| Real path | Before P1 read/write/sync calls | After P1 | Substrate bytes written |
|---|---:|---:|---:|
| Aligned 8 KiB Write | 24 / 40 / 20 | 16 / 32 / 20 | 160 KiB |
| 512 B RMW Write | 21 / 30 / 15 | 15 / 24 / 15 | 120 KiB |
| Aligned 8 KiB Read | 6 / 0 / 0 | 6 / 0 / 0 | 0 |

These are substrate API counts, not syscall traces. P1 coalesces contiguous
redo BODY writes and reuses metadata already read and validated in the same
serialized program operation. It preserves checksums, exact format bytes,
all five commit barriers, neighboring metadata and partial-write recovery.
There is no cross-operation metadata cache or disabled integrity check.

A 32 MiB sequential write diagnosis still writes 745119744 substrate bytes,
with 91365 sync calls and 17 checkpoints. The single x86 observation changed
from 2.144 to 2.090 seconds; this small unpaired difference does not establish
a reproducible throughput gain. The amplification remains about 22.21 times.
The changed path passed actual reopen/readback plus the existing ten real-path
interruption cases under Clang ASan/UBSan. Direct media tests cover all five
redo cuts, including an exact interior prefix of a coalesced BODY write.

```sh
make -C frontends/headless-scale -f ftl.mk check-cost
```

Use only the documented independently provisioned capped tmpfs. This diagnosis
does not weaken sync semantics or replace disk persistence qualification.
The next priority is the retained-parent and within-parent batch mechanism,
not another sequence of small CRC/syscall tweaks. Native multi-queue remains
planned, but cannot fix this headless storage amplification.

## Physical v2 compatibility path: singleton measurements

The [physical v2 media](../media/file-nand-v2/README.md) replaces payload/sector
postimage redo with a bounded INTENT, direct-home installation and matching
COMMIT or recovered ABORT. It retains three real group barriers and explicit
physical page/OOB/generation state. The compatibility entry runs through the
existing C3 NFC and FTL, including an actual reported INTENT-sync failure on
journal A. Recovery observes A as ECC and B as complete erased data, preserves
the earlier acknowledged bytes, rotates the journal and continues SELF writes.

Current 8-KiB QD1 cost diagnosis, **still using singleton NFC programs**:

| Quantity | v1 after P1 | v2 |
|---|---:|---:|
| Backend bytes per 8-KiB Write | 163840 | 23808 |
| Sync calls per 8-KiB Write | 20 | 12 |
| Backend bytes for 32-MiB sequential Write | 745119744 | 107873088 |
| Sync calls in that 32-MiB case | 91365 | 54819 |
| Instrumented Write-stage elapsed time | 2.090 s | 1.072 s |

The observations are unpaired local x86 measurements, not new ARM or native
performance results. The v2 Write stage is about 31 MB/s, not 10 GB/s. Its
write-byte ratio is about 3.215x including the same 17 checkpoints. This is
substantial waste removal, but not completion of the performance objective.

Direct component execution measured 280128 written bytes and three syncs for
a full 64-page/256-KiB physical batch. The legacy C3 binding does **not**
consume that batch entry. The explicit window construction below now does;
dirty-segment checkpoints and matched measurement remain subsequent work.
Do not silently omit v1's exported digest or assert that byte-cost arithmetic
proves a 1–3% throughput loss.

## Explicit native CRC build profile

The optional `FWLAB_CRC_NATIVE=1` build uses the build machine's instruction
set (`-march=native`). The CRC helper selects x86-64 SSE4.2 or little-endian
ARM CRC when the compiler enables it, otherwise the existing portable table.
There is no runtime CPU detection or guarantee that a native binary runs on a
different CPU. Generic and native default build directories are separate; use
fresh explicit `BUILD` directories when changing compiler/options yourself.

```sh
make -C frontends/headless-scale -f ftl.mk check-crc-fast
make -C frontends/headless-scale -f ftl.mk FWLAB_CRC_NATIVE=1 check-crc-fast
make -C frontends/headless-scale -f ftl.mk FWLAB_CRC_NATIVE=1 check-media-v2-cost
```

Four independent 1-KiB raw CRC lanes and immutable GF(2) combination preserve
the exact checksum, unaligned access, complements and four-zero-byte field
semantics. All eight GCC/Clang generic/native strict/sanitizer runs pass 48635
cases; native paths additionally check all 1024 shift-table entries. ARM CRC
execution under local QEMU passed too, but is not native ARM performance data.
Only the unfrozen FTL-scale and physical-v2 CRC callers select the helper;
FNV, persistent bytes, synchronization and C3 semantics remain unchanged.

An unpaired x86 native-build observation of the same 8-KiB-QD1 32-MiB workload
took 0.623 s for Write and 0.171 s for Read, versus the earlier generic v2
1.072/0.318 s. The native Write remains about 54 MB/s; it retains exactly the
same 107873088 backend bytes, 54819 syncs and 17 checkpoints. Compiler native
flags can also affect other generated code: do not attribute every timing
change exclusively to CRC or treat this as a paired 10-GB/s acceptance result.

## Actual FTL/NFC windows

The explicit [format-2 FTL](../core/ftl-scale/README.md) and
[NFC PAGE2-R0](../core/nfc-page-v2/README.md) construction now consumes physical
batches inside one retained parent. It preserves physical main/OOB, checksums,
generation, program order, DATA→A→B durability and cancellation/recovery. R0 is
an explicitly functional model, not equivalence to the old C3 injected-fault,
retry or timing machinery. Legacy constructors and formats remain separate.

One actual 1-MiB Block Write used four 64-page DATA batches, four OPEN records,
four MAP_WINDOW records, no explicit CLOSE, 1215744 backend bytes and 60 syncs.
The real 32-MiB sequence used 39333696 backend bytes, 2163 syncs and one full
checkpoint, rather than the older small-command path's many checkpoints.
Reads used 128 batches of 64 pages and 384 backend reads for that sequence.

One unpaired native-x86 diagnostic measured 20.08 ms Write and 9.70 ms Read for
32 MiB via 1-MiB Block requests: about 1.67 and 3.46 decimal GB/s. Input pattern
generation and readback checking are outside the accumulated command execution
interval, while actual FTL/NFC/media work and induced checkpoint cost remain
inside. The fixture and byte-call observers remain present. These short results
do not prove 10 GB/s, a 1–3% loss, ARM performance, or native NVMe bandwidth.

GCC and Clang ASan/UBSan pass real aligned/unaligned/RMW, SELF restart, holes,
partial-window/live GC, pre-dispatch cancel, post-DATA A/B drain, UNKNOWN
quarantine/recovery and both constructor-format rejection directions. The
Linux-profile/lifecycle at this checkpoint also consumed the new binding but
still limited commands to 8 KiB; later Large/MQ2 extends this explicitly.
No legacy lifecycle or native SQ/CQE executor has
been duplicated to obtain a faster result.

```sh
make -C frontends/headless-scale -f ftl.mk FWLAB_CRC_NATIVE=1 check-parent-window-v2
make -C frontends/headless-scale -f ftl.mk FWLAB_CRC_NATIVE=1 check-window-v2-cost
```

Use the same existing capped tmpfs, serially. None of these entries formats an
existing image or qualifies physical persistence. Remaining work is matched
measurement of actual bottlenecks; the subsequently adopted native large/MQ2
path has its own evidence. No expansion of the correctness framework is implied.

## Staged route

One subsequent optional POSIX descriptor reuses file validation only inside a
single synchronous physical operation, with complete entry/exit checks and an
explicit exclusive-backend assumption. The default callback-validation profile
is unchanged. See its [detection-interval limits](../media/file-nand-v2/README.md).

Three alternating local x86 16-MiB comparisons, using the same reusable buffers
and real physical/NFC engines, measured these median NFC Write rates:

| Group | Default callback checks | Operation-boundary checks |
| --- | ---: | ---: |
| 8 KiB | 1.159 GB/s | 1.574 GB/s |
| 256 KiB | 5.195 GB/s | 5.749 GB/s |

The actual byte calls, written bytes, CRC/OOB validation and three physical
barriers are unchanged. These are small working-set diagnostics, not the earlier
ARM 512-MiB measurements, native NVMe rates or a 10-GB/s/1–3% acceptance. The
remaining gap is real; partial syscall reduction is not the complete solution.

| Stage | Deliverable | Boundary |
|---|---|---|
| P0 | Byte-equivalent CRC repair | Implemented; does not close throughput goal |
| P1 | Remove proved redundant work in unfrozen storage/wrappers | Keep existing ordering and barriers; frozen NFC is not silently rewritten |
| M1 | Scaled native binding, synchronous HIF pump, two real I/O queues and three MSI-X vectors | Implemented with ONE global I/O frame plus ONE Admin reserve, not a per-queue credit pool |
| P2 | Explicit large-transfer profile, bounded buffer pool and complete PRP graph | One outer Block token; private subgroups, prefix-aware failure and FUA |
| P3 | Versioned low-amplification physical media/NFC batching and packed/incremental FTL metadata | Physical PPA/OOB/generation, recovery and durability remain real |
| I1 | Native and same-function M5 integration | One immutable candidate and matched performance/correctness confirmation |

P1 and M1 can proceed independently. P2 and P3 share buffer, prefix and durability
contracts. A merely faster CRC or a larger MDTS constant cannot close the gap.

The lower P2-A seam is now implemented: an explicit extended FTL constructor
retains one Block request up to 1 MiB, streams existing v1 subgroups and permits
CP/GC at resolved boundaries. The real-media adjacent-buffer journey passes
GCC and Clang ASan/UBSan, including prefix/cancel/recovery and live GC while owned.
It adds 640 bytes of parent control state, not a per-command 1 MiB payload.
That lower seam alone is not a large NVMe profile, native or throughput result;
later native evidence is recorded separately in the current matrix.
See the [FTL construction and test boundary](../core/ftl-scale/README.md).

### Multi-queue ownership

The adopted profile has two I/O queues, depth32, Admin vector0 and I/O vectors1/2.
It negotiates supported paired queue counts rather than the Host CPU count.
The earlier proposed two-Admin/fifteen-per-I/O credit split was not implemented:
there is ONE global I/O frame plus ONE Admin reserve. Host queue depth is not
the storage engine's accepted-operation window. See [ADR-0014](adr/0014-native-profile-and-serial-mq2.md).

Use bounded round-robin capture, admission and publication; one blocked/full CQ
must not starve the other queue or Admin. Queue incarnation and captured CQ
association remain with each request. Equal CIDs on different queues cannot
alias. Queue deletion is an asynchronous closing/draining transaction, not a
permanent cached busy error. Reset and M5 drain cover every queue, mapping,
buffer, publisher and vector/PBA reference.

### Large requests and persistence

A 1 MiB request can span 257 controller 4 KiB pages when unaligned. Its complete
bounded DMA graph and buffer credits must exist before effects. The FTL retains
one aggregate Block result while initially streaming existing small mapping
groups. Parent state and active subgroup state must be separate so checkpoint
and GC can run between resolved groups without credit deadlock.

That serial small-group schedule is provisional. At QD1, the later batching
engine must form windows within the same parent command rather than wait for
another Host command. Respect page/RMW dependencies and dispatch nonempty partial
windows at parent tail, dependency/maintenance fences, available-credit limits,
close/cancel or lack of currently issuable work. Low space can shrink a window;
publication still advances only an ordered committed prefix. Inter-parent
packing is optional, not a hidden prerequisite for theQD1 target.

Success/FUA waits for all required groups to become durable. Known partial
failure and uncertain journal completion remain different outcomes; do not
promise whole-command atomicity or authorize failed-Read DMA-out. Flush uses a
bounded namespace-wide ordering fence and an FTL-owned durability frontier.

New physical batching, packed journal records and incremental checkpoints need
explicit versioned contracts and recovery cases. A single-payload physical-home
design is gated on durable intent, consumed/torn recovery, packed-neighbor
protection, erase-generation accounting and safe log reuse. It cannot declare
an interrupted dirty cell erased, bypass NFC or address the backing file by LBA.
Old images are not automatically reformatted or converted.

## Stop rules

Stop a stage after its named real journey, source confirmation and cost report.
Do not add a new test framework, recursive mutation programme or full CI after
every micro-change. Correct original findings and directly induced changes only.
If measured throughput misses the objective, report the remaining gap and advance
the necessary bounded design dependency; do not relabel it as performance success.

The native startup/reset budget must match advertised readiness timeouts at the
new capacity. Existing M5 revoke/drain/zero/grant ordering and one authoritative
SQ consumer/CQE publisher remain. Raw-device deployment, physical NAND, broader
NVMe features and real PCIe signaling are separate capabilities.
