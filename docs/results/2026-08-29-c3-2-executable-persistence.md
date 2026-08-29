<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3.2 executable persistence-policy result

- Date: 2026-08-29
- Disposition: **PASS for the narrow symbolic C3.2 gate**
- Immutable source commit: `73ec2419103d202ce8f270f2f20ce34c292f73f9`
- Frozen policy prerequisite: ADR-0007 at `5c45bd9`
- Evidence capture: `2026-08-29T11:49:12Z`
- Execution profile: unprivileged native tests plus user-mode cross-ISA runs

## What passed

C3.2 implements an independently executable, transport-free persistence
policy. Its public boundary contains only versioned profiles, opaque mutation
tokens, facts, obligations, PLP envelopes, witnesses and pure policy
functions. Tiny-model geometry and all recovery machinery remain private.

The physical settlement stage may inspect frozen B/A/C operations and admitted
PLP envelopes. It then projects a logical image whose C type contains no
physical ledger, firmware RAM, Host cache, command observation or PLP state.
`logical_recover()` accepts only this projection. The architecture checker
compiles that signature independently and rejects forbidden fields or runtime
dependencies.

The model uses deterministic FIFO level-order BFS over 11 closed scenario
families. Every generated canonical state is checked at controller reset,
simulated SSD power loss and daemon/substrate crash; the Host-cache family adds
the Host-crash metamorphic event. Explicit little-endian encoding supplies the
visited key, and a matching 64-bit hash still requires comparison of the full
canonical bytes. Depth, state-table, hash-table and cut-count exhaustion are
hard failures rather than silent pruning.

## Exit evidence

| ADR-0007 / C3.2 exit item | Evidence |
| --- | --- |
| value-only public policy boundary | repository policy and public-consumer compile PASS |
| invalid unvalidated PLP rejected | three invalid configuration instances rejected before traversal |
| command and fixed-frontier fence witnesses | policy unit suite PASS |
| physical settlement separated from logical recovery | type-isolation script and recovery unit suite PASS |
| Host/RAM excluded from recovery authority | projection check and Host-cache metamorphic counterexample PASS |
| 13 independent invariant checkers | positive coverage mask `1fff` |
| 11 closed scenario grammars | 39 profile/request/initial-state runs PASS |
| all legal cuts in every canonical base state | 637 states and 1,912 recovery checks PASS |
| no silent state/depth pruning | 128 terminal states, maximum depth 14, all fixed limits respected |
| deterministic action grammar | all 17 action kinds covered; repeated stdout identical |
| stable canonicalization | zero full-hash collisions; 70 canonical duplicate states merged after byte comparison |
| one named mutation per invariant | 13 level-order shortest counterexamples emitted PASS |
| GCC and Clang strict builds | GCC 13.3 and Clang 18.1.3 PASS with `-Werror -Wpedantic` |
| sanitizer lanes | ASan/UBSan and TSan run all unit/model/negative gates PASS |
| cross-ISA and cross-endian determinism | AArch64, RISC-V and big-endian s390x stdout exactly matches native |
| no heap/thread/clock dependency | unresolved-symbol and architecture audits PASS |
| repository integration | root `make check`, isolated component fake link, policy, links, SPDX and REUSE PASS |

## Deterministic results

```text
C3.2 policy unit: PASS (4 cases)
C3.2 recovery unit: PASS (3 cases)
C3.2 invariant unit: PASS (14 cases)
C3.2 exhaustive model: PASS
  runs=39 rejected=3 states=637 terminals=128 cuts=1912
  collisions=0 duplicates=70 max-depth=14
  actions=0001ffff invariants=1fff
  hash=b399ec240bd8838f
C3.2 broken variants: PASS
  shortest-counterexamples=13
  hash=216e7d7d0d5a88ed
```

The five program outputs (policy, recovery, invariant, positive model and
negative model) are byte-identical under native x86-64 and:

- AArch64 GCC 13.3 through the AArch64 user-mode runner;
- RISC-V GCC 13.3 through the RISC-V user-mode runner;
- big-endian s390x GCC 13.3 through the s390x user-mode runner.

## Shortest named counterexamples

| Invariant | Broken mutation | Minimum actions |
| --- | --- | ---: |
| P-UNIQUE | `BM_UNIQUE_KEEP_PREDECESSOR` | 8 |
| P-NO-TORN | `BM_TORN_SKIP_CHECKSUM` | 0 |
| P-DEPEND | `BM_MAP_OMIT_DATA_C_GUARD` | 0 |
| P-DURABLE-FLOOR | `BM_RECOVERY_SKIP_TAIL_AFTER_CKPT` | 0 |
| P-VOLATILE-BOUND | `BM_RECOVER_HIGHEST_DATA_WITHOUT_MAP` | 2 |
| P-TRIM | `BM_RELOC_OVERRIDES_TOMBSTONE` | 5 |
| P-GC | `BM_GC_ERASE_AFTER_COPY` | 5 |
| P-CHECKPOINT | `BM_ANCHOR_BEFORE_CKPT_COMPLETE` | 6 |
| P-EPOCH | `BM_DELIVERY_MATCH_SLOT_ONLY` | 1 |
| P-FENCE | `BM_FENCE_LT_FRONTIER` | 3 |
| P-PLP | `BM_READY_BEFORE_PLP_DRAIN` | 4 |
| P-CONSERVE | `BM_ALLOC_LEAVES_FREE_BIT` | 2 |
| P-NO-HOST-AUTHORITY | `BM_HOST_FALLBACK_ON_NO_MAP` | 0 |

