<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Transport and harness adapters

The first implemented adapter is the
[C3.5 integrated headless firmware harness](headless-c35/README.md). It composes
the portable lifecycle, persistence, NFC, mapping and file-media layers without
adding a transport protocol.

Future BSD-3-Clause user-space adapters include `vfio-user`. Host synthetic
PCI, custom emulated VFIO and real endpoint integration use separate subtrees
and trust profiles.

Adapters translate transport mechanics into the same canonical command/capability ABI. They do not own protocol/FTL policy.
