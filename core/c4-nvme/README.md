<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Portable NVMe policy boundary

This component begins Cycle 04 with an independently authored, address-free
command/profile ABI and a common address-free action envelope. C4.1 implements
only explicit byte codecs, fixed-profile validation and fake-adjacent checks.
Protocol legality and the multi-action `c4_command_graph_v1` remain later C4
gates.

C4.3 design is now fixed by ADR-0011. Phase 1 adds only versioned typed
contracts, the seven-member C43 archive skeleton, layout/reserved-zero checks
and fake-adjacent linkage. Protocol and graph behavior remain later phase
checkpoints; the phase-1 Identify encoder explicitly returns NOT_IMPLEMENTED
without changing its output buffer.

The Phase 2 reservation checkpoint reserves one complete command closure
atomically: eight action records plus transaction, lease, consume and finalizer
identities.
`prepare_start`/`prepare_query` cover four fixed slots, exact-key response-loss
recovery, backpressure and counter exhaustion without admitting command
ownership. Policy, action submission and completion behavior remain
unimplemented.

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
make check-c43-phase1
make check-c43-phase2
make fake-link-c43
make check-all
```

Still outside: protocol command execution, PRP walking, queue state, DMA,
persistent storage, PCI/BAR/MSI-X, QEMU/vfio-user and native Linux-driver
interoperability.
