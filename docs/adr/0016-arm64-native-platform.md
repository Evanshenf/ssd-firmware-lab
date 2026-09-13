<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0016: ARM64 native platform binding and atomic DMA callbacks

- Status: Implemented; named native L1 64-GiB data/control/filesystem matrix complete, ARM M5 unqualified
- Date: 2026-09-11
- Updated: 2026-09-13
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

## MSI allocation owns the IRQ lookup tuple

The Linux-7 parent-domain path must not call `msi_get_virq()` while holding
`config_lock` or from IRQ work. That lookup acquires the MSI descriptor mutex.
During the subsequent native cut campaign, a worker waited for this mutex from
`fwlab_m4_irq_valid_locked` under `config_lock`; concurrently Linux MSI shutdown
held the descriptor mutex and entered the PCI configuration callback, waiting
for `config_lock`. Both sides were recovered from the stalled guest's real
stacks. This was a native transport deadlock, not an FTL or NAND-format defect.

The parent-domain allocation/free callbacks already own the per-vector
`allocated`, `virq` and `generation` record. IRQ preparation and validation use
that record under `config_lock`, together with the existing owner, effects,
BAR, permission and enable checks. Allocation advances the generation, performs
hwirq/chip/handler setup outside `config_lock`, and only then publishes the
usable tuple. Linux NVMe must finish MSI allocation and `request_irq` before
queue/vector use; parent callback completion alone is not a generic MSI-client
readiness guarantee. Free invalidates the tuple and tickets under the lock,
releases the lock, synchronizes pending IRQ work, then frees IRQ data.

This bounded correction changes only the shared Linux-7 PCI/MSI implementation.
The older compatibility branch is unchanged and receives no new fix or runtime
qualification claim. The correction subsequently passed all four fired native
cuts, reset/rebind readback and normal teardown at both 64 MiB and 64 GiB,
without new kernel warnings or lockups. It also passed the later controls with
pending I/O. This closes the observed incident within that scope, not arbitrary
MSI clients or the complete release. See the
[native result boundaries](../results/2026-09-13-native-arm64.md).

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
[ADR-0017](0017-large-controller-readiness.md). The pre-MSI-correction candidate
subsequently passed a full 64-GiB fill/readback, 32-GiB striped overwrite and
cold recovery with both complementary 32-GiB readbacks (224 GiB of native I/O).
Seven finite random/mixed jobs and three separate ordinary/FUA/Flush worker-loss
legs also passed within their recorded boundaries. Those are retained results
for that source, not evidence that the later cut campaign or corrected candidate
passed. The affected corrected control cases subsequently passed as described
above; the subsequent ext4 full-space/recovery/hash episode also passed. These
named L1 results do not establish arbitrary workloads, hardware or ARM M5.

L1 does not prove ARM L2/KVM or owner switching. The current ARM guest lacks
`/dev/kvm`; the existing QEMU journey launcher is x86-specific. Do not substitute
TCG silently or infer nested KVM from the outer VM's KVM use. Full native 64-GiB
integrity, sustained performance, owner switching and physical power loss remain
separate evidence scopes.
