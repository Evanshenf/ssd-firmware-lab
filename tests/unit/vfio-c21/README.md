<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C2.1 userspace contract tests

This suite compiles the same explicit-little-endian decoder and per-device
state engine intended for the V1 kernel harness as ordinary C11 userspace
code. It injects a one-page fake copy provider and requires no root access,
kernel module, VFIO, iommufd, PCI, firmware, NFC or media implementation.
It covers literal request/result/state golden records, fake transition success
and failure, both transition-to-submit and copy-to-transition lock order,
bounded contention observation, partial-side-effect semantics and two-device
isolation.

Run:

```sh
make -C tests/unit/vfio-c21 check
make -C tests/unit/vfio-c21 check-clang
make -C tests/unit/vfio-c21 check-sanitize
make -C tests/unit/vfio-c21 check-thread
make -C tests/unit/vfio-c21 fuzz-smoke
```

`fuzz-smoke` runs 10,000 deterministic variable-length inputs with UBSan. The
same source retains a `LLVMFuzzerTestOneInput` entry point for a later runner
whose libFuzzer runtime is known to be compatible.

C2.1 does not validate a VFIO device-fd region or a real IOAS copy. A real
`vfio_dma_rw()` provider is a separate C2.2 gate.
