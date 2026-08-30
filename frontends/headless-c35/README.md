<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3.5a failure-atomic headless firmware remediation

> **Review status:** `REVIEW_HOLD / C3.5a IN PROGRESS`. The remediation is
> implemented locally and must still pass its clean source/evidence freeze and targeted
> second-opinion review before the hold can be closed. The original finding is
> recorded in the [public hold record](../../docs/results/2026-08-29-c3-5-review-hold.md).

This directory composes the frozen C3.1 lifecycle, C3.2 persistence policy,
C3.3 programmable NAND controller and C3.4 mapping/file media behind a
transport-free headless interface. It adds no NVMe opcode, queue, PCI function,
BAR, device DMA engine, interrupt or QEMU device.

```text
headless HIF + value-only publication
  -> C3.1 lifecycle through the validated lifecycle port
       -> S: scripted lifecycle-only binding
       -> C3.4 mapping + C3.2 witness + C3.3 NFC
            -> coherent RAW_MEDIA / PHYSICAL_TXN bundle
                 -> M: memory media
                 -> B: 64-KiB byte-image engine
                 -> P: anonymous POSIX regular fd

publication value -> independent trace observer (never authoritative)
```

## Failure-atomic contract

The authoritative API is tokenized and caller-serialized:

```text
submit_start / completion_start / reset_start / teardown_start
  -> operation_progress(token, bounded_budget)
  -> operation_query / operation_finalize
  -> operation_retire
```

`progress(..., 0)` is valid and does not mutate the instance. Final outcome,
commit state, cleanup state, source layer/raw code, retry class and immutable
publication remain queryable until explicit retirement. Data-operation and
control-operation UID domains are separate, so business UID exhaustion cannot
consume the teardown path.

Binding v2 uses stable transaction IDs and explicit prepare/commit/query/abort
or prepare/query/ACK ledgers. The permanent ordering is:

```text
binding prepare -> C3.1 submit -> binding commit/query -> first C3.1 step

completion acquire -> result prepare/validate -> C3.1 consume
  -> freeze publication -> binding ACK/query cleanup

reset preflight -> C3.1 begin/drain/ACK -> freeze reset publication
  -> binding recovery/quiescence -> adopt epoch and reopen

teardown alignment/drain/ACK -> freeze teardown publication
  -> binding/resource quiescence -> resumable bundle release
```

The generic headless source contains no trace pointer or trace-codec call.
Observer full/corrupt/IO-style failure cannot change command, DMA, reset or
teardown authority and cannot block bundle/fd release. Before/after-effect
fault decorators exist only in test links; architecture checks reject them
from S/M/B/P final links.

The fixed profile begins at C31 epoch 1. Resets 1 through 15 reach epoch 16;
the next business reset returns `COUNTER_EXHAUSTED / NOT_STARTED` without
calling C3.1. NFC epoch 17 is reserved for teardown cleanup. Recoverable C3.1
`FAULTED` states admit teardown. A true frozen C3.4 phase-2 failure returns
`POISONED_REPAIR_REQUIRED` and retains the bundle; a plain
`quiescent=false` remains resumable and is never force-released by timeout.

## Fixed profile and bundle

The media profile uses explicit little-endian wire descriptors:

- 32-byte `G35A` geometry wire, ID `0x736c9756`;
- 64-byte `P35A` media wire, ID `0x758162ca`;
- profile-bound B/P UUID `F35A || media-id || geometry-id || image-serial`;
- full encoded-wire comparison in addition to IDs;
- exact provider version/size/reserved/role/feature/callback validation;
- one coherent context/cookie and one live claimant.

Capacity dominance is checked for C31, C34 and NFC limits. The C34 adapter
tracks conservative runtime credits (inner/NFC UID/generation/submit/cache,
trace/tick and physical operation/sequence) plus a storage-persistent record
upper bound. Guard rejection happens before C3.1 submit; the credit ledger is
not L2P or recovery authority.

## Evidence lanes

| Lane | Link contents | Comparable evidence |
| --- | --- | --- |
| S | C3.1 plus scripted binding | Lifecycle only |
| M | C3.4 + C3.3 + memory media | Lifecycle, semantic result, raw NAND |
| B | C3.4 + C3.3 + byte-image engine | Semantic/raw and deterministic container |
| P | C3.4 + C3.3 + anonymous-fd adapter | Semantic/raw, container, target Linux ABI |

```text
E_life:       S == M == B == P
E_sem/raw:        M == B == P
E_container:          B == P
E_two-atom:       M == B == P
```

S is not a storage provider. M has no file-container claim. Fake DMA and the
semantic storage request remain separate transport-neutral test operations;
the DMA fixture is resumable through the same lifecycle port, but this is not
a hardware DMA-to-FTL command.

## Build and checks

```sh
make check-gcc
make check-clang
make check-sanitize
make check-thread
make check-analysis
make check-architecture
make check-determinism
make check-cross
make check-all
```

`libfwlab_portable_core_c31_c34.a` contains only the frozen 12-object C31,
C32 and C34 transport-free core. It excludes C33, media, bindings, lifecycle
adapter, headless code and observer. Its native GCC SHA-256 remains:

```text
b1f95f2787fe9a3d585f467d4ba72e6de32938c51273d4ad7f411dc1e644cf6f
```

The remediation gates include:

- checked/canary trace bounds and publication-UID idempotence;
- 80 before/after-effect, zero-budget and repeated-finalize transaction cuts;
- teardown takeover from six C31 command states, consume before/after and five
  reset phases;
- reset 15+1 and real command/slot/op/lease/ticket exhaustion cleanup;
- a live M/B/P inner=30, physical/sequence=16, record=14 maintenance boundary
  whose next mutation is rejected before C3.1 submit and whose retained values
  survive rebuild;
- exact provider/profile/malformed-table and observer non-authority cases;
- M/B/P two-atom write/trim with two rebuilds and exact semantic/raw/container
  comparison;
- 16 legacy composition invariants plus 14 remediation invariants, with one
  shortest counterexample per named mutation;
- all 924 unique 6+6 actor-choice strings across two families and three
  provider pairs: 5,544 fresh live executions, 20,586 within-matrix unique
  prefix labels and 72,072 live prefix observations;
- 192 barrier-started independent-instance pthread runs under Clang TSan;
- strict GCC/Clang, ASan+UBSan, two static analyzers, deterministic native
  compiler comparison and native/AArch64/RISC-V/s390x byte equality, with
  s390x verified big-endian.

The 5,544 schedule claim is exhaustive only for the frozen six-by-six macro
grammar, not arbitrary C, thread, NFC or physical schedules. Same-instance
calls remain caller-serialized.

## Claim boundary

These tests use caller-owned memory and newly created, immediately unlinked
regular files. They use no raw block device, privileged lab host, KVM or
physical SSD. They do not prove NVMe protocol behavior, BAR/MMIO, device DMA,
MSI-X/IRQ, VFIO/QEMU integration, physical NAND timing/endurance, real
power-loss/PLP, filesystem-independent durability, same-instance thread safety,
32-bit targets or bare metal.
