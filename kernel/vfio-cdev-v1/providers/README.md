<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C2 provider boundary

C2.1 deliberately has no Linux/VFIO copy provider. The only implementation of
the injected synchronous copy interface is the userspace fake under
`tests/unit/vfio-c21/`.

A provider that calls `vfio_dma_rw()` belongs to C2.2 and must arrive as an
independently reviewed change. Pinning, deferred work, IRQs and mmap remain out
of scope.
