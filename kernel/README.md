<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Linux kernel components

Kernel source under this directory is GPL-2.0-only and carries per-file SPDX identifiers and module licenses. The current implementation is the [native M4 synthetic PCI transport](m4-native/README.md).

Kernel code owns mechanism and a generated memory-safety envelope, not a second protocol implementation.

The H0 and custom emulated VFIO experiments have been retired from `main`. Their source and test entries are preserved in the [legacy experiment archive](../docs/legacy-experiments.md).

The deliberately inert [H0 synthetic PCI enumeration probe](https://github.com/Evanshenf/ssd-firmware-lab/blob/4b2a56272e567a5c8071819f506e4a7bd0acac24/kernel/host-pci-h0/README.md) had no storage class, BAR, DMA or IRQ; it validated exported host-bridge APIs and cleanup.

The independent [V0 emulated VFIO cdev contract harness](https://github.com/Evanshenf/ssd-firmware-lab/blob/4b2a56272e567a5c8071819f506e4a7bd0acac24/kernel/vfio-cdev-v0/README.md) validated cdev/iommufd ownership and a software region without pretending to be a PCI function.

The archived [V1 work area](https://github.com/Evanshenf/ssd-firmware-lab/blob/4b2a56272e567a5c8071819f506e4a7bd0acac24/kernel/vfio-cdev-v1/README.md) began with the C2.1 A-prime wire/state contract and injected fake copy-provider seam. Its later IOAS and lifecycle results retain their separate recorded scopes.

The [C2.5 peer fixture](https://github.com/Evanshenf/ssd-firmware-lab/blob/4b2a56272e567a5c8071819f506e4a7bd0acac24/kernel/vfio-cdev-v1-peer-fixture/README.md) was a separate, test-only platform-device instantiator. It asked the frozen V1 driver to probe one extra device for per-instance state and remove-isolation tests.
