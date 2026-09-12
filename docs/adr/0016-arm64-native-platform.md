<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0016: ARM64 native platform binding and atomic DMA callbacks

- Status: Implemented candidate; native 64-MiB bring-up passed, larger and owner journeys pending
- Date: 2026-09-11
- Refines: [ADR-0014](0014-native-profile-and-serial-mq2.md), [ADR-0015](0015-capacity-presets-and-mapped-budgets.md)

## Same data path, different Linux platform setup

The native ARM64 binding uses the same synthetic PCI function, software IOMMU,
HIF, MQ2 worker, protocol/lifecycle, FTL, PAGE2 NFC and physical-v2 media code as
x86. It is not a new NVMe executor, a direct-LBA image device or an ARM hardware
controller. This candidate targets Ubuntu 7.0.0-30 with 4-KiB pages, EFI and
ACPI. Other ARM kernels, page sizes, bare metal and RTOSes are not qualified.

PCI callbacks obtain context from host-bridge private data. Architecture hooks
still receive `pci_sysdata` on x86 and `pci_config_window` on ARM64. The ARM
window has a NULL parent, the upstream ACPI hook's non-ACPI-root case; a plain
synthetic device must not be cast to an ACPI device. The allocated emulated
domain belongs to the bridge before subsequent fallible initialization.
Software MSI-X domains and IRQ work remain shared. Native ARM bring-up delivered
all three vectors and used both I/O queues; queues still share one global I/O
frame, not two concurrent FTL executions.

## BAR is boot-owned memory, not mapped ordinary RAM

The ARM candidate requires one newly dedicated, exactly 16-KiB EFI Unusable
range with WB attributes, no Runtime/read/write protection, and no cached linear
mapping. The kernel also verifies the exact nonbusy `reserved` resource, rejects
System RAM, and only then uses `ioremap`. This is deliberately narrower than
general ARM platform support.

The demonstrated setup uses stock GRUB 2.14 `cutmem FROM TO` (exclusive end).
Its EFI implementation changes the firmware map, which Linux converts to NOMAP.
`reserve_mem=` alone does not remove the cached linear map. Existing firmware
reservations are never borrowed. Inspect the current bootloader map before the
reservation and the exact descriptor afterward: command status alone does not
prove success. Preserve the original default boot path and use a separate
one-shot entry. A normal reboot removes this volatile reservation; module unload
does not release it. Boot-map evidence establishes ownership, not just the
kernel's shape/type checks.

## CPU-copy coherency and atomic callback contract

This software IOMMU copies through cached CPU mappings. Its matched ARM endpoint
sets `dev->dma_coherent` before Linux allocates/maps NVMe queues and payloads.
Advertising IOMMU cache coherency does not configure the DMA device. This
assertion applies to the CPU-copy endpoint, not physical requester DMA or NAND.

The first actual ARM run exposed a pre-existing callback-context defect:
`iova_to_phys` took a sleeping rwsem from completion softirq; `map_pages` could
wait in the submission RCU critical section. Contention caused atomic-scheduling
warnings and kernel panic. Compilation and prior uncontended execution did not
validate those contexts.

All six domain-authority users now take one IRQ-safe spinlock. Mapping/ID pairs
remain atomic; identity validation and the full bounded copy remain protected
against unmap. Lock order stays endpoint mutex, domain authority, PCI effect
guard. Mapping callbacks do not take the endpoint mutex. XArray allocation
clears direct-reclaim permission while authority is held, retaining rollback on
failure. No sleeping allocation or user-memory copy occurs inside the lock.
This correction applies to both architectures.

## Evidence and limits

Corrected ARM passed native 64-MiB Identify, six data shapes, reset, rebind and
cold worker/module recovery without reformatting retained NAND. One-MiB
application transfers were eight 128-KiB wire commands. All three MSI-X vectors
delivered interrupts. The first panic transcript is retained; its unflushed
disk logs are not successful evidence. Only that run's new volatile test image
was lost while recovering the panicked disposable VM.

ARM and x86 Linux-7 MQ2/PUMP module builds passed. The existing large ioctl frame
warning remains disclosed. A Linux-6.8 build failed at the pre-existing
`linux/unaligned.h` include and adds no 6.8 support claim.

Native 64-GiB probe and shaped read/write passed. The observed reset/shutdown
deadline failures were corrected and confirmed as described in
[ADR-0017](0017-large-controller-readiness.md). Full-volume qualification remains
open; shaped I/O and clean reset do not prove a complete 64-GiB fill/readback.

L1 does not prove ARM L2/KVM or owner switching. The current ARM guest lacks
`/dev/kvm`; the existing QEMU journey launcher is x86-specific. Do not substitute
TCG silently or infer nested KVM from the outer VM's KVM use. Full native 64-GiB
integrity, sustained performance, owner switching and physical power loss remain
separate evidence scopes.
