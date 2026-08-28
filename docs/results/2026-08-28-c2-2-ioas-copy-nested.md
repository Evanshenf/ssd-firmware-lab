<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C2.2 synchronous IOAS copy — Profile-Nested

- Date: 2026-08-28
- Result: PASS
- Evidence level: `Profile-Nested`
- Implementation commit: `95e7a052ffc8320d13b1ec23ea82f0de21afe830`
- Scope: one page, synchronous CPU-mediated IOAS copy, no driver-retained VFIO pin lease

This result covers one controlled load of the exact implementation commit. It is not device DMA and does not authorize pinning, IRQ, BAR, PCI, QEMU, NVMe protocol or raw-media work.

## Immutable source and build identity

```text
repository root tree: 6c0122f05ecce5c932b4e1240aa6347ee48fb54d
kernel/vfio-cdev-v1 tree: 3b18f44b8d76baa7c327bd188bafdd6beaaaebc1
tools/vfio-cdev-v1 tree: b495253cdfc8fdd84fb354c1a938701a190df88f

OS: Ubuntu 26.04 LTS
kernel: 7.0.0-30-generic
kernel package/headers: 7.0.0-30.30
compiler: x86_64-linux-gnu-gcc 15.2.0
module vermagic: 7.0.0-30-generic SMP preempt mod_unload modversions
module srcversion: FFE4E0F87FA9FA275C67192
module SHA-256:
8c9f1418dcdce7e0ebbd18d97adcc9e11a7a9f7e2bfe5b565f18f8db8714b072

userspace oracle SHA-256:
430583501e1cc13950e6c1b247f233698c15d9b0978b9010395f901df0cf3872

privileged script SHA-256:
6636cc8f9211b43a9698d501ecb0f369cb8d84b3050f7614217109fb1b271931

vfio_dma_rw modversion: 0xaa22e02a
```

The complete adapter, frozen C2.1 contract, oracle and script source set matched the implementation commit file-by-file by SHA-256. The module passed clean `W=1` build, modpost and strict checkpatch with zero errors, warnings or checks. The userspace oracle passed GCC and Clang strict builds and its unprivileged region-layout self-test.

Generated binaries are not distributed; their digests identify the exact tested builds.

## Adapter boundary

- one platform VFIO cdev instance, not a PCI function;
- data region index 0 and A-prime control region index 1, each exactly 4 KiB, read/write and non-mappable;
- non-overlapping absolute offsets returned by `VFIO_DEVICE_GET_REGION_INFO` rather than assumed by the oracle;
- frozen per-device parser/state engine and one mutex across submit, copy and lifecycle transitions;
- ATTACH, REPLACE and DETACH call the standard emulated iommufd helpers inside the same state transition critical section;
- one synchronous `vfio_dma_rw()` call per accepted operation;
- no `vfio_pin_pages()`, retained page, translation, userspace pointer, `.dma_unmap`, worker, IRQ, mmap, PCI, QEMU, firmware, NFC or media path.

The provider directions were:

```text
write=false: IOAS → bounded kernel scratch → internal data commit on success
write=true:  internal data → IOAS
```

## Successful primary sequence

```text
VFIO cdev open
→ BIND_IOMMUFD
→ IOAS_ALLOC
→ exact aligned 4 KiB MAP at a fixed nonzero IOVA
→ ATTACH
→ both copy directions at lengths 1, 37, 64 and 256
→ exact whole-page UNMAP
→ new accepted request reports operation errno -ENOENT
→ DETACH
→ DESTROY IOAS
→ close and unload
```

Every request/result checked operation, sequence, generation, IOVA, length and operation errno. IOAS-to-buffer tests preserved the untouched data tail and the complete Host page. Buffer-to-IOAS tests checked the complete page and required all bytes outside the requested range to remain unchanged.

The post-unmap failure also preserved the entire 256-byte internal data window, confirming that the real IOAS-to-buffer provider did not commit a failed scratch copy.

## Independent attach-before-map sequence

A second cdev session tested the reverse order required by the emulated access alignment contract:

```text
BIND_IOMMUFD
→ IOAS_ALLOC
→ ATTACH the empty IOAS successfully
→ unmapped copy reports -ENOENT
→ MAP one exact page into the already attached IOAS
→ the next sequence copies successfully without another ATTACH
→ UNMAP
→ DETACH
→ DESTROY and close
```

## Safety, cleanup and recovery

Before load, the script required unsafe no-IOMMU mode to remain disabled, exact module vermagic, the expected `vfio_dma_rw` CRC, a clean module/platform state and read-only/unmounted future media. It recorded module, tool and script hashes before `insmod` and ran the pure region-layout self-test.

The tool was bounded by a timeout. Cleanup checked actual `/proc/modules` state, used only normal `rmmod`, waited for asynchronous cdev removal and required zero module, platform-device and cdev residue. Kernel logs showed the expected VFIO load, IOMMU-group add, registration and group removal sequence with no BUG, WARNING, Oops, sanitizer, lockdep, refcount, UAF or hung-task diagnostic.

The dedicated future media device remained kernel read-only, unmounted and unused. Main-device and partition mount, holder and swap checks passed, and its write-I/O and sectors-written counters did not change.

Only expected out-of-tree and unsigned-module taint bits were added (`12288`). A normal reboot restored taint to zero; the system returned to a running state with no failed units and no V1 residue.

The retained filtered kernel journal has SHA-256:

```text
913d7c445a04f1312c19c22d45f76d41ffa58e39f5af25a435d956ed50338438
```

## What this proves

On the pinned Ubuntu GA 7.0 nested environment, an emulated VFIO cdev can synchronously copy in both directions through one exact IOAS page, supports both MAP-before-ATTACH and ATTACH-before-MAP, rejects a new access after exact unmap and cleans up without retained project objects.

## What remains STOP

This result does not cover or authorize:

- permission, hole, cross-page, multi-area or late-partial characterization;
- IOAS replacement or duplicate-detach abnormal states;
- copy racing with unmap, replace, detach, reset or close;
- pinning, zero-copy or a DMA engine;
- IRQ/eventfd, BAR/PAT, PCI composition or QEMU assignment;
- native NVMe binding, protocol behavior, bare metal or raw NAND-media writes.

Those remain independent C2.3, C2.4 and later gates.
