<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# VFIO cdev V0 userspace smoke

`vfio_cdev_v0_smoke` opens a dynamically discovered VFIO device cdev and `/dev/iommu`, then exercises:

```text
access denied before BIND
→ VFIO_DEVICE_BIND_IOMMUFD
→ IOMMU_IOAS_ALLOC
→ VFIO_DEVICE_ATTACH_IOMMUFD_PT
→ GET_INFO / GET_REGION_INFO
→ region pread/pwrite
→ VFIO_DEVICE_RESET
→ DETACH / IOMMU_DESTROY / close
```

It does not map userspace memory into the IOAS and does not request simulated DMA or IRQ.

The exact-code smoke passed under `Profile-Nested`; see the [2026-08-28 result](../../docs/results/2026-08-28-m0-vfio-cdev-v0-nested.md).
