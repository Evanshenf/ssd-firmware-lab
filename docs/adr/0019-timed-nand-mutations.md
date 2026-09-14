<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0019: Timed NAND mutations before multi-head FTL changes

Status: accepted for the bounded LAB-RW-R2 construction, not vendor NAND or a
native-controller timing profile. Refines the N2 sequence in
[ADR-0018](0018-resource-scheduled-nand-read-lab.md); R0 and LAB-READ-R1 remain.

## Decision

Add one explicit always-timed READ/PROGRAM/ERASE construction to the existing
four-slot NFC event engine. First bind it to the ordinary serial format2 FTL and
physical-v2 media. Do not simultaneously change write heads, journal/recovery
formats or the outer lifecycle. This N2a slice precedes N2b multi-head striping.

PROGRAM owns its complete main/OOB snapshot at admission. Each page follows
LOAD/address, data-in, confirm, array program and status. The real synchronous
`program_pages(count=1)` call occurs at array completion; its result, not the
scheduled timestamp, determines actual physical effects. ERASE similarly has
command, array and status stages and calls the actual scalar erase operation.
Physical-v2 still owns its transactions, CRC and substrate barriers. There are
no suspended overlapping media transactions and no direct logical-LBA bypass.

Each LUN reserves its array/register resources independently. The first profile
conservatively reserves a LUN for an entire accepted PROGRAM group through its
last STATUS. Array busy time counts only array stages. LOAD, data, confirm and
status arbitrate the shared channel; waiting for status does not occupy that
channel or block on the same group's LUN reservation. Result-buffer ownership
continues after hardware resources are released, until take/discard.

Before the first PROGRAM confirm begins, normal cancellation drains any
already-started transfer and suppresses all mutation. After that point it drains
the current accepted group, not the rest of the Host parent. An issued ERASE
drains its operation/status. Fully successful physical drain returns
SUCCESS/reason NONE/APPLIED_COMPLETE even when cancellation was requested.
An already DONE result is immutable and its cancel call is an API_OK no-op.
This is a bounded controller policy, not atomic group writes or power-loss proof.

Genuine failure stops later page effects. Completed pages keep their facts;
only an attempted non-OK mutation callback makes its current page UNKNOWN.
Typed NONE/NONCOMPLETE facts stay typed, and unattempted suffix pages expose only
FACT_EFFECT/NONE. No further media callback executes after RW quarantine. A
separate aligned validation scratch prevents overwriting accepted PROGRAM bytes.

## Existing FTL and ownership boundary

`scale_storage_mutation_lab_factory_init()` selects the real LAB provider and
`fwlab_ftl_scale_init_window_v2()`. The FTL never imports a LAB model or clock.
Startup/recovery are timed too; observations use before/after counters, not a
clock/UID reset, live provider swap or N1 read-only phase transition.

Host close first closes Block admission and marks the parent cancelled. Existing
DATA, journal A/B and MAP reconciliation retain internal buffer ownership.
`close_step()` closes NFC only after that work drains. Completed physical success
must not be relabelled CANCELLED and thereby force unnecessary quarantine.
For a nonfinal parent group, MAP retains the base frontier and later groups are
not issued. A fully completed parent may truthfully retain its internal SELF
witness; revoked Host authority still prevents Host publication. No interface
converts an internal buffer lease into Host DMA or completion authority.

## Performance interpretation

All timings and channel byte rates are explicit positive LAB configuration.
There is no implicit commercial-part preset or fixed whole-drive rate ceiling.
Each LUN applies the homogeneous array timings; channels count main/OOB transfer
and status response traffic. Independent resources overlap only when there is
enough independent work. Four slots, serial FTL or a shared bus can limit scale
before the configured LUN count does. Package/die membership and extra capacity
alone do not multiply speed. Independent-plane operations remain unsupported.

An integer-time event engine runs unpaced: modeled delay is not a host sleep.
Model-time throughput, actual simulator CPU/backend execution rate, Host payload
and NAND traffic/WAF are separate. The post-cache Host target does not impose an
equal raw-NAND cap or an assumed fixed FTL overhead. Test costs are not measured
vendor latencies; physical lookup overhead is not fictional NAND bus traffic.

## Finite validation and next boundary

Five groups define N2a: actual snapshot/effect/status; resource overlap and
status progress; cancel/issue boundaries; later-page effect facts; and existing
serial FTL RMW/SELF/FUA/Flush/close/reopen, including a nonfinal parent group.
The [implementation results](../results/2026-09-14-timed-nand-mutations.md)
bind the exact source and execution scopes. Completing these groups ends N2a.

N2b jointly addresses multiple write heads, reservations, one ordered metadata
issuer and OPEN/CLOSE/MAP/rebuild representation. Settle NAND-page/mapping-grain
assumptions before choosing that persistent format. Real geometry, TLC/pSLC
folding, 4-TB mapping/checkpoint/GC budgets and native calibration remain later.
The physical erase-generation persistence gap P01 is unchanged: backend-only
migration to real NAND is not established.

## Source anchors

- [LAB configuration and events](../../include/fwlab/private/nfc_page_v2_lab.h).
- [Shared event engine](../../core/nfc-page-v2/nfc_page_v2_lab.c) and
  [shared effect interpretation](../../core/nfc-page-v2/nfc_page_v2_internal.h).
- [Construction](../../frontends/headless-scale/scale_storage.c), unchanged
  [FTL adapter](../../core/ftl-scale/ftl_scale_nfc_v2.c),
  [window/MAP path](../../core/ftl-scale/ftl_scale_window.c) and
  [close order](../../core/ftl-scale/ftl_scale_runtime.c).
- [Real physical effects](../../media/file-nand-v2/physical_nand.c).
