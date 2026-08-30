<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Portable NVMe policy boundary

This component begins Cycle 04 with an independently authored, address-free
command/profile ABI and a common address-free action envelope. C4.1 implements
only explicit byte codecs, fixed-profile validation and fake-adjacent checks.
Protocol legality and the multi-action `c4_command_graph_v1` remain later C4
gates.

The portable values contain no queue identifier, command identifier, queue
phase or Host/guest address. Native structures are not packed wire images; all
byte boundaries use the explicit codecs in `c41_codec.c`.

The fixed profile is deliberately small: one controller, one namespace with
eight 512-byte LBAs, a 4-KiB maximum transfer, one I/O queue pair, integration
depth four and a queue-engine hard maximum of 32. It is a deterministic test
oracle, not an advertised PCI controller profile.

```sh
make check-c41
make fake-link-c41
make check-all
```

Still outside: protocol command execution, PRP walking, queue state, DMA,
persistent storage, PCI/BAR/MSI-X, QEMU/vfio-user and native Linux-driver
interoperability.
