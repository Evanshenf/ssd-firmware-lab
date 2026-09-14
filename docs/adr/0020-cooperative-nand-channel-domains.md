<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0020: Independent NAND channel domains before parallel FTL writes

Status: design accepted; bounded A implementation at `647fd732034a1b6e7c66a6989f16d26551125af1`.
Source confirmation is recorded in the [result](../results/2026-09-14-channel-domains.md).
Extends [ADR-0019](0019-timed-nand-mutations.md); does not replace its profile.

## Decision and physical ownership

Use a logical execution domain per channel. LUNs retain independent array and
command/register state; channel transfers arbitrate one shared bus. Package/die
membership does not multiply speed. OS workers and NUMA placement are execution
choices, not NAND geometry. Plane-specific operations require actual profile
support; a geometry field alone does not supply independent-plane execution.

First construction **WAVE4-LAB4K-A** is cooperative, with at most four channels
and **four total request credits**. It uses current 4-KiB main/128-B OOB pages,
explicit synthetic timing and existing serial format-2 FTL. It is not a
multi-threaded, multi-head, modern 16-KiB or 4-TB construction.

Each channel owns one unchanged physical-v2 engine and one file, with local
geometry `channels=1`, local PPA channel zero, unique child UUID, transaction
sequence, two banks and scratch. An immutable physical-assembly manifest binds
global UUID/geometry to exact channel ordinals, child UUIDs, local geometry,
format, size and fixed relative filename. The manifest is portable LE with CRC.
PPA routing changes only the channel address; main/OOB bytes remain opaque.
There is no logical-LBA-to-file route or synthetic global media sequence.

Create uses a new empty private directory, volume lock and fixed-order shard
holders. Child formats and the manifest are synchronized before publication.
The manifest is published without replacing another name; interrupted pending
publication is retained and rejected on restart. Recovery creates/resizes
nothing, and checks child identity against the manifest using fresh runtime
holders. Device/inode is not persisted as portable identity. Physical assembly
readiness does not prove FTL initialization; blank FTL recovery rejects, never
autoformats. No arbitrary old-shard mixing or multi-file atomicity is claimed.

## Closed dispatch batches and time

The FTL remains the single serialized caller of a PAGE2 hub. PROGRAM is copied
into an owned hub frame before ACCEPTED; the existing child NFC copies that
frame again on admission. Both copies are retained and counted, not zero-copy.

The first step seals all batch ingress before any child advances. Each child
admits its requests at the same committed global model time, in UID order.
Bounded cooperative steps advance the existing child event loops and internally
collect actual results. JOIN waits for real callbacks and all terminal reports,
then publishes results and commits the maximum child time. It never waits for
the upper caller to consume a result that is still hidden before JOIN.

Every next batch uses that committed time as each idle child's real admission
floor. For example, a metadata request on an idle channel cannot start at time
zero after the DATA it depends on completed at 100 us on another channel.
No new dependent work or lower cancellation is injected during a sealed batch.
Arbitrary timed arrivals and modeled shared cross-channel ECC/DRAM/thermal
resources require a finer scheduler later, not a pretend independent clock.

Take/discard consumes the caller's result but retains its frame and credit until
a later bounded retirement-ACK step. No next batch admits until the previous
batch fully retires. FTL submission backpressure must therefore drive control
work even if no accepted FTL IO remains. Ordered READ fill owns new submissions;
result collection cannot let a higher UID bypass an unaccepted lower UID.

## Close and failures

The explicit WAVE4 hub treats cancellation of accepted requests as drain-only
control; it never forwards it to the child N2a cancel mechanism. The upper owner
suppresses Host publication. Existing serial FTL still stops its unaccepted
Host DATA suffix. An accepted PROGRAM can complete and reconcile internal MAP
after Host close; internal buffer ownership does not restore Host authority.

NFC reset closes hub admission, then drains accepted work, caller results and
retirement ACKs before closing children. Pending actor reports or missing
callbacks are not zero. A failed shard does not stop already accepted healthy
siblings from draining; results retain their actual per-request effects.
Unusable child control/report APIs retain ownership instead of inventing JOIN.
The composition closes media only after full NFC/FTL finalization. The aggregate
scalar media route is for serialized construction/quiescent diagnostics, never
independent access to actor-owned state.

## Finite sequence

A ends after real assembly/recovery, independent channel/LUN work and idle-time
floor, snapshot/JOIN/ACK/BP/final-close ownership, and the existing serial FTL/J0
journey plus affected portability and one bounded source confirmation.

The following remain separate, unimplemented slices:

- **B:** multi-head format 3, finite DATA waves and one ordered MAP issuer.
  OPEN/CLOSE/MAP/rebuild/GC-destination semantics change together. Before-MAP
  DATA failure leaves the current wave unmapped; failure during MAP retains
  its actual durable prefix. No whole-wave atomic MAP is promised.
- **C:** real channel job transport with cooperative/one/four data workers,
  identical logical topology, credits and batch inputs. Count coordinator CPU
  separately. A's cooperative state is not already a thread-safe interface.
- **D:** actual independent-plane READ, with shared channel traffic and
  same-LUN mutations initially exclusive. Multi-plane PROGRAM/cache/suspend
  remain explicit later capabilities.

These do not require native 8-GB/s or full 4-TB qualification to close. Future
16-KiB NAND needs whole-page program effects plus explicit mapping-slot, OOB,
packing/RMW and GC representations; four 4-KiB programs are not one physical
16-KiB program. Hardware erase-generation persistence gap P01 remains open.

## Source anchors

- [Physical assembly](../../media/file-nand-v2/channel_volume.c) and its
  [portable physical binding](../../include/fwlab/private/nand_channel_v2.h).
- [Cooperative PAGE2 hub](../../core/nfc-page-v2/nfc_channel_v2.c) and
  [existing timed child](../../core/nfc-page-v2/nfc_page_v2_lab.c).
- [FTL BP progress](../../core/ftl-scale/ftl_scale_nfc_v2.c),
  [ordered READ fill](../../core/ftl-scale/ftl_scale_read.c),
  [existing storage construction](../../frontends/headless-scale/scale_storage.c).
- [Actual J0 fixture](../../frontends/headless-scale/test_channel_j0.c).
