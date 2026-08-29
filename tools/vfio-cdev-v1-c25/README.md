<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C2.5 two-instance isolation oracle

This userspace oracle opens two distinct V1 VFIO cdevs under one owner
`/dev/iommu` file. It allocates two different IOAS objects and maps the same
numeric IOVA to two different anonymous Host pages. Distinct full-page and
internal-data patterns make an accidental provider, IOAS or per-device-state
alias observable.

The bounded matrix checks both copy directions, exact byte-for-byte observer
stability, independent generation and sequence state, reset, exact unmap and
remap, detach and reattach, attached close, IOAS destruction and a 24-round
parallel smoke test on each cdev. The privileged wrapper then uses
`--hold-survivor` to keep the base cdev attached while it unloads the peer
platform-device fixture. After the peer's real remove path completes, the base
session must perform another synchronous copy successfully.

Build and run the pure record/layout selftest without VFIO:

```sh
make -C tools/vfio-cdev-v1-c25 check
make -C tools/vfio-cdev-v1-c25 check-clang
make -C tools/vfio-cdev-v1-c25 check-sanitize
```

The privileged modes are owned by `tests/privileged/c2_5_vfio_cdev_v1.sh` and
must not be run directly on an unprovisioned host.

This is a `Profile-Nested` mechanism oracle. It does not implement device DMA,
pinning, IRQ, mmap/BAR, PCI, QEMU, NVMe, firmware, NFC or media behavior. Its
architecture-isolation claim is limited to the tested V1 per-instance state and
the current H0/V1/source-boundary gates; it is not Isolated-B* runtime
containment or a final adapter.
