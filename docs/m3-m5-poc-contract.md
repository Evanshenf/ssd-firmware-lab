<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Finite M3–M5 transport-mechanism PoC contract

This document bounds one disposable nested-lab PoC.  It demonstrates the
mechanism needed to put a replaceable media/firmware boundary behind a
Host-visible synthetic NVMe transport and then move that function to an L2
guest through upstream VFIO.  The M3 FTL restart spike is exercised separately;
the kernel NVMe fixture does not yet link the portable C3/C4 firmware core.
This is not an M3, M4 or M5 graduation and must not grow into production NAND
fidelity, full NVMe compliance, migration or performance certification.

## Revised milestone meaning

- **M2 is optional.** `vfio-user` remains a useful fast guest adapter and
  differential oracle, but it is not on the critical path.
- **M3 owns media semantics.** NFC scheduling, NAND behavior, FTL, GC/WL,
  recovery and persistent truth remain portable and transport-independent.
- **M4 owns the Host transport.** PCI config/BAR, software-IOMMU-backed DMA,
  MSI-X, reset and native Host `nvme` binding belong here.
- **M5 owns assignment and ownership switching.** It uses upstream Host
  `vfio-pci` plus QEMU; it does not implement a project VFIO driver.

M3 may continue independently.  This PoC uses the already bounded ordinary-file
media fixture and must not access the reserved raw NAND disk.

## Frozen PoC profile

- one synthetic PCI function in a 16-bit lab PCI segment;
- one kernel-registered software-IOMMU provider and one endpoint group;
- one fixed, boot-reserved BAR aperture;
- one Admin queue pair and one I/O queue pair;
- fixed queue depth 32, matching the upstream Linux `nvme-pci` Admin queue;
- one namespace with a very small fixed capacity;
- 512-byte LBA and maximum 8-KiB transfer; NVMe MDTS cannot encode a 4-KiB
  maximum at a 4-KiB minimum page size, so MDTS=1 is the smallest bounded
  value and the fixture accepts PRP1, a direct PRP2 page, or the at-most-two
  PRP-list entries needed by an unaligned 8-KiB transfer;
- Identify, create/delete I/O queues, Read, Write and Flush only as required by
  the pinned Linux driver path;
- one ordinary file created for the test and removed after the run;
- one L1 Host and one nested-KVM L2 guest;
- no raw-device write, discard, P2P, ATS/PASID, SR-IOV or migration.

## Gate P0 — software IOMMU and upstream driver bind

Status: **passed on the nested Ubuntu GA 7.0 profile**.

Require a normal IOMMU group, `enable_unsafe_noiommu_mode=N`, upstream Host
`vfio-pci` bind/unbind and endpoint-first/provider-second teardown.  This gate
does not open the VFIO device or exercise mappings.

## Gate P1 — reserved BAR and private lab driver

Status: **passed on the nested Ubuntu GA 7.0 profile**.

Boot with one exact aligned `memmap=$` E820-reserved aperture from the pinned
L1 memory profile.  Expose it as one 64-bit memory BAR on the vendor-class
function.  A private lab driver must map the BAR, write one
doorbell value and observe an emulator acknowledgement.  Verify BAR sizing,
resource ownership, mapping type, bounds, unload cleanup and no unexpected
kernel diagnostics.

Do not use the NVMe class at P1.

## Gate P2 — software DMA through the IOMMU domain

Status: **passed on the nested Ubuntu GA 7.0 profile**.

Exercise paging-domain attach, map, translate, permission failure, partial
unmap, full revoke and detach using bounded pages.  The software DMA engine may
copy only through a live mapping lease; it must never treat an IOVA as a host
physical pointer.  Unmap/reset waits for or revokes every in-flight copy.

Exit only when read, write, unmapped, wrong-direction, boundary and stale-epoch
cases are deterministic and teardown leaves zero mappings.

## Gate P3 — isolated MSI-X and upstream-VFIO handoff admission

Status: **passed on the nested Ubuntu GA 7.0 profile**.

Add a synthetic MSI/IRQ domain, one vector first, MSI-X table/PBA semantics,
mask/unmask and a controlled interrupt source.  Prove that the private lab
driver receives exactly the requested interrupts and that mask, reset and
unload suppress stale delivery.  Do not claim interrupt isolation merely by
setting an IOMMU capability bit.

Before native NVMe, bind upstream `vfio-pci`, open the VFIO cdev, perform
`VFIO_DEVICE_BIND_IOMMUFD` plus physical IOAS attach, map/unmap bounded pages,
mmap and round-trip BAR0, deliver the vector through eventfd, then detach and
close.  Require both `enable_unsafe_noiommu_mode=N` and
`allow_unsafe_interrupts=N`.

## Gate P4 — minimal native Host NVMe

Status: **passed for the bounded transport fixture on Profile-Nested**.

Change to NVMe class only after P1–P3 pass.  Bind the upstream Host `nvme`
driver, complete the fixed Admin bring-up, create one I/O queue pair and verify
bounded Write/Read/Flush data against the disposable ordinary-file backend.
Reset and driver unbind must leave no DMA, queue, completion or interrupt from
the old controller epoch.

## Gate P5 — upstream VFIO L2 assignment

Status: **passed for two functional cold owner cycles on Profile-Nested**.

From a quiescent Host controller:

1. unbind upstream `nvme`;
2. perform the project reset/revoke fence;
3. bind upstream Host `vfio-pci`;
4. repeat the already-passed P3 VFIO/IOMMUFD admission preflight;
5. assign the same function to an L2 QEMU guest;
6. bind the L2 native `nvme` driver and repeat bounded Write/Read/Flush.

QEMU must not implement the NVMe command, FTL or NAND data path.

## Gate P6 — Guest-to-Host restoration

Status: **passed for two functional cold owner cycles on Profile-Nested**.

Stop L2, close VFIO/IOMMUFD, verify all mappings and interrupt routes are gone,
unbind `vfio-pci`, reset the controller, and rebind the L1 native `nvme`
driver.  Repeat a small fixed number of complete owner cycles and verify media
data plus epoch separation.

This round verified successful teardown/rebind and new-owner data, not explicit
negative use of old IOVAs, eventfds or completion epochs.  Those stale-authority
canaries remain the separately named P7 gate.

## Hard stop conditions

Stop the current mechanism on any of the following:

- BAR overlaps System RAM or another resource, or mapping types alias;
- a missing IOMMU group, no-IOMMU fallback or unmediated address access;
- synthetic MSI without demonstrable isolation/masking;
- module unload requiring a reboot or leaving a device/group/resource;
- stale DMA, CQE, IRQ, mapping, pin, queue identity or media owner after reset;
- any write to the reserved raw NAND disk;
- a kernel warning, oops, BUG, lockdep report or unexplained hang;
- pressure to broaden the fixed profile merely to make the native driver pass.

## PoC completion

The transport-mechanism round completed P0–P6 on the pinned nested profile.
Completion authorizes a design review; it does not prove that C3/C4 portable
firmware is wired into M4, graduate M3/M4/M5, or authorize bare-metal,
raw-media or performance claims.

P4–P6 exercised an independent kernel NVMe command/media fixture.  They did not
execute or validate the portable C3/C4 HIF, command lifecycle, FTL, NFC or media
stack.  The separately tested M3 file-backed FTL restart spike was not on the
P4–P6 I/O path; no end-to-end FTL persistence, crash consistency or
transport-to-portable integration claim follows.
