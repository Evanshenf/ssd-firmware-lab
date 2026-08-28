<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C2.3 IOAS protection, range and error characterization — Profile-Nested

- Date: 2026-08-28
- Result: PASS
- Evidence level: `Profile-Nested`
- Oracle commit: `bf99d04ba9d1670a382d9a985fe6a47a7f494504`
- Reused frozen module commit: `95e7a052ffc8320d13b1ec23ea82f0de21afe830`
- Scope: single-thread protection, range, malformed request and error-side-effect characterization

C2.3 reused the exact C2.2 module without modifying its frozen adapter, provider, parser, state engine or unstable UAPI. It added an independent userspace oracle and safety script only.

## Immutable identities

```text
oracle commit root tree: c266c4ced006f75947af2d500e775d0dd96a1fc9
tools/vfio-cdev-v1-c23 tree: 7d19a86b94856413ce0ef3fd35cfb022ecb65458

oracle source SHA-256:
66c851c59deb78a77d6385ee060829f6513ee874e30bf8d661ac85bc22fcb18c

oracle Makefile SHA-256:
7dc18874e91a260f737dfb689dec9af796091ea095a7f64e3af392b0d37b8052

privileged script SHA-256:
21dabcc8dedd27b3c8a8687ea90babb0f25523f5a842ed0088e42380b0ee0f98

reused module SHA-256:
8c9f1418dcdce7e0ebbd18d97adcc9e11a7a9f7e2bfe5b565f18f8db8714b072
module srcversion: FFE4E0F87FA9FA275C67192
vfio_dma_rw modversion: 0xaa22e02a

L1 GCC 15 oracle SHA-256:
70f53eb01b23e2db2ef14e0ada1e99474e1a5abdb63b1833c4a74fdcee3afdf5
```

The oracle and script matched their commit file-by-file by SHA-256. GCC 15 and Clang 21 strict builds, an ASan/UBSan build, the pure little-endian/interval self-test and shell syntax checks passed before load.

## Three error classes

The oracle treated three classes differently:

```text
parser/state rejection:
  pwrite returns a negative errno; sequence and all observables stay unchanged

accepted provider error:
  pwrite returns 64; sequence advances; RESULT contains the exact operation errno

IOMMU ioctl error:
  ioctl returns its exact errno; engine state/result/data and Host pages stay unchanged
```

The complete observable was raw STATE, raw RESULT plus validity, the full 4 KiB internal data region, an 8 KiB Host VMA and a separate 4 KiB Host VMA. Relevant negative cases compared every byte before and after.

## Request and state rejection matrix

The following exact results passed without consuming a sequence or changing the observable:

| Case | Errno |
|---|---:|
| pre-BIND device info/read/write | `EINVAL` |
| bound but unattached submit | `ENOTCONN` |
| bad magic, ABI, size, operation, flags or reserved fields | `EPROTO` |
| zero length, length 257, IOVA overflow or cross-page request | `ERANGE` |
| stale generation | `ESTALE` |
| replayed sequence | `EALREADY` |
| skipped sequence | `ERANGE` |
| 63/65-byte or two fragmented 32-byte submits | `EMSGSIZE` |
| wrong submit offset | `EINVAL` |

The unattached case retained `ENODATA` for RESULT and also preserved prefilled internal data and both Host VMAs.

## MAP and attach error matrix

- no readable or writeable permission: `EINVAL`;
- unknown MAP flag or nonzero reserved field: `EOPNOTSUPP`;
- non-page-aligned IOVA, length or user address after attach: `EINVAL`;
- overlapping MAP: `EEXIST`, followed by successful access through the original mapping;
- a safe unaligned mapping created before attach: ATTACH returned `EADDRINUSE`, with complete engine/data/page state unchanged;
- exact removal of that unaligned mapping followed by aligned MAP, ATTACH and copy recovered successfully.

## Provider permission and unmapped behavior

With a READABLE-only mapping:

- IOAS-to-buffer succeeded;
- buffer-to-IOAS was accepted and reported `-EPERM`;
- the conservative destination-partial-possible flag was present;
- the complete Host page and complete 4 KiB internal source remained unchanged.

With a WRITEABLE-only mapping:

- IOAS-to-buffer was accepted and reported `-EPERM` without the partial flag;
- the complete internal data remained unchanged;
- buffer-to-IOAS succeeded and the oracle compared the full expected 8 KiB VMA and the independent 4 KiB VMA.

Both directions to a completely unmapped IOVA were accepted and reported `-ENOENT`. IOAS-to-buffer did not commit scratch data; buffer-to-IOAS left complete Host VMAs unchanged.

These early permission/unmapped failures happened before a byte copy on this kernel. The result does not generalize that behavior into transactional rollback for later failures.

## Range, unmap and adjacent mappings

- a one-byte request at the last byte of a page succeeded;
- a 256-byte request ending exactly at the page boundary succeeded;
- a two-byte request beginning at the last byte was rejected by the parser with `ERANGE` and no observable change;
- a half-page UNMAP against one exact 4 KiB mapping returned `ENOENT` and changed no engine/data/page observable;
- both copy directions remained usable after the rejected partial UNMAP, followed by exact whole-page unmap;
- two separately mapped adjacent pages were independently accessible at the first page's last byte and second page's first byte;
- a request spanning their boundary was rejected by the parser with `ERANGE` before the provider;
- after exact unmap of page A, page B remained accessible.

C2.3 did not issue a multi-area UNMAP that could remove a complete prefix before failing on a partially covered later area.

## Cleanup and recovery

The script required the frozen C2.2 module hash, exact `vfio_dma_rw` CRC, matching vermagic, unsafe no-IOMMU disabled, clean project state and read-only/unmounted future media. The oracle was time-bounded and single-threaded.

Module, platform device and cdev disappeared after normal unload. Kernel logs contained the expected VFIO registration and IOMMU-group add/remove sequence with no BUG, WARNING, Oops, sanitizer, lockdep, refcount, UAF or hung-task diagnostic. Future-media mount, holder, swap and write-counter checks remained unchanged.

The filtered execution-boot journal has SHA-256:

```text
e1531ee600a2e2f000b33fb0646c65fb32c2e199c8baf9c204a64db21f386ea7
```

Expected out-of-tree/unsigned taint was `12288`. A normal reboot restored taint to zero, returned the system to a running state with no failed units and left no V1 residue.

## Evidence boundary

C2.3 proves exact error classification and state/data side-effect behavior within the supported single-page synchronous envelope. It does not claim that a real multi-area late-partial copy was observed: the frozen parser rejects cross-page requests before the provider. C2.1 fake-provider tests remain the evidence for partial-then-error state semantics.

The following remain STOP for C2.4 or later:

- concurrent UNMAP;
- IOAS replacement and failed replacement;
- duplicate detach and close without explicit detach;
- submit racing with replace, detach, reset or close;
- pinning, zero-copy, IRQ, BAR/PAT, PCI, QEMU, protocol or media I/O.
