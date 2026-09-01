<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0011: C4 fixed-profile policy and command graph v1

- Status: Accepted design; implementation and evidence pending
- Date: 2026-09-01
- Depends on: ADR-0003, ADR-0006, ADR-0007, ADR-0008 and ADR-0010
- Preserves: the reviewed C4.1 and C4.2 frozen inputs
- Prepares: C4.4/B4 capability integration constrained by ADR-0010, without
  implementing it

## Context

C4.1 froze one address-free command/completion/profile boundary. C4.2 froze a
headless raw-queue HIF and its generational publication lifecycle. Neither gate
implements command legality, protocol control transactions or the generalized
multi-action lifecycle required by later data movement.

The C4.3 gate must add those semantics without retaining raw queue identifiers
in portable state, modifying the reviewed C4.2 HIF, or treating an unexecuted
data plan as successful I/O.

## Fixed profile

`C43-P1` has:

- one controller and namespace identifier 1;
- eight 512-byte logical blocks and a 4096-byte maximum transfer;
- one I/O completion queue and one I/O submission queue;
- exact protocol queue depth four;
- four outstanding graph commands and eight action records per command;
- no metadata, SGL, fused commands or power-loss-protection claim;
- a present, enabled volatile write cache; FUA only on Write;
- caller-serialized, fixed-arena execution with no heap or wall-clock semantic.

The C4.2 engine's depth-32 limit is not a C43-P1 protocol range. The later Linux
profile is separately versioned.

## Authority split

The frozen canonical command exists only in C42 and the frontend adapter's
synchronous admission frame. The adapter immediately creates a sanitized typed
request. Graph arena state, APIs, observers and traces contain no complete
canonical command, generic control command dwords, QID, SQID or CID.

For Create/Delete and Abort, an external HIF bridge later obtains the immutable
raw snapshot by handle plus opaque origin. It privately decodes queue/target
operands and returns only address-free facts and opaque references.

Portable policy owns opcode/queue-class legality, namespace/feature/field
validation, exact transfer shape and semantic status/result. Providers return
facts, effects and witnesses, never protocol status. HIF owns raw memory,
queue/ring identity, physical CQE publication, notification and Host
acknowledgement.

Every top-level C4.3 request, terminal, graph configuration, observer and
provider bundle is self-describing with its own version/size prefix. Native
layout padding is represented by named reserved fields and validated as zero;
unused tagged-request branches are also zero. The C4.3 v1 provider bundle binds
the frozen profile and permits only the validation-only block provider. A later
data/durability-authoritative provider requires an explicit versioned profile
binding rather than self-asserted capability bits.

## Command subset

C43-P1 covers:

- Identify Controller, Namespace, Active Namespace List and Namespace
  Descriptor List;
- Set Features / Number of Queues;
- Create/Delete I/O CQ/SQ;
- Abort;
- Read, Write, Flush and Write FUA as address-free plans.

Unsupported commands and invalid fields produce stable firmware-owned
completion policy. A small project-authored status mapper is used; no complete
specification table is copied into the repository.

Identify produces a 4096-byte semantic recipe and payload. Read/Write use
checked widened `NLB+1`, `SLBA+NLB` and byte-length arithmetic. Flush captures a
fixed frontier only when a future durability-authoritative provider accepts the
action.

Valid Identify, Read, Write and Flush/FUA are not success-eligible in C4.3.
They remain held until a future C4.4 provider supplies the exact DMA, block and
durability witness mask. The C4.3 block fake may report only
`VALIDATED_ONLY` or failure; validation is never data completion.

## Queue and Abort transactions

Queue effects use prepare followed by exactly one commit-or-abort decision.
Create prepare may own an invisible candidate. Delete prepare records an
immutable snapshot only; the live queue remains visible until commit
revalidates the relation and enters C4.2 PREQUIESCE. Unknown responses are
queried by the same token and are never retried as a new effect.

Create order is CQ then SQ. Delete order is SQ then CQ. Number-of-Queues must be
negotiated before Create CQ and resets only on controller reset.

Abort resolution returns an exact graph ticket/handle and opaque HIF reference,
or NOT_FOUND/TOO_LATE/STALE. A cancel request alone does not win. The serialized
terminal event that wins first determines the target outcome; a cancelled
target may still report none, full, prefix or unknown-prefix prior effect.
Abort success never means rollback.

## Command graph

`c4_command_graph_v1` is the sole C4 outer lifecycle. C31/C35 remain unchanged
regression references and are not linked into the runtime.

The graph has fourteen observable phases, with terminal winner and publication
state represented independently. Prepare reserves the complete command,
eight-action DAG, intent, ready event, completion lease, consume transaction,
target/abort credits and finalizer before ownership. Reset and teardown have
separate protected coordinators.

The exact common witness vocabulary is:

```text
VALIDATED_ONLY
QUEUE_EFFECT_COMMITTED
PAYLOAD_READY
DMA_IN_COMPLETE
BLOCK_READ_READY
BLOCK_WRITE_COMPLETE
DMA_OUT_COMPLETE
DURABILITY_COMPLETE
```

Stronger data/durability witnesses require an exact completed predecessor
reference and a bound later-profile provider.

Graph consume makes a completion lease permanently one-use. It does not equal
the C42 physical cross-commit. Unknown consume responses retain a tombstone and
are queried with the same token. Host CQ acknowledgement never returns to the
graph.

## No-reentry integration

C42 command-port callbacks may update graph-local records or enqueue a bounded
mailbox only. They never call C42 candidate/delete/target APIs inline. An outer
scheduler returns from C42 completely before one bridge step invokes those
public C42 APIs. Zero-delay provider completion is still reported by a later
poll.

## Evidence boundary

The finite `C43-T1` contract has eight executable artifacts, eight bounded
model families, forty differential rows, twelve mailbox schedules, twenty-six
reset/teardown phase cuts and twenty-two named first-order canaries. It has no
Cartesian obligation generator, second-order mutation or recursive test-of-test
claim.

Every existing repository freeze remains immutable. C4.3 uses its own source
list, frontend subdirectory, runner, checker and workflow. The reviewed C42 H3
source is a read-only differential producer, not a new C42 attestation.

## Non-claims

This decision does not implement PRP/SGL walking, data capability, DMA, block
data, persistent media, FTL/NFC, power-loss durability, PCI/BAR/MSI-X,
VFIO/QEMU/Linux binding, same-instance concurrency, performance or standards
conformance. It is not a working NVMe controller claim.
