<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0010: Linux HIF to portable executor contract

- Status: Accepted authority design; byte-level ABI, executable dependency gates and implementation pending
- Date: 2026-09-01
- Refines: ADR-0003, ADR-0006 and ADR-0008
- Supersedes only: ADR-0003 placement of usable data capabilities/ranges in the initial canonical descriptor
- Required by: ADR-0009 M4/M5 route

## Context

A disposable Host transport fixture can combine queue walking, protocol
decisions, PRP access, file I/O and CQE publication in one component. The real
project cannot: that would create a Linux-only second firmware implementation
and prevent the protocol, FTL, NFC and recovery source from moving to an
endpoint SoC.

The Linux interoperability profile also differs from the frozen Cycle 04
software oracle. It needs larger queues/transfers and additional Admin behavior.
Those differences require a new profile, not raw transport fields in portable
firmware and not reinterpretation of the sealed C4 profile.

`M3-P` below means the future scalable 512-byte-LBA block/NFC/media provider,
not the historical two-atom proof geometry.

## Decision

Define versioned `B*-ABI v1` authority around four conceptual objects. Concrete
wire layouts are frozen only when their implementation gate opens; this ADR
freezes their owners and forbidden contents.

### Canonical command

The trusted HIF captures one immutable raw SQE and transport identity, performs
only structural preflight, and emits an address-free canonical command plus an
opaque origin. The portable executor decides opcode/feature/namespace legality,
status/result and exact transfer shape. Initial policy admission carries no
usable data capability. Maximum structural preflight may neither dereference
Host data memory nor mint a capability usable by an action.

The portable object contains no raw SQE bytes, QID, CID, BDF, raw PRP/SGL
addresses, pointer-descriptor bytes or address graph, IOVA, GPA/HPA, PFN,
kernel pointer, file descriptor or Linux type. Canonical pointer-format and
address/metadata-presence facts needed for portable protocol legality are
allowed; they never carry an address or transport-resolved segment.

### Owner-bound data capability

After the portable policy returns an exact shape, the HIF resolves the address
graph from the same immutable capture and may mint a bounded capability. A
capability binds at least:

- controller instance and owner epoch;
- controller/reset and queue generation;
- command identity, DMA-admission/BME generation and mapping lifetime;
- exact range, direction and operation/reference lease.

Bus-master enable, controller readiness and current owner are checked at action
acceptance and every effect/retry point. Clearing BME or readiness closes new
starts and revokes the DMA-admission generation; re-enabling creates a fresh
generation and cannot revive an old capability. Unmap, queue recreation, reset
or owner change also revokes it. An accepted DMA action holds an exact map/pin
lease to terminal; unmap first closes new acquisition, then cancels/drains
holders before unpin. Queue delete/recreate likewise drains SQ capture, CQ
reservation and completion leases. A stale operation may retire only its old
bookkeeping; it cannot access memory or mutate a new owner.

### Completion intent

The portable executor returns an immutable address-free status/result and
durability/data-complete witness. The HIF alone owns SQHD/SQID/CID, CQ slot,
phase, physical CQE publication, PBA and interrupt routing.

Publication order is:

```text
data visible
→ non-phase CQE fields visible
→ status/phase publication
→ interrupt/PBA effect
```

The HIF writes a CQE body with no valid phase marker, executes the required
DMA/release barrier, and publishes status/phase as the final validity marker.
If the vector is unmasked it then signals the current IRQ route. If masked, it
sets that vector's PBA bit and emits no IRQ; unmask consumes pending state and
replays exactly once. Reset/owner switch masks the source, disables new IRQ
enqueue, drains/synchronizes IRQ and irq-work, then clears the old PBA and route
before any route reuse.

An intent/lease is bound to owner, controller and queue generation. It cannot
publish into a re-created or new-owner CQ.

### Cancellation and quiescence

