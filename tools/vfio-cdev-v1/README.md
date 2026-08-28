<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C2.2 synchronous IOAS-copy oracle

`vfio_cdev_v1_c2_2` is a privileged test program for the disposable V1
platform harness. It implements an independent byte-wise little-endian oracle
for A-prime; it does not link the C2.1 parser.

The program queries both VFIO regions and uses a valid STATE record to identify
the control region, so absolute region offsets and dynamic `vfioN` numbers are
never hard-coded. It maps exactly one anonymous 4 KiB page, exercises both
synchronous CPU-mediated IOAS-copy directions, verifies guards, unmaps the
exact mapping and requires the next operation result to contain `-ENOENT`.

It also starts a separate cdev session that first ATTACHes an empty IOAS,
requires copy to report `-ENOENT`, then maps the page into that already-attached
IOAS and requires the next sequence to copy successfully. This validates the
attached-access alignment/order without a second ATTACH.

Build only:

```sh
make -C tools/vfio-cdev-v1
```

The ignored build artifact is
`tools/vfio-cdev-v1/build/vfio_cdev_v1_c2_2`.

The pure interval/overflow validator can be exercised without VFIO or root:

```sh
tools/vfio-cdev-v1/build/vfio_cdev_v1_c2_2 --selftest-layout
```

Runtime discovery requires exactly two non-overlapping 4096-byte regions whose
flags are exactly read/write with no mmap support. Every byte in both half-open
intervals must be representable by both `uint64_t` and `off_t`.

Execution is delegated to `tests/privileged/c2_2_vfio_cdev_v1.sh` after an
explicit safety review. The tool never opens a raw-media device.
