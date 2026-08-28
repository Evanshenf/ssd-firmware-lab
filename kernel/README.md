<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Linux kernel components

Future kernel source under this directory is GPL-2.0-only and will carry the correct per-file SPDX identifier and module license. Planned components are the experimental Host synthetic PCI and custom emulated VFIO owner adapters.

Kernel code owns mechanism and a generated memory-safety envelope, not a second protocol implementation.

The first implementation is the deliberately inert [H0 synthetic PCI enumeration probe](host-pci-h0/README.md). It has no storage class, BAR, DMA or IRQ and exists only to validate exported host-bridge APIs and cleanup.

The independent [V0 emulated VFIO cdev contract harness](vfio-cdev-v0/README.md) validates cdev/iommufd ownership and a software region without pretending to be a PCI function.

The [V1 work area](vfio-cdev-v1/README.md) begins with the C2.1 A-prime wire/state contract and injected fake copy-provider seam. C2.1 contains no VFIO device or real IOAS provider; those remain separate later gates.