Each machine-readable record contains the scenario/profile/initial variant,
initial and pre-cut state hashes, physical-image and recovery hashes, complete
action trace, cut kind, frozen B outcomes, selected checkpoint, PLP drain
count, durable floors, allowed sets, recovered atoms and actual violation mask.
The first matching node is minimal because all earlier BFS levels were checked
before it.

## Commands executed on the immutable source commit

```sh
make -C core/c32 check
make -C core/c32 check-clang
make -C core/c32 check-sanitize
make -C core/c32 check-thread
make -C core/c32 check-cross
make check
reuse lint
gcc ... -fanalyzer -c core/c32/*.c
clang --analyze ... core/c32/*.c
```

All commands completed successfully. Both static analyzers produced no
finding. The repository fake-link policy built and executed C3.1 and the new
independent `core/c32` component without changing the frozen C3.1 Makefile.

## Toolchain

| Tool | Version |
| --- | --- |
| Host GCC | Ubuntu 13.3.0 |
| Clang | Ubuntu 18.1.3 |
| AArch64 GCC | Ubuntu 13.3.0 cross |
| RISC-V GCC | Ubuntu 13.3.0 cross |
| s390x GCC | Ubuntu 13.3.0 cross |
| user-mode runner | 8.2.2 |
| Python | 3.12.3 |
| REUSE tool | 2.1.0 |
| Host kernel used for unprivileged execution | 6.8.0-138-generic |

The Host kernel is incidental to this transport-free gate. No debug machine,
KVM guest, VFIO path or physical storage device was needed or exercised.

## Source and test manifest

| Path | SHA-256 |
| --- | --- |
| `core/c32/Makefile` | `2d1335969e5373fb0260f1586361906bf04771ea6de88b5f5339b74fec2680ac` |
| `core/c32/c32_canonical.c` | `4e8b5ff34917b78352951600829116a1dda90f82cd2344169fd4291083d1d4cc` |
| `core/c32/c32_internal.h` | `91857434f24bc5c54a2ae5e24eb0a45f5f24990153e483697626a68f84c50570` |
| `core/c32/c32_invariants.c` | `421f979eebc61270e529f62d2d1f02f1d370a6d49199f642b41cbe4a739f3255` |
| `core/c32/c32_model.c` | `643484af4bb347125168276e96584be1f977f64c01ef6a1f7db9f9eb9b824cc8` |
| `core/c32/c32_policy.c` | `a761c86492eb91dfa697690f976afd59248432350946498a6ffb0f28b3403fa9` |
| `core/c32/c32_recovery.c` | `944ff6f37190d21be950a07efe95adcbf69f590a270c8a4c85cc74c5a0dab9a0` |
| `core/c32/fakes/c32_fake_main.c` | `317c9c0644a05cbfb5fa0e99e188320a90a52a146bfa4c01f840016293c9acef` |
| `core/c32/tests/broken_c32.c` | `e61e4f0f319fa2e5547601bad4ce5eaff711f66dca0abd2b896dad874654bbbb` |
| `core/c32/tests/model_c32.c` | `7001a2ddba90daef49d008787ff60d4edb2cc51b7147efb08c33c2e506838f9b` |
| `core/c32/tests/test_invariants.c` | `5f82c2afa2175886b4b09a9915bad541d32ecda427c10ae152221d059c853021` |
| `core/c32/tests/test_policy.c` | `7954520efc38e12f13544ce3528ee7c59ce4d3991af4492c9618d9018a5afc50` |
| `core/c32/tests/test_recovery.c` | `5fe94a6a9c65b88cdb5896f4020d4576bf01d6a9733f737f880b717d4b75c535` |
| `include/fwlab/contracts/persistence_facts.h` | `c08942af70f63ad765311e9bf0ca69f42e7ab0e6add15958e4f6f6543c98b1d2` |
| `include/fwlab/portable/persistence_policy.h` | `1e57c776791e491dcdae9b26b640afd82c1516237bed3ba27f5ebfed1169d665` |
| `scripts/check_c32_architecture.py` | `16a0c0435f623203f219f947c1584eb01195c0c9f5661dfcdb3c50dc88283a3b` |
| `scripts/check_c32_cross.py` | `97f74c6e2467c049b3435d2587206627d88355966c62246ba0b7ec746b9a6bfe` |

These hashes bind the tested build inputs at the immutable source commit. This
result and the repository policy freeze them in the following evidence commit.

## Claim boundary

C3.2 is a bounded symbolic model. Its PASS result does not claim:

- an NVMe command, queue, register, status or conformance implementation;
- NAND geometry, NFC scheduling, page/OOB format, ECC, timing, wear or bad
  blocks;
- a production FTL, journal layout or checkpoint binary format;
- file-backed or raw-block persistent media behavior;
- real PLP hardware, capacitor hold-up or physical power-cut survival;
- DMA, BAR, MSI-X, PCI, QEMU, VFIO or native-driver behavior;
- performance, crash probability or hardware fidelity.

Those remain later, separately gated layers. C3.2 also does not reinterpret the
C3.1 lifecycle `SUCCESS` contract or publish a completion itself.

## AI assistance and human responsibility

- AI-assisted: yes
- Model family: OpenAI GPT-5.6, 2026-08-29
- Input categories: frozen public ADRs and repository-owned policy/build files
- Third-party implementation source copied or transformed: no
- Responsible owner and final reviewer: Evanshenf
- Human review remains required before any release or external protocol claim
