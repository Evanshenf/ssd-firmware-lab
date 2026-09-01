<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# M4 software-IOMMU and upstream vfio-pci feasibility result

- Date: 2026-08-31
- Disposition: **PASS for the bounded enumeration/group/driver-bind PoC**
- Source: isolated local branch `poc/ftl-m4-spikes`
- Target: L1 VM101 `ssd-fwlab-dev`
- Kernel: Ubuntu `7.0.0-30-generic`
- Recovery: L0 PVE control plus serial console verified before module loading

This is an experimental checkpoint, not C4 evidence and not an M4 graduation.

## Question answered

An in-memory synthetic PCI function can be associated with a
kernel-registered software IOMMU provider, acquire a normal Linux IOMMU group,
and bind to the upstream host `vfio-pci` driver while VFIO
`enable_unsafe_noiommu_mode` remains `N`.

No project VFIO driver was used.

## Observed result

The enumeration lane reported:

```text
BDF=7000:00:00.0 IOMMU_GROUP=0
7000:00:00.0 Unassigned class [ff00]: Device [fffa:0002] (rev 01)
        IOMMU group: 0
M4 synthetic PCI + kernel IOMMU enumerate/unload: PASS
```

The upstream VFIO lane reported:

```text
BDF=7000:00:00.0 IOMMU_GROUP=0 DRIVER=vfio-pci
7000:00:00.0 Unassigned class [ff00]: Device [fffa:0002] (rev 01)
        Kernel driver in use: vfio-pci
M4 official vfio-pci bind/unbind: PASS
```

Both lanes removed the endpoint first and the provider second.  No project
module, synthetic PCI function, or synthetic group member remained.  The new
kernel log contained no warning, oops, BUG, lockdep report, or general
protection fault.

## Implementation facts

- `ssd_fwlab_sw_iommu_poc.ko` registers an IOMMU provider through the Linux
  IOMMU core and accepts only the lab identity in PCI domains `0x7000..0x7fff`.
- `ssd_fwlab_synth_pci_poc.ko` creates the root bus and vendor-class endpoint.
- The provider supplies a blocked default domain and allocatable paging
  domains with 4-KiB page-granular IOVA mappings.
- The endpoint has no BAR, DMA execution, IRQ/MSI-X or NVMe class in this PoC.
- Upstream `vfio-pci` was selected through `driver_override`; bind and unbind
  used normal PCI driver-core operations.

## Two integration findings

### PCI scan and fwspec ordering

Linux 7.0 PCI scanning performs `device_add()` before an external synthetic
host-controller module receives the resulting `pci_dev`.  The exported
`iommu_fwspec_init()` requires an already registered provider, while
`iommu_probe_device()` is not exported for a post-scan replay.  The PoC uses
the standard fwnode-less platform-IOMMU path and filters `probe_device()` to
the synthetic endpoint.  The pinned L1 profile had no competing fwnode-less
IOMMU provider.

A production fwspec design may need a small in-tree hook or an exported
post-scan probe helper.  This is a kernel-integration decision, not a reason to
use VFIO no-IOMMU mode or fabricate a group.

### Provider/module ownership

IOMMU core pins the provider module while an endpoint uses its ops.  Combining
the endpoint creator and provider in one unloadable module formed a reference
cycle: module exit could not run to remove the endpoint.  Splitting them into
provider and endpoint modules resolved the lifecycle cleanly.

## NVMeVirt comparison

NVMeVirt commit `61c90f7758cbd9545b4a4727e89377bf88eab060` was inspected as a
reference.  Its current PRP path directly converts supplied physical addresses
with `pfn_to_page`, `kmap_atomic_pfn`, or `memremap`, and its interrupt path
retriggers host IRQ state.  Replacing those foundations would be a transport
rewrite.  The FWLab H0 bridge and provider-oriented firmware/HIF are therefore
the selected base.  No NVMeVirt code was copied.

## Claims deliberately not made

This result does not prove:

- BAR allocation, MMIO side effects, doorbells or reserved-memory safety;
- software DMA consumption, permission enforcement under concurrent unmap, or
  PRP/SGL walking;
- MSI/MSI-X allocation, masking, PBA or interrupt-remapping isolation;
- opening the VFIO device, IOMMUFD map/unmap traffic or QEMU/L2 assignment;
- native Linux `nvme` binding or NVMe command/data correctness;
- reset, hot-unplug under load, Host/Guest owner switching or migration;
- file/raw-block/NAND media integration or performance.

## Next bounded stage

Keep the two-module lifetime split.  Add a reserved-memory BAR and a private
lab driver first; then an isolated MSI domain; then exercise paging-domain
map/unmap plus a permission-aware software DMA probe.  Only after those pass
should the function become NVMe class or be opened by QEMU.
