<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Linux kernel components

Future kernel source under this directory is GPL-2.0-only and will carry the correct per-file SPDX identifier and module license. Planned components are the experimental Host synthetic PCI and custom emulated VFIO owner adapters.

Kernel code owns mechanism and a generated memory-safety envelope, not a second protocol implementation.

The first implementation is the deliberately inert [H0 synthetic PCI enumeration probe](host-pci-h0/README.md). It has no storage class, BAR, DMA or IRQ and exists only to validate exported host-bridge APIs and cleanup.
