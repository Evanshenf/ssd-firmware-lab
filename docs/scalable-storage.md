<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Scalable storage development

This plan extends the [vertical-spine preview](results/2026-09-05-vertical-spine-preview.md).
It does not change that release's tested envelope or claim that a large-capacity
SSD has already been implemented. The first real full-workload target is 64 GiB;
128 GiB depends on provisioned storage and memory. Larger capacities are design
and arithmetic targets until actually exercised.

## Preserve the existing data path

Profile -> shared lifecycle -> aggregate Block -> FTL -> NFC -> physical NAND
media. The Host interface still owns queues, raw addresses, DMA and completion
publication. GC, RMW and metadata children remain below Block. Host/Guest use the
same implementation through the existing exclusive ownership transition.

Namespace capacity, physical NAND geometry and backing-file size are distinct.
No transport may translate an NVMe LBA directly into a backing-file offset.
Replacing a file with a raw block substrate remains separate future work.

## Algorithm baseline

Use established techniques first, then measure possible optimizations:

- One compact resident committed page map plus a bounded pending overlay. A
  provisional 16-byte entry per 4-KiB logical page costs 256 MiB at 64 GiB,
  512 MiB at 128 GiB and 4 GiB at 1 TiB. These are arithmetic, not observed peak
  memory; block metadata, buffers and filesystem cache are additional.
- Commit pending updates in ordered prefixes when the overlay fills. It must
  not grow until the Host sends Flush. Preserve old committed pages until their
  replacement is logically durable.
- Greedy GC with per-block live/reclaimable/pinned summaries and bounded victim
  work. Read victim OOB for reverse logical identity, then validate its exact
  physical/data incarnation against the map. Do not scan the entire logical
  map for each victim page. Keep independent data and metadata emergency space.
- Wear-aware free-block allocation as the first wear policy; background/static
  wear leveling and hot/cold streams are later measured refinements.
- Stream multi-page checkpoints with an exact committed frontier, allocator
  roles and recovery tails. A first implementation may pause map changes while
  encoding instead of allocating another full RAM snapshot. Old recovery state
  is reclaimed only after its replacement and dependencies are durable.
- Reuse bounded in-flight slots and logs. Persistent identities are wide,
  checked and preserved across recovery; old small experimental lifetime limits
  cannot be presented as sustained large-device support.

Demand-cached mapping is an established alternative described by
[DFTL](https://doi.org/10.1145/1508244.1508271). It is deferred until a measured RAM
budget requires its extra eviction and recovery machinery. The
[SPDK FTL design](https://spdk.io/doc/ftl.html) is another useful reference for
mapping memory, reverse metadata and reclamation, not a drop-in physical NAND
provider for this project. No new algorithm or comparative performance is
claimed by adopting these baseline ideas.

## Four bounded slices

| Slice | Actual deliverable | Exit condition |
|---|---|---|
| A | Compact physical file-NAND, second real binding at current tiny geometry | Real data path, recovery/GC and named physical redo cuts pass |
| B | Scalable map, geometry, checkpoint/replay, GC and practical runtime budgets | Same implementation crosses wider address/index boundaries and continues after reclamation/restart |
| C | Provisioned 64-GiB native workload; 128 GiB conditional | Full-capacity data verification, sustained overwrite, filesystem use and agreed recovery/ownership cases |
| D | A useful algorithm study, matched-semantic comparisons and reproduction package | Fixed-source research candidate, with benefits, costs and limitations reported |

The [compact media implementation](../media/file-nand-v1/README.md) is slice A.
It does not remove current M3P/NFC geometry limits. Firmware mapping formats and
physical media formats have separate version/compatibility boundaries. Old
preview images use the old executable/format; no implicit conversion is offered.

Before a large test, account for namespace, over-provisioning, firmware metadata,
OOB, physical redo and host-filesystem free space. A sparse file's apparent size
is not secured capacity, and a system disk must not be filled by the experiment.
Initialization creates a new image explicitly; recovery never formats a missing,
incompatible or damaged image.

## Keep validation finite

Use real vertical paths and direct isolated component checks with a fixed set
of failure scenarios. Run affected local checks during development and one
complete source-bound validation at a release candidate, not duplicate full CI
after every step. Do not create another mutation hierarchy or public observer
ABI to support this work.

If two checkpoints do not advance the actual journey, retain the concrete
counterexample and revise that boundary. After the agreed slice passes, move
forward. RAID, management interfaces, multiple RTOS ports, full NVMe, real NAND
and production endurance are not automatic conditions for completing this plan.

Persistent simulated block state still supplies erase-generation information.
The physical-NAND persistence/recovery contract remains open; a new backend is
not evidence of backend-only migration to silicon. Host process interruption,
modeled NAND partial effects and physical host power failure remain distinct.
