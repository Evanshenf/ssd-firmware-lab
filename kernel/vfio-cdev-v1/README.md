<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# VFIO cdev V1 work area

C2.1 contains only the explicitly unstable A-prime wire decoder, per-instance
state machine and an injected synchronous-copy seam. It has no VFIO device,
iommufd object, firmware, NFC or media dependency and can run as an ordinary
userspace unit test.

Attach, IOAS replacement and detach are represented by a per-call synchronous
transition callback. The callback runs under the same per-device mutex as
submit/reset/close; only a successful callback advances generation and commits
the new local state. A failed callback leaves the prior generation, sequence,
result and data unchanged.

The A-prime header is test-only. It must not move into portable contracts or be
treated as a final HIF ABI.

## C2.2 load-gated adapter

C2.2 adds a one-instance platform VFIO cdev adapter whose build alone does not
authorize module load. Region index 0 is a 4 KiB read/write data buffer;
region index 1 is a 4 KiB read/write A-prime control region. Their offsets are
discovered with
`VFIO_DEVICE_GET_REGION_INFO`, do not overlap, and neither advertises mmap.
Probe fails with `-EOPNOTSUPP` unless the running kernel uses 4 KiB pages, which
is the exact disposable C2.2 mapping contract.

The adapter copies every userspace request into a fixed kernel snapshot before
calling the frozen engine. Attach, IOAS replacement and detach run through the
engine transition callback while its per-device mutex is held. The only real
copy provider synchronously invokes `vfio_dma_rw()`; it retains no page,
translation or pointer and provides no `dma_unmap`, pin, IRQ, worker, mmap, PCI
or QEMU path.

Build and inspect only:

```sh
make -C kernel/vfio-cdev-v1 W=1
modprobe --dump-modversions \
  kernel/vfio-cdev-v1/ssd_fwlab_vfio_v1.ko | grep vfio_dma_rw
```

Successful compile and modpost evidence are necessary but do not by themselves
authorize `insmod` or `modprobe`; the recorded safety and review gates decide
whether one exact build may be loaded.
