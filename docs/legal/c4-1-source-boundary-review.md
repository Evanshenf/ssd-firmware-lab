<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C4.1 source-boundary review

- Scope: fixed software-oracle profile, address-free canonical codecs, private
  64-byte command capture and 16-byte completion codec
- Date: 2026-08-30
- Disposition: engineering boundary accepted for pre-alpha C4.1

The C4.1 implementation is project-authored BSD-3-Clause code. No source,
comment, structure definition or table was copied from Linux, SPDK, QEMU,
FEMU, NVMeVirt or an official specification. No specification PDF, logo or raw
model transcript is present in the repository.

External records in `docs/provenance/sources.yaml` are used only to document
behavioral context, current terminology and the source/licensing boundary. All
three C4-specific records state `code_copied: false` and permit only document
destinations.

The implementation contains the minimum byte fields needed by the C4.1
fixture; it does not reproduce a register, opcode or feature table. Protocol
legality, actual transfer shape and command execution are not implemented in
C4.1. The public claims are limited to the project-authored fixed grammar and
literal byte tests.

This engineering review does not claim NVM Express endorsement, certification,
membership rights, patent clearance or legal advice. Formal human/legal review
remains a release requirement where applicable; per repository policy, that
does not block the initial independently authored design implementation.
