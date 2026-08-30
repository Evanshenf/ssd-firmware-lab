<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3.5b wrapper recovery and trace-coherence result

- Date: 2026-08-30
- Implementation commit: `d4fe5715b8238a147c41f484915684f61d88d7cd`
- Source commit: `10b66debcd1a59a91504e709e57b81833ae2330a`
- Evidence finalized: `2026-08-30T05:20:34Z`
- Result: **C3.5b PASS / REVIEW_PENDING**
- Cycle disposition: **REVIEW_HOLD remains active**
- Input evidence: [C3.5a remediation](2026-08-30-c3-5a-failure-atomic-remediation.md)

C3.5a closed the original unchecked trace arithmetic and finite-reset cleanup
defects, but its targeted source review found that exported convenience
wrappers could still hide the only resumable operation token or suppress a
transaction-retirement failure. The same review also found that trace cache
metadata was not tied strongly enough to the encoded publication record.

C3.5b is the narrow follow-up for those two findings. This result does not
close the review hold. Closure still requires a targeted review of this exact
source/evidence pair and a separate public closure commit if no Critical
finding remains.

Follow-up status: that targeted review closed the trace-coherence item but
found a remaining cleanup-admission Critical. Reset and teardown shared one
finite control-token counter, and active-reset takeover modified control state
before proving teardown allocation. C3.5c remediation is required;
the subsequent [C3.5c result](2026-08-30-c3-5c-teardown-reserve.md) reports
`PASS / REVIEW_PENDING`. REVIEW_HOLD remains active.

## Exported wrapper recovery

The canonical token API is unchanged. Bounded submit, completion, reset and
teardown wrappers now share one caller-serialized compatibility ownership
slot. A zero or short budget leaves the exact stable token queryable through
`c35_headless_compat_query()`. Repeating the same wrapper with the same request
or command resumes that token; a different wrapper or different input is
rejected without mutating headless state.

When binding transaction retirement fails, the operation record remains
finished and queryable:

```text
authoritative outcome/publication: unchanged
cleanup:                         PENDING
cause:                           BINDING + exact C35 result
retry:                           SAME_TOKEN
```

The next retirement attempt reconciles a before-effect failure by performing
the retirement and an after-effect failure through the provider tombstone.
No production or C35 test/runtime helper discards
`c35_operation_retire()` with a `(void)` cast; the architecture gate rejects
such a path.

The runtime finalizer queries the compatibility slot before starting teardown:

- active teardown is adopted directly;
- active or retirement-pending submit/completion/reset is retained as a
  pending token while a teardown token is created;
- after C31 reaches `DEAD`, both the teardown token and any pending wrapper
  token are retired before bundle release;
- a fully retired headless teardown leaves a value-only adoption tombstone, so
  a later runtime finalizer can release the still-claimed bundle without
  attempting an invalid second teardown.

The dedicated wrapper driver reports:

```text
C3.5b wrapper recovery: PASS
  invalid-contract checks
  zero/one-unit/same-token resume
  mismatched-input bitwise nonmutation
  submit/completion/reset/teardown finalizer takeover
  completed-teardown tombstone adoption
  transaction-retire before/after/permanent failure retention
  zero operation/ledger residue and exact P-lane fd close
```

The permanent `quiescent=false` child now proves the full sequence rather than
only exiting while claimed: it remains `TEARING_DOWN + IN_PROGRESS` with the
same queryable teardown token, does not force-release or invent poison, then
resumes through the runtime finalizer after quiescence is restored.

## Trace schema v3 coherence

The event size remains 96 bytes. Schema v3 uses previously reserved bytes to
encode the observer reservation generation. The full validator traverses all
records and proves:

- zero events if and only if cached UID/generation/offset are zero;
- publication ordinals are zero-based and contiguous; reservation generations
  are nonzero and strictly increasing;
- cached last UID, generation and offset equal the actual last encoded
  publication record, including when a projection follows it;
- active and recorded generation/UID ordering matches `next_generation`;
- reservation state, offset, length and reserved fields match the encoded or
  active record before query, commit or cancel.

