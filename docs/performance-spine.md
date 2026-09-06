<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Single-core performance and multi-queue roadmap

This is an unreleased implementation roadmap, not a claim of10 GB/s throughput,
PCIe compliance, production readiness or completed multi-queue support. The
fixed-profile preview and its historical evidence remain unchanged.

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

## Staged route

| Stage | Deliverable | Boundary |
|---|---|---|
| P0 | Byte-equivalent CRC repair | Implemented; does not close throughput goal |
| P1 | Remove proved redundant work in unfrozen storage/wrappers | Keep existing ordering and barriers; frozen NFC is not silently rewritten |
| M1 | Scaled native binding, synchronous HIF pump, two real I/O queues and three MSI-X vectors | One firmware executor,32total command credits; initial8 KiB transfers |
| P2 | Explicit large-transfer profile, bounded buffer pool and complete PRP graph | One outer Block token; private subgroups, prefix-aware failure and FUA |
| P3 | Versioned low-amplification physical media/NFC batching and packed/incremental FTL metadata | Physical PPA/OOB/generation, recovery and durability remain real |
| I1 | Native and same-function M5 integration | One immutable candidate and matched performance/correctness confirmation |

P1 and M1 can proceed independently. P2 and P3 share buffer, prefix and durability
contracts. A merely faster CRC or a larger MDTS constant cannot close the gap.

### Multi-queue ownership

Start with two I/O queues, depth32, Admin vector0 and I/O vectors1/2. Negotiate
actual supported queue counts rather than the Host CPU count. Initially share
32 command records: two Admin credits and fifteen per I/O queue. The storage
engine's accepted-operation window is a separate resource bound.

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
