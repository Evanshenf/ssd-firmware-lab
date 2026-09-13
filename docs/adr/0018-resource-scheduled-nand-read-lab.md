<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0018: Resource-scheduled NAND and the first parallel-read slice

- Status: Selected design; N0 lower-binding implemented/checked, N1--N4 pending
- Date: 2026-09-13
- Starting baseline: `bd522bb4505cc24e144920cd93319aaae947b01a`
- Refines: [ADR-0012](0012-versioned-physical-nand-media.md) and
  [ADR-0013](0013-scalable-ftl-and-page-windows.md)
- Preserves: ordinary PAGE2-R0 behavior, physical-v2 format and Host ownership
- N0 evidence: [source-bound lower-binding results](../results/2026-09-13-nand-read-lab.md)

## Decision

Make simulated NAND throughput follow resource occupancy and legal overlap,
not a multiplier derived from capacity, channel count or a target Host rate.
Preserve the existing NVMe/lifecycle/Block/FTL/NFC/physical-page path. NAND child
work remains inside the storage implementation; the outer action program does
not become a flash scheduler.

The first slice is a LAB-only multi-slot READ NFC using the existing physical-v2
media, followed by parallel physical READ runs inside one retained Block parent.
This proves a useful asynchronous boundary without simultaneously changing
multi-open-block write ordering, metadata publication and recovery format.
It is not an additional NVMe executor or a claim of modern NAND write performance.

## Current limitations motivating the change

FTL receives geometry and produces channel/LUN/plane/block/page addresses, but
the current implementation has one Host write head and one `io/window/parent`.
PAGE2-R0 accepts one same-block group at a time. Its 64-page batch amortizes
software work; it is not 64-way NAND parallelism.

The current geometry validates at most four channels, four LUNs per channel,
four planes per LUN, 4-KiB main/128-byte OOB and 32/64 pages per block. Combined
with its block-address width, it cannot describe a 4-TB volume by changing a
capacity constant. A full 16-byte mapping entry per 4-KiB logical unit would
alone require 15.625 GB for a 4-TB decimal namespace. Existing full-map
checkpoint/recovery and whole-victim GC costs remain separate scaling work.

## Resource and timing ownership

The private LAB construction explicitly maps each `(channel, LUN)` to its
target/CE and package/die membership. Membership is not a performance multiplier;
this initial profile conservatively has one read register/array owner per LUN.
It does not infer independent plane programs from the number of planes.

Each physical page follows:

| Stage | Resource ownership | Data action |
| --- | --- | --- |
| Command/address | Channel plus reserved LUN/read register | No media read |
| Array read | LUN/register; channel released | At array-ready, read actual main/OOB and validate media facts |
| Data-out | Channel and retained register | Charge the transfer for main plus OOB bytes |
| Result retained | Operation/result buffer, not the freed hardware resources | Copy valid output only on result consumption |

Four finite request slots allow independent resources to overlap. One serialized
worker advances integer-time events; no thread per die, sleep per page or
allocation per event is required. `step(budget)` counts state transitions.
Admission uses current model time and must not be backdated. A same-block
multi-page group retains page order; it does not become a parallelism multiplier.

An unstarted cancelled page need not issue. A started cancelled page drains its
defined stages and discards the payload; no later pages of that group start.
Reset closes admission but does not immediately reuse owned resources or result
buffers. All accepted results must be consumed/discarded before quiescence.
Once terminal, the result is immutable: a late cancel does not choose a second
outcome. The upper owner still gates whether that retained result may be published.

## Explicit preparation and measured interval

The LAB uses one immutable provider/media/topology construction with a one-way
`PREP -> TIMED_READ` phase. PREP runs existing R0 physical operations, including
format/recovery preparation, outside the timed observation. It does not replace
the ordinary R0 default.

The phase transition requires all upper startup/normalization, parent/window/io
work and all lower operations/results to be idle. It uses a private live-idle
check, not the closed-only `quiescent` API. It does not reset UIDs or rebind a
live FTL. TIMED_READ rejects mutating NAND commands rather than assigning them
zero simulated cost. The later FTL integration must also reject non-READ Block
admission before modifying FTL state, and must not run hidden unmeasured
checkpoints while closing that interval.

The first times are explicitly synthetic LAB parameters. Virtual service time,
simulator CPU/copy/CRC/backend cost and native Host wall-clock rate are different
measurements. Fast-forwarding events does not demonstrate native throughput.
Wall-clock pacing and vendor calibration remain explicit later bindings/work.

## Bounded migration

1. **N0:** multi-slot LAB READ NFC, real physical-v2 page/OOB binding, finite
   resource timelines, pressure and cancellation/drain. Lower-binding evidence
   only; no parallel FTL or native qualification.
2. **N1:** one existing Block parent submits multiple prepared physical read
   runs before advancing NFC. Collect by token, publish actual controller-buffer
   spans by verified logical prefix, and keep advancing/collecting on error until
   every sibling drains. No concurrent writes/GC/checkpoint in this first reader.
   Prepare cross-resource mappings through real serial FTL writes, not map edits.
3. **N2:** timed program/erase and multiple write heads, with OPEN/MAP/CLOSE,
   reservation, metadata sequencing and recovery updated together. NAND
   completion order must not redefine the Host durable frontier.
4. **N3/N4:** real geometry/program modes, large-capacity mapping/checkpoint/GC
   budgets, and native integration/calibration. A large address-space constructor
   is not a full-capacity data or performance test.

N0 ends at its finite lower-binding cases and advances to N1. N1 ends at real
cross-resource read, arbitration, out-of-order result, partial/hole, failure,
cancel/drain and same-format recovery cases. Neither opens another generic
testing framework or an open-ended clean-review campaign.

The physical-NAND erase-generation persistence gap documented in ADR-0012/0013
remains open. This decision does not promise backend-only migration to silicon,
bit-level power-loss modeling, real TLC/pSLC behavior or a 4-TB/8-GB/s result.

## Source anchors

- [PAGE2 contract](../../include/fwlab/contracts/nfc_page_v2_provider.h): one
  serialized caller, token-keyed submission/cancellation/results and close/drain.
- [LAB construction](../../include/fwlab/private/nfc_page_v2_lab.h): explicit
  finite configuration and private evidence; not a stable observer ABI.
- [Existing FTL window](../../core/ftl-scale/ftl_scale_window.c): current
  sequential run grouping and ordered mapping/frontier publication.
- [Physical-v2 binding](../../media/file-nand-v2/physical_nand_batch.c): physical
  main/OOB access, not a Host-LBA file offset path.
