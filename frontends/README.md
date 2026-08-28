<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Transport and harness adapters

Future BSD-3-Clause user-space adapters include headless and `vfio-user`. Host synthetic PCI, custom emulated VFIO and real endpoint integration use separate subtrees and trust profiles.

Adapters translate transport mechanics into the same canonical command/capability ABI. They do not own protocol/FTL policy.
