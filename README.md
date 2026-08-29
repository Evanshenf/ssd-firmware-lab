<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ssd-firmware-lab

`ssd-firmware-lab` is a pre-alpha, AI-assisted, open-source laboratory for developing portable SSD controller firmware, a programmable NAND flash controller model, and crash-consistent persistent media.

The repository currently freezes the architecture and contribution boundaries. It does **not** yet contain a working NVMe controller, production firmware, or benchmark result.

## What we are building

```text
Linux storage driver
        │
        ▼
transport / ownership adapter
        │  canonical commands + bounded capabilities
        ▼
portable firmware core
protocol policy · request lifecycle · FTL · GC/WL · recovery
        │  NAND-controller descriptors
        ▼
custom NFC model
timing · channel/LUN/plane scheduling · ECC/fault outcomes
        │
        ▼
persistent physical media
page/OOB truth · wear/bad-block state · physical-operation WAL
```

The v0.x release baseline is a headless test harness plus a `vfio-user` guest adapter and persistent file/raw-block media. The longer-term project target adds:

- a host synthetic PCI function that the native Linux storage driver can use;
- a destructive, sequential Host-to-Guest ownership switch through a custom VFIO adapter;
- the same portable protocol/media firmware core running behind a real FPGA or endpoint-SoC adapter.

Host and Guest never own the same controller concurrently. A Host-to-Guest transition must stop application writes, Flush/unmount/close holders, unbind the Host driver, revoke all old capabilities, prove references are zero, reset the controller, and only then publish a new Guest owner.

## Storage separation

- PCI configuration, BAR state, queue shadows and controller DRAM live in volatile memory.
- NAND pages, OOB, wear/bad-block truth and recovery records live in a persistent image or dedicated block device.
- A synthetic `/dev/nvmeXnY` is the tested namespace. Its raw backing device is private implementation media and must never be mounted or used for unrelated data.

Raw block media is destructive. The normal daemon will never auto-format it; a separate initializer must verify whole-device identity, serial, exact size and an operator confirmation token. File-backed media is the safe default.

## Authenticity boundary

The project separates four evidence levels: behavioral model, host-native portable firmware, ISA/SoC-profile firmware in an ISS, and a real PCI endpoint. Results are labeled with the level actually tested. Nested KVM can validate functional behavior, but cannot graduate bare-metal BAR, DMA, IRQ or power-failure claims.

This independent project is not affiliated with, endorsed by, recognized by, or certified by NVM Express. Open-source publication does not require official recognition. That is separate from the license/provenance or patent basis of particular third-party material and implementations, which contributors must review. The project does not use an official logo or claim certification.

## Start here

- [Requirements](docs/requirements.md)
- [Architecture](docs/architecture.md)
- [Portable command lifecycle core](core/README.md)
- [Executable persistence model](core/c32/README.md)
- [Milestone-0 risk plan](docs/m0-plan.md)
- [Roadmap](docs/roadmap.md)
- [Architecture decisions](docs/adr/README.md)
- [Generic nested-KVM lab topology](docs/lab/pve-nested-kvm.md)
- [Contribution and source-boundary rules](CONTRIBUTING.md)
- [Security and raw-media warning](SECURITY.md)

## License

Original user-space source, schemas, scripts and tests use BSD-3-Clause. Future Linux kernel source under `kernel/` uses GPL-2.0-only. Documentation uses CC-BY-4.0. See [LICENSES.md](LICENSES.md) and per-file SPDX identifiers.
