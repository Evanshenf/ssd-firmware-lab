<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ssd-firmware-lab

`ssd-firmware-lab` is a pre-alpha, AI-assisted, open-source laboratory for developing portable SSD controller firmware, a programmable NAND flash controller model, and crash-consistent persistent media.

The repository contains an experimental Host-visible software PCI/NVMe controller,
portable firmware/FTL/NFC, persistent file-NAND and sequential QEMU ownership
journeys. This is not production firmware, a physical endpoint or a performance
result. Implementation and individual test results are not reviewed milestone
graduation; exact release evidence must state its source and remaining limits.

Current preview: **v0.1.0-spine-preview.1**, a reviewed fixed-profile software
baseline. See the [source-bound results and limitations](docs/results/2026-09-05-vertical-spine-preview.md)
for the 1-MiB/file-NAND envelope, native/QEMU results and remaining risks.

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

The current vertical path uses the portable firmware/NFC stack and persistent
file-NAND, exposed through a headless binding or the experimental native PCI/HIF.
Raw-block backing is deferred. A `vfio-user` guest adapter is also optional and
deferred, not a release prerequisite. The project roadmap includes:

- a host synthetic PCI function that the native Linux storage driver can use;
- a destructive, sequential Host-to-Guest ownership switch of that same function through upstream `vfio-pci`, IOMMUFD and QEMU;
- the same portable protocol/media firmware core running behind a real FPGA or endpoint-SoC adapter.

Host and Guest never own the same controller concurrently. A Host-to-Guest transition must stop application writes, Flush/unmount/close holders, unbind the Host driver, revoke all old capabilities, prove references are zero, reset the controller, and only then publish a new Guest owner.

The project does not implement a custom VFIO userspace ABI or a QEMU NVMe device model for this path. M4 Host-native operation and M5 upstream-VFIO assignment share one synthetic endpoint and trusted HIF, but remain separate graduation claims. See [ADR-0009](docs/adr/0009-upstream-vfio-route-and-milestones.md) and [ADR-0010](docs/adr/0010-linux-hif-portable-executor-contract.md).

## Storage separation

- PCI configuration, BAR state, queue shadows and controller DRAM live in volatile memory.
- NAND pages, OOB, wear/bad-block truth and recovery records currently live in a persistent file image; dedicated raw-block backing is later work.
- A synthetic `/dev/nvmeXnY` is the tested namespace. Its backing image is private implementation media and must never be mounted or used for unrelated data.

Raw block media would be destructive. Its future initializer must verify
whole-device identity, serial, exact size and operator approval; it is not
implemented by the current worker. The file worker creates a new image only
with explicit `--format` and otherwise recovers the existing UUID-bound image.

## Authenticity boundary

The project separates four evidence levels: behavioral model, host-native portable firmware, ISA/SoC-profile firmware in an ISS, and a real PCI endpoint. Results are labeled with the level actually tested. Nested KVM can validate functional behavior, but cannot graduate bare-metal BAR, DMA, IRQ or power-failure claims.

This independent project is not affiliated with, endorsed by, recognized by, or certified by NVM Express. Open-source publication does not require official recognition. That is separate from the license/provenance or patent basis of particular third-party material and implementations, which contributors must review. The project does not use an official logo or claim certification.

## Start here

- [Requirements](docs/requirements.md)
- [Architecture](docs/architecture.md)
- [Current native firmware/data path and bounded checks](frontends/linux-m4/README.md)
- [Portable command lifecycle core](core/README.md)
- [Executable persistence model](core/c32/README.md)
- [Programmable NAND/NFC model](nfc/README.md)
- [Integrated headless firmware graduation](frontends/headless-c35/README.md)
- [Portable NVMe policy boundary](core/c4-nvme/README.md)
- [Headless memory-transport reference](frontends/headless-c4/README.md)
- [Milestone-0 risk plan](docs/m0-plan.md)
- [Roadmap](docs/roadmap.md)
- [Architecture decisions](docs/adr/README.md)
- [Generic nested-KVM lab topology](docs/lab/pve-nested-kvm.md)
- [Contribution and source-boundary rules](CONTRIBUTING.md)
- [Security and raw-media warning](SECURITY.md)

## License

Original user-space source, schemas, scripts and tests use BSD-3-Clause. Future Linux kernel source under `kernel/` uses GPL-2.0-only. Documentation uses CC-BY-4.0. See [LICENSES.md](LICENSES.md) and per-file SPDX identifiers.
