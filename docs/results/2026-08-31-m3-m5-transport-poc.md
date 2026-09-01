<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# M3 side-spike and M4/M5 transport-mechanism PoC result

- Date: 2026-08-31
- Disposition: **PASS for the finite P0–P6 Profile-Nested mechanism PoC**
- Source: isolated local branch `poc/ftl-m4-spikes`
- L0: PVE `9.2.11`, kernel `7.0.14-14-pve`, VM101
- L1: Ubuntu `7.0.0-30-generic`, 8 vCPU, 16 GiB RAM
- QEMU: `10.2.1` with nested KVM and IOMMUFD
- L2: direct-booted Ubuntu `7.0.0-30-generic`, one vCPU, 256 MiB RAM,
  generated static-BusyBox initramfs
- Implementation commit: `c05a81357371a1c77f6e1dada34a3b2afb75556a`

This is an experimental transport result, not an M3/M4/M5 graduation.  The M3
FTL restart program and the M4/M5 kernel transport are separate spikes; the
portable C3/C4 firmware core is not yet linked into the kernel NVMe fixture.

P4–P6 exercised an independent kernel NVMe command/media fixture.  They did not
execute or validate the portable C3/C4 HIF, command lifecycle, FTL, NFC or media
stack.  The separately tested M3 file-backed FTL restart spike was not on the
P4–P6 I/O path; no end-to-end FTL persistence, crash consistency or
transport-to-portable integration claim follows.

## Outcome

The same synthetic PCI function, `7000:00:00.0 [fffa:0002]`, completed two
exclusive owner cycles:

```text
L1 upstream nvme
→ unbind + FLR
→ upstream vfio-pci cdev + IOMMUFD admission
→ QEMU assignment
→ L2 upstream nvme Write/Read/Flush
→ QEMU close + VFIO unbind + FLR
→ L1 upstream nvme data verification
```

No project VFIO driver or QEMU NVMe device model was used.  The synthetic
function continued to execute the NVMe/data path while QEMU transported its
BAR, DMA mappings and MSI-X eventfd route to L2.

## Gate results

| Gate | Result | Bounded observation |
|---|---|---|
| P0 | PASS | normal IOMMU group, upstream `vfio-pci` bind/unbind, both unsafe modes `N` |
| P1 | PASS | fixed 16-MiB E820-reserved BAR0, private driver doorbell/ack, two FLRs |
| P2 | PASS | paging-domain permissions, holes, remap and revoke; native DMA API path |
| P3 | PASS | isolated one-vector MSI-X plus VFIO cdev/IOMMUFD BAR, IOAS, eventfd and reset admission |
| P4 | PASS | L1 native `nvme`, 1-MiB namespace, aligned and deliberately unaligned 8-KiB I/O, bounded PRP list, Flush, reset/rebind retention, ordinary-file restart |
| P5 | PASS | two QEMU/L2 assignments; L2 native `nvme` wrote and read distinct 4-KiB patterns |
| P6 | PASS | both cycles restored L1 native ownership and read the exact Guest patterns |

The key terminal lines were:

```text
M4 native nvme-pci write/read/flush/reset/rebind: PASS
native unaligned 8-KiB PRP-list write/read/flush: PASS
M4 native nvme-pci ordinary-file restart persistence: PASS
VFIO NVMe admission cdev/IOMMUFD/BAR/IOAS/MSI-X/reset: PASS
FWLAB-L2-NVME-CYCLE-1: PASS
FWLAB-L2-NVME-CYCLE-2: PASS
M5/P6 upstream VFIO L2 two-cycle owner restoration: PASS
```

The independent M3 side spike reported:

```text
FTL file-backed write/trim/restart/recovery POC: PASS
```

It uses the existing C3.4 64-KiB ordinary-file image and validates durable
write, trim tombstone, restart and physical reconstruction.  It is not the
backend used by `m4_nvme.c` in this round.

## Fixed profile and one correction

The original plan said queue depth four and maximum 4 KiB.  The upstream Linux
driver always creates a depth-32 Admin queue, and NVMe MDTS cannot encode a
4-KiB maximum when MPSMIN is 4 KiB.  The frozen executable profile was therefore
corrected, once, to:

- one depth-32 Admin queue pair and one depth-32 I/O queue pair;
- one vector and one 1-MiB/512-byte-LBA namespace;
- MDTS=1 (8 KiB), with PRP1, direct PRP2 and only the at-most-two PRP-list
  entries needed at that ceiling.

This is the smallest clean upstream-driver profile; it is not open-ended
protocol expansion.  Both aligned direct-PRP2 and intentionally unaligned
three-page/two-list-entry 8-KiB passthrough cases passed.

## Findings that changed the implementation

- The synthetic endpoint and IOMMU provider must be separate modules to avoid
  an unload reference cycle.
- The pinned nested kernel can use the fwnode-less platform-IOMMU route only
  because it has exactly one such provider.  Bare metal may require an in-tree
  association hook.
- The reserved BAR must be inserted under the real E820 resource, not a
  temporary resource copy returned to an iomem walker.
- A pure software isolated MSI parent plus `irq_work` is required; borrowing an
  x86 vector-domain parent was not a valid software interrupt source.
- CC.EN reset must clear old SQ/CQ doorbells.  Without that, a second native
  bind consumed the prior Admin tail and hung probe.  The negative run was
  recovered through the verified L0 reset path, fixed and repeated.
- Default QEMU VFIO mmap attempted an optional P2P BAR mapping and warned that
  P2P was unsupported.  `x-no-mmap=on` keeps BAR access in the VFIO trap path,
  removes that out-of-scope attempt and preserves complete L2 functionality.

## Safety and cleanup

