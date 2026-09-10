<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Native firmware PCI/HIF (experimental J1)

This builds a Host-visible software PCI function and software IOMMU for a
disposable x86-64 Linux lab VM. It is not a physical endpoint or a vfio-user
device. It requires an explicitly reserved, exactly 16-KiB memory aperture;
the module refuses ordinary/unreserved RAM. Do not load it on a production host.
The current integration target is Ubuntu `7.0.0-30-generic`; other kernels are
not a portability claim.

This page retains the tagged reference and development construction contracts.
For the adopted 64-MiB/Large/MQ2 path, see the
[current matrix](../../docs/current-status.md),
[operator sequence](../../docs/native-scaled-usage.md) and
[separately scoped native results](../../docs/results/2026-09-10-scaled-storage-mq2.md).

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

The development source additionally accepts the separate, explicitly versioned
`FWLAB_M4_ATTACH_IDENTITY` ioctl. Ordinary native EXCHANGE v1 remains unchanged;
legacy ATTACH means media format 1. Explicit attachment pins one UUID/format/
binding-digest tuple (declared formats 1 and 2 only). Identical same-descriptor
retry is idempotent after failed copyout; a changed tuple rejects, including
legacy ATTACH after format 2. Attachment never grants readiness or opens effects.
Owner observation returns the pinned identity across reset and owner epochs.
Closing an attached descriptor still quarantines the function. The separately
selected scaled worker does not expand the old qualification described below;
its subsequent real probe/reset/M5 evidence has its own development record.

### Construction-selected singleton pump (development candidate)

`make -C kernel/m4-native FWLAB_M4_PRODUCER=2 W=1` selects the firmware-driven
producer. Default `1` retains the BAR-thread reference. Mode 2 does not compile
or create the BAR thread; it is not a runtime module parameter or owner option.
Both use the same HIF capture/control and CQE implementation and unchanged
mapping/IRQ gates. Deferred IRQ work still exists and must be counted.

Use the separately named `scaled-pump-worker` userspace target. Its 128-byte
`FWLAB_M4_ATTACH_MODE` v2 command negotiates the producer before identity pinning;
the old 112-byte attachment and legacy EXCHANGE attachment remain BAR-only.
Wrong-mode/old-kernel combinations reject startup, without a fallback. Media
format/UUID/binding identity and portable owner ABI are unchanged.

The fixed 48-byte `FWLAB_M4_PUMP` advances control and at most one SQ capture,
visiting at most the existing Admin and I/O queues. `result` is admission;
`service_result` is the accepted HIF step's outcome. A service fault continues
STATUS/owner/drain/reset, without new business admission that turn. Repeating
after lost copyout is another tick, not idempotent replay. `captured=0` never
acknowledges work or establishes an empty delivery queue; NEXT remains separate.

For its bounded lab check, existing root-only `native_cut=4` returns one service
error after capturing the client's 512-byte Q1/NSID1/LBA128 Read, before delivery.
The ordinary service fault path closes effects and requests reset. `cut4` reuses
the existing exclusive native client and reset/readback journey. It is disabled
by default and does not broaden ordinary supported commands. This candidate
has subsequent native/owner/PBA results in the development evidence index;
offline checks are not kernel fault/locking evidence or a new performance claim.

### Large serialized Host profile

The development `FWLAB_M4_HOST_PROFILE=2` construction requires producer 2
(PUMP); default profile 1 retains SMALL behavior. Its explicit attachment-v3
uses a distinct 160-byte ioctl command and exact profile/limit matching before
media identity pinning. This does not change media format or old 112/128-byte
attachment layouts. Large construction is restricted to the tested 4 KiB kernel
page environment and does not claim ARM kernel portability.

Profile 2 has one I/O and one Admin ingress reservation from SQ capture through
transport RETIRE, including payload-free commands. Separate snapshot/graph
storage backs 1 MiB I/O and 4 KiB Admin payloads. Capture leaves a blocked queue
untouched and continues checking the other queue. Drained RESET_ACK/CERTIFY
cleans retained cancelled requests and their frames before reporting zero.

