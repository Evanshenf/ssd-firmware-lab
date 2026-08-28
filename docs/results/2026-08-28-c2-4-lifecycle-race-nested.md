<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C2.4 lifecycle and bounded real-race robustness — Profile-Nested

- Date: 2026-08-28
- Result: PASS
- Evidence level: `Profile-Nested`
- Oracle commit: `eb5f351a2fc39e7a59c348d38fbc0e037a73d7ec`
- Reused frozen module commit: `95e7a052ffc8320d13b1ec23ea82f0de21afe830`
- Scope: owner transition, teardown and bounded real-iommufd launch stress

C2.4 reused the exact frozen C2.2 module. It added no kernel behavior, pinning, IRQ, mmap, PCI, QEMU or media path.

## Immutable identities

```text
oracle commit root tree: 1d3c0c56ab0c0fe7a098d1afbf94928b81ef92ba
tools/vfio-cdev-v1-c24 tree: cc013fb0408c3412cffbb0b4c9f9fedfbb7d823a

oracle source SHA-256:
4d568b3b1359cc2f5d5b4c5cc1a95122c1281871463fe66e4c6cb11853a112f6

oracle Makefile SHA-256:
4a1f0cd62d61121342a04242b4ebe6cec9c0ce8e76a308be12a06d5128e488da

privileged script SHA-256:
12bc26d54cfc0b203df47aa5480b603a70b17c16d5addb36af4c5c201f28d2db

reused module SHA-256:
8c9f1418dcdce7e0ebbd18d97adcc9e11a7a9f7e2bfe5b565f18f8db8714b072
module srcversion: FFE4E0F87FA9FA275C67192
vfio_dma_rw modversion: 0xaa22e02a

L1 GCC 15 oracle SHA-256:
ddd13708e5f7486fb539ebc493272a2b777fd1c937acec985f441aec2a2f9800
```

The oracle and script matched their commit file-by-file. GCC 15, Clang 21, ASan/UBSan selftests, sh/dash/bash syntax and repository checks passed before load. Independent final reviews found no remaining code P0/P1 and gave the hardened wrapper a static GO.

## Lifecycle matrix

### Replacement and failed replacement

- IOAS A and B were mapped at distinct IOVAs.
- A attached and copied successfully.
- Replacing A with B advanced generation, cleared RESULT/data, made A return `-ENOENT` and made B usable.
- Replacing B with a deliberately misaligned IOAS C returned `-EADDRINUSE`; the complete engine observable and B ownership remained unchanged.

### Attach-result copyout failure

With A already attached, a read-only userspace attach argument requested replacement by B. The underlying replacement succeeded but the core could not copy the returned PT ID to userspace, so the ioctl returned `EFAULT` and VFIO core invoked DETACH.

The final state was exactly generation `+2`, OPEN_UNATTACHED, RESULT `ENODATA` and zero data. Both A and B could then be exactly unmapped and destroyed, proving no hidden owner reference. A newly created B could attach and copy successfully.

### Duplicate detach and serial close

- The first DETACH advanced generation and cleared state.
- The second DETACH returned success because the VFIO callback is void; the engine observable was an exact no-op.
- Exact unmap and destroy of the IOAS then succeeded, proving no hidden owner.
- A separate close-while-attached case was deliberately serial: no thread used the fd during close. The old IOAS could be unmapped/destroyed, and a new bind reopened at exactly the prior generation `+2` before successful attach/copy.

### Module unload ordering

A helper completed cdev BIND and held a live module reference. Ordinary, non-forced `rmmod` was promptly rejected as in-use while module, platform device and cdev remained intact. After the helper closed normally and refcount returned to zero, the final ordinary unload succeeded.

## Bounded real-iommufd launch stress

The pthread barrier coordinated userspace release only. It did not prove forced overlap inside the kernel, and outcome counters were allowed to contain one linearization class. Deterministic two-order serialization evidence remains the frozen [C2.1 injected-fake contention result](2026-08-28-c2-1-a-prime-fake-provider.md); C2.4 adds repeated real-iommufd stress and rejects every result outside the allowed classes.

Observed distribution:

```text
submit vs exact UNMAP: 24 rounds; success=0, -ENOENT=24
submit vs RESET:       12 rounds; pwrite64=0, -ESTALE=12
submit vs REPLACE:     12 rounds; pwrite64=0, -ESTALE=12
submit vs DETACH:      12 rounds; pwrite64=0, -ENOTCONN=12
```

Therefore this run observed the action-first class in every real stress round. It does not claim that both classes were observed.

Every round still enforced:

- exact UNMAP success and length 4096;
- submit result restricted to success or `-ENOENT`;
- complete Host source page unchanged and internal data either correctly committed or entirely unchanged;
- every new request after UNMAP returned `-ENOENT`;
- RESET/REPLACE/DETACH advanced generation by exactly one, cleared RESULT and all data;
- BUFFER_TO_IOAS pwrite success left exactly the requested 64-byte Host mutation, while a state rejection left the complete owner page unchanged;
- the complete nonowner page remained unchanged;
- replacement selected one unique owner, and detach reached OPEN_UNATTACHED before controlled recovery.

## Hardened wrapper and cleanup

The wrapper required a clean-taint boot, the frozen module hash/CRC/srcversion, unsafe no-IOMMU disabled, and read-only/unmounted future media with unchanged write counters. A root-only private lock directory prevented concurrent gate runs and unsafe lock-file substitution.

HUP, INT and TERM retained nonzero statuses and invoked bounded cleanup. The helper, selftest, main oracle and all ordinary `rmmod` operations had deadlines; helper cleanup escalated through TERM and KILL only for the userspace helper, never forced module removal. Temporary FIFO/log/directory and process absence were verified.

One exact frozen-adapter driver diagnostic was expected from the deliberate duplicate DETACH:

```text
engine detach rejected: -22; forcing VFIO detach
```

It appeared exactly once. No other engine close/detach/generation diagnostic, BUG, kernel WARNING, Oops, sanitizer, KFENCE, lockdep, refcount, UAF, hung-task, lockup or RCU-stall diagnostic appeared.

Module, platform device, cdev, helper and temporary resources had zero residue. Future-media RO/mount/holder/swap and write-counter checks remained unchanged.

The filtered execution-boot journal has SHA-256:

```text
1810152db5613d003ac25be2c15d16bf324a3039eaf2cb6b0439fa07ea191282
```

Expected out-of-tree/unsigned taint was `12288`. A normal reboot restored taint to zero, returned the system to a running state with no failed units and left no V1 residue.

## Evidence boundary

C2.4 proves lifecycle convergence and absence of contract-external results in its fixed iteration counts. It is not an exhaustive scheduler proof and did not use userfaultfd to force in-kernel overlap.

The following remain STOP:

- pinning, zero-copy or a DMA engine;
- IRQ/eventfd;
- BAR/PAT and Host native binding;
- PCI composition and QEMU assignment;
- NVMe protocol, firmware/NFC/media behavior or raw-media writes.

The next gate is C2.5 two-instance and architecture-isolation graduation. C2.5 completion reaches the fifth Cycle 02 round and triggers the next file-based Pro review.
