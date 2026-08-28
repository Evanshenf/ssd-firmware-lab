<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C2.1 A-prime contract and fake provider — unprivileged unit gate

- Date: 2026-08-28
- Result: PASS
- Scope: A-prime wire, per-device lifecycle state and injected fake adjacent providers
- Privilege: ordinary userspace; no root or kernel module
- Implementation commit: `3ca449f017cba1a9cd9dbbe3ef1e2c23a4fd16f8`
- Implementation root tree: `c2cb2963656a5ec6318c718f828347f2e00f2b29`
- V1 kernel load status after this result: `STOP`

This result closes Cycle 02 sub-gate C2.1 only. It does not validate a VFIO device fd, iommufd object, IOVA mapping, `vfio_dma_rw()`, pinning, IRQ, PCI, QEMU, firmware, NFC or media behavior.

## Frozen implementation identity

```text
kernel/vfio-cdev-v1 tree: 9e146ae2b25d12109364739999a3edf604a6b63e
tests/unit/vfio-c21 tree: efe9e01bb111d4258d24b7dc0035a90109dccf1a

A-prime unstable header SHA-256:
12d5441802117e65b8492161c33d2eda256402aca9e919456ab7acb9f1c34269

wire engine SHA-256:
cc1289758471a21976330bd9339d2fa9e6d1519becdba791375cb3146c67ad0b

state engine SHA-256:
68079ddd88a1a7ffe51762a4aa7c765ba34993c2bf3cb75c75cc4a2b52ecefbf

unit suite SHA-256:
465df1234e710a4acb72bf826e2a0b9e51f38344a35d3056fe5de2da82fb5bd0

unit Makefile SHA-256:
2abf3e27030fc3cec90d645cc04c6683fe1417b530d30346d2544e994ea50054
```

The complete source-file set was copied to the nested development VM and compared file-by-file by SHA-256 before the secondary compiler runs; the comparison was empty.

## Build and runtime identities

Primary unprivileged run:

```text
OS: Ubuntu 24.04.2 LTS
kernel: 6.8.0-138-generic x86_64
compiler: GCC 9.5.0
unit binary SHA-256:
998293fb467e44bc0e10b03c310f72264138baaa18c970b0ac7764b9c06485dd
```

Secondary `Profile-Nested` compiler/sanitizer run:

```text
OS: Ubuntu 26.04 LTS
kernel: 7.0.0-30-generic x86_64
compiler: Clang 21.1.8
ordinary unit binary SHA-256:
c428006b53d3fb896b13aefb77de9e317573147d2315b11f7ad399c4eca9b298
ASan+UBSan unit binary SHA-256:
ee7861e131de8689952100d661457031edb458db2b1c8f85afe652434661f0c5
ThreadSanitizer unit binary SHA-256:
e0f33811f7fd8fe7f7f061aa7b272f5c2b6a510e3632f1bbfc4adb6301aca347
structured-fuzz binary SHA-256:
cd47e503756888fbf4bb3ff26581b8f13a6f57a3280255981c8ecb67ccf2d7bd
```

Generated binaries are not distributed. Their digests identify the exact tested builds.

## Contract proven by the unit gate

### Wire and structural validation

- request, result and state are independent literal 64-byte records;
- three hand-written golden vectors verify every byte independently of the offset macros;
- compile-time checks pin every field and control-slot offset and require the final field to end at byte 64;
- all multi-byte fields use explicit little-endian loads and stores, without packed/native structure casts;
- wrong magic, version, size, operation, flags, reserved values, offset and short or oversized access fail without consuming sequence or replacing an older result;
- malformed result/state combinations and errno encodings fail closed;
- request length is `1..256`, IOVA arithmetic cannot wrap and the accepted range remains within one 4 KiB page.

### Copy and partial-side-effect semantics

- `COPY_IOAS_TO_BUFFER` uses a bounded scratch buffer and commits internal data only after full provider success;
- `COPY_BUFFER_TO_IOAS` reports no completed-byte count and conservatively marks a possible requested-range prefix change on every provider error;
- read/write permission, hole, forced error and partial-then-error fake modes are covered;
- guard observations prove no write outside the requested fake-page range;
- illegal adjacent-callback return values normalize to `-EPROTO`.

### Lifecycle and concurrency

- all mutable state is per instance and protected by one injected device lock;
- open, reset, attach, replace, detach and close advance generation and clear prior session state exactly as specified;
- replay, gap, stale generation and counter exhaustion fail closed;
- attach/replace/detach execute a per-call adjacent transition while the same device mutex is held; failure preserves the old state, generation, sequence, result and data, while success atomically advances the generation;
- deterministic contention handshakes prove reset/close wait behind a delayed copy;
- all three transitions, on success and failure, are tested with a submit waiting behind a delayed transition;
- replace/detach, on success and failure, are tested waiting behind a delayed copy;
- two independent device instances retain distinct state, generation, sequence, data and provider pages.

## Executed gates

```text
GCC -std=c11 -Wall -Wextra -Werror -Wpedantic: PASS, 9/9
Clang 21 ordinary compile and run: PASS, 9/9
GCC ASan+UBSan: PASS, 9/9
Clang 21 ASan+UBSan: PASS, 9/9
Clang 21 ThreadSanitizer: PASS, 9/9
architecture include/undefined-symbol gate: PASS
absolute out-of-tree BUILD_DIR: PASS, 9/9
structured deterministic UBSan fuzz: PASS, 10,000 inputs
repository policy, frozen Cycle 01 hashes, SPDX, links and diff checks: PASS
```

The structured fuzz runner tests arbitrary variable-length bytes and separately mutates valid request, result and state seeds so validation paths behind each 32-bit magic are reachable.

## Safety observations

- no kernel module was compiled or loaded for C2.1;
- no VFIO cdev or iommufd object was opened;
- no PCI function, QEMU process, IRQ, mmap, page pin or worker was created;
- the nested VM kernel taint remained `0`;
- its dedicated future media device remained kernel read-only (`RO=1`); the test did not open it.

## What remains STOP

C2.2 may add a compile-only kernel adapter and real synchronous `vfio_dma_rw()` provider. Before the first V1 load it must still record:

1. an exact Ubuntu GA headers build with `W=1`, strict checkpatch and successful modpost;
2. an actual `vfio_dma_rw` modversion dependency matching the installed export;
3. a kernel mutex/usercopy adapter review and no reverse dependency into portable firmware, NFC or media;
4. a privileged test for one exact page mapping and both copy directions, with no pin, IRQ, worker, mmap, PCI, QEMU or raw-media access;
5. cleanup and kernel-diagnostic evidence on the pinned nested environment.

Until those items pass, the first V1 kernel module load remains `STOP`.
