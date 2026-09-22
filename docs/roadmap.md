<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Roadmap

Current order: publish the adopted scalable-storage / serial-credit MQ2 work,
keep exact evidence and limits visible, then select one bounded follow-up.
See [current constructions](current-status.md) and
[development evidence](results/2026-09-10-scaled-storage-mq2.md).
Publishing development source is separate from freezing a new release.

The initial M0/M1 schedules below are historical planning context, not pending
prerequisites to native integration. Time ranges are not commitments.

## M0 — risk experiments (3–6 weeks, parallel)

Freeze public contracts and run the BAR, Host-DMA, runtime-death, owner-assignment and persistence experiments in [the M0 plan](m0-plan.md).

## M1 — headless controller (5–7 weeks)

Build a fixed-arena portable firmware core, headless HIF, fake DMA/NFC, memory media and deterministic trace oracle. Exit on sanitizer/property/fuzz checks and stale-free reset/queue lifecycle.

The historical Cycle 03 correctness sequence was intentionally transport-free:

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

The old C4.3–C4.5 component-first sequence is not the current critical path.
The adopted vertical spine uses profile adapters, one shared lifecycle and
aggregate Block → FTL → NFC → physical NAND. Named J0–J3 software journeys
were frozen in the existing preview. New scalable FTL/PAGE2 and native MQ2
builds extend that spine with separately scoped evidence; they do not reopen
or redefine the frozen C4 reference oracles. See
[ADR-0013](adr/0013-scalable-ftl-and-page-windows.md) and
[ADR-0014](adr/0014-native-profile-and-serial-mq2.md).

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
- Additional kernel/native lab lanes: only an explicitly provisioned and version-recorded environment. No automatic secondary 6.18 lane, arbitrary-kernel or full power-cut coverage is claimed.
- Release: applicable storage tests, full power-domain matrix, reproducible builds, support/evidence matrix and known limitations.

For privileged Host work, run affected local checks during development, seal an immutable source commit, then run unprivileged GitHub CI and privileged exact-profile lab gates in parallel. Bind both to that source identity and add an evidence-only child commit. Do not rerun the full remote matrix for every intermediate edit, and do not feed evidence or runner output back into source compilation.

## Bounded follow-ups, not current release promises

1. Finish source/documentation/performance publication with preserved thematic
   commits and one final hosted CI run. Keep the existing preview tag unchanged.
2. Choose the next measured native-path bottleneck or larger-capacity integration
   task. Two queue pairs currently share one global I/O credit; parallel FTL
   requires a separate ownership/resource design, not more queues alone.
3. Larger native capacity, raw-block admission and sustained overwrite/GC
   behavior have separate capacity, memory, recovery and evidence budgets.
   The historical 64-GiB ARM headless campaign is not native 64-GiB qualification.
4. Real NAND/SoC, RTOS, RAID and management interfaces remain separate platform
   work. P01 generation persistence needs an explicit hardware contract.

Do not grow a new test framework or reopen closed component reviews merely to
advance the roadmap. Name one real journey, its owned changes, executed checks
and stop condition before beginning another implementation slice.

The NAND resource route now has a finite [A–D sequence](adr/0020-cooperative-nand-channel-domains.md):
A cooperative channel domains with real independent shards and serial FTL/J0;
B multi-head format-3 DATA waves and ordered MAP/recovery; C the same logical
resources on one/four actual data workers; D actual independent-plane READ.
[A](results/2026-09-14-channel-domains.md),
[B](results/2026-09-14-multihead-write-waves.md) and
[C](results/2026-09-14-channel-workers.md) and
[D](results/2026-09-14-independent-plane-read.md) now have bounded implementation
evidence; each result records its exact-source disposition. The subsequent
[combined mutable format3 construction](results/2026-09-14-mutable-read-write.md)
now connects both schedules in one real writable Block/J0 instance. The
[native constructor opt-in](results/2026-09-14-native-channel-construction.md)
also executes the same path through the actual worker loop with fake Host
ioctls. A subsequent [bounded native x86-64 L1 episode](results/2026-09-14-native-channel-l1.md)
passed actual-driver data, quiescent reset and cold reopen on that same path.
The [per-runtime worker integration](results/2026-09-14-native-worker-lifetime.md)
now passes its fixed software/real-thread checks. A subsequent
[native four-worker L1 journey](results/2026-09-14-native-four-worker-l1.md)
also passed real-driver data, reset and cold recovery. The subsequent
[ARM64 native256MiB episode](results/2026-09-22-native-arm-channel-256.md)
passed the same bounded four-worker path with selected extents. New-construction
64GiB native/full-capacity, M5, NUMA/performance and vendor geometry
remain separate finite work, not reasons to
repeat or broaden the completed A–D tests.
Threads/NUMA do not define NAND topology or guarantee a throughput multiplier.