Forged-empty cache state, cached UID/generation/offset mismatch, encoded
UID/generation/ordinal mismatch, invalid committed-state input, forged
recorded/active reservations and active/recorded ordering errors are rejected
without mutating trace bytes or metadata. The original exact-fit, overflow,
stored-length and canary cases remain regression gates.

## Directed, model and inherited evidence

The two new abstract invariants are:

```text
A-WRAPPER-TOKEN-RETAINED
A-RETIRE-ERROR-VISIBLE
```

Their named mutations are caught at shortest depth 2. Together with the prior
14 remediation invariants, the bounded abstract result is:

```text
C3.5b remediation model: PASS
  invariants=16 states=60 transitions=47 max-depth=4

C3.5b broken variants: PASS
  shortest-counterexamples=16
```

This is bounded abstract-oracle evidence, not a C refinement proof. The full
C3.5a matrix also remained green: 80 transaction cuts, reset/teardown phase
takeover, reset 15+1, real C31 counter-fault cleanup, 5,544 fresh live schedule
executions, 20,586 within-matrix prefix labels, 72,072 observations, M/B/P
two-atom rebuild and the fixed-profile capacity boundary.

## Clean build and portability

At exact source commit `10b66deb...`, `make clean` removed the complete
component build tree, including the new native/Clang/sanitizer wrapper binaries.
The build directory was proven absent before running `make check-all` from zero.
It completed in 240.51 seconds and passed:

- strict GCC and strict Clang;
- ASan+UBSan and Clang TSan;
- GCC `-fanalyzer` and Clang static analysis over 21 exact production sources;
- architecture/link and suppressed-retirement-result auditing;
- exact GCC/Clang determinism;
- native, AArch64, RISC-V 64 and s390x 64 execution, with s390x proven
  big-endian.

The repository-level policy, all earlier layer regressions/fake links,
relative links, SPDX and REUSE checks then passed. REUSE
reported 304/304 files with copyright and license information. Full command
logs are retained in the private continuity record rather than published as
independent evidence.

## Canonical hashes and source identity

The frozen 12-object C31/C32/C34 portable-core archive remains byte-identical.
Trace v3 intentionally changes the lifecycle and semantic/raw projections;
the storage container and two-atom storage projection remain unchanged:

```text
archive   b1f95f2787fe9a3d585f467d4ba72e6de32938c51273d4ad7f411dc1e644cf6f
life      3b38adcdaa0f3a6b8c2ab5ea54d4a134826a49a456f3e32672126ae811ce5842
sem/raw   68d49de982b70406c3c1e1baf39549502c384afe5827eb3acc7f155026040508
container b6e9125697ed85894aa4ac26c5167b3c05ccbf51f8c8d0f41fb6bbb1f77d1399
two-atom  dc7e66f4a627939eddac9979ca3e46f5de7b4bc61eff7180ef3f48ad076b3b92
```

Source-commit submanifests use sorted `SHA-256  path` rows over Git blobs:

```text
frontends/headless-c35 (52 files)
0495e466452e5a48955dcb7a7212575dc3834627410ef8a1d372b6aba3d26c59

four C3.5 checker scripts
962dd4d10b94e0fa8be75aaaa12cacb08645bd1883a7ccb67d4522f47caa8b48

combined 56-file source/checker set
dda6e78116ccfa7507ff2b7fbfaa57de78f14a9e7b798391493961d857969891
```

## Exact claim boundary

C3.5b changes only the transport-free C35 wrapper/finalizer/observer seam and
its tests/checkers. It does not add or graduate NVMe protocol/queues, hardware
DMA, IRQ/MSI-X, BAR/PCI, VFIO, QEMU-device integration, a Host-native binding,
raw block media, physical NAND/FPGA, real PLP/power failure, same-instance
thread safety, 32-bit/freestanding execution or bare metal.

The P lane still proves an ordinary-file target Linux ABI against the same
Host kernel/filesystem. The targeted web review remains a second opinion, not
certification, endorsement or independent reproduction.
