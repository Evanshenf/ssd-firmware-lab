<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Requirements baseline

## Goal tiers

Requirements describe intended responsibility and acceptance targets, not a
claim that every interface or profile implements every item. Current
construction-specific facts are in the [status matrix](current-status.md).

The delivered pre-alpha software baseline provides:

1. a headless harness and portable firmware/NFC/media core;
2. explicitly initialized regular-file physical NAND models, with separate
   disk and tmpfs evidence;
3. named trace, fault, interruption and recovery tests under their selected
   models, not one uniform fault model for every construction;
4. source provenance, license and unprivileged CI policy checks.

A `vfio-user` adapter may later provide an unmodified Guest-driver differential lane. It is optional and does not block the v0.x baseline, M3, M4 or M5.

The project-level success target additionally requires:

1. a Host-enumerable synthetic PCI function backed by the same firmware core;
2. a sequential Host-to-Guest-to-Host ownership switch of that function through upstream `vfio-pci`, IOMMUFD and QEMU;
3. an exclusive, explicitly initialized raw-block byte adapter below the same
   NAND model;
4. migration of the portable protocol/media core to a real FPGA or endpoint SoC.

Items 1 and 2 have the named Profile-Nested/native-VM evidence, including the
selected MQ2 journey; that is not bare-metal graduation. Raw-block deployment
and physical endpoint migration remain unimplemented. Publishing this v0.x
baseline does not claim completion of the longer-term target.

## Functional requirements

- Volatile PCI/BAR/controller memory and persistent NAND/media state are separate domains.
- The portable firmware owns command semantics, request lifecycle, status/result,
  namespace policy, FTL, garbage collection, wear policy, metadata and recovery.
  The current scalable engine is serialized, has foreground greedy GC and
  erase-aware allocation; advanced wear leveling is later work.
- HIF hardware/models own queue mechanics, Host address walking, bounded DMA authorization, completion publication and interrupt mechanics.
- Firmware never receives Host, guest-physical, host-physical, I/O-virtual or page-frame addresses.
- The full NFC-model target includes physical NAND transactions, staged
  read/program operations, erase, resource/timing constraints, ECC/read-retry,
  bad blocks, wear and deterministic faults. The C3 reference provides a
  functional model of these, with uncalibrated ticks. Current PAGE2-R0 provides
  typed physical page groups and result/payload ownership but rejects unsupported
  timing, retry and injected-fault settings; it is not C3-equivalent.
- Completion must respect actual backend outcome and requested durability.
  Modeled NAND-time gating applies only to a model that implements it; PAGE2-R0
  makes no calibrated NAND-time claim.
- Where seeded random faults are supported, they derive from a declared
  seed/profile version and are replayable. A deterministic test alone does not
  establish fault coverage in another model.
- A future raw block device must be exclusive physical media, not a filesystem
  and not the exported namespace. Current constructors accept regular files,
  and the current mapped native path requires bounded tmpfs.

Physical-model health and erase generations remain explicit simulator state.
A real NAND port must define their durable owner and recovery, including blank
blocks and interrupted erases; replacing only the byte backend does not close
that contract. See [ADR-0012](adr/0012-versioned-physical-nand-media.md).

## Harness and adapters

```text
                          portable firmware + NFC + media
                    /              /              |             \
          headless harness  optional vfio-user  Host synthetic  real EPF
                                                 /          \
                                      Host native owner   Guest owner
                                                         via upstream
                                                   vfio-pci/IOMMUFD/QEMU
```

Headless is a test harness rather than a PCI transport. Host native `nvme` and upstream `vfio-pci` are sequential owners of one Host synthetic function, not parallel controllers and not simultaneous ownership. Upstream VFIO is infrastructure, not a project-owned protocol or media implementation.

## Evidence levels

| Level | Claim permitted only after its tests pass | Not established by that level |
|---|---|---|
| Behavioral | command/media behavior in the harness | firmware execution |
| Host-native firmware | portable firmware state machine, FTL and recovery | controller-CPU or PCIe cycle accuracy |
| ISS/SoC profile | ISA, boot ABI, IRQ/MMIO and bounded firmware memory | electrical PCIe or physical NAND fidelity |
| Real endpoint | real PCI function, requester DMA and interrupts | production silicon/NAND accuracy without calibration |

Nested-KVM evidence is labeled `Profile-Nested`. Host BAR/PAT/DMA/IRQ graduation and raw-block power-failure claims require bare-metal evidence or independently proven propagation.

## Initial non-goals

- electrical or signal-level PCIe simulation;
- complete coverage of every command set and optional feature;
- concurrent Host and Guest ownership;
- live migration of the raw NAND medium;
- a large permanent QEMU fork;
- a single IOPS number presented as correctness or realism.
