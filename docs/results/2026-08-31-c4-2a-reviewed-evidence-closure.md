<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C4.2a reviewed fixed-profile evidence closure

- Date: 2026-08-31
- Reviewed/frozen candidate:
  `4bb4e567250ffb9e76831c6094e3f97cdf7bc56d`
- Recovery epoch: `C42A-P1-2026-08-31`
- Targeted disposition: **TARGETED_CONFIRMED_CLOSED**
- Final stop: **ENOUGH / FREEZE FIXED PROFILE**
- Closure transaction: documentation and freeze policy only

This record closes the C4.2/C4.2a `REVIEW_HOLD` for the exact fixed-profile
candidate above. It follows one bounded discovery review, two accepted evidence
findings, their finite recovery, exact local/hosted attestation and one targeted
confirmation. It does not change production source or rerun the semantic matrix.

The review is project-recorded quality evidence. It is not independent
reproduction, certification, endorsement, NVMe compliance recognition or
hardware validation.

## Findings closed

### C42A-H2-QB-01 — synthetic owned kills

The earlier candidate counted changes to detached expected/observed arrays as
provider and state mutation kills. The reviewed candidate deletes that oracle,
its two C-stimulus generators and all compiled generated-obligation inputs.
Provider, phase and replay programs now report positive-witness evidence only.

The semantic inventories remain complete and separate:

```text
active claims                 9
field/outcome slots         643
identity edges/domains       22
transitions                  11
build nodes                  25
```

Mutation adequacy is deliberately narrower and explicit:

```text
provider source canaries     12
identity source canaries      7
phase source canaries         9
build substitution canaries   4
owned denominator            32
effective lanes             134
```

Each owned row binds an active-claim obligation to a named first-order source
change/substitution, unique anchor, exact changed files, narrow executor and
exact diagnostic set. GCC and Clang compile and execute the provider and
identity/phase canaries in isolated source copies. The retained wider mutation
sets remain regression tests and do not inflate the denominator.

### C42A-H2-QB-02 — unreceipted generated inputs

The earlier runner receipted one pair of generated files while Make generated
and compiled another. The reviewed candidate has one canonical obligation-lock
generator, no provider/state `.inc` generation rules and no executor dependency
on generated obligation arrays. The logical authority DAG contains 25 nodes;
the direct runtime receipt contains the 14 invocations actually launched by the
authoritative runner.

A targeted execution trace records zero invocations of either retired
generator. The reviewed authority aggregate reports 13 distinct fresh ELF
programs, an exact execution receipt and an unchanged guarded workspace.

## Evidence retained

Exact candidate `4bb4e567...` passed:

- provider `26` variants / `52` binaries / owned `12/12`;
- dynamic `51` mutants / `144` binaries / owned `16/16`;
- four exact build substitution canaries;
- GCC, Clang, ASan+UBSan, TSan, GCC/Clang analyzers and strict flags;
- 13-program GCC/Clang complete-output determinism;
- native/aarch64/riscv64/s390x execution, including s390x big-endian identity;
- root repository checks, layer fake links, policy, SPDX and REUSE `400/400`.

Hosted exact-head attestations:

- `policy-smoke` run
  [33435087895](https://github.com/Evanshenf/ssd-firmware-lab/actions/runs/33435087895):
  success;
- `c4-portable` run
  [33435087791](https://github.com/Evanshenf/ssd-firmware-lab/actions/runs/33435087791):
  success.

The web review inspected the public source/commit and supplied package, but did
not independently rerun the complete local matrix, hosted jobs or full trace.

## Frozen boundary

The frozen result is a caller-serialized, deterministic software semantic
oracle for one controller, one namespace, `8 x 512-byte` LBAs, maximum 4-KiB
transfer and one I/O queue pair. It retains the headless memory-transport C4.2
queue/CQ/identity reference and the address-free portable policy boundary.

It is not a complete NVMe controller and does not graduate Admin/NVM command
legality, PRP/data DMA, BAR/MMIO, MSI-X/interrupt delivery, PCIe, vfio-user,
QEMU integration, storage-backed media, NAND timing/physics, real power loss,
same-instance thread safety, 32-bit/freestanding execution or bare metal.

No C4.2 source/evidence change, broader mutation request or additional review
streak is authorized after this closure. Reopening requires an executable
counterexample to an active frozen claim, a missing mapping for existing claim
wording, an undeclared repository-controlled verdict input, a model
contradiction or failure of a declared authority-root assumption.

C4.3 is a separate product gate. It may build on this frozen C4.2 boundary but
does not retroactively broaden the reviewed claim.
