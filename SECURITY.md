<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Security policy

This pre-1.0 project is supported only on the `main` branch. There is no vulnerability bounty or fixed response SLA.

Report vulnerabilities through GitHub Private Vulnerability Reporting when enabled. Do not publish an exploitable DMA, kernel memory-safety or destructive-media issue before coordination.

## High-risk operations

Future kernel, VFIO and DMA components can crash or corrupt the Host if their safety boundary is wrong. Raw media operations can permanently destroy data.

- Use an ordinary image file by default.
- A raw block device must be dedicated, unmounted, free of partitions/filesystems/holders and excluded from snapshots, backups, replication and live migration.
- Initialization must be a separate explicit command requiring `--allow-raw-device`, expected serial, exact size, whole-device identity and a one-time confirmation token.
- The runtime daemon must never auto-format, resize or enable discard on raw media.
- Any ambiguity must fail closed. Never test against a Host system disk or a Guest root disk.

CI and untrusted contributions must not run privileged hardware paths.
