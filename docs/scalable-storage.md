<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Scalable storage development

This plan extends the [vertical-spine preview](results/2026-09-05-vertical-spine-preview.md).
It does not change that release's tested envelope or claim that the 64-GiB
workload has already been qualified. The first large full-workload target is 64 GiB;
128 GiB depends on provisioned storage and memory. Larger capacities are design
and arithmetic targets until actually exercised.

## Current implementation: SCALE-B2 bounded qualification

The [new scalable FTL](../core/ftl-scale/README.md) now connects to the same
headless Linux profile, lifecycle, Block contract, NFC executor and compact
physical NAND. Real 64-MiB and 256-MiB logical volumes passed disk-backed full
fills, interleaved half-volume overwrites, restart and complete readback.
The fixed interruption/active-close cases also passed. Neither a 64-GiB workload
nor larger native-worker deployment is claimed here.

The old M3P reference remains available for its existing formats. An explicit
construction runner selects one FTL, while the same ready-only logical volume
descriptor supplies Identify and request bounds. Scaling this implementation
does not require changing the Linux profile, lifecycle, Host DMA or Block ABI.
The new format uses a 16-byte committed map, indexed block summaries, streamed
checkpoints, two independent journal rails and atomic whole-victim GC. It starts
with write-through rather than a larger volatile mapping overlay.

Use `make -C frontends/headless-scale -f ftl.mk check` for the functional path;
the README above describes full-volume and named-cut commands and their scope.
The released native worker still uses its original 1-MiB profile.

The same full workload also passes as explicitly labeled tmpfs **functional**
regression. A dedicated 1-GiB mount and serial execution keep the observed
filesystem high-water at 339.1 MiB, separate from 5.7 MiB process maximum RSS.
FTL/NFC and synchronization calls are unchanged. These RAM-backed results do
not replace disk persistence, power-loss or storage-performance qualification.

## Completed interface checkpoint: SCALE-B1

The same headless program now creates and recovers two real logical volumes,
512 KiB and 1 MiB. FTL format 2 persists logical capacity and physical geometry;
recognized format 1 keeps its old 1-MiB meaning and encoding. The FTL exports a
volume descriptor only after recovery/cleanup succeeds. J0 pairs it with the
actual Block service and then constructs the Linux profile: Identify and range
checks consume the same immutable logical capacity. A recovery expectation is
an assertion, not a replacement capacity or an instruction to format.

There is one stored Block service for submit/query/cancel/retire. A logical
descriptor contains no file offsets, NAND allocation policy or Host address.
The companion interface remains private/provisional; two capacities are not
evidence of multiple independent FTL implementations. The lifecycle, Host DMA,
Block request ABI and kernel ownership implementation are unchanged.

Separately, an NFC-owned scaled constructor initializes the existing NFC model
with its real geometry. It reuses the original provider, scheduler, fault and
media execution code. Physical 80-MiB and 320-MiB address journeys exercise block
319 and linear page 81919, readback after restart, erase/reprogram and a modeled
partial-erase cut. The original constructor retains its small-profile limits.

These were **different evidence scopes**: the reference FTL uses its
256-entry representation ceiling, and the large physical-address tests do not
pass through a large namespace. Neither is a 64-GiB or fully populated storage
result. SCALE-B2 above supplies the separate scalable representation.

Run the bounded checks as an ordinary user:

```sh
make -C frontends/headless-scale check
make -C frontends/headless-scale -f nfc_geometry.mk check
```

The first command includes legacy-format preservation, both volume capacities,
Identify, boundary I/O, FUA, RMW, checkpoint rollover, GC, recovery expectation
rejection and close-before-ready. The second uses private regular files and
reports their paths and actual allocation. Those sparse physical-test images
are retained for inspection; their apparent sizes are not full-capacity proof.
Neither command loads a module, accesses a raw disk or changes the live lab.

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
- Any later volatile overlay must commit bounded ordered prefixes rather than
  growing until the Host sends Flush. The initial SCALE-B2 policy needs only
  the current uncommitted group and commits it before returning success.
  Preserve old committed pages until their replacement is logically durable.
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
It does not itself remove reference M3P/NFC geometry limits. B1 supplied the
separate scaled NFC constructor, and B2 supplies the larger FTL. Mapping formats and
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
