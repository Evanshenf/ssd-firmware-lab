<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Native transport provenance

The PCI and software-IOMMU mechanism starts from this project's public PoC
commit `f3438fee61f82cf305f29bc232aa2c0fa9e1b166`, specifically
`kernel/m4-synthetic-pci-poc/m4_pci.c`, `m4_pci_main.c`, `m4_iommu.c`,
`m4_internal.h` and `m4_dma_api.h`. Their GPL-2.0-only licenses are preserved.

This target removes the old whole-executor binding and vendor/NVMe mode switch.
It does not link `m4_frontend`, `m4_nvme` or `m4_media_fixture`. The new HIF
connects the synthetic function to the portable userspace firmware process.
The BAR is a caller-specified, already reserved 16-KiB resource; no physical
address is selected automatically.

The mapping snapshots bind domain, attachment generation and per-map identity
before transfer. They are kernel-private mechanical state, not firmware DMA
or controller-buffer tokens. Native integration and later owner-switch results
must be recorded independently; the donor's PoC results do not graduate this
new binding.
