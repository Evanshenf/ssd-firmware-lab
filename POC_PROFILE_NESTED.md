<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Branch-only M4/M5 Profile-Nested transport PoC

This branch publishes one bounded, disposable mechanism experiment for source
review and reproduction. It is not the project's current portable-firmware
implementation and is not a merge candidate for `main`.

The experiment previously demonstrated this exact cold sequence on its pinned
Ubuntu `7.0.0-30-generic` nested profile:

```text
L1 native nvme
-> unbind + FLR
-> upstream vfio-pci cdev + IOMMUFD
-> QEMU assignment
-> L2 native nvme Write/Read/Flush
-> QEMU close + reset
-> L1 native nvme data verification
```

The endpoint exposes a 1-MiB, 512-byte-LBA namespace backed by disposable
memory or an exact-size regular file. The NVMe executor and media provider are
fixtures. They do not execute the portable C3/C4 firmware, scalable FTL, NFC or
persistent NAND stack. P0-P6 are finite mechanism evidence, not M3, M4 or M5
graduation.

## Safety boundary

The privileged scripts load out-of-tree kernel modules, bind and unbind native
drivers, write to a synthetic namespace, alter VFIO ownership, start nested
QEMU and, for the reservation installer, modify GRUB configuration. Run them
only inside a disposable VM with an independently verified console/reset path.
Never run them on a workstation or a machine containing valuable data.

All host-mutating smoke scripts fail before their first privileged action
unless both conditions hold:

1. the running kernel is exactly `7.0.0-30-generic`;
2. the caller supplies the literal acknowledgement below.

```sh
FWLAB_POC_ACK=I_ACCEPT_DESTRUCTIVE_PROFILE_NESTED_POC
```

This acknowledgement is not a security boundary. It prevents accidental
execution; it does not make the kernel fixture safe, complete or supported.
The scripts must keep `vfio.enable_unsafe_noiommu_mode=N` and
`iommufd.allow_unsafe_interrupts=N`. Any ambiguous device identity, missing
IOMMU group, stale module, BAR mismatch, kernel warning or data mismatch is a
hard failure.

The PoC never opens the reserved raw NAND disk. Its file-backed lane creates a
new 1-MiB regular file and its L2 lane writes only the synthetic namespace.
Even so, device numbering is not a sufficient safety proof; use the exact
pinned disposable topology described by the experiment documentation.

## What can be built without privileges

```sh
make -C experiments/ftl-restart-poc check
make -C experiments/m4-iommu-poc architecture
make -C experiments/m4-iommu-poc user
make -C experiments/m4-iommu-poc \
  KDIR=/lib/modules/6.8.0-138-generic/build W=1
```

The 6.8 lane is build-only. Runtime evidence belongs only to the exact 7.0
profile above. CI must never load these modules, access KVM or run a smoke
script as root.

## Reproduction entrypoint

Read the [finite PoC contract](docs/m3-m5-poc-contract.md) and the
[experiment README](experiments/m4-iommu-poc/README.md) completely before
using the privileged lane. The recorded result and its non-claims are in the
[P0-P6 evidence note](docs/results/2026-08-31-m3-m5-transport-poc.md).

## Known blockers

- no portable C3/C4 executor on the NVMe path;
- no M3-P block/NFC/media provider;
- no C4.4 capability or C4.5 completion integration;
- no P7 cross-owner stale-IOVA/eventfd/CQE canary matrix;
- no general holder closure or coordinator-crash recovery;
- no bare-metal IOMMU/interrupt-remapping or real requester-DMA evidence;
- no raw-media, power-loss, performance, migration or production claim.

The current formal route is documented by ADR-0009 and ADR-0010 on this branch.

## Publication provenance

- candidate base: public `main` commit
  `5368ab6b41223f72487dc519dda1af3971de82e9`;
- project-local unpublished donor:
  `74768949be9cbc76b7ae386cd7cc7606f630aa69`;
- donor tree: `c57c614b5c074fbc846f3c4532487ebd046d0849`;
- selected-path donor archive SHA-256:
  `4cc54308afdb1814435339068801b32f0f71ab51984f1c27d6141e2f086edf12`;
- no active C4.3 worktree or private lab file was imported;
- AI-assisted: yes, OpenAI Codex, 2026-09-01;
- human review remains required before reuse, PR creation or any privileged
  execution.
