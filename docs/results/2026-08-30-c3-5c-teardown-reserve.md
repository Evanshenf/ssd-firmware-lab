<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3.5c protected teardown-admission result

- Date: 2026-08-30
- Implementation commit: `f037ae1008167064930ba00342ffd95d2c8026c7`
- Source commit: `9c91538b78d88af88dc6a63da4fd10f1209fe14f`
- Evidence finalized: `2026-08-30T06:39:00Z`
- Result: **C3.5c PASS / REVIEW_PENDING**
- Cycle disposition: **REVIEW_HOLD remains active**
- Input evidence: [C3.5b wrapper recovery](2026-08-30-c3-5b-wrapper-recovery.md)

Targeted review of C3.5b found that reset and teardown still shared one finite
control-token counter. Repeated epoch-exhausted reset denials could consume all
512 control UIDs, and active-reset takeover moved/finished the reset before
proving that a teardown token could be allocated.

C3.5c is the narrow follow-up for that cleanup-admission defect. This result
does not close the review hold. Closure requires targeted review of this exact
source/evidence pair and a separate public closure commit if no Critical
finding remains.

## Protected teardown identity

Data operations, reset and teardown now use distinct UID domains. Reset retains
the existing finite business-control domain. Teardown has an independent,
instance-lifetime domain with exactly one cleanup token:

```text
next_teardown_uid = 1
teardown_uid_limit = 1
```

Reset wrappers and epoch-denial results cannot consume that token. Reset denial
therefore remains a queryable
`COUNTER_EXHAUSTED / NOT_STARTED / CLEANUP_NONE / READY` operation while reset
identity capacity exists. Exhausting the reset identity domain rejects further
reset starts without removing the separate route to C31 `DEAD`.

Reset and teardown may both have numeric UID 1, but complete operation-token
identity includes kind. Teardown does not use a binding registration
transaction ID, so the independent numeric domain does not alias a reset
ledger.

## Failure-atomic takeover

`c35_teardown_start()` now validates and constructs the protected teardown
token in a local value before it moves, finishes or clears an active reset.
Only after that capacity proof does it install the teardown record and commit
the teardown UID.

The architecture checker requires the protected fields, rejects any use of the
reset allocator for teardown and verifies that `teardown_token_prepare()`
precedes the first active-control mutation. A forced teardown-domain exhaustion
case snapshots the complete headless object and output token; teardown returns
`COUNTER_EXHAUSTED` with both bitwise unchanged. Restoring the reserve then
allows the same reset/finalizer sequence to complete.

## Boundary and takeover evidence

The C3.5c limits driver executes these new cases:

```text
minimum capacity:
  zero-budget reset UID 1 -> finalizer teardown UID 1 -> C31 DEAD
  one successful reset at reset limit 1 -> independent teardown -> C31 DEAD

fixed profile, each S/M/B/P lane:
  15 successful resets -> epoch 16
  497 queryable epoch-denial reset tokens -> reset UID 512
  next reset identity admission rejected bitwise unchanged
  command still admitted at epoch 16
  independent teardown token -> bundle release
  non-S NFC cleanup old epoch 16 -> cleanup epoch 17
```

The result is:

```text
C3.5c limits/cleanup: PASS
  15+497+1 reset UID boundary x S/M/B/P
  independent teardown reserve
  low-limit mutate-before-admit nonmutation
```

The permanent-retirement combination gap is also closed on the P lane. A
completion wrapper first enters retirement-pending state. The runtime finalizer
takes ownership while `transaction_retire` remains permanently failing. Across
zero- and one-unit progress, the pending token and completion/teardown
publications remain byte-identical, the bundle remains claimed and no apparent
success is returned. After restoring the provider, the same finalizer retires
the pending token, releases the bundle, leaves zero operation/ledger residue
and permits exact fd close.

## Wording and abstract regression

The C3.5b trace sentence is corrected to the actual codec contract:
publication ordinals are zero-based and contiguous; reservation generations
are nonzero and strictly increasing. No numeric monotonicity claim is made for
publication UIDs.

C3.5c adds:

```text
A-TEARDOWN-RESERVE    / BM_RESET_CONSUMES_TEARDOWN_RESERVE
A-TEARDOWN-ADMIT-FIRST / BM_TAKEOVER_MUTATES_BEFORE_ADMIT
```

The bounded abstract result is now:

```text
C3.5c remediation model: PASS
  invariants=18 states=65 transitions=50 max-depth=4

C3.5c broken variants: PASS
  shortest-counterexamples=18
```

This remains bounded abstract-oracle evidence, not a C refinement proof.

## Clean build and portability

At exact source commit `9c91538b...`, `make clean` removed the complete C3.5
build tree and the build directory was proven absent. `make check-all` then ran
from zero and completed in 243.62 seconds. It passed strict GCC, strict Clang,
ASan+UBSan, Clang TSan, two static analyzers, architecture/link auditing,
determinism and native/AArch64/RISC-V/s390x execution with s390x proven
big-endian.

The inherited 80 transaction cuts, 5,544 fresh live schedules, 20,586 prefix
labels, 72,072 observations, phase takeover, trace v3 corruption cases,
two-atom rebuild, reset 15+1 and fixed-profile capacity boundary remained
green.

Repository policy, all earlier layer regressions/fake links, relative links,
SPDX and REUSE checks then passed. REUSE reported 305/305 files with copyright
and license information. Full command logs remain in the private continuity
record rather than being presented as independent public reproduction.

## Canonical hashes and source identity

The new admission reserve does not alter the frozen portable archive or the
canonical successful workload bytes:

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
dee00b41b55f7282352064a90d0db3a1cb12f61dc8988efb8cebbb31cc64cdaf

four C3.5 checker scripts
4d839c9679a9b4dd9384a44d105cbdaa98a31f0ba4132bb8e076358252d82785

combined 56-file source/checker set
38245ff5f2b9616d07dba48c9fde265170d874f68a3d912d0f95c164ecf18c07
```

## Exact claim boundary

C3.5c changes only the transport-free C35 cleanup-admission seam and its
tests/checkers. It does not add or graduate NVMe protocol/queues, hardware DMA,
IRQ/MSI-X, BAR/PCI, VFIO, QEMU-device integration, a Host-native binding, raw
block media, physical NAND/FPGA, real PLP/power failure, same-instance thread
safety, 32-bit/freestanding execution or bare metal.

The P lane remains an ordinary-file target Linux ABI result against the same
Host kernel/filesystem. The targeted web review remains a second opinion, not
certification, endorsement or independent reproduction.