Every asynchronous port transfers ownership only on explicit acceptance,
returns one terminal event, supports idempotent cancel and exposes a bounded
quiescence/zero-reference witness. An owner transition atomically closes
admission/enqueue, enters `QUIESCING`/`NO_OWNER` and advances `owner_epoch`. A
controller-only reset closes admission but retains the current owner and
independently advances `controller_epoch` at reset-begin. When reset is part of
an owner transition, both rules apply. Unmap and queue teardown retire
mapping/ring generations only at their own linearization points. Old effect
authority is therefore non-current before DMA, mapping references,
command/completion workers and IRQ work are canceled and drained; only
old-bookkeeping retirement remains legal. The transition then clears
routes/pending/PBA and performs destructive reset. Publishing a fresh owner and
route is a later, separate linearization point.

## Transaction

```text
capture raw SQE + frozen transport tuple
→ structural preflight
→ canonical command + opaque origin
→ portable legality + exact shape
→ HIF address resolution + owner-bound capability
→ command graph orders DMA and typed block/NFC actions
→ portable completion intent
→ HIF phase-last CQE + MSI-X/PBA
```

The capture linearization point atomically binds the copied SQE, acquired SQ
index/head, origin/CID reservation and queue configuration/generation. A torn
snapshot during that capture is retried or rejected. Later tail advance within
the same generation describes another command and does not invalidate the
copied SQE. Owner, controller or ring-generation change makes the captured
command stale; queue base/depth cannot be mixed across a generation.

## Dependency rules

- portable executor builds and runs without Linux, PCI, VFIO or QEMU headers;
- Linux HIF builds with fake owner, fake IOMMU/mapper, fake IRQ/PBA and fake
  executor;
- portable executor builds with fake HIF effects and fake block/NFC/media
  providers;
- the owner-lifecycle port builds and runs with fake, Host-native and
  VFIO-assigned owner adapters;
- the M3-P block provider builds and runs with a fake command graph and fake
  NFC/media, while NFC and media each retain their own fake-adjacent lane;
- the software-IOMMU provider and synthetic endpoint remain separate
  unloadable modules so their device/provider references cannot form an unload
  cycle;
- executor/provider operation tables hold a module or process-lifetime lease
  from acceptance through terminal retirement; unregister first closes
  admission, drains callbacks/work/RCU readers and only then drops the lease;
- Host-native operation does not depend on VFIO/QEMU code;
- the VFIO assignment lane does not become an M4 build or runtime dependency;
- transport source may not own `struct file`, a pathname, VFS calls, logical
  LBA-to-file mapping, FTL state or durability policy.

Executable repository gates must reject reverse/upward imports and production
linkage of an experimental direct-file transport fixture, and must build/run
each fake-adjacent matrix above. Until those gates exist and pass, this ADR is a
G1 design freeze rather than a complete G1 PASS.

## Owner/reset closure

The Host implementation gate must provide deterministic canaries for:

- old IOVA reused for a different page;
- old mapping reference released after detach/rebind;
- old completion intent released after CQ/CID/slot reuse;
- old IRQ work released after vector/eventfd reuse;
- mask→pending→unmask replay and reset-clear of real per-vector PBA;
- cross-owner sentinel scan over the entire exposed volatile BAR;
- orchestrator and portable-executor death at every owner-transition/live
  command state.

Any nonzero old mapping, pin, command, completion lease, CQE worker, IRQ work or
media operation at the publication boundary enters `QUARANTINED`.

## Version and profile rules

Every concrete B*-ABI object carries version, size and reserved-zero fields.
Semantic changes create a new ABI/profile version. `Linux-profile-v1` extends
the software oracle design with its own claim and evidence manifests; it does
not change the fixed C4 design or reopen the sealed C4.2a bytes, claims or
obligation denominator. C4.3–C4.5 remain pending under their existing profile.

## Non-claims

This ADR fixes authority and dependency direction. It does not implement a
Linux driver, byte-level ABI, scalable FTL, P7, bare-metal support, M4/M5
graduation, performance isolation or a secure untrusted-Guest boundary.
