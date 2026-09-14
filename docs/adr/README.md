<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture decision records

- [ADR-0001: System architecture](0001-system-architecture.md)
- [ADR-0002: Power domains and persistence](0002-power-domains-and-persistence.md)
- [ADR-0003: Firmware/hardware contract](0003-firmware-hardware-contract.md)
- [ADR-0004: Kernel baseline and escalation policy](0004-kernel-baseline.md)
- [ADR-0005: Synchronous IOAS-copy contract gate](0005-synchronous-ioas-copy-gate.md)
- [ADR-0006: Portable headless command-lifecycle contract](0006-portable-command-lifecycle-contract.md)
- [ADR-0007: Command durability and executable persistence policy](0007-command-durability-and-persistence-policy.md)
- [ADR-0008: Generalized NVMe command-graph boundary](0008-generalized-nvme-command-graph-boundary.md)
- [ADR-0009: Upstream VFIO route and M2/M4/M5 milestones](0009-upstream-vfio-route-and-milestones.md)
- [ADR-0010: Linux HIF to portable executor contract](0010-linux-hif-portable-executor-contract.md)
- [ADR-0011: C4 fixed-profile policy and command graph v1](0011-c4-command-graph-v1.md)
- [ADR-0012: Versioned physical NAND media and explicit substrate profiles](0012-versioned-physical-nand-media.md)
- [ADR-0013: Ready-volume scalable FTL, retained parents and PAGE2 windows](0013-scalable-ftl-and-page-windows.md)
- [ADR-0014: Construction-selected native profiles and serial-credit MQ2](0014-native-profile-and-serial-mq2.md)
- [ADR-0015: Shared capacity construction and explicit mapped-media budgets](0015-capacity-presets-and-mapped-budgets.md)
- [ADR-0016: ARM64 native platform binding and atomic DMA callbacks](0016-arm64-native-platform.md)
- [ADR-0017: Drained shutdown and successor controller readiness](0017-large-controller-readiness.md)
- [ADR-0018: Resource-scheduled NAND and the first parallel-read slice](0018-resource-scheduled-nand-read-lab.md)
- [ADR-0019: Timed NAND mutations before multi-head FTL changes](0019-timed-nand-mutations.md)
- [ADR-0020: Independent NAND channel domains before parallel FTL writes](0020-cooperative-nand-channel-domains.md)
- [ADR-0021: Format3 physical head domains and ordered write waves](0021-multihead-ftl-write-waves.md)
- [ADR-0022: Channel-owned actors with a separate Linux execution transport](0022-channel-worker-execution.md)

Accepted ADRs define the design baseline, not an assertion that the component is already implemented or validated. A superseding decision must link the prior ADR and describe migration and compatibility impact.

ADR-0012--0014 record implemented decisions at source
`a6ee009bbca5932857d51c3a5f265e0b60183a76`; they are not release approval.
Historical status and fixed-profile limits in older records describe their
original gates. Use the explicit refinement/supersession links for newer
construction, without broadening old test evidence or silently converting media.
