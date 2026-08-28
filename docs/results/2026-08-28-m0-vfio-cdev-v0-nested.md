<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# M0 V0 emulated VFIO cdev contract — Profile-Nested

- Date: 2026-08-28
- Result: PASS
- Evidence level: `Profile-Nested`
- Scope: platform-device VFIO cdev, iommufd ownership and one software region

## Environment and build identity

```text
OS: Ubuntu Server 26.04 LTS
kernel: 7.0.0-30-generic
compiler: GCC 15.2.0
module vermagic: 7.0.0-30-generic SMP preempt mod_unload modversions
module srcversion: 755BDD519F5679749E9F0ED
module SHA-256: aa5c65f15910ea20aaf0a3f313c18e06b16605b42f152ecd2af20808aa21eeba
userspace SHA-256: 186e5d925212b7ff3a60d9513cfbb0470e9716f07d9a54e6729c1c996fc7041e
```

The generated binaries are not distributed. Their digests identify the locally tested build only. Kernel code passed `W=1`, modpost and strict checkpatch. Userspace built with `-Wall -Wextra -Werror -Wpedantic`.

## Safety pre-gates

- platform parent had a bound driver and valid module owner;
- `CONFIG_VFIO_DEVICE_CDEV` and `CONFIG_IOMMUFD` were enabled;
- unsafe no-IOMMU mode was `N` and was never enabled;
- no V0 module, platform device or VFIO cdev existed before load;
- initial kernel taint was zero;
- hypervisor console/reboot recovery was available;
- private media remained read-only and unmounted;
- no mmap, IRQ, DMA, `vfio_dma_rw` or page pinning existed in V0.

## Successful contract sequence

```text
VFIO core ready; unsafe_noiommu=N
→ platform driver/device probe
→ vfio_alloc_device
→ vfio_register_emulated_iommu_dev
→ dynamic /dev/vfio/devices/vfioN discovery
→ access rejected before BIND
→ VFIO_DEVICE_BIND_IOMMUFD (observed devid=1)
→ IOMMU_IOAS_ALLOC (observed IOAS=2)
→ VFIO_DEVICE_ATTACH_IOMMUFD_PT
→ GET_INFO / GET_REGION_INFO
→ software-region read/write and boundary tests
→ VFIO_DEVICE_RESET and zero verification
→ VFIO_DEVICE_DETACH_IOMMUFD_PT
→ IOMMU_DESTROY
→ close cdev/iommufd
→ vfio_unregister_group_dev
→ vfio_put_device and platform remove
```

Dynamic numeric IDs are observations, not stable ABI values.

## Region assertions

- exactly one 4096-byte region at offset zero;
- flags are read/write only; mmap is not advertised;
- initial contents are zero;
- a 64-byte pattern round-trips through `pwrite`/`pread`;
- region index 1 is rejected;
- read/write at offset 4096 returns zero bytes;
- a 64-byte request at offset 4088 is truncated to 8 bytes;
- reset synchronously clears the region to zero.

During test development, the first boundary oracle incorrectly expected `ENOSPC` for a write at offset 4096. Linux `simple_write_to_buffer()` specifies a zero-byte return when `pos >= available`; the oracle was corrected, the host rebooted to a clean taint state, both artifacts rebuilt, and the exact-code run above passed. The kernel registration, BIND and cleanup paths had already completed without error in that diagnostic run.

## Cleanup evidence

- userspace reported the complete BIND/ATTACH/region/reset/cleanup PASS chain;
- module unloaded normally without force;
- platform device and dynamic VFIO cdev disappeared;
- VFIO reported addition to and removal from its emulated IOMMU group;
- no new BUG, WARNING, Oops, KASAN, UBSAN, lockdep, refcount or use-after-free diagnostic appeared;
- only expected out-of-tree/unsigned taint bits were added (`12288`);
- normal reboot restored taint to zero;
- private media remained read-only, unmounted and unchanged.

## What this proves

Ubuntu GA 7.0 exports the VFIO cdev and iommufd wrappers required for an out-of-tree emulated-device contract harness. A custom kernel is not required for cdev BIND, IOAS attach/detach, bounded software-region access or synchronous reset.

## What remains STOP

V0 is not a PCI function and does not prove:

- QEMU device assignment or PCI configuration regions;
- mmap or a real BAR;
- eventfd/IRQ/MSI/MSI-X;
- IOVA mapping, `vfio_dma_rw`, pin/unpin or unmap revocation;
- Host-to-Guest ownership switching;
- native storage-driver binding or protocol behavior;
- bare-metal graduation.
