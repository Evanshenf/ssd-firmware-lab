<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C2 provider boundary

C2.1 deliberately has no Linux/VFIO copy provider. The only implementation of
the injected synchronous copy interface is the userspace fake under
`tests/unit/vfio-c21/`.

A provider that calls `vfio_dma_rw()` belongs to C2.2 and must arrive as an
independently reviewed change. Pinning, deferred work, IRQs and mmap remain out
of scope.

The C2.2 `vfio_rw` provider is load-gated. It performs exactly one synchronous
`vfio_dma_rw()` call per accepted operation, using `write=false` for IOAS to
device-buffer copies and `write=true` for device-buffer to IOAS copies. It does
not retain pages, mappings, addresses or callbacks after the call returns.
