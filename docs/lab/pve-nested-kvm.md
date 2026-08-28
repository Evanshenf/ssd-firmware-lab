<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Generic PVE nested-KVM lab profile

This is a topology example, not a record of a particular host.

```text
L0 PVE host
└── L1 development VM
    ├── /dev/sda  Linux system disk
    ├── /dev/sdb  exclusive, unmounted raw NAND medium
    ├── M2: vfio-user presents synthetic NVMe directly to L2
    │   └── L2 has its own VirtIO/SATA system disk
    └── M4/M5: synthetic NVMe is first owned by L1 Host,
        then destructively reset and assigned to L2 through custom VFIO
```

The L1 system disk, private NAND backing and exported synthetic namespace are three different objects. `/dev/sdb` contains only the project's physical media container. It is never mounted and is never passed through to L2. The synthetic NVMe is always a test data disk, never the L1 or L2 root disk.

## Suggested functional profile

- L1: 8 Host-model vCPUs, 16 GiB fixed RAM, 80–120 GiB system disk and a separate 160 GiB media disk.
- L2: 4–6 vCPUs, 4–8 GiB RAM and a 20–40 GiB independent system disk.
- Record exact L0/L1/L2 kernels, QEMU, adapter revision, CPU model and storage cache/flush configuration in every result.

Use the L1 distribution GA 7.0 kernel as the primary development baseline. Do not add a second custom kernel until an exported-API PoC identifies the exact missing mechanism. Upstream 6.18 LTS is a later compatibility lane, not the default L1 boot kernel.

Use a stable virtual serial/by-id for the media disk, exclude it from backup/snapshot/replication/live migration, and set `backup=0` in the VM configuration. Do not assume a per-disk `snapshot=0` option makes a whole-VM snapshot safe: verify the actual hypervisor behavior. For a system-only snapshot, stop the VM, record the complete media-drive identity/options, safely detach (not delete) the raw-media volume, snapshot the system disk, then reattach the same volume. Pin cache mode, AIO and iothread settings. Before any power-failure durability claim, prove Flush/FUA propagation through L1 QEMU, the L0 storage layer and the physical device; otherwise report only daemon-crash consistency.

## Two implementation phases

In M2, L1 runs the firmware lab and `vfio-user`, while L2 alone sees the synthetic test NVMe. L1 does not yet get a synthetic `/dev/nvmeXnY`.

In M4/M5, the Host synthetic adapter creates the test NVMe in L1. After all L1 use is stopped, only that synthetic function is unbound and handed to L2. The private `/dev/sdb` medium remains exclusively in L1.

## Evidence label

Nested KVM is appropriate for firmware semantics, Guest-driver behavior, FTL/NFC/recovery, fault injection, ISS integration and relative performance. Label it `Profile-Nested`. Repeat Host BAR/memory-type, DMA, interrupt and raw power-failure graduation on bare metal with an exact distribution/upstream source revision, package ABI, config, compiler and module identity.
