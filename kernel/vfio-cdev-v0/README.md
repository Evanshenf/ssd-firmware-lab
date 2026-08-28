<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# V0 emulated VFIO cdev contract harness

V0 validates the Ubuntu GA 7.0 exported contract for an emulated VFIO device using the cdev and iommufd path. Its parent is an ordinary platform device with a bound platform driver so VFIO can safely hold the driver's module owner during `VFIO_DEVICE_BIND_IOMMUFD`.

V0 provides exactly one 4 KiB software region with `pread`/`pwrite` access and synchronous reset-to-zero. It deliberately provides no PCI BDF, mmap, IRQ/eventfd, DMA, page pinning, migration or storage protocol.

The required lifecycle is:

```text
platform probe
→ vfio_alloc_device
→ vfio_register_emulated_iommu_dev
→ open cdev
→ BIND_IOMMUFD
→ IOAS_ALLOC and ATTACH_IOMMUFD_PT
→ region read/write/reset
→ DETACH and IOAS destroy
→ close cdev
→ vfio_unregister_group_dev
→ vfio_put_device
→ platform remove
```

## Build

```sh
make -C kernel/vfio-cdev-v0
make -C tools/vfio-cdev-v0
```

## Privileged smoke

Run only on the dedicated nested lab with console/reboot recovery:

```sh
sudo tests/privileged/m0_vfio_cdev_v0.sh \
  kernel/vfio-cdev-v0/ssd_fwlab_vfio_v0.ko \
  tools/vfio-cdev-v0/vfio_cdev_v0_smoke
```

The shell test discovers the dynamic `vfioN` node through platform sysfs. It never assumes `vfio0`.

## Stop boundary

Passing V0 proves only cdev ownership, IOAS attach/detach and software-region semantics. It is not a Host synthetic PCI function and does not authorize H1 BAR work, IRQ, `vfio_dma_rw`, long-term pinning, QEMU assignment or native storage-driver binding.

The first exact-code run passed under `Profile-Nested`; see the [2026-08-28 result](../../docs/results/2026-08-28-m0-vfio-cdev-v0-nested.md).
