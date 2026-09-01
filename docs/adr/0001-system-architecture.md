<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0001: System architecture

- Status: Partially superseded by ADR-0009 and ADR-0010
- Date: 2026-08-28

Supersession note (2026-09-01): ADR-0009 makes `vfio-user` optional and replaces
the project-owned custom-VFIO implementation route with upstream `vfio-pci`,
IOMMUFD and QEMU. The historical decision text below is retained; ADR-0009 is
authoritative for those clauses and preserves this ADR's owner-safety and
graduation requirements. ADR-0010 supersedes only the ordering in the
historical correctness-path diagram: the HIF may validate a maximum structural
envelope before portable policy, but it mints the final exact data capability
only after portable policy returns the exact transfer shape.

## Context

A Host-native software device needs kernel PCI/MMIO/DMA/interrupt mechanisms, while portable, fuzzable firmware must not be coupled to Linux internals. A Guest path has a different address owner and IOMMU lifecycle. A future endpoint moves both mechanisms into hardware while retaining firmware policy.

## Decision

Use three implementation stages around one portable core:

```text
Stage C   headless + vfio-user software baseline
                         │
Stage B*  kernel/HIF safety TCB ↔ portable firmware runtime
                         │
Stage D   real FPGA or endpoint-SoC adapter
```

There is one headless harness and four transport/owner adapters: `vfio-user`, Host synthetic PCI, custom emulated VFIO ownership, and real endpoint. Host synthetic and custom VFIO operate on the same logical controller in different, sequential owner epochs.

The v0.x stable baseline is Stage C with persistent NAND recovery. Host, owner switching and endpoint migration remain project-level goals but stay experimental until their evidence gates pass.

## Responsibility split

Trusted HIF owns PCI/BAR/doorbells, queue capture, raw transport addresses, address-graph safety, bounded capability creation, queue indices, physical completion publication and interrupt mechanics.

Portable firmware owns protocol and namespace policy, command lifecycle/result/status, request dependencies, FTL, garbage collection, wear leveling, firmware metadata and recovery.

NFC owns physical resource/timing/ECC/fault outcomes. Media owns persistent page/OOB, generation, wear/bad-block truth and a private physical-operation WAL. Media never accepts a logical mapping or GC victim as an interface.

## Correctness path

```text
capture and validate transport graph
→ issue exact bounded capability
→ address-free canonical command
→ firmware policy + FTL + NFC
→ completion intent
→ HIF publishes queue completion and interrupt
```

The first implementation uses bounded bounce buffers. Optimization order is batching, larger bounded slots, fewer wakeups, exact pinned leases, then proven-revocable zero-copy. Zero-copy is never a correctness prerequisite.

## Owner switch

Host-to-Guest requires application-write stop, Flush/unmount/holder closure, a transition lock, bounded old-owner teardown while the Host driver unbinds, full entry closure, owner revocation, zero-reference proof, destructive reset, a new IOAS and only then Guest publication. Any unresolved old reference enters quarantine. The reverse path is symmetric.

## Consequences

Benefits: the policy-bearing firmware is testable in user space and portable to an endpoint; unsafe Host mechanisms are isolated and can be stopped independently; the persistence model stays identical across adapters.

Costs: the safe baseline copies data; HIF still needs a small generated transfer-safety validator; kernel/runtime ABI and lifecycle need long-term versioning; a software Host PCI adapter may remain an experimental research mechanism.

## Stop and graduation rules

An adapter stops on memory-type aliasing, arbitrary/untracked DMA, stale-identity writes, non-revocable leases or a need to disable the system IOMMU. Stable graduation requires a published support matrix and two release cycles of relevant correctness/security evidence. Stopping an implementation triggers redesign; it does not turn the v0.x subset into completion of all project goals.
