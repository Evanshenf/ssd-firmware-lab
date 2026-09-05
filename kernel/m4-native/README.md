<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Native firmware PCI/HIF (experimental J1)

This builds a Host-visible software PCI function and software IOMMU for a
disposable x86-64 Linux lab VM. It is not a physical endpoint or a vfio-user
device. It requires an explicitly reserved, exactly 16-KiB memory aperture;
the module refuses ordinary/unreserved RAM. Do not load it on a production host.
The current integration target is Ubuntu `7.0.0-30-generic`; other kernels are
not a portability claim.

The two modules are `ssd_fwlab_native_iommu` and `ssd_fwlab_native_pci`.
`bar_start` must be supplied explicitly. The endpoint begins with
`driver_override=none`, without a running firmware process. Its root-only
`/dev/fwlab-native-BDF` interface permits one attached process. Attach the
firmware and wait for `NATIVE_READY` before binding the ordinary Linux `nvme`
driver to that exact BDF. Do not globally unbind an NVMe driver or device class.

```text
Linux nvme -> synthetic PCI SQ capture -> userspace HIF decode
-> Linux-profile-v1 -> shared command-spine lifecycle
-> aggregate Block -> M3-P FTL -> C3 NFC -> physical file-NAND
-> immutable completion intent/lease -> kernel CQE phase/IRQ
```

The kernel does not interpret NVMe opcode policy or perform logical-file I/O.
The explicit Kbuild object list excludes donor NVMe/media executors. Two
distinct strong link anchors name the one SQ consumer and CQE publisher.
Mappings bind domain, attachment, mapping identity, direction and controller
epoch; copy-time checks enforce the active PCI/CC state. This software IOMMU
does not claim to protect a physical DMA master.

Build using the installed kernel headers:

```sh
make -C kernel/m4-native W=1
make -C frontends/linux-m4 worker native-io
```

Use the worker's `--media-dir` only with a new private directory owned by its
effective user. `--format` exclusively creates `nand.bin`; omit it to recover
the same media with its exact UUID. No raw block backend is implemented here.
The worker's `--binding-sha` records the supplied build identity; it is not
remote attestation. The private ioctl ABI is intentionally outside portable
headers and currently supports the tested native 64-bit process only.

The independent `j1_native_io` client has `write`, `verify`, `cut1`, `cut2` and
`cut3` modes. It checks an exclusive, unmounted 1-MiB namespace, namespace ID,
vendor/model identity and the exact synthetic sysfs BDF before any write.
Its one-shot cuts are root-only and disabled by default. They interrupt one
specific test origin at DMA-in, DMA-out or pre-CQE, followed by a real Linux
controller reset and data/canary comparison. They do not constitute the later
cross-owner stale-IOVA/eventfd/lease canary gate.

The current profile is bounded: one 1-MiB namespace with 512-byte LBAs, one I/O
queue pair, depth 32, 8-KiB maximum transfer, direct PRPs or a two-entry PRP
list, basic Identify/SMART/queue setup/Read/Write/Flush and write FUA. Linux LR
and read-prefetch hints retain those same finite semantics. No full NVMe,
arbitrary-capacity, SGL, performance, wear-leveling or physical NAND claim is
made. Fixed lifetime UID/operation budgets also remain; reclaiming command
slots does not imply an unbounded runtime.

For cleanup, unbind the exact native PCI function while firmware is running,
terminate the worker gracefully, then unload the PCI module and IOMMU module
in that order. A stopped attached process quarantines the endpoint; reload its
module before attaching a new worker. Preserve the media file for recovery.
The later M5 owner-switch journey is not established by J1 native I/O.

## J2 owner-control binding

The native worker can expose a root-private control socket with `--owner-dir`.
The directory must already exist with private ownership; no existing socket
is overwritten. The socket is a coordinator lease, not an ordinary status
connection: disconnecting the active coordinator initiates revoke. Kernel
effect gates and retained transition/certificate/grant records are authoritative;
the userspace binding implements the existing `owner_control_v0` contract using
actual lifecycle/Block/NFC drain results. NO_OWNER does not rebuild a runtime.
Grant creates its exact successor only after old-epoch zero certification.

The same native integration client has `owner-host`, `owner-qemu`,
`owner-prekill` and `owner-postkill` journeys. The QEMU client requires an explicit
kernel, generated RAM-only initramfs and private working directory. It starts
QEMU with KVM, upstream vfio-pci/IOMMUFD and paused CPUs, verifies QMP state,
grants the VFIO owner, then starts execution. The guest first reads Host A,
writes and flushes B, and the restored native Host reads B. A child QEMU's
parent-death signal prevents a killed coordinator from leaving that guest
running. All paths terminate at explicit NO_OWNER after their checks.

The initial binding supports DRAIN_ONLY; durable-frontier revoke policy is
rejected before its LP. Use explicit native FUA/Flush before transfer. Stable
identity is function nonce, media UUID/format and an externally recorded build
manifest; volatile caches and controller epochs can be reconstructed. The
separate J3 stale-alias and architecture-freeze requirements still apply.
