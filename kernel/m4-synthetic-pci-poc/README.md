<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# M4 synthetic PCI kernel PoC

GPL kernel modules for the bounded M4/M5 Profile-Nested experiment live here.
The runnable gates, user-space VFIO admission probe and lab boot assets are in
[`experiments/m4-iommu-poc`](../../experiments/m4-iommu-poc/README.md).

These modules are feasibility fixtures, not a stable kernel ABI or a graduated
SSD firmware implementation.

## Internal module boundaries

- `m4_pci.c`: synthetic PCI/config/BAR, transport services and owner-visible
  reset/notification mechanics;
- `m4_frontend.[ch]`: versioned bind/reset/poll/stop/quiescent seam;
- `m4_nvme.c`: replaceable native-NVMe executor fixture with no PCI context,
  direct BAR access, direct IOMMU call or VFS dependency;
- `m4_media.h`: replaceable bounded media operations;
- `m4_media_fixture.c`: disposable memory/regular-file provider and the only
  VFS-owning source;
- `m4_iommu.c`: software-IOMMU provider and permission-checked DMA service.

The current objects remain one composite experimental endpoint module.  The
seams prove dependency direction; they are not a loadable-provider ABI and do
not claim compatibility with the still-evolving C4.3/C4.4 B*-ABI.
