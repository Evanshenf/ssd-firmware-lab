<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Milestone 0: architecture-risk reduction

M0 builds small, disposable experiments for questions that can invalidate an adapter or persistence model. It does not build a complete SSD.

## M0-0: publication/source boundary

Confirm independent authorship, per-file licensing, immutable provenance and trademark-safe wording. Official recognition and certification are not project requirements. Separately review the document-license, implementation-rights and patent basis relevant to each protocol contribution. Do not publish official PDFs/logos or near-verbatim protocol tables; do not copy incompatible reference source into BSD components.

## M0-1: synthetic BAR, memory type and doorbells

Start on an exact Ubuntu GA 7.0 package/ABI/config/compiler tuple and test a reserved physical aperture, BAR claim, mapping type, ordering and a minimal doorbell path. Use exported APIs first; a custom kernel requires evidence of the exact missing export/core change. Stop the adapter design on aliasing, unbounded polling loss or unexplained ordering. Nested evidence is `Profile-Nested`; graduation requires repetition on bare metal with an equally exact kernel evidence tuple.

The no-BAR H0 exported-API sub-gate passed on `Profile-Nested`; see the [recorded result](results/2026-08-28-m0-h0-nested.md). BAR/PAT, IRQ, DMA, native-driver and bare-metal gates remain open.

## M0-2: Host DMA gate

For one explicit Host profile, account for every DMA address, mapping direction, range, lifetime and revocation. Unknown DMA paths fail closed. Never treat a generic DMA address as a physical pointer. Stop Host data-path work if any arbitrary or untracked access remains.

## M0-3: HIF/runtime containment

At every stage—capture, DMA-in, firmware, NFC, DMA-out and publication—kill, hang, reset or revoke the runtime. Require bounded asynchronous behavior, `controller_epoch` change, zero stale DMA/completion/interrupt, and no use-after-free. Rebuilding the whole controller also changes `instance_nonce`.

## M0-4: historical custom emulated VFIO risk experiment

Prototype standard QEMU consumption of a VFIO cdev/iommufd device with configuration/BAR regions, one interrupt mechanism, reset and baseline IOVA reads/writes. Do not add zero-copy until exact leases can be synchronously revoked. A standalone PoC can proceed independently; a full owner switch depends on a validated Host adapter and zero-reference proof.

ADR-0009 supersedes this experiment as the implementation route: M5 uses the Host synthetic function with upstream `vfio-pci`, IOMMUFD and QEMU. The historical experiment remains evidence for its exact contracts and for rejected design risks; it does not authorize a project-owned VFIO ABI or satisfy M5.

The platform-device V0 cdev/iommufd ownership and software-region sub-gate passed on `Profile-Nested`; see the [recorded result](results/2026-08-28-m0-vfio-cdev-v0-nested.md). PCI/QEMU, mmap/BAR, IRQ, IOVA access, pinning and owner-switch gates remain open.

The unprivileged [C2.1 A-prime contract and fake-provider gate](results/2026-08-28-c2-1-a-prime-fake-provider.md) also passed. It freezes the disposable wire, partial-side-effect rules and atomic per-device lifecycle transition seam without opening VFIO or an IOAS. The real `vfio_dma_rw()` provider, kernel build/load and all later mechanisms remain separate stopped gates.

The bounded [C2.2 synchronous IOAS-copy gate](results/2026-08-28-c2-2-ioas-copy-nested.md) passed on `Profile-Nested` for one exact mapped page and both MAP/ATTACH orders. Permission/hole/partial characterization, lifecycle races, pinning, IRQ, BAR, PCI and QEMU remain stopped independent gates.

The [C2.3 protection/range characterization](results/2026-08-28-c2-3-negative-characterization-nested.md) passed its exact permission, malformed, alignment, partial-unmap and adjacent-page rejection matrix. Lifecycle mutation and concurrency remain stopped for C2.4.

The [C2.4 lifecycle and bounded real-race gate](results/2026-08-28-c2-4-lifecycle-race-nested.md) passed replacement, unwind, duplicate-detach, serial close/reopen, unload-open ordering and fixed-count real-iommufd stress. The stress observed only its action-first class; deterministic two-order serialization remains the C2.1 injected-fake evidence.

The [C2.5 two-instance and architecture-isolation gate](results/2026-08-29-c2-5-two-instance-isolation-nested.md) passed its bounded two-cdev/IOAS matrix, peer removal with a live survivor, current dependency-direction checks and an independent clean-boot H0 regression on `Profile-Nested`. This completes Cycle 02 at 5/5 and triggers the scheduled architecture review; it does not authorize the next mechanism or establish Isolated-B* runtime containment.

## M0-5: persistence lattice

Use a tiny geometry to enumerate B/A/C/S crash points, checkpoint rollover, trims, relocations and reset fences. Require one recoverable physical truth, no resurrected trim, no dual live mapping and no success completion ahead of its declared durability. Removing any Host-side decoded mapping cache must not prevent firmware recovery from its NAND/OOB metadata.

## Exit rule

Each experiment records input, immutable environment, pass/fail evidence, fallback and reopen condition. Failure demotes or redesigns the affected adapter; it does not silently weaken the evidence claim.
