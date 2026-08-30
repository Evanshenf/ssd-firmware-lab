<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3.5a failure-atomic headless remediation result

- Date: 2026-08-30
- Source commit: `f3e28d7e368efa312e4909c2a8167867b31757c5`
- Evidence finalized: `2026-08-30T02:38:12Z`
- Result: **C3.5a PASS / REVIEW_PENDING**
- Cycle disposition: **REVIEW_HOLD remains active**
- Input finding: [C3.5 review hold](2026-08-29-c3-5-review-hold.md)
- Execution profile: unprivileged native and user-mode cross-ISA execution,
  using caller-owned memory and newly created, immediately unlinked regular
  files

This result records the C3.5a remediation run. It does not close the review
hold. Closure requires a targeted second-opinion review of this exact source
and evidence, followed by a separate public closure commit if no Critical
finding remains.

Follow-up status: that targeted review closed C-01 and C-03 but found a
tokenless compatibility-wrapper blocker and a trace-metadata coherence
requirement. The subsequent [C3.5b result](2026-08-30-c3-5b-wrapper-recovery.md)
reports `PASS / REVIEW_PENDING`; REVIEW_HOLD still remains active.

## Critical closure implemented

### C-01: checked observer bounds

The trace schema is v2. Every append/reserve/commit/hash/equality operation
first validates the complete stored object. Projection append checks the
remaining capacity before subtraction. Exact-fit length 65,516 succeeds;
65,517 and 65,533 through 65,535 are rejected without mutating trace bytes,
metadata or adjacent canaries. Stored lengths 65,536, 65,537 and `UINT32_MAX`
are covered under strict and sanitizer builds.

Reservations include an encoded maximum and are idempotent by publication UID.
Repeated commit accepts only byte-identical content; different content for an
already recorded UID is rejected without mutation.

### C-02: authority is independent of evidence

The generic headless object has no trace pointer, trace include or undefined
`c35_trace_*` symbol. Completion, DMA, reset and teardown return immutable,
fixed-size publication values. The observer runs afterward and reports
recorded, no-capacity or invalid-sink state separately.

Full and corrupt observers produce the same DMA output, semantic completion,
reset epoch and immutable command/reset/teardown publications as an empty
observer. They do not block C31 consume/ACK, runtime finalization, bundle
release or POSIX fd cleanup.

Headless teardown and bundle release are joined by the tokenized finalizer.
After C31 reaches `DEAD`, bundle release may remain
`COMMITTED + CLEANUP_PENDING`; the same token resumes release/query until
complete. Before- and after-effect release errors reconcile through the bundle
tombstone. Observer work remains outside this dependency graph.

### C-03: finite reset cleanup

The fixed business epoch starts at 1. Resets 1 through 15 reach epoch 16. The
next reset returns:

```text
outcome=COUNTER_EXHAUSTED
commit=NOT_STARTED
cleanup=NONE
service=READY
```

The lifecycle fault counter proves that this rejection makes no C31 reset
call. Admission remains usable at epoch 16. Teardown then consumes the NFC-only
cleanup reserve; the NFC trace contains 16 reset entries with final old epoch
16, proving cleanup epoch 17.

Independent boundary profiles force C31 command UID, slot, operation, lease
and abort-ticket generation exhaustion. Each enters `FAULTED_CLEANUP`, rejects
new business work and still reaches C31 `DEAD` through teardown.

## Transaction and failure matrix

The authoritative interface uses stable operation tokens and explicit
start/progress/query/finalize/retire calls. `progress(..., 0)` is bitwise
non-mutating. Data and control UID domains are separate, so business operation
UID exhaustion cannot consume teardown capacity.

Binding v2 provides registration prepare/commit/query/abort and result
prepare/query/abort/ACK ledgers. C31 mutating access passes through one exact,
validated lifecycle port. Test-only lifecycle and binding decorators inject
one failure before or after the selected effect; link-map checks exclude both
decorators from final S/M/B/P links.

The directed matrix reports:

```text
C3.5a transaction faults: PASS (80 resumable/zero-budget cuts)
C3.5a phase takeover: PASS
  six C31 command states; pre/post-consume; five reset phases -> teardown
C3.5a poison cleanup: PASS
  phase2 -> POISONED; quiescent=false -> retained claim/IN_PROGRESS
```

The cuts cover submit, step, command query, acquire, release, consume,
abort-request/query/ACK, registration prepare/commit/query/abort, result
prepare/query/abort/ACK, reset begin/step/ACK/recover/query/quiescence,
teardown begin/step/ACK/finalize/quiescence, transaction retirement, DMA and
runtime finalization. Publication values captured immediately after completion
consume or reset ACK remain byte-identical after injected cleanup failures.

True C34 phase 2 is exercised in a separate POSIX child. It returns source
domain `C34`, raw `C34_WRONG_STATE`, `POISONED_REPAIR_REQUIRED`, leaves the
bundle claimed and leaves the fd open until the parent reaps the child. A
separate child proves that plain `quiescent=false` remains resumable and is not
converted to poison or force-release by a retry count.

## Fixed profile, capacity and two-atom evidence

The profile uses canonical little-endian wire descriptors:

| Field | Value |
| --- | --- |
| Geometry wire | 32-byte `G35A` |
| Geometry ID | `736c9756` |
| Media wire | 64-byte `P35A` |
| Media profile ID | `758162ca` |
| B/P UUID | `F35A || media-id || geometry-id || image-serial` |