Large SHAPE walks at most 257 data references and two PRP-list pages, with an
8-byte-aligned initial list pointer and page-aligned continuation. Graph/scratch
storage is preallocated. Authority is published only after complete validation;
repeated accepted SHAPE returns the original IDs. Existing small per-segment
mapping/copy guards remain unchanged; there is no 1 MiB spinlocked copy. Invalid
or stale later mappings can still yield declared partial DMA, not atomic DMA.
The matching worker retains AER as immediate Unsupported, not a waiting command.

Profile 3 (`FWLAB_M4_HOST_PROFILE=3`, PUMP only, Linux 7 interface) is the MQ2
serialized candidate. It has paired Q1/CQ1/vector1 and Q2/CQ2/vector2 plus Admin
vector0, with the same global one-I/O/one-Admin frame contract. IRQ allocation
uses the actual MSI descriptor index; each vector has its own pending ticket,
generation and deferred IRQ work item, not an extra firmware data thread.

Captured origins hold SQ/CQ incarnations until retirement. Accepted deletion
returns private `-EINPROGRESS` while draining, not a terminal busy result.
Reset/revoke retains queue objects until holders and accepted queue operations
are drained, then clears them before ACK/zero certification. Completing an
accepted deletion under the closed effect gate is metadata/route cleanup only.
The profile-3 NoQ operation carries positive requested SQ/CQ counts in the
private exchange's `queue_entries` / `associated_queue` fields and returns its
stable `result_dword0`; other queue operations retain their named field meanings.

This is a development construction, not native MQ2 qualification by compilation.
Old profiles 1/2 remain one-vector references; no SGL, multi-namespace, concurrent
FTL, physical NAND or whole-SSD throughput claim is added.

Use the explicitly named `large-worker` and `native-io-large` targets. Existing
lab isolation, fresh-only format, recovery, ownership and cleanup rules still
apply. Native data/fault/reset/owner evidence is recorded separately; adjacent
parser or frame-owner checks alone do not qualify this kernel construction.

The independent `j1_native_io` client has `write`, `verify`, `cut1`, `cut2` and
`cut3` modes. It checks an exclusive, unmounted 1-MiB namespace, namespace ID,
vendor/model identity and the exact synthetic sysfs BDF before any write.
Its one-shot cuts are root-only and disabled by default. They interrupt one
specific test origin at DMA-in, DMA-out or pre-CQE, followed by a real Linux
controller reset and data/canary comparison. They do not constitute the later
cross-owner stale-IOVA/eventfd/lease canary gate.

The tagged reference profile is bounded: one 1-MiB namespace with 512-byte LBAs, one I/O
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

## J3 stale-authority journey

`j1_native_io owner-stale OWNER_DIRECTORY BDF` requires this exact synthetic
function, no bound native driver, and the worker's private owner-control socket.
It uses the normal upstream VFIO cdev/IOMMUFD interfaces for two sequential
owner epochs, each with its own IOAS and backing allocation. The function and
NAND identity are retained; the old IOAS/device ownership is released before
the second owner is granted. Retained old user memory/eventfd snapshots are
observations, not live DMA pins or interrupt routes.

The four cases cover old DMA authority at reused IOVA, an old mapping reference,
old firmware/kernel completion identities at a reused CQ slot, and an old IRQ
ticket against the replacement eventfd route. They require stale rejection,
unchanged buffers/CQ/PBA, no eventfd delivery, then successful current Identify
data/CQE and delivery only to the current route. The final revoke must return a
zero-reference certificate. This is software owner-epoch evidence, not a
physical DMA-master or arbitrary-configuration claim.

The kernel IRQ publisher uses a private ticket bound to owner, bus, effect,
BAR and route generations. Only the exact live ticket may alter pending/PBA
state. These private controls do not widen the portable firmware ABI.
