<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Portable firmware core

Future BSD-3-Clause source here owns protocol policy, command lifecycle/dependencies, namespace state, FTL, garbage collection, wear leveling, metadata and recovery. It consumes only the versioned address-free HIF/NFC contracts.

It must not include Linux-private, QEMU, VFIO, filesystem or simulator-WAL interfaces. No implementation source exists in the initial design baseline.

The first implementation is governed by the fixed-arena ownership and reset
contract in [ADR-0006](../docs/adr/0006-portable-command-lifecycle-contract.md)
and the independent durability policy in
[ADR-0007](../docs/adr/0007-command-durability-and-persistence-policy.md).
Neither accepted ADR is evidence that a portable core already exists.
