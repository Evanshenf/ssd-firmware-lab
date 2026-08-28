<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C2.3 negative IOAS-copy oracle

This independent, single-threaded oracle targets the frozen V1 module and its
explicitly unstable A-prime mailbox. It does not link the C2.1 engine or the
C2.2 userspace oracle.

The permission expectations come directly from the Linux IOAS access path:
`iommufd_access_rw()` returns `-EPERM` when an area lacks the requested
`IOMMU_READ` or `IOMMU_WRITE` permission, and `-ENOENT` when the requested IOVA
range is not completely mapped. The engine may conservatively report
`DEST_MAY_HAVE_PARTIAL` for a buffer-to-IOAS error; this oracle never interprets
that flag as a copied-byte count.

Coverage includes pre-BIND rejection, bound-but-unattached state, malformed
mailbox records, replay/stale/gap identity, one-way mapping permissions,
unmapped starts, rejected partial unmap, two adjacent exact mappings and
cross-page structural rejection. Every structural/state rejection must preserve
the previous STATE and RESULT byte-for-byte, the complete 4 KiB internal data
region, and both userspace page buffers.

The IOAS ioctl matrix uses source-reviewed exact errors: an attached IOAS
rejects missing permissions and page-alignment violations with `-EINVAL`,
unknown flags or nonzero reserved fields with `-EOPNOTSUPP`, and overlap with
`-EEXIST`. A single-area partial unmap is expected to return `-ENOENT` and keep
the mapping usable. Multi-area partial unmap is deliberately not attempted
because it can remove an earlier prefix before reporting a later hole.

An unaligned mapping made before attach is safe to construct in an unattached
IOAS; attach must then fail with `-EADDRINUSE` without changing engine identity
or data. The oracle removes that exact mapping and proves recovery with an
aligned map and successful attach. Cross-page mailbox requests are rejected by
the A-prime parser with `-ERANGE`; this is a structural test and makes no claim
about a late or partially completed real IOAS transfer.

Build and run the pure helper selftest without VFIO:

```sh
make -C tools/vfio-cdev-v1-c23
tools/vfio-cdev-v1-c23/build/vfio_cdev_v1_c23 --selftest
```

Privileged execution is performed only through
`tests/privileged/c2_3_vfio_cdev_v1.sh` after review. No code in this directory
opens a raw-media block device.
