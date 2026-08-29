<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C2.5 two-instance and architecture-isolation graduation — Profile-Nested

- Date: 2026-08-29
- Result: PASS
- Evidence level: `Profile-Nested`
- Final implementation commit: `e3e518e15c5eb600d6e1f757deb214096d907bbb`
- Scope: frozen V1 per-instance state, two real cdevs/IOAS objects, peer removal and current source/build boundaries

C2.5 completed the fifth and final Cycle 02 sub-gate. It did not modify the
frozen C2.1 contract or C2.2 V1 driver/provider. A separate test-only fixture
registered one additional platform device with the frozen V1 driver's name,
causing a second normal probe and an independent `struct fwlab_v1` allocation.

## Immutable source and artifact identities

```text
implementation root tree:
99ce4d95f3de91df62482809e2863eda9194a5fd

peer fixture tree:
4bfd9126742ccf694b318fb2fc8a87e2fdbe5e93

C2.5 userspace tool tree:
768b41baaecad21853c8341d5177e66730e63f74

frozen V1 module SHA-256:
8c9f1418dcdce7e0ebbd18d97adcc9e11a7a9f7e2bfe5b565f18f8db8714b072
srcversion: FFE4E0F87FA9FA275C67192
vfio_dma_rw CRC: 0xaa22e02a

peer fixture module SHA-256:
514cb0cdb31c855b581927f4fa3fb16e2bf777c731853866e4892ce839d0c7df
srcversion: 2AEB33A9E7688A0E2131FF0

C2.5 userspace oracle SHA-256:
b21da6de6dea77488377c6389bdd19a2efd29cdb321b620adef56d27c8a26ae5

C2.5 privileged wrapper SHA-256:
612892b74ede04906a1fea7d7ba90871b44546de483bdcee8acb8a4834edf310
```

The exact commit was deployed into a root-only source directory. All 113
tracked files matched the commit byte for byte. The executable artifacts were
copied into a separate root-only directory before privileged use. On the exact
GA 7.0 headers, the frozen V1, peer fixture and H0 independently passed `W=1`
build and modpost. The peer fixture had no module dependency and only Linux
platform/device undefined symbols; strict checkpatch reported no finding.

## Review-driven wrapper hardening

Independent pre-load review stopped two earlier candidates before any module
was loaded:

1. C2.4 and C2.5 originally used different gate locks, and an attempted load
   could be mistaken for ownership during cleanup.
2. The parallel base/peer seeds originally had equal low bytes, producing
   identical payloads despite different integer constants.
3. A signal could be delivered after foreground `insmod` returned but before
   the shell assigned module ownership.

The final wrapper reuses the frozen C2.4 V1 lock, obtains ownership only from a
successful `insmod`, compares open cdevs by `dev_t`, propagates ordered teardown
errors and records signals as pending across the load critical section. Both
`dash` and `bash` synthetic signal tests demonstrated that ownership is now
committed before deferred cleanup. The repository checker includes regression
tripwires for the shared lock and this load ordering.

## Real two-instance matrix

The wrapper resolved exactly two platform devices to two different cdevs. One
`/dev/iommu` owner allocated distinct IOAS IDs `2` and `4`. Both IOAS objects
mapped the same numeric IOVA `0x100000000` to different anonymous Host pages.

The oracle required exact byte-for-byte stability of the other instance's
64-byte state, 64-byte result, 4 KiB internal data and 4 KiB Host page around
each isolated operation. It observed:

- different full-page and internal-data patterns at the same numeric IOVA;
- successful IOAS-to-buffer and buffer-to-IOAS copies in both instances;
- independent accepted sequence and generation state;
- base reset, exact unmap/remap and detach/reattach with peer unchanged;
- peer reset, exact unmap/remap and detach/reattach with base unchanged;
- 24 bounded parallel copy rounds per cdev with different payload families;
- attached peer close and peer IOAS destruction with base unchanged;
- peer fixture unload after all peer fds closed, which invoked the frozen V1
  peer remove path while the base cdev stayed open and attached;
- a new successful base copy after the peer platform device and cdev had
  disappeared;
- ordered base detach, unmap, IOAS destroy and close before final V1 unload.

The parallel smoke starts two userspace workers together. It does not prove a
particular kernel overlap or enumerate every scheduler interleaving. C2.4 and
the frozen fake-provider barriers remain the lifecycle-order evidence.

## Architecture-boundary evidence

- The frozen H0 and V1 trees have no include, symbol or build dependency on
  each other.
- The peer fixture includes no project implementation header and contains no
  V1 data, generation, sequence, provider or IOAS state.
- A-prime remains at its unstable kernel-test path and did not enter `core`,
  `nfc` or `media`.
- The portable implementation source count is currently zero. The passing
  checker therefore establishes the current repository boundary, not a future
  portable stack's runtime or link isolation.

After C2.5 cleanup, the machine was normally rebooted to a new clean boot with
taint zero. Only then was the byte-identical frozen H0 module independently
loaded, enumerated as vendor class `fffa:0001` with no BAR, bus mastering or
interrupt, and removed. V1 and the peer fixture were absent for the entire H0
run. A second normal reboot again produced taint zero, a healthy system and no
project module, platform object, VFIO cdev or H0 PCI function.

```text
C2.5 run-log SHA-256:
dbed14bfd0284437c6dd7220a47d1ff32c2ecbff90c8047614c8ad2c41a53d95

independent clean-boot H0 run-log SHA-256:
0eda7493c0d15e49cd04bcf8db240bb5f76a151812e51f4ca827c3d7f032757f
```

The reserved raw-media device remained read-only, unmounted, outside swap and
without holders. Its write-I/O and write-sector counters remained `0:0` through
the C2.5 run, both clean boots and the independent H0 regression.

## Proof boundary and next action

This PASS shows bounded two-instance isolation for the tested synchronous,
CPU-mediated IOAS-copy mechanism and preserves the current H0/V1/source
boundaries. It is not device DMA, an arbitrary-N proof, Isolated-B* firmware
runtime containment, a production hotplug mechanism or the final Host/custom
VFIO/QEMU adapter.

Pinning/zero-copy, IRQ, mmap/BAR/PAT, PCI composition, native NVMe binding,
QEMU assignment, firmware, NFC and media I/O remain stopped independent gates.
Cycle 02 is now 5/5 and must return to the scheduled file-based architecture
review before any next mechanism is selected.
