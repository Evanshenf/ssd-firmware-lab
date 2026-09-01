<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Roadmap

All time ranges are planning estimates, not commitments.

## M0 — risk experiments (3–6 weeks, parallel)

Freeze public contracts and run the BAR, Host-DMA, runtime-death, owner-assignment and persistence experiments in [the M0 plan](m0-plan.md).

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

## M2 — optional `vfio-user` differential adapter (deferred)

M2 is no longer on the critical path or in the v0.x release promise. If scheduled, pin a `libvfio-user` revision behind a thin adapter and consume the same versioned portable boundary as every other frontend. It may exercise an unmodified Linux Guest and provide differential evidence, but it must not introduce a second protocol, command-lifecycle, FTL or media truth. M2 does not block M3, M4 or M5.

## M3 — NFC, FTL and recovery (8–12 weeks)

Add resource scheduling, staged read/program, erase, ECC/retry, page FTL, GC/WL, OOB metadata, B/A/C/S persistence, deterministic faults and three timing modes. The transport-integration profile, M3-P, adds a scalable 512-byte-LBA block-action provider; the historical two-atom C34/C35 proof geometry is not a namespace implementation. Exit on exhaustive small-geometry crash points, coverage-guided long traces and real NFC/media witnesses for Read/Write/Flush/FUA without a transport-side file shortcut.

## M4/M5 — one Host-assignable transport epic, two graduation gates

M4 and M5 share the synthetic endpoint, trusted Linux HIF, owner-lifecycle, DMA, reset, CQE and IRQ mechanisms. They do not share one graduation claim. The project uses upstream `vfio-pci`, IOMMUFD and QEMU for assignment and does not implement a custom VFIO ABI or QEMU NVMe model. See [ADR-0009](adr/0009-upstream-vfio-route-and-milestones.md).

Before Host integration, finish the unchanged C4.3–C4.5 fixed software-oracle gates. G1 first freezes the B*-ABI authority and executable fake-adjacent dependency gates. M3-P and a separate `Linux-profile-v1` may then proceed in parallel and meet at that versioned boundary. Base M3 work that does not depend on the Host profile may continue independently.

### M4 — Host-native portable path (experimental)

- `M4-N` is bounded `Profile-Nested` admission only.
- `M4-B` graduates only when the native Host driver executes the portable firmware/M3-P/NFC/media path on bare metal with IOMMU and interrupt remapping enabled, complete reset/cleanup evidence and no transport-side VFS/LBA/durability truth.

### M5 — upstream-VFIO owner switch (experimental)

- `M5-N` exercises bounded nested Host→Guest→Host mechanisms and the P7 stale-authority canary matrix.
- `M5-B` starts only after M4 graduation and repeats P7 plus owner-cycle fault injection on bare metal. Old DMA, mappings, pins, completion leases, CQEs, IRQ work and PBA state must be zero or synchronously stale-rejected before a new owner is published.

Repeated successful cold switches are a soak signal, not a substitute for P7. Nested results alone graduate neither M4 nor M5.

## M6 — firmware ELF and real endpoint (3–6+ months)

Build a RISC-V-first bare-metal SoC profile, then an ARM adapter and semantic differential tests. Select a real FPGA/endpoint SoC only after its PCIe endpoint, outbound DMA and interrupt capabilities are understood. A real endpoint validates transport hardware; it does not automatically establish production NAND fidelity.

## CI levels

- Pull requests: format/static checks, unit/property tests, sanitizer/fuzz smoke, SPDX/provenance policy. No root, KVM, module loading or raw media.
- Nightly: the pinned primary GA kernel plus a secondary upstream 6.18 LTS compatibility lane, pinned QEMU/adapter matrices, differential and power-cut coverage on disposable runners.
- Release: applicable storage tests, full power-domain matrix, reproducible builds, support/evidence matrix and known limitations.

For privileged Host work, run affected local checks during development, seal an immutable source commit, then run unprivileged GitHub CI and privileged exact-profile lab gates in parallel. Bind both to that source identity and add an evidence-only child commit. Do not rerun the full remote matrix for every intermediate edit, and do not feed evidence or runner output back into source compilation.
