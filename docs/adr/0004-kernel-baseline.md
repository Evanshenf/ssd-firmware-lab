<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0004: Kernel baseline and escalation policy

- Status: Accepted
- Date: 2026-08-28

## Context

The lab needs Linux PCI host-bridge, VFIO cdev, iommufd and endpoint-framework mechanisms, but Linux does not promise a stable in-kernel module API. Selecting an older long-term kernel does not make private/internal interfaces stable. Selecting a second custom kernel before an exported-API experiment fails would add maintenance without evidence.

Ubuntu 26.04 ships a 7.0 GA kernel. The reference GA configuration enables VFIO, VFIO device cdev, iommufd and the PCI endpoint framework. Its exported symbols cover the first Host-bridge and emulated-VFIO proof-of-concept path. Between upstream v6.18 and v7.0, the public iommufd kernel header used by this design is unchanged, while v7.0 adds useful VFIO registration/region hooks and endpoint dynamic/subrange BAR support.

## Decision

1. The primary M0/M4/M5 development baseline is the distribution GA 7.0 kernel, identified by the complete evidence tuple: distribution package/ABI, source revision, config digest, compiler and module build identity.
2. Begin with GPL out-of-tree modules using exported APIs only. Do not depend on `vfio_pci_core` internals or assume a distribution module ABI applies to another kernel build.
3. Do not install or maintain a second custom kernel until a small PoC records the exact missing export or required core change.
4. Upstream 6.18.y remains a secondary LTS compatibility/regression lane, not the primary runtime and not an automatic patch target.
5. If a core patch becomes necessary, develop it against the maintained upstream tree current at that decision, then decide whether an explicit 6.18 backport is worth maintaining.
6. Real-endpoint work selects a maintained kernel when endpoint hardware is chosen; it is not frozen to 6.18 years in advance.

## Evidence levels

Nested-KVM results remain `Profile-Nested`. They can validate buildability, lifecycle and functional behavior, but cannot graduate Host BAR/PAT, DMA, IRQ or physical power-failure claims. Graduation repeats the same test contract on bare metal and records its exact kernel evidence tuple.

## Upgrade discipline

Security updates to the GA kernel are allowed. A result remains reproducible only under its recorded ABI; after an ABI update, rebuild modules and rerun the affected gates. CI later adds the 6.18 LTS lane rather than replacing the primary GA lane.

## Consequences

This avoids an unnecessary second boot stack, exercises the kernel actually shipped with the development OS and reaches the newer VFIO/endpoint hooks. It also makes kernel coupling explicit: any reliance on non-exported internals must arrive as a reviewed kernel patch, not as an accidental out-of-tree dependency.
