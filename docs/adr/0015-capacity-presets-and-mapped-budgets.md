<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0015: Shared capacity construction and explicit mapped-media budgets

- Status: Implemented construction amendment; qualification scopes remain separate
- Date: 2026-09-10
- Source: `11cb8a80bafa02db960cab8ef31d356888166cc1`; native client `8acc238948a5ec477d544654323b1c7bea90ecf8`
- Refines: [ADR-0012](0012-versioned-physical-nand-media.md), [ADR-0013](0013-scalable-ftl-and-page-windows.md), [ADR-0014](0014-native-profile-and-serial-mq2.md)

## Decision

Use one construction helper, `scale_storage_capacity_mib`, for the existing
64/256/65536-MiB namespace presets in both headless and native scaled frontends.
Their modeled NAND main areas are80MiB/320MiB/80GiB respectively. These are
initialization inputs, not independent Host capacity authorities. The existing
FTL layout calculation validates fit; a ready recovered FTL volume supplies
Linux Identify and command range checks through `bind_ready_volume`.

The scaled native worker accepts `--namespace-mib 64|256|65536`, default64.
Format is explicit and creates a new file only. Recovery requires the matching
capacity/geometry and UUID; an expectation is never permission to resize an
image. No online namespace/filesystem growth or automatic format conversion is
implemented. The ordinary1MiB reference worker remains separate and rejects
the new option.

`fwlab_file_nand_v2_config.mapped_budget_bytes` is an optional per-open host
resource ceiling. Zero preserves the600MiB default; explicit ceilings are at
most90GiB and remain subject to platform address/offset limits. The large preset
requires an independently provisioned, bounded tmpfs with actual RAM headroom.
The field is not serialized into NAND metadata. Existing physical page/OOB,
CRC, redo, synchronization, exclusive OFD ownership, strict cold recovery and
full mapped preallocation/prefaulting remain unchanged. There is no disk/raw
fallback, direct-LBA backing or second large-capacity FTL implementation.

## Host transport and compatibility

Namespace capacity is not BAR size, queue depth or a DMA mapping size. No
PCI/HIF/Host-profile wire or lifecycle change is needed for these presets.
LBA range and Identify continue to use the same64-bit recovered volume value.
Native construction/drain iteration allowances accommodate large metadata work;
normal command-step budgets and the firmware loop do not change. Increasing
an iteration allowance does **not** extend Linux's controller-ready timeout.

The standalone native test client takes the same explicit capacity expectation
for plan/write/verify modes, with exact block-device identity and size checks.
Owner/QEMU/guest modes retain their earlier64MiB qualification and reject the
new capacity option. A larger native owner-switch claim requires its own evidence.

`check-64g` now selects the current PAGE2/window-v2/mapped-physical-v2 route.
The old C3/compact-v1 route is retained as `check-64g-reference-v1`; its frozen
results remain historical and are not relabeled as evidence for the new path.

## Qualification boundary and stop

Use existing tests: full small headless journeys, actual native worker fixture
at64/256MiB, explicit budget rejection/recovery checks, and a fresh ARM64
64GiB full-fill/half-overwrite/GC/checkpoint/restart/readback journey. Native
Linux256MiB Identify, tail I/O, reset/rebind/cold recovery and filesystem mount
are a separate real-Host episode. See the [capacity result](../results/2026-09-10-current-capacity.md).

Do not infer native64GiB reset timing, ARM PCI support, real-disk power durability
or SSD throughput from the headless run or cross compilation. Do not change
FTL algorithms, generic interfaces or test frameworks to complete this seam.
