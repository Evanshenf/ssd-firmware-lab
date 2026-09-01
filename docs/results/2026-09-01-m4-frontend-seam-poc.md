<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# M4 transport/executor/media frontend-seam PoC

- Date: 2026-09-01
- Disposition: **PASS for source separation and unchanged Profile-Nested P0–P6 behavior**
- Branch: `poc/ftl-m4-spikes`
- Base evidence: `c05a81357371a1c77f6e1dada34a3b2afb75556a`
- Implementation commit: `b56bf1a350aa7cc51b1b417af7029d680de54d90`
- Source archive SHA-256: `27e28de4b0c780589312235321c709bffc1f1c82965b083ada88b44cca3a2fbc`

This optimization does not import the active C4.3 worktree and does not claim a
portable B*-ABI implementation.  It removes avoidable coupling from the
already-passed synthetic transport mechanism so a later frozen executor bridge
can replace the protocol fixture without rewriting PCI/IOMMU/MSI-X.

## Separation achieved

```text
m4_pci.c
  owns PCI context, BAR mapping, DMA service and vector notification
        │
        ▼
m4_frontend_services / m4_frontend_ops
        │
        ▼
m4_nvme.c
  owns only the bounded NVMe fixture and queue state
        │
        ▼
m4_media_ops
        │
        ▼
m4_media_fixture.c
  owns disposable memory/file substrate
```

The static architecture gate proves:

- transport has no file/path/VFS/media dependency;
- the NVMe fixture has no PCI context, direct BAR mapping, direct MMIO primitive
  or direct software-IOMMU function call;
- the media fixture has no NVMe, PCI, MSI-X or DMA dependency;
- frontend services/ops are versioned and expose explicit reset, poll, stop and
  quiescence operations.

## Lifecycle finding

The first nested run correctly produced a warning on module unload: after the
native driver stopped, the fixture could retain an internal queue ledger if no
final worker poll happened.  Relying on a final poll was an implicit lifetime
assumption.  The seam now has a synchronous `stop(controller_epoch)` callback;
transport stops the worker, explicitly closes fixture state, verifies
quiescence and only then destroys the binding.

After that correction, a clean diagnostic boot passed:

```text
M4 frontend architecture: PASS (transport/executor/media split)
M4 fixed BAR aperture claim/map round trip: PASS
M4 synthetic PCI + kernel IOMMU enumerate/unload: PASS
M4-A synthetic BAR/lab-driver/reset: PASS
M4-B software-IOMMU/DMA admission: PASS
M4-C isolated MSI-X lab stage 4: PASS
M4-D native DMA API/default domain: PASS
M4-C secure upstream VFIO cdev handoff: PASS
native unaligned 8-KiB PRP-list write/read/flush: PASS
M4 native nvme-pci write/read/flush/reset/rebind: PASS
M4 native nvme-pci ordinary-file restart persistence: PASS
FWLAB-L2-NVME-CYCLE-1: PASS
FWLAB-L2-NVME-CYCLE-2: PASS
M5/P6 upstream VFIO L2 two-cycle owner restoration: PASS
```

The final state had no project module, synthetic BDF, QEMU process or severe
kernel diagnostic.  The reserved `/dev/sdb` medium remained untouched.

The exact rebuilt synthetic endpoint module SHA-256 was
`a47ac6ca0e6a5afada9c7b4b86264a8fe3340d0e04cb3d8c8a0528e831b532b4`.
The sorted manifest of individual gate-log SHA-256 values hashes to
`e552a1e7f5fed54f6f8f50eedf2058afc4a5dc88a6e4700c8ec411a74f326b24`.

## Boundary

This result proves dependency direction and fixture replaceability only.  The
minimal executor still implements protocol and queue behavior in the kernel,
the media provider is not M3-P, and the composite module is not yet a dynamic
provider system.  C4.3–C4.5 must freeze the real typed executor contract before
the fixture is replaced.
