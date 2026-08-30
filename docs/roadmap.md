<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Roadmap

All time ranges are planning estimates, not commitments.

## M0 — risk experiments (3–6 weeks, parallel)

Freeze public contracts and run the BAR, Host-DMA, runtime-death, custom-VFIO and persistence experiments in [the M0 plan](m0-plan.md).

## M1 — headless controller (5–7 weeks)

Build a fixed-arena portable firmware core, headless HIF, fake DMA/NFC, memory media and deterministic trace oracle. Exit on sanitizer/property/fuzz checks and stale-free reset/queue lifecycle.

The current Cycle 03 correctness sequence is intentionally transport-free:

1. portable headless command lifecycle with fake providers;
2. executable persistence lattice;
3. programmable NFC/NAND model;
4. minimal crash-consistent mapping with file-backed test media;
5. integrated portability and provider-replaceability graduation.

ADR-0006 and ADR-0007 freeze the lifecycle and durability prerequisites. They
do not themselves open an implementation gate. IRQ, BAR, PCI, QEMU, native
NVMe binding and raw media remain later independent work.

Cycle 04 adds a fixed-profile software semantic oracle in five independent
gates: source/profile/wire, queue/CQ identity, portable protocol/control plus a
generalized multi-action graph, bounded transfer/capability data movement, and
headless integration. Only the portable policy is transport-neutral; the
headless memory-queue HIF is a replaceable reference. This sequence does not
open PCI, vfio-user, native-driver or persistent-NAND claims.

## M2 — `vfio-user` guest (6–8 weeks)

Pin a `libvfio-user` revision behind a thin adapter. Exercise an unmodified Linux guest with file media and a minimal raw-block adapter. Raw initialization is a separate identity-checked command; discard remains off. Exit on data verification, repeated enable/reset/queue cycles and fail-closed unmap/runtime restart.

## M3 — NFC, FTL and recovery (8–12 weeks)

Add resource scheduling, staged read/program, erase, ECC/retry, page FTL, GC/WL, OOB metadata, B/A/C/S persistence, deterministic faults and three timing modes. Exit on exhaustive small-geometry crash points and coverage-guided long traces.

## M4 — Host synthetic PCI (8–12 weeks, experimental)

Only after BAR, Host-DMA and containment gates pass, add the Host synthetic adapter. Begin on the distribution GA 7.0 kernel with GPL out-of-tree modules and exported APIs. Introduce a custom kernel only after recording the exact missing export/core change. Graduate the adapter only after native driver data verification with IOMMU/interrupt remapping enabled and exact bare-metal kernel evidence. Nested results alone do not graduate it.

## M5 — custom VFIO owner adapter (6–10 weeks, experimental)

The standalone VFIO PoC starts after its M0 gate. Full Host-to-Guest-to-Host switching also requires M4, Host driver teardown, zero old references and Host-DMA revocation. Exit on repeated switches with no stale DMA/completion/interrupt/pin and no permanent QEMU fork.

## M6 — firmware ELF and real endpoint (3–6+ months)

Build a RISC-V-first bare-metal SoC profile, then an ARM adapter and semantic differential tests. Select a real FPGA/endpoint SoC only after its PCIe endpoint, outbound DMA and interrupt capabilities are understood. A real endpoint validates transport hardware; it does not automatically establish production NAND fidelity.

## CI levels

- Pull requests: format/static checks, unit/property tests, sanitizer/fuzz smoke, SPDX/provenance policy. No root, KVM, module loading or raw media.
- Nightly: the pinned primary GA kernel plus a secondary upstream 6.18 LTS compatibility lane, pinned QEMU/adapter matrices, differential and power-cut coverage on disposable runners.
- Release: applicable storage tests, full power-domain matrix, reproducible builds, support/evidence matrix and known limitations.
