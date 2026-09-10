<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0013: Ready-volume scalable FTL, retained parents and PAGE2 windows

- Status: Recorded implemented decision; integration/release validation is separate
- Date: 2026-09-10
- Implementation baseline: `a6ee009bbca5932857d51c3a5f265e0b60183a76`
- Refines: ADR-0003, ADR-0007 and ADR-0010 for the scalable Block consumer
- Preserves: frozen small C3/C4/M3P oracles and the finite outer action vocabulary
- Related: ADR-0012 physical format; ADR-0014 native construction

## Decision

Keep namespace capacity owned by a ready FTL volume. Select a scalable FTL at
construction through the existing aggregate Block service, and keep large
command decomposition, mapping, GC and metadata scheduling inside that FTL.
Select PAGE2 through a separate constructor and FTL disk format, rather than
silently changing the older C3 model or extending the Host lifecycle into a
NAND scheduler.

### Capacity comes from recovered authority, not a frontend constant

`fwlab_ftl_scale_format_start(lba_count)` explicitly creates the logical format;
`fwlab_ftl_scale_recover_start(expected_lba_count)` reads it back. An expected
nonzero capacity is an assertion, not permission to resize; zero requests
discovery. `mapping_slots` is a RAM/resource ceiling and may reject a recovered
volume that does not fit, but is not the namespace's authoritative capacity.

`fwlab_ftl_scale_volume_query` returns the root's `lba_count` together with its
actual Block service only after recovery/cleanup has made the volume ready.
`bind_ready_volume` in [j0_construction.c](../../frontends/headless-j0/j0_construction.c)
checks that service identity against the constructed storage and passes the
volume to `fwlab_linux_profile_v1_adapter_init_limits`. Identify and protocol
range validation therefore consume the same ready volume that executes Block
requests. Changing a physical image's size alone does not expand a namespace.

This is an implemented seam, not a registry claim: J0 can construct the retained
M3P reference or the actual `scale_storage_factory_init` /
`scale_storage_window_v2_factory_init` binding. Selection has no error fallback
to a different FTL and does not format on recovery failure. The shared
[command-spine lifecycle](../../core/command-spine/README.md) is not taught FTL
mapping or NAND operation kinds by these factories.

### Bounded memory, private maintenance and persistent authority

The [scalable FTL](../../core/ftl-scale/README.md) uses:

- one 16-byte committed-map entry per allocated slot, one physical-page
  validity bit, a 32-byte block summary and indexed free/victim heaps;
- explicit roots, streamed checkpoint banks and independent A/B journal rails;
- reservation before work, out-of-place DATA, bounded RMW, whole-victim GC
  mapping publication and erase-intent/done reclamation;
- foreground, serialized checkpoint/GC work rather than a background or
  parallel FTL claim.

`sf_map_entry` size is asserted in
[ftl_scale_internal.h](../../core/ftl-scale/ftl_scale_internal.h).
`sf_meta_step` streams checkpoint/recovery and retires the old root before
new-epoch writes or reclaiming DATA it could reference. `sf_gc_step` performs
private relocation and `sf_record_validate_apply` updates the authoritative map.
Journal A becomes durable before B; valid replay dependencies, not the host
file's sync alone, decide mapping authority. Recovery can use one valid rail,
requires valid duplicate copies to agree, and fails closed where authority
cannot be reconstructed.

Map-sized checkpoint I/O and serialized maintenance are accepted initial
costs. No second full visible map or whole-volume recovery DATA table is added.
The default factory may allocate map slots up to the physical-page ceiling;
the arithmetic bytes for one logical map are not total process RAM.

### One retained Block parent, bounded child groups

The extended constructor accepts up to 2048 512-byte LBAs (1 MiB) in one Block
request. `sf_parent_admit` retains its original token, request and controller
buffer lease; `sf_parent_step` executes private subgroups and permits reserved
maintenance only at resolved boundaries. There is still one aggregate result
and one retirement obligation above FTL, not one Host action per NAND page.

The original C3/format-1 construction streams its small groups. Non-final
groups do not advance the parent's Host durability frontier; successful final
completion requires the whole requested dependency closure. Cancellation with
a known committed prefix is distinguished from uncertain mapping effects.
Neither format promises atomic all-or-nothing recovery for a 1-MiB command.

The caller keeps the controller buffer lease alive and Write input stable until
retirement. That internal lease is not Host DMA authority and is not an
immutable loan capability. Reset/revoke does not let private RMW or metadata
reconciliation mint new Host DMA. Those domains stay separate in the existing
Host/data-mover binding.

### Explicit FTL format 2 and PAGE2-R0

