<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Reference and clean-source policy

The BSD user-space core is authored from documented interfaces, independently derived design contracts and black-box tests. Reference implementations may be studied for architecture and behavior, but their source does not enter the BSD authoring context.

Rules:

- Never vendor or add source submodules from FEMU, NVMeVirt, QEMU, the Linux kernel or another third-party project in the initial repository.
- Never copy source, comments, tables, diagrams or line-by-line explanations into a differently licensed component.
- A GPL-derived implementation is allowed only in an explicitly approved GPL component with original attribution, fixed source revision and modification history. `kernel/` is not a laundering path for unknown source.
- BSD code must not include Linux-private, QEMU-internal or VFIO implementation headers.
- Protocol implementation must be independently authored and reviewed. Do not commit official specification PDFs, logos, or near-verbatim register/opcode/bitfield tables.
- Record every material external source in `docs/provenance/sources.yaml` with a fixed revision or immutable release URL, license, use and whether any code was copied.
- Raw ChatGPT or other model transcripts are not authoritative sources and are not part of the public repository.

The project does not need NVM Express recognition or certification to exist and publish independently authored open-source work. This statement does not resolve the document-license, implementation-rights or patent basis of a particular protocol contribution. Those questions receive scoped provenance/rights review; that review is not an application for official approval and is not legal advice.