Bundle initialization validates exact versions, sizes, reserved fields, roles,
features, callbacks, encoded profile bytes, IDs, coherence cookie, common
context and initial physical quiescence. Every required binding, lifecycle,
raw-media and physical-transaction callback is individually nulled in the
negative matrix. Rejection preserves the media hash, claim state and output
object. B/P UUID mismatch leaves the complete 65,536-byte image unchanged.

The fixed factory checks C31/C34/NFC capacity dominance. The C34 adapter keeps
conservative runtime credits for inner/NFC UID, generation, submit, cache,
trace/tick, physical operation/sequence and a storage-persistent record upper
bound. A live M/B/P sequence reaches inner 30, physical/sequence 16 and record
14. The next mutation is rejected before C31 submit and before any C34 counter
changes; retained values remain readable after rebuild.

M/B/P also execute one durable write mask `0x03` with distinct atom payloads,
read both atoms, rebuild, read again, durable-trim mask `0x03`, verify both
tombstones/absence, rebuild and verify absence again. Semantic result, raw NAND
projection and B/P container bytes match exactly. This is not a claim of
command-wide two-atom crash atomicity.

## Live schedule and model evidence

The scheduler enumerates all 924 unique 12-choice strings containing six
choices for each actor. For two families and MM, BB and MB pairs, every case
creates two fresh live runtimes and storage instances:

```text
unique actor-choice strings                   924
labeled fresh live executions               5,544
within-matrix unique prefix labels         20,586
live prefix observations including empty   72,072
```

Each observation compares both live instances with separately executed solo
references from the same implementation. This is exhaustive only for the
frozen 6+6 macro grammar, not arbitrary C, thread, NFC or physical schedules.

The 16 legacy C3.5 invariants and broken variants remain regression gates.
C3.5a adds 14 abstract remediation invariants and one named mutation per
invariant:

```text
C3.5a remediation model: PASS
  invariants=14 states=52 transitions=41 max-depth=4
  state-cap=8192 successor-cap=6 depth-cap=20

C3.5a broken variants: PASS
  shortest-counterexamples=14
```

This is bounded abstract-oracle evidence, not a C refinement proof. The Clang
TSan gate separately runs 192 barrier-started independent-instance twins;
same-instance calls remain caller-serialized.

## Archive, build and portability

The renamed archive is deliberately not called an integrated C3.5 firmware:

```text
libfwlab_portable_core_c31_c34.a
```

It contains the same frozen 12 C31/C32/C34 objects in the same order and
excludes C33, media, bindings, lifecycle port, finalizer, headless and observer
objects. Native GCC reconstruction remains byte-identical to the prior hash:

```text
archive   b1f95f2787fe9a3d585f467d4ba72e6de32938c51273d4ad7f411dc1e644cf6f
life      fa7e54ad77ab7e66bf89ce458c9ae83a105932ffe7285d772a8dca33d958d93a
sem/raw   a529682388062101969911900aa385adc491a28d6b498c7a5cae115bb1f2fde0
container b6e9125697ed85894aa4ac26c5167b3c05ccbf51f8c8d0f41fb6bbb1f77d1399
two-atom  dc7e66f4a627939eddac9979ca3e46f5de7b4bc61eff7180ef3f48ad076b3b92
```

The clean source-commit run removed the complete component build directory and
then ran `make check-all` from zero. It passed in 3:44.98 and included strict
GCC, strict Clang, ASan+UBSan, Clang TSan, GCC `-fanalyzer`, Clang static
analysis, architecture/link auditing, exact GCC/Clang comparison and native,
AArch64, RISC-V 64 and s390x 64 execution. The checker verifies ELF64 and
s390x big-endian before running it.

The repository-level `make check reuse-check` then passed all earlier layer
regressions, fake links, policy, SPDX, relative-link and REUSE checks. REUSE
reported 301/301 files with copyright and license information.

Source-commit submanifests use sorted `SHA-256  path` rows over Git blobs:

```text
frontends/headless-c35 (51 files)
91a1ecbe221606326acef8ba37393bf0eb80389247933261165f16a2270cb9d0

four C3.5 checker scripts
88f247545a587d1eb290d76d75788c944ca554af335f3bb966b7587b5730bf7b

combined 55-file source/checker set
cbc096c92085f6ff1b7904560c6bd205dccb89fbc8a65e22e7b68c7dad258dae
```

| Tool | Version |
| --- | --- |
| Host GCC | Ubuntu 13.3.0 |
| Clang | Ubuntu 18.1.3 |
| AArch64/RISC-V/s390x GCC | Ubuntu 13.3.0 cross |
| user-mode runner | QEMU 8.2.2 |
| Python | 3.12.3 |
| incidental Host kernel | 6.8.0-138-generic x86-64 |

## Exact claim boundary

This result does not add or graduate NVMe protocol/queues, hardware DMA,
IRQ/MSI-X, BAR/PCI, VFIO, QEMU-device integration, a Host-native binding, raw
block media, real PLP/power failure, physical NAND/FPGA, same-instance thread
safety, 32-bit/freestanding execution or bare metal.

The P lane proves an ordinary-file target Linux ABI against the same Host
kernel/filesystem. It is not guest-device, filesystem-independent durability
or physical-power-loss evidence. The web review is a second opinion, not
certification, endorsement or independent reproduction.
