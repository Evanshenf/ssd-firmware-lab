<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0005: Synchronous IOAS-copy contract gate

- Status: Accepted
- Date: 2026-08-28

## Context

The V0 platform VFIO cdev probe established device registration, iommufd ownership, IOAS attach/detach and bounded software-region access. It deliberately did not map an IOVA or copy data through an IOAS. The next experiment must answer that one mechanism question without simultaneously adding interrupt publication, retained mappings, PCI composition, a BAR, QEMU, firmware, NFC or media behavior.

Linux `vfio_dma_rw()` is a synchronous, CPU-mediated copy through an emulated iommufd access object. It is not device DMA. It reports success or a negative errno but no trustworthy partial byte count. Its mapping locks prevent an IOAS mapping object from being removed under an active copy, but they do not create the project's stronger owner-lifecycle ordering after the copy returns.

The VFIO device-feature namespace has no project-private allocation range. An arbitrary private feature number would risk collision and could be mistaken for a durable generic UAPI.

## Decision

### Gate identity and order

V1 is named:

> Synchronous CPU-mediated IOAS-copy gate, with no driver-retained VFIO pin lease, no device DMA and no IRQ.

This gate runs before IRQ/eventfd, pinning, zero-copy, BAR/PAT, PCI composition, QEMU assignment and native storage-driver binding.

### Disposable A-prime test interface

V1 uses an explicitly unstable, device-specific software control region called A-prime (`A′`). It does not allocate a project-private `VFIO_DEVICE_FEATURE`, define a new top-level ioctl or expose the interface as a portable firmware contract.

The test adapter has separate non-mappable data and control regions. Control records are fixed 64-byte byte arrays at exact offsets for submit, result and state. Every multi-byte field has explicit little-endian encoding. Native or packed C structures are not a wire representation; unknown versions, flags, operations, nonzero reserved fields, shifted offsets and short or oversized accesses fail closed.

A′, IOVA and Linux errno remain inside `kernel/vfio-cdev-v1` and its adjacent tests. They never enter the portable HIF, firmware, NFC or media contracts. A future PCI/NVMe or endpoint adapter must replace A′ rather than promote it into a BAR or firmware ABI.

### Copy envelope and error semantics

The supported positive envelope is one nonzero request of at most 256 bytes, wholly contained within one 4 KiB page and one mapping. Cross-page, hole and multi-area requests are negative characterization cases.

The result contains no completed-byte count. Success means the full requested length completed. On error, the provider does not expose how many external bytes changed:

- IOAS-to-internal-buffer first copies to a bounded scratch array and commits internal data only after complete success;
- internal-buffer-to-IOAS cannot roll back a late external prefix change, so an error conservatively marks that a requested-range prefix may have changed;
- no byte outside the requested range may change.

### State, locking and ownership

All mutable state is per device. One device mutex protects session generation, accepted sequence, attach/closing/reset state, request/result snapshots, the internal data region, the synchronous provider call and result publication.

Generation advances on open, reset, successful attach or replace, successful detach and close. Sequence starts at one for each generation and is consumed exactly once only for an accepted request. Stale generation, replay, sequence gap and malformed records do not consume sequence or replace the previous result. Counter wrap enters a fail-closed state.

The normal userspace owner serializes submit against IOAS UNMAP, replace, detach, reset and close. Deliberately concurrent mutation is a robustness characterization test: an overlapping copy may succeed if it linearized first, but every new request submitted after unmap returns must fail. A stronger revoke-to-publication guarantee belongs to a later owner-epoch/quiesce contract.

### Injected provider and Linux boundary

The state engine depends only on two synchronous adjacent-layer operations: IOAS-to-buffer and buffer-to-IOAS. C2.1 uses a fake provider and runs without root, VFIO, iommufd, H0, PCI, QEMU, firmware, NFC or media. C2.2 may add a Linux provider that calls `vfio_dma_rw()` only after independent review.

The Linux provider must retain the four `vfio_iommufd_emulated_*` wrappers. For this exact synchronous, non-retaining envelope, the driver does not implement `.dma_unmap`. That conclusion expires if a later gate retains a page, translation, pointer, mapping lease or asynchronous operation.

### Cycle 02 sub-gates

1. A′ contract, fake provider and immutable evidence baseline.
2. Real single-page synchronous IOAS copy on the pinned GA kernel.
3. Protection, range, hole and partial-side-effect characterization.
4. Attach/replace/unmap/reset/close ordering and race robustness.
5. Two-instance and architecture-isolation graduation.

Each sub-gate has an independent result and stop boundary. Completion of the fifth sub-gate triggers another architecture review; it does not automatically authorize IRQ, pinning, BAR, PCI or QEMU work.

## Rejected alternatives

- A project-private `VFIO_DEVICE_FEATURE` number is rejected because the feature index is global and has no private allocation range.
- Pinning or zero-copy is rejected for V1 because it introduces retained mapping lifetime and `.dma_unmap` obligations.
- IRQ or BAR before the synchronous-copy gate is rejected because each adds an independent lifetime and teardown domain.
- Reusing A′ as the final HIF, BAR mailbox or firmware ABI is rejected.
- Linking H0, V1 and the portable SSD stack into one test binary is rejected; each mechanism harness must run independently with fake adjacent behavior.

## Consequences

C2.1 can validate decoder, state, failure semantics and lifecycle serialization with deterministic unprivileged tests. It cannot validate a VFIO device-fd region, real IOAS mapping, `vfio_dma_rw()`, kernel teardown or hardware behavior. Those claims remain stopped until their exact later gates pass.

The narrow interface is intentionally disposable. Its value is the independently tested copy and lifecycle semantics that a future Host, Guest or endpoint adapter can translate into its own transport without exposing infrastructure addresses to portable firmware.
