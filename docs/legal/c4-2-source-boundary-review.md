<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C4.2 source-boundary review

- Scope: headless SQ/CQ memory queues, opaque command identity, completion
  publication, Host acknowledgement, queue delete/reset and fake providers
- Date: 2026-08-30
- Disposition: engineering boundary accepted for pre-alpha C4.2

The C4.2 implementation, contracts, fakes, tests and model are independently
authored project code under BSD-3-Clause. No source, comment, structure, table
or test vector was copied or transformed from Linux, SPDK, QEMU, FEMU,
NVMeVirt or an NVM Express specification. No specification PDF, logo or raw AI
transcript is stored in the repository.

The Linux v7.0 and SPDK v26.05 records in
`docs/provenance/sources.yaml` were consulted only for public behavioral
context: a Host writes fixed-size SQ entries before advancing a tail, a CQ
consumer observes a phase value, and an implementation must not overwrite a
full queue. Both records state `code_copied: false` and allow documentation
destinations only.

The one-empty-slot ring rule, fixed maximum depth 32, phase-last two-operation
fake publication, queue-memory capability, graph-owned handle, opaque origin,
stable reconcile tokens and `COLD_NO_QUEUES` reset boundary are project
engineering choices. They are not presented as reproduced specification text,
PCI/DMA behavior or conformance requirements.

C4.2 deliberately has no opcode/status legality table, Admin command decoder,
Identify data, Read/Write/Flush execution, PRP walker, storage backend,
BAR/MMIO register model, interrupt, PCI function, QEMU adapter or vfio-user
path. Its command port carries address-free lifecycle values and scripted fake
results; it does not claim to be the final protocol-policy ABI.

This review does not claim NVM Express endorsement, certification, patent
clearance or legal advice. Human source/legal review remains required before a
release or external conformance claim.
