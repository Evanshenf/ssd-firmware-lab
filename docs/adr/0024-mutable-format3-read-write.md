<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0024: One mutable format3 instance with the existing read/write schedules

Status: accepted for the bounded construction at `03cd40db3694f40fa2ed59f18436a1464a1e367e`;
one independent exact-source confirmation returned NoRequired/STOP. Builds on
[format3 writes](0021-multihead-ftl-write-waves.md),
[channel workers](0022-channel-worker-execution.md) and
[independent-plane READ](0023-independent-plane-read.md).
See the [real combined result](../results/2026-09-14-mutable-read-write.md).

## Close the composition gap without replacing algorithms

B/C's original format3 constructor allocates a write pool and reads serially.
N1/D allocate a read pool on format2, prepare with ordinary writes, then enter
readonly mode. Their separate successes did not establish one mutable SSD
instance with both schedules.

The new `fwlab_ftl_scale_init_read_write_v3()` and paired size function allocate
both existing pools. One checked layout is:

```text
existing FTL/window arena -> existing write pool -> aligned existing read pool
```

Do not overlay pools because they normally alternate, or invoke both old
constructors sequentially at their shared tail offset. Construction initializes
common state once, attaches disjoint pools and records the full arena size.
Persistent format stays3; no conversion of format1/2 or new map/OOB format.

## Scheduling capability is not write permission

| Construction/state | Format | Pools | Parallel READ | Readonly |
|---|---:|---|---:|---:|
| Original B/C |3|write|0|0|
| N1/D preparation |2|read|0|0|
| N1/D after existing transition |2|read|1|1|
| New combined constructor |3|read + write|1|0|

A private `parallel_reads` capability controls READ dispatch. `read_only`
retains its non-READ admission restriction. WRITE still selects the existing
wave, and Flush still follows the existing frontier path. No schedule switch,
UID reset or provider replacement is needed between commands.

Reuse `scale_storage_multihead_lab_factory_init()` with construction-only
`multihead_read_schedule`: SERIAL0 keeps B/C defaults, PARALLEL1 selects the
new size/init pair. Existing NAND `read_policy` and optional channel executor
remain orthogonal frontend choices. The combined mode is not D's
`parallel_channel`/readonly-transition mode; no additional factory is added.

## Why writable READ snapshots remain stable

There is still **one Host parent**, not concurrent READ/WRITE/GC parents.
Admission rejects new parents while parent/work, metadata or IO remains owned.
Whole IO idle excludes both active pools; control IO excludes the read pool.
Clean-boundary maintenance also requires no head reservation.

An intermediate READ publication may clear `work.kind`, but both parent
ownership and read-pool active state remain. Thus it cannot open a new-parent,
GC or checkpoint gap. The step driver advances the read pool exclusively
while active. Existing map snapshots, block UID/generation/OOB checks and the
pre-publication map comparison remain. No map lock, per-PPA pin or new GC
algorithm is required for this single-coordinator construction.

READ/write/RMW/metadata use the same monotonic NFC UID issuer. The next
operation's normal backpressure path retires prior lower ACKs. Flush is a
durable frontier, not a certificate that every lower ACK is retired.
Do not hide control work with a manual drain between every Host command.

READ cancellation stops new issue/publication and drains accepted children.
An inactive write pool contributes no stale READ prefix. Safe drained cancel
can be followed by another write; quarantined NAND/control failures cannot be
cleared to force that result. Accepted writes retain DATA/MAP reconciliation.
Close waits for parent retirement, both pools, metadata and real lower closure,
including worker joins when selected. No Host authority is reminted.

## Evidence and STOP

The [fixed Block/J0 journeys](../results/2026-09-14-mutable-read-write.md) execute
interleaved mutable commands, actual GC-created two-plane reads, checkpoint,
partial-publication exclusion, cancellation, accepted READ/WRITE close and
same-format recovery with continued writing. Block uses a full namespace byte
oracle; legal J0 verifies partial-RMW neighbors and the real NAND result path.

Stop after those bounded cases, affected checks and one exact-source
confirmation. No new test framework, worker matrix or old capacity replay.
NAND, actor/job/worker, mapping/GC/recovery and physical-media algorithms are
unchanged by this composition. The native worker still selects its prior
R0/format2 path. No native/NUMA/pacing/vendor/16KiB/4TB or bandwidth claim follows.
