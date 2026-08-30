<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C4.2a remediation source-boundary review

- Scope: C4.2 ownership, reset supersession, queue association, CQ scrub
  retirement, reserve-time completion fields and evidence observability
- Date: 2026-08-30
- Disposition: independently authored pre-alpha remediation; review hold
  remains until source and evidence gates finish

C4.2a is an independently authored correction to the project's own C4.2
software-semantic HIF. No source, comment, structure, table or test vector was
copied or transformed from Linux, SPDK, QEMU, FEMU, NVMeVirt or an NVM Express
specification. The original C4.2 boundary record remains byte-for-byte
historical evidence; this supplemental record covers only the remediation.

The component and queue-memory port advance to project-local version 2. The
new states and operations make ambiguous provider ownership explicit, keep old
tokens observable across reset/teardown linearization, bind a prepared SQ to
the exact associated CQ generation, and require a quiescent retired scrub
proof before its candidate record can be forgotten. These are project safety
contracts, not copied protocol requirements.

The read-only observer exports normalized queue, command, publication,
reconcile, notification, candidate, control and target facts for independent
tests. It exports no raw 64-byte submission entry, Host address, provider
pointer or platform handle, and it never participates in production decisions.
Malicious fake-provider scripts are test infrastructure: they inject numeric
return values and missing outputs to prove that the production consumer fails
closed.

C4.2a still contains no opcode/status legality table, Admin decoder, Identify
data, Read/Write/Flush execution, PRP walker, storage backend, BAR/MMIO model,
interrupt, PCI function, QEMU adapter or vfio-user path. It does not claim a
working or conformant NVMe controller, hardware timing, certification, patent
clearance or NVM Express endorsement.
