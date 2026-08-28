<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Requirements baseline

## Goal tiers

The v0.x release baseline must provide:

1. a headless harness and portable firmware/NFC/media core;
2. a `vfio-user` adapter for an unmodified Linux guest driver;
3. persistent file and explicitly initialized raw-block media;
4. deterministic trace/replay, fault injection and power-cut recovery tests;
5. source provenance, license and unprivileged CI policy checks.

The project-level success target additionally requires:

1. a Host-enumerable synthetic PCI function backed by the same firmware core;
2. a sequential Host-to-Guest-to-Host ownership switch through a custom VFIO adapter;
3. migration of the portable protocol/media core to a real FPGA or endpoint SoC.

The v0.x baseline may ship while an experimental adapter is redesigned. That does not satisfy the longer-term project target.

## Functional requirements

- Volatile PCI/BAR/controller memory and persistent NAND/media state are separate domains.
- The portable firmware owns command semantics, request lifecycle, status/result, namespace policy, FTL, garbage collection, wear leveling, metadata and recovery.
- HIF hardware/models own queue mechanics, Host address walking, bounded DMA authorization, completion publication and interrupt mechanics.
- Firmware never receives Host, guest-physical, host-physical, I/O-virtual or page-frame addresses.
- A custom NFC exposes channel/LUN/die/plane/block/page transactions, staged read/program operations, erase, timing, ECC/read-retry, bad blocks, wear and deterministic faults.
- Backend readiness, modeled NAND time and requested durability jointly gate completion.
- Every random fault is derived from a declared seed/profile version and is replayable.
- A raw block device is exclusive physical media, not a filesystem and not the exported namespace.

## Harness and adapters

```text
                         portable firmware + NFC + media
                    /             /             |             \
          headless harness  vfio-user Guest  Host synthetic  real EPF
                                                   │
                                       custom VFIO owner adapter
```

Headless is a test harness rather than a PCI transport. Host synthetic and custom VFIO are sequential owner adapters for one logical controller, not parallel controllers and not simultaneous ownership.

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