- `vfio.enable_unsafe_noiommu_mode=N`.
- `iommufd.allow_unsafe_interrupts=N`.
- L0 exposes the 160-GiB `sdb` medium read-only (`ro=1`, discard ignored,
  backup/replication disabled).  Every storage gate compared `/sys/block/sdb/stat`
  before and after and observed no change.
- File-backed tests used newly created exact-size regular files on L1 `/dev/sda1`
  and deleted them after each run.
- Every gate removed endpoint first and provider second; final state contained
  no project module, synthetic PCI function, VFIO owner, QEMU process or test
  media file.
- The BAR reservation is a one-shot GRUB diagnostic entry.  The default next
  boot has no `memmap=` parameter.

The exact L1 kernel and config SHA-256 values were:

```text
vmlinuz  0ad39b13e289e1a5cf806d14541ac8f221eefe849017f5915e0846917ed67785
config   b07d3cb0d53236b021d73038e315018801fa6b843529d53129ad94a2a5233bf6
```

The implementation source archive (`git archive --format=tar`) SHA-256 was
`72b3b8995f4479a0a24b956e982efdcf8cf50e335f619a36a8e6dcc9bed79571`.
The exact L1 build artifacts were:

| Artifact | SHA-256 |
|---|---|
| `ssd_fwlab_bar_aperture_probe.ko` | `a77e588e1bdac41f1c9600bf0a9e1a91290e3032cc87936838e94259c72c51a1` |
| `ssd_fwlab_bar_lab.ko` | `a40f35d415ba9e5b7354f7c4b0cc78dc58f1f4d2dafbde9df1562be1dd55a8d8` |
| `ssd_fwlab_dma_api_lab.ko` | `1e55e940b52f33b250ce99ca9d6e9f0309be19bdb550cc44fbd970f6c9136a5a` |
| `ssd_fwlab_dma_lab.ko` | `183d023a7f466c9b36a74fe07a0d56704d1db6fa921b44d4dd8ea16e1f128d58` |
| `ssd_fwlab_msix_lab.ko` | `ba0f4d04a1d985b5b4eb21a51eed541161dc33440d824f8615a6826409d87d9b` |
| `ssd_fwlab_sw_iommu_poc.ko` | `2d282d4a860dc9743c942686c14cc85edd42f7f72d9dacb619b0a430899841a3` |
| `ssd_fwlab_synth_pci_poc.ko` | `2940d312c84bc688d4a611621ce3e86bee872bcb7a53a36737452b68f90839de` |
| `vfio_handoff_probe` | `3ad0e81264dafa7474515e0d6694df7f43d55eab327fde9a6e22054dd932f35f` |
| `nvme_unaligned_probe` | `e9b2dd0cc161bbe8f01a5b03460e2e19796ee94dfd9233374340409c44afe71e` |
| L2 initramfs | `c38a3974fb407b4bf5e9486d7bef9022bc11bc35c0e38f62b32108cd6e28375f` |
| QEMU binary | `0cd4112a8f0cb891eb7c10e8df38c9dfeec8c7389bb22db6aa425f0d6fe733dc` |
| static BusyBox | `df12634c17fcdca839ae5dc47d7627b7558511f7645de7c99ccf097a0f28ed5b` |

The sorted manifest of individual final-test log SHA-256 values hashes to
`8382d6399f8303604b72fdb0cad87c60f2d920db20fc7366ed167be1e0aa106b`.
The exact QEMU command line is frozen in `smoke-vfio-l2.sh` at the implementation
commit.

## Repository-check qualification

Repository policy, SPDX, relative-link checks, the local 6.8 `W=1` build and
the authoritative L1 7.0 `W=1` build passed.  The umbrella `make check` did not
produce a portable green result for two pre-existing test-governance reasons:

- a C3.5 archive byte hash changes with the compiler (`gcc-13` and `gcc-15`
  produced different hashes from the frozen value); an untouched export of
  the pre-PoC HEAD reproduced the same failure;
- the C4.2 protected-target comparison consumes GNU make database text and
  reports differences under L1 GNU make 4.4 even though the protected Makefile
  SHA-256 is identical to the locally accepted file.

The PoC did not update either frozen value.  These are test-system portability
defects to address in the separate gate-governance work, not evidence that an
M4/M5 runtime gate passed.

## Commands

```sh
make -C experiments/ftl-restart-poc check
make -C experiments/m4-iommu-poc W=1
make -C experiments/m4-iommu-poc user

cd experiments/m4-iommu-poc
sudo ./smoke-bar-aperture.sh
sudo ./smoke-enumerate.sh
sudo ./smoke-bar-lab.sh
sudo ./smoke-dma-lab.sh
sudo ./smoke-dma-api-lab.sh
sudo ./smoke-msix-lab.sh
sudo ./smoke-vfio-bind.sh
sudo ./smoke-vfio-cdev.sh
sudo ./smoke-native-nvme.sh
sudo ./smoke-native-file.sh
sudo ./lab/build-l2-initramfs.sh ./build/fwlab-l2-initramfs.cpio.gz
sudo ./smoke-vfio-l2.sh
```

## Claims not made

This result does not prove portable-core-to-M4 integration, full NVMe
compliance, NAND timing/ECC fidelity, raw-device media, reset-under-load,
bare-metal IOMMU/IRQ isolation, electrical PCIe behavior, live migration,
power-loss durability or performance.  The L2 used a direct kernel/initramfs
test appliance rather than the planned persistent L2 system disk.  Those items
remain later, separately gated work.

It also does not claim an explicit cross-owner stale-authority proof.  P2/P3
independently exercise mapping and eventfd revoke, while P5/P6 establish two
successful clean cold cycles.  A P7 canary gate must negatively exercise old
L1/L2 IOVAs, eventfds and completion epochs before the stronger stale-free
owner-transition wording is allowed.
