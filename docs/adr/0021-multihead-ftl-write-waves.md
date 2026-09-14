<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0021: Format3 physical head domains and ordered write waves

Status: accepted for bounded LAB format3, source
`3bce6ab19ab4613e300345335ddf223ccd3bf670`. Source confirmation is recorded in
the [result](../results/2026-09-14-multihead-write-waves.md).
Implements B from [ADR-0020](0020-cooperative-nand-channel-domains.md), not C/D.

## Physical domains, capacity and persistent format

Format3 fixes a deterministic domain policy from persisted geometry:

```text
heads_per_channel = min(luns_per_channel, floor(4 / channels))
domain = channel * heads_per_channel + (lun % heads_per_channel)
```

There are at most four domains, none crossing a channel. All LUNs, planes and
blocks remain eligible; OS worker and NUMA identities are absent. A domain can
have no DATA blocks after the metadata prefix. Choose any usable unselected
domain, not an unavailable preferred one, and narrow the wave when appropriate.
Do not initiate GC merely to fill four runs.

The existing metadata layout and capacity bound remain. For the 32-block small
geometry, metadata consumes blocks 0–11; DATA-domain counts are `[0,4,8,8]`.
Mandatory four-head readiness would incorrectly discard usable capacity.
Keep **one global emergency free block**, not one reserve per domain. When no
run can progress, seal the finite heads and use global reclamation/GC. Once all
heads are victim-eligible, the existing capacity inequality still guarantees
an eligible sufficiently sparse victim under its healthy-media assumptions.

The existing free-heap allocation is partitioned into domain segments. The
same heap algorithm and wear/block ordering are reused; global free selection
compares at most four roots. The victim heap stays global. No hot-path scan of
every free block or duplicated heap implementation is introduced.

Keep the 16-byte map/block entries and current root/CP/rail/DATA-OOB layouts,
with exact format discriminator **3**. No persisted runtime head table is
needed: geometry plus this version derives each block's domain. Rebuild permits
one HOST_OPEN block per domain and rejects duplicates within a domain. OPEN,
CLOSE and MAP match that domain's head, block UID, generation and committed end.
A full MAP_WINDOW closes only its own head. Reservations are volatile; recovery
seals every recovered HOST_OPEN/GC_DEST and checkpoints before admission.

Old constructors still require formats1/2. The new constructor requires3;
there is no automatic fallback, migration or image conversion. A future change
to the domain policy requires an explicit format decision.

## One parent, finite DATA runs and one metadata issuer

`fwlab_ftl_scale_init_multihead_v3()` allocates one private write pool with at
most four runs. Each owns its IO token, controller-byte snapshot, before/after
map facts and exact physical reservation. Physical-page and mapping-delta
counts remain separate concepts, even though both are 4-KiB grains here.

Finish required space work and durable OPENs before final map snapshots or
reservations. Replan after maintenance. Resolve every partial RMW before
allocating/admitting DATA UIDs. All UID/block-UID/record/Host-sequence issuance
remains on the coordinator; the FTL imports no NFC clock or thread primitives.

Fill eligible DATA in UID order before lower advancement. A backpressured UID
remains first; collectors handle accepted tokens, not fresh submissions. A
wave can span multiple lower batches. Collect and validate every accepted DATA
result before staging MAP records in logical order.

Preparing metadata is not NAND admission. The existing channel hub returns BP
until previous DATA retirement ACKs are gone; the generic BP driver advances
that work. Actual MAP ACCEPTED therefore supplies the needed lower barrier.
No new idle callback and no inference from `step()==0` is required.

Scalar control-IO availability permits retained write facts during RMW/MAP.
Whole FTL idle, maintenance, retirement and close still exclude an active pool
and all head reservations. The existing three-way IO/metadata/work driver stays
in place, so write-pool work cannot starve its own RMW or MAP runner.

Call parent group-success **once per aggregate resolved wave**, after all MAPs.
Calling it per run would clear work ownership and expose a cancelled parent to
premature clean-boundary termination. A private failure summary retains known
committed MAP-prefix LBAs. The last MAP advances the Host frontier only when
its accepted prefix actually completes the entire parent.

## Close, uncertainty and GC

Normal close stops the unaccepted logical suffix but never cancels an accepted
PROGRAM. Accepted DATA and required ordered MAP reconcile internally without
restoring revoked Host authority. An incomplete parent has no SELF witness.

Any DATA/fact failure before MAP leaves the entire current wave unmapped.
Latch the fault, consume accepted siblings, then use existing NFC reset/step/
quiescent to drain ACKs before entering recovery-required state. Successful
unmapped DATA remains orphaned under durable OPEN. Unusable control APIs retain
nonzero ownership; they cannot be turned into a successful drain certificate.

A MAP failure is different: earlier durable records remain, and a valid rail
of the uncertain MAP may be replayed on recovery. Retain known prefix and
UNKNOWN/no SELF semantics; do not claim atomic-wave MAP or rollback. Pending
control-MAP ownership can remain quarantined. Process-reopen evidence is not
a graceful successor-owner grant or a physical power-loss qualification.

Serial GC runs between waves with no reservations. Seal all heads, choose a
global victim and free destination, relocate using the existing bounded
transaction, then promote the destination into **its own** physical domain.
Reserve `head_count + 4` journal records for head closure plus relocation/erase,
not the old fixed five. The existing small-block/live-page limit remains.

## Scope and STOP

The real path is unchanged protocol/lifecycle/Block → format3 FTL → cooperative
PAGE2 hub → timed NFC → actual physical-v2 shards. Serial READ/Flush reuse the
existing window path. NAND children do not enter the outer command graph.
The native factory remains R0/format2; B does not change its selected behavior.

B stops after legal multi-head J0 I/O, four-run/out-of-order/BP cases, close and
DATA/MAP-failure recovery, rollover/one relocation plus bounded reserve-pressure
progress, affected checks and one exact-source confirmation. No clean streak,
new test framework or replay of unrelated old large-capacity campaigns.

C real OS workers, D independent-plane READ, 16-KiB whole physical pages, 4-TB
scale, vendor timing, TLC/pSLC and hardware erase-generation persistence remain
separate work. B provides neither a thread speedup nor a new throughput result.

## Source

- [Head domains](../../core/ftl-scale/ftl_scale_heads.c),
  [mapping/rebuild](../../core/ftl-scale/ftl_scale_mapping.c),
  [GC](../../core/ftl-scale/ftl_scale_gc.c).
- [Private write pool](../../core/ftl-scale/ftl_scale_write.c),
  [parent semantics](../../core/ftl-scale/ftl_scale_parent.c),
  [runtime](../../core/ftl-scale/ftl_scale_runtime.c).
- [J0 cases](../../frontends/headless-scale/test_multihead_j0.c) and
  [large Block cases](../../frontends/headless-scale/test_multihead_parent.c).
