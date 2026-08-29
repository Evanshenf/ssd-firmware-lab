<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# V1 peer platform-device fixture

This test-only module registers one additional platform device named
`ssd-fwlab-vfio-v1` with ID 0. Registration is synchronous: module load succeeds
only when the already-loaded, frozen V1 driver probes the peer, binds it and
stores non-NULL per-device driver data. A successful load therefore creates the
second platform path `ssd-fwlab-vfio-v1.0` and its independent VFIO cdev.

The fixture contains no V1 implementation, project header, H0, portable-core,
firmware, NFC or media dependency. It has no soft dependency and does not load
V1 itself. The privileged C2.5 wrapper owns the load order and must load the
frozen V1 module first.

Unload the fixture only after userspace has closed the peer cdev. Unload
unregisters exactly this peer platform device and exercises the frozen V1
driver's per-device remove path while its original platform device may remain
active. The single module-global pointer is only the peer registration handle;
it carries no V1 generation, sequence, buffer, provider or IOAS state.

Build against the exact running-kernel headers:

```sh
make -C kernel/vfio-cdev-v1-peer-fixture
```

This is an instantiation fixture, not a second adapter, stable platform ABI,
firmware interface or production hotplug mechanism.
