<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3.5 post-review hold and C3.5a requirement

- Date: 2026-08-29
- Reviewed source: `48567dae4f3246c2eddb83a28a30c526947dbc86`
- Reviewed evidence: `4d3641aa62c6cb27babb15b2dc13fbfac3324acb`
- Current disposition: **REVIEW_HOLD / C3.5c REQUIRED**

The scheduled Cycle 03 second-opinion review found three source-level defects.
They were then checked directly against the immutable public source and
accepted by the project. The previously recorded test runs and hashes remain
valid evidence for their exact successful workloads, but they are not
sufficient to graduate the reviewed source.

The web review is a second opinion, not certification, endorsement or an
independent reproduction. The hold is based on source inspection, not on that
reviewer's authority.

## Critical findings confirmed in source

### Trace projection bounds

`c35_trace_append_projection()` subtracts an untrusted `uint32_t` length before
proving that the subtraction is defined in the intended range. Projection
lengths 65,533 through 65,535 can underflow the capacity expression and lead to
an out-of-bounds write. `c35_trace_equal()` also needs to reject an invalid
stored length before calling `memcmp()`.

### Evidence failure crosses irreversible lifecycle steps

The current completion path can acquire a lease and then strand it when trace
append fails. It can consume the C3.1 completion before a failing binding ACK.
Reset and teardown can commit their C3.1 state before a failing trace append;
the teardown failure then prevents the runtime wrapper from releasing its
storage bundle. Fake-DMA evidence has the same committed-state/failed-return
ambiguity.

Evidence must become non-authoritative. C3.5a must expose whether an operation
was not started, remains resumable, committed with evidence loss, or requires
reconciliation. Cleanup and bundle release cannot depend on successful trace
publication.

### Finite reset exhaustion prevents cleanup

The fixed C3.1 profile starts at controller epoch 1 and has epoch limit 16.
Fifteen resets succeed. The next reset faults C3.1 with counter exhaustion.
C3.5 has already closed admission, maps the error to an undifferentiated
invariant failure, and then rejects its teardown wrapper because admission is
closed. The storage bundle cannot be released through the normal runtime path.

A finite profile may exhaust, but exhaustion must be explicit and teardown
must remain reachable.

## Required C3.5a scope

C3.5a must include:

- checked trace arithmetic, corrupted-length validation and canary/sanitizer
  boundary tests;
- explicit resumable completion/reset/teardown phases and idempotent
  reconciliation/finalization;
- teardown and exact resource release from faulted/exhausted states;
- two-phase or compensatable binding registration, including
  mutation-before-failure injection;
- failure injection at copy/release/consume/ACK, reset callbacks/ACK, teardown
  callbacks/ACK and evidence publication;
- exact finite-capacity/error classes instead of collapsing exhaustion into
  transient capacity or invariant failure;
- full provider operation-table validation and fixed-profile compatibility;
- integrated two-atom write/trim coverage;
- narrowed schedule wording and archive naming.

The schedule result must say that 924 unique actor-choice strings were applied
to two families and three provider-pair matrices, yielding 5,544 labeled
reference-product cases. Each matrix contains 3,431 legal prefix cases, for
20,586 labeled prefix cases. Only 36 selected cases executed two live stacks in
the reviewed source.

The reusable archive contains the portable C3.1/C3.2/C3.4 mapping core. The
integrated C3.5 stack exists only in final links that add C3.3, binding, provider
and headless objects; the archive alone is not the integrated firmware stack.

## What remains accepted

The review did not reject the tiered lane design:

```text
E_life:      S == M == B == P
E_sem/raw:       M == B == P
E_container:         B == P
```

S remains lifecycle-only and is correctly excluded from storage/durability
claims. The full C3.4 stack remains one fixed geometry; standalone C3.3
dual-geometry evidence remains provider/media isolation only. Separate-instance
pthread evidence does not imply same-instance thread safety.

## Stop boundary

Cycle 03 cannot be marked reviewed or graduated until a new C3.5a source and
evidence pair closes the Critical findings. Cycle 04 is not open.

The C3.5a source/evidence pair now reports `PASS / REVIEW_PENDING`; see the
[2026-08-30 remediation result](2026-08-30-c3-5a-failure-atomic-remediation.md).
Its targeted review closed the trace-arithmetic and reset-exhaustion Criticals
but found that exported convenience wrappers could still lose their resumable
token or suppress transaction-retirement failure. It also required stronger
trace cache/record coherence.

The follow-up C3.5b source/evidence pair reports `PASS / REVIEW_PENDING`; see
the [wrapper-recovery result](2026-08-30-c3-5b-wrapper-recovery.md). This does
not remove the hold. Its targeted review found that reset and teardown still
shared a finite control-token counter and that active-reset takeover mutated
state before proving teardown admission. C3.5c must provide a reset-inaccessible
teardown reserve, failure-atomic takeover and new reviewed evidence before a
separate closure commit can be considered.

NVMe protocol/queues, device DMA, IRQ/MSI-X, BAR/PCI, VFIO, QEMU-device,
Host-native binding, raw media, physical NAND/FPGA, real PLP/power failure,
same-instance concurrency, 32-bit/freestanding and production containment
remain outside the current scope.
