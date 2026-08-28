<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# VFIO cdev V1 work area

C2.1 contains only the explicitly unstable A-prime wire decoder, per-instance
state machine and an injected synchronous-copy seam. It has no VFIO device,
iommufd object, firmware, NFC or media dependency and can run as an ordinary
userspace unit test.

Attach, IOAS replacement and detach are represented by a per-call synchronous
transition callback. The callback runs under the same per-device mutex as
submit/reset/close; only a successful callback advances generation and commits
the new local state. A failed callback leaves the prior generation, sequence,
result and data unchanged.

The A-prime header is test-only. It must not move into portable contracts or be
treated as a final HIF ABI.
