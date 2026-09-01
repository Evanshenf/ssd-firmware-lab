<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# M4/M5 synthetic PCI and owner-switch PoC

> **Destructive disposable-lab code.** Read
> [`POC_PROFILE_NESTED.md`](../../POC_PROFILE_NESTED.md) first. Every
> host-mutating smoke script refuses to run outside the pinned runtime kernel
> or without the explicit `FWLAB_POC_ACK` acknowledgement. That guard prevents
> accidents; it does not make this fixture supported or safe for a real Host.

This directory contains a bounded, disposable Profile-Nested experiment.  It
proved that one synthetic PCI function can be used first by the L1 native Linux
`nvme` driver and then by an L2 native Linux `nvme` driver through upstream
`vfio-pci`, IOMMUFD and QEMU.  No project VFIO driver is present.

The code is mechanism evidence, not the production firmware architecture.  In
particular, [`m4_nvme.c`](../../kernel/m4-synthetic-pci-poc/m4_nvme.c) is a deliberately small protocol fixture and does not
replace or graduate the portable C3/C4 firmware core, NFC model or FTL.

The optimized PoC now places two explicit internal seams around that fixture:

```text
synthetic PCI transport
  BAR/config · DMA · MSI-X · reset
          │ m4_frontend_services v1
          ▼
minimal NVMe executor fixture
          │ m4_media_ops v1
          ▼
memory/file media fixture
```

`m4_frontend_services` is an internal PoC contract, not the portable B*-ABI.
It keeps the PCI context, BAR pointer, `pci_dev` and direct IOMMU implementation
out of the NVMe fixture.  A future C4.5/Linux-HIF bridge replaces the complete
NVMe fixture ops; a future M3-P provider replaces the media ops.

## Components

```text
boot-reserved 16-MiB aperture
        │
synthetic PCIe endpoint (vendor mode or NVMe mode)
        ├── PCI config + 64-bit BAR0 + PCIe FLR
        ├── software-IOMMU provider + normal iommu_group
        ├── permission-checked software DMA
        └── isolated one-vector MSI-X domain
                │
                ├── L1 upstream nvme
                └── L1 upstream vfio-pci + IOMMUFD + QEMU
                                                │
                                                └── L2 upstream nvme
```

The endpoint and IOMMU provider are separate modules.  Linux pins an IOMMU
provider while an endpoint uses its operations, so a combined unloadable module
would create a lifetime cycle.  Teardown always removes the endpoint first and
the provider second.

The fwnode-less IOMMU association is acceptable only on the pinned nested lab,
which has no competing fwnode-less provider.  A production/bare-metal adapter
may require a small in-tree association hook; this PoC does not hide that issue
with VFIO no-IOMMU mode.

## Modes and fixed profile

The endpoint defaults to vendor-class lab mode.  This preserves the independent
BAR, DMA and MSI-X tests.  `nvme_mode=1` selects the native-NVMe fixture:

- NVMe 1.0 register/profile subset;
- one Admin and one I/O queue pair, depth 32;
- one 1-MiB namespace with 512-byte LBAs;
- MDTS=1, at most 8 KiB through PRP1, direct PRP2 or a bounded two-entry PRP
  list; the smoke includes an intentionally unaligned 8-KiB passthrough;
- Identify, required Get Log/Features, create/delete queues, Read, Write, Flush
  and one deferred AER;
- memory media by default, or an existing exact-size regular file selected by
  `backend_path=`.

The module refuses a non-regular or incorrectly sized file.  The scripts also
reject a file located on `/dev/sdb`; the reserved NAND disk is never opened.

## Build

The authoritative runtime lane is Ubuntu `7.0.0-30-generic`.  Ubuntu 6.8 is a
build-only compatibility lane.

```sh
make -C experiments/m4-iommu-poc W=1
make -C experiments/m4-iommu-poc user
make -C experiments/m4-iommu-poc architecture
```

The architecture gate fails if transport regains VFS/media ownership, if the
NVMe fixture reaches into the PCI context or raw BAR mapping, or if the media
fixture imports NVMe/PCI/DMA mechanics.

The L2 smoke uses a generated 1.3-MiB initramfs containing static BusyBox and
the exact running-kernel NVMe modules:

```sh
sudo experiments/m4-iommu-poc/lab/build-l2-initramfs.sh \
  experiments/m4-iommu-poc/build/fwlab-l2-initramfs.cpio.gz
```

## Gates

Run only on a disposable machine with a verified hypervisor console/reset path
and the exact one-shot boot reservation `memmap=16M$0x70000000`.

```sh
cd experiments/m4-iommu-poc
ack=I_ACCEPT_DESTRUCTIVE_PROFILE_NESTED_POC
sudo env FWLAB_POC_ACK="$ack" ./smoke-bar-aperture.sh
sudo env FWLAB_POC_ACK="$ack" ./smoke-enumerate.sh
sudo env FWLAB_POC_ACK="$ack" ./smoke-bar-lab.sh
sudo env FWLAB_POC_ACK="$ack" ./smoke-dma-lab.sh
sudo env FWLAB_POC_ACK="$ack" ./smoke-dma-api-lab.sh
sudo env FWLAB_POC_ACK="$ack" ./smoke-msix-lab.sh
sudo env FWLAB_POC_ACK="$ack" ./smoke-vfio-bind.sh
make user
sudo env FWLAB_POC_ACK="$ack" ./smoke-vfio-cdev.sh
sudo env FWLAB_POC_ACK="$ack" ./smoke-native-nvme.sh
sudo env FWLAB_POC_ACK="$ack" ./smoke-native-file.sh
sudo ./lab/build-l2-initramfs.sh ./build/fwlab-l2-initramfs.cpio.gz
sudo env FWLAB_POC_ACK="$ack" ./smoke-vfio-l2.sh
```

The last gate performs two complete owner cycles:

```text
L1 nvme → unbind → FLR → upstream vfio-pci/IOMMUFD admission
→ QEMU/L2 nvme I/O → QEMU close → VFIO unbind → FLR → L1 nvme
```

Both `enable_unsafe_noiommu_mode` and `allow_unsafe_interrupts` must remain
`N`.  Any missing group, stale module/device, write to `/dev/sdb`, QEMU timeout,
kernel warning/oops or data mismatch is a hard failure.

## Non-claims

Passing these scripts does not establish bare-metal BAR/PAT/DMA/IRQ behavior,
full NVMe compliance, C3/C4-to-M4 integration, NAND fidelity, raw-media safety,
power-loss durability, migration or performance.  The bounded result is
recorded in `docs/results/2026-08-31-m3-m5-transport-poc.md`.

The frontend seam optimization is recorded separately in
`docs/results/2026-09-01-m4-frontend-seam-poc.md`; it does not alter the frozen
2026-08-31 evidence identity.

The production-oriented owner, DMA, IRQ, media and graduation rules are defined
in `docs/m4-m5-host-assignable-ssd-design.md`.  A pulled C4.3 Phase-2 snapshot
is exercised only as the non-authoritative shadow scaffold in
`experiments/m4-headless-backend-poc`.

Before portable integration, freeze a replaceable command-executor/HIF seam.
The Host transport may retain PCI/BAR, queue/epoch identity, bounded DMA,
MSI-X and reset, but no `struct file`, pathname, VFS operation, LBA-to-offset
policy, FTL map or durability decision.  The direct file fixture must disappear
when the real portable executor replaces it.