`fwlab_ftl_scale_init_window_v2` chooses FTL disk format 2 and the PAGE2 adapter.
The old constructors retain format 1 and C3. Root-format mismatch is rejected;
there is no mixed-format root, automatic conversion or recovery fallback.

One additional 270336-byte FTL window holds at most 64 full contiguous physical
pages with OOB. `sf_window_prepare` splits on RMW and allocation-block
boundaries; `sf_window_step` orders DATA, journal A, journal B and then the
`MAP_WINDOW` mapping transition. Accepted DATA reconciliation still drains its
mapping transaction after cancellation. Reads group only compatible consecutive
mapped runs, validate each page/OOB identity and generation, and copy a group to
the controller buffer only after those checks. Unmapped holes are supplied by
FTL, not by treating a file offset as an LBA.

The actual lower binding is:

```text
profile action -> j0 block_submit_action -> Block service
-> retained FTL parent -> private window/GC/checkpoint work
-> sf_nfc_page2_adapter -> PAGE2-R0 -> physical NAND v2 -> byte adapter
```

`fwlab_nfc_page_v2_model` is an explicit one-slot, caller-serialized functional
model. `try_submit` snapshots PROGRAM bytes before ACCEPTED and clears caller
payload pointers. An exact active-key retry does not resnapshot or issue a
second operation. A result occupies the slot until consumed/discarded.
`take_result` publishes Read bytes only for a validated result; mutating backend
errors are UNKNOWN when dispatch may already have affected media. This remains
copied controller-owned storage, not zero-copy or a retained caller loan.

R0 rejects injected fault, retry and timing settings it does not implement.
Physical page/OOB integrity, TORN/unknown, health and generation checks still
participate. One step performs at most one synchronous physical operation: a
work bound, not a disk-latency bound, calibrated NAND timing, C3 fault-model
equivalence or 64-way die parallelism.

## Compatibility and limits

- Physical media version, FTL disk version, PAGE2 interface/model revision,
  namespace capacity and Host transfer/queue profile are separate decisions.
  A 1-MiB Block parent does not force a 1-MiB namespace or claim that every Host
  transport already admits such a request. Native profiles are fixed in
  [ADR-0014](0014-native-profile-and-serial-mq2.md).
- Existing C34/C35, C43-P1 and M3P formats remain historical references; no
  frozen oracle is broadened by changing its constants. Current scalable
  production support is Read/Write/Flush, one namespace and write-through
  behavior. A Block TRIM enum is not implemented scalable TRIM support.
- GC uses a bounded greedy whole-victim policy with erase-aware allocation.
  Advanced WL, background/parallel GC, large-capacity production guarantees,
  RTOS/real-NAND integration and independent parallel FTL workers remain later
  capabilities, not prerequisites to document this implemented path.
- P01 is still real: `ftl_scale_nfc_v2.c` consumes
  `final_erase_generation`; recovery/reclamation persists it, and
  `publish_read` compares it with the map before `sf_data_oob_validate`.
  Simulated physical block metadata supplies that fact today. The durable
  owner/recovery design for physical NAND, including blank/interrupted-erase
  blocks, is not implemented. [ADR-0012](0012-versioned-physical-nand-media.md)
  explicitly prevents a backend-only silicon-migration claim.

## Source anchors and bounded evidence use

- [ftl_scale_runtime.c](../../core/ftl-scale/ftl_scale_runtime.c): construction,
  explicit format/recover starts, ready-volume query and aggregate Block ops.
- [ftl_scale_parent.c](../../core/ftl-scale/ftl_scale_parent.c),
  [ftl_scale_window.c](../../core/ftl-scale/ftl_scale_window.c): retained request,
  committed-prefix handling and private windows.
- [ftl_scale_recovery.c](../../core/ftl-scale/ftl_scale_recovery.c),
  [ftl_scale_gc.c](../../core/ftl-scale/ftl_scale_gc.c),
  [ftl_scale_mapping.c](../../core/ftl-scale/ftl_scale_mapping.c): persistent and
  mapping authority, reclamation and recovery.
- [scale_storage.c](../../frontends/headless-scale/scale_storage.c),
  [j0_action_drivers.c](../../frontends/headless-j0/j0_action_drivers.c): real
  construction and the single aggregate Block call path.
- [nfc_page_v2.c](../../core/nfc-page-v2/nfc_page_v2.c),
  [ftl_scale_nfc_v2.c](../../core/ftl-scale/ftl_scale_nfc_v2.c): accepted payload
  ownership, result facts and actual typed NFC consumption.

Reuse exact-source functional, restart and capacity evidence; do not turn an
available test target or an isolated layer rate into a new native/64-GiB
qualification. This decision adds no test framework or unbounded review gate.
