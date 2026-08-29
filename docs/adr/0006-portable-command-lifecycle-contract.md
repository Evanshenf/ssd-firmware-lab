<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0006: Portable headless command-lifecycle contract

- Status: Accepted semantic baseline; C3.1 implementation remains gated
- Date: 2026-08-29

## Context

Cycle 02 graduated and froze one Linux-specific synchronous IOAS-copy
mechanism. Its A-prime records, Linux errors and lifecycle implementation are
disposable test infrastructure and must not become portable firmware ABI.

The first real portable source needs permanent ownership and stale-event
semantics before any protocol, FTL, NAND or transport implementation is added.
At the same time, the existing architecture's full HIF identity contains
transport-private queue and command fields. Exposing those fields directly to
the portable core would couple firmware to a future transport layout.

This decision refines ADR-0003. It freezes semantic object ownership and event
ordering, not native C structure layout, a protocol descriptor or an NFC ABI.
ADR-0007 independently refines durability and recovery policy. Neither ADR may
depend on the other's implementation.

## Decision summary

C3.1 uses a fixed-arena, single-executor, poll-driven portable core with:

- transport-neutral lifecycle identity plus an opaque HIF origin token;
- generational command, capability, provider-operation and completion tokens;
- explicit ownership transfer at submit, provider acceptance and completion
  lease acquisition;
- no provider callback into the core and no inline completion;
- epoch-first reset followed by bounded drain and proof of quiescence;
- separate API, completion, fault and control-result namespaces;
- no heap, external data address, Linux type or durability claim.

The core produces an immutable completion intent. HIF owns all physical
completion publication and notification.

## Dependency and ownership boundary

```text
transport / HIF
  raw submission, owner/queue identity, address graph,
  capability authority, physical completion publication
                    │
                    │ opaque origin + address-free descriptor + capabilities
                    ▼
portable command-lifecycle core
  command ownership, reset epoch, provider operations,
  cancellation, completion intent and resource retirement
                    │
                    │ private C3.1 lifecycle-only provider seam
                    ▼
fake DMA and fake NFC fixtures
```

The C3.1 core does not call a persistence implementation. A later integration
layer may make a completion eligible only when the command remains live, its
semantic result is ready and its opaque dependencies are satisfied.

## Identity split

### Portable interpreted identity

The core interprets only:

- `instance_nonce`: changes whenever an instance is rebuilt;
- `controller_epoch`: advances at reset-begin, before old state is revoked;
- `cmd_uid`: unique within one instance and never silently reused;
- a command handle containing instance/epoch, slot and slot generation.

Epoch, UID or generation exhaustion fails closed. Tests may use reduced widths
to force wrap; production widths are chosen with the public header review.

### Opaque HIF origin token

HIF mints a fixed-width opaque token that privately binds the complete origin,
including any owner, source, queue, ring and transport command generation. The
portable core may copy the token and compare it for exact equality only. It
does not parse queue identifiers, command identifiers or transport fields.

HIF keeps the token-to-transport mapping and rejects stale publication after a
queue or owner change. This preserves the stale-queue safety property in
ADR-0003 without exposing a queue layout to firmware.

The origin token, instance nonce, controller epoch and command UID jointly bind
every buffer capability, provider operation and completion lease. In a trusted
single-process profile, an opaque token gives type and lifetime safety; it is
not a hostile-process security capability.

The trace cookie remains a separate random value that is not derived from raw
submission bytes and cannot replace identity.

## Fixed arena and execution model

The caller first requests the required arena size from a versioned capacity
configuration. Configuration names bounded capacities for commands, provider
operations, events, completion intents, abort tickets and scratch storage, with
compile-time hard maxima.

Initialization receives one aligned arena and its size. The caller owns this
storage and keeps its address and lifetime stable until teardown is
acknowledged. This initialization pointer is not a command data pointer.

The core:

- never calls `malloc`, `calloc`, `realloc` or `free`;
- uses no VLA, recursion or unbounded container;
- copies every accepted descriptor into the arena;
- exposes generational handles instead of arena pointers;
- reserves a command record and its terminal completion record atomically
  before accepting a command;
- returns explicit no-capacity without taking ownership.

Each instance has one serialized executor. Submit, abort, reset, step,
completion-lease and teardown calls for that instance are serialized by its
caller. Different instances may execute on different threads. The portable
core contains no platform lock or worker thread.

All progress occurs through an explicit, budgeted `step` operation and
provider polling. Semantics never depend on wall-clock sleep. The same initial
state, input records, provider events and logical ticks must produce the same
state trace.

## Submit ownership

Contract validation occurs before ownership transfer.

| Submit outcome | Descriptor ownership | Completion |
|---|---|---|
| invalid envelope/version/reserved field | remains with caller | none |
| no capacity | remains with caller | none |
| accepted | immutable copy owned by core | exactly one terminal intent or fail-closed instance fault |

The ownership linearization point is successful admission. Semantic command
invalidity after admission is a normal firmware completion intent, not an API
submit failure.

No command may be accepted if the core cannot reserve enough state to retire it
after provider backpressure, cancellation or reset.

## Portable lifecycle

The semantic states are:

```text
FREE
  → ACCEPTED
  → DISPATCHED ↔ HELD
  → RUNNING
  → CANCEL_PENDING
  → COMPLETION_READY
  → COMPLETION_LEASED
  → RETIRED
  → FREE
```

Not every command visits every middle state. A policy-only command may move
from ACCEPTED to COMPLETION_READY. Provider backpressure leaves a command in a
retryable DISPATCHED or HELD state without transferring provider ownership.

`PUBLISHED`, queue consumption, physical acknowledgement and interrupt state
are not portable lifecycle states. They belong to HIF.

An invariant violation, duplicate provider terminal event or impossible state
transition moves the instance to `FAULTED`: new admissions stop, and only
fail-closed reset/teardown and evidence extraction remain permitted.

## Completion ownership lease

The core creates one immutable completion intent containing the exact portable
identity, opaque origin token, semantic result and normalized fault.

HIF acquires a one-use completion lease:

1. acquisition changes READY to LEASED and binds the exact intent generation;
2. before physical publication, HIF may release the lease and return it to
   READY;
3. after HIF has consumed the intent for physical publication, it returns a
   consumed outcome and the core retires the command;
4. duplicate acquire/release/consume, wrong instance or stale epoch has no
   state effect other than a stable stale/wrong-state result.

The core never writes a CQE, queue pointer or interrupt. HIF alone guarantees
data visibility before physical completion visibility before notification.

If reset linearizes before lease consumption, the old lease is stale and cannot
publish. If HIF publication committed first, HIF records that fact privately;
reset may retire remaining core resources but cannot publish a duplicate.

## Provider contract

DMA and NFC fixtures use a common lifecycle envelope but separate private
payloads. A provider operation has an exact command token, epoch, operation
generation and cookie.

`try_submit` returns one of:

- `ACCEPTED`: provider owns the operation and must later expose exactly one
  terminal event;
- `BACKPRESSURE`: provider did not take ownership and the core may retry;
- `REJECTED`: provider did not take ownership and reports a normalized stable
  cause.

Cancel is idempotent. Recording a cancel request does not prove cancellation or
rollback. Every accepted operation still ends through one polled terminal
event.

`poll(budget)` returns at most the requested number of immutable value events.
Events contain exact operation/command/epoch identity and never point into
provider temporary storage. A terminal event ends that provider operation;
another event for it is a contract violation.

Providers never call the core from submit, cancel or reset. Even zero-delay
completion appears in a later poll. A platform callback may enqueue into a
bounded provider-owned ingress queue and request a wakeup, but cannot mutate
portable core state.

Before reset acknowledgement, every old accepted provider operation must be
terminal and incapable of a later physical effect or event. If the provider
cannot prove that property, reset remains faulted and admission stays closed.

## Buffer capability and fake DMA

Capability authority remains in HIF/DMA provider. The core receives only an
opaque command-scoped token plus declared direction and authorized length. A
request names:

- capability token and capability offset;
- controller-arena region and offset;
- length and direction;
- ordering flags and operation cookie.

No Host, guest, physical or I/O-virtual address, fd, IOAS object, page identity
or userspace data pointer crosses the boundary.

The provider validates exact identity, epoch, direction, range and capability
generation. Any mismatch fails closed.

For capability-to-controller transfers, bytes first enter per-operation scratch
and update authoritative controller data only after complete success. For the
opposite direction, an error may leave an external prefix changed. The
terminal event reports only a provable effect class:

- none;
- full;
- exact prefix with length;
- unknown prefix bounded by the request.

It never fabricates rollback or completed bytes. Reset or abort prevents new
old-command requests, while already accepted operations must drain normally.

The fake DMA registry and backing bytes live only in fixture context. The core
does not inspect them.

## Fake NFC boundary

C3.1 freezes only the provider lifecycle envelope. Its fake NFC payload is a
test-private fixture for deterministic delay, success, normalized fault,
backpressure and cancellation.

It does not define page/OOB, geometry, read/program/erase, ECC, retry, wear,
bad blocks, FTL or GC. Those semantics remain C3.3 work. Expected outcomes are
configured in the fixture, not embedded as answers in tested commands. The
portable core has no fake-versus-model branch.

## Abort

Abort is a generic cancel/query/ack control protocol, not a protocol command
format.

- The request names an exact live command handle.
- One target has at most one active abort ticket; repeated requests query it.
- Before provider ownership, the command may produce an aborted intent.
- After provider acceptance, the command enters CANCEL_PENDING and sends an
  idempotent provider cancel request.
- Normal and cancel terminal events linearize in the order the single executor
  consumes them; the first terminal result wins.
- Cancellation may report none, exact-prefix or unknown-prefix external effect
  and never implies rollback.
- Reset supersedes a pending abort; the abort ticket reports reset-superseded
  and the old command cannot produce a publishable completion.
- The requester acknowledges the terminal abort outcome before ticket reuse.

Future protocol code may map a protocol-specific abort operation to this
control contract. This ADR defines no opcode or protocol status mapping.

## Reset and teardown

At reset-begin the core, in order:

1. closes admission;
2. advances controller epoch, failing closed on exhaustion;
3. makes every old completion and capability lease stale;
4. moves live old commands into RESET_DRAIN;
5. requests provider cancel/quiesce and drains terminal events.

Old events may release old resources. They cannot start a new transfer, modify
new-epoch command state or produce a new completion intent.

Reset becomes ACK-ready only when all old provider operations are terminal,
all old capability/completion leases are gone and all old command references
are retired. HIF acknowledges before admission reopens in the new epoch.

Teardown uses the same drain rules but ends in DEAD and returns the arena only
after teardown acknowledgement. There is no force-forget path that clears
tables while a provider may still produce an effect.

Reset or abort controls command visibility and ownership. They do not erase or
roll back a physical effect that already occurred. ADR-0007 governs the
persistence consequences of such effects.

## Type and status separation

Portable code uses separate types for:

1. API/control result: ownership and call state such as OK, NO_CAPACITY,
   INVALID_CONTRACT, WRONG_STATE, STALE_TOKEN, UNSUPPORTED_VERSION,
   COUNTER_EXHAUSTED and INVARIANT_FAILURE;
2. firmware completion intent: semantic SUCCESS, INVALID_COMMAND,
   UNSUPPORTED_COMMAND, ABORTED, TRANSFER_FAILURE, MEDIA_FAILURE,
   RESOURCE_FAILURE or INTERNAL_FAILURE;
3. normalized fault: portable domain, retry class, effect class and stable
   reason code;
4. control outcome: abort/reset/teardown pending, terminal, too-late, stale,
   superseded or faulted states.

Provider native error values, Linux errno and transport status never enter a
public header, firmware state or logical trace. API failure does not
automatically become a firmware completion.

C3.1 success makes no durability statement. Its persistence claim is always
NONE. ADR-0007 facts are consumed only by a later integration layer.

## Representation and cross-endian rule

This ADR freezes semantics, not native-structure byte layout. Public native C
types use fixed-width portable integers and explicit version/size/reserved
rules but are not declared packed wire records.

Any serialized command fixture, event, trace or future cross-process record
uses an explicit byte codec with declared endianness, exact size and zero
reserved bytes. Literal golden vectors and a big-endian simulation or
cross-build verify the codec; native `memcpy` serialization is forbidden.

## Required invariants

1. Portable descriptors, tokens, status and traces contain no external address.
2. Every accepted command has exactly one lifecycle owner.
3. Accepted descriptors and completion intents are immutable.
4. Admission reserves all storage required for terminal retirement.
5. Each command/epoch has at most one completion intent and one consumed lease.
6. Old/wrong-instance leases and events have zero new-state effect.
7. Reset advances epoch before revocation or drain.
8. Reset ACK requires zero old operation, capability, completion and command
   ownership capable of future effect.
9. Capability identity, epoch, direction and range are checked exactly.
10. Every provider-accepted operation has exactly one terminal event.
11. Abort never promises rollback of an external effect.
12. Arena exhaustion produces no partial ownership transfer.
13. The core has no heap, unbounded work, reentrant callback or wall-clock
    semantic dependency.
14. Two instances with identical slot numbers still have no shared state.
15. Counter wrap and ABA risk fail closed.
16. C3.1 SUCCESS is never described as durable.

## C3.1 exit gate

Before C3.1 can pass:

- public headers and link maps contain no Linux, VFIO, A-prime, IOVA, errno,
  NVMe queue/opcode, IRQ, BAR, PCI, QEMU, filesystem or simulator-WAL
  dependency;
- no heap symbol or hidden unbounded allocation exists;
- GCC and Clang strict builds pass on x86-64, AArch64 and RISC-V;
- explicit codecs pass literal-vector and cross-endian tests;
- ASan/UBSan and TSan pass;
- a bounded model covers submit ownership, backpressure, cancellation,
  completion lease, reset, teardown, duplicate/late/wrong-instance events,
  pool exhaustion and reduced-width wrap;
- deterministic fuzz covers command envelopes and provider/control events;
- DMA none/full/exact-prefix/unknown-prefix cases are tested;
- reset is injected in every live state and cannot falsely quiesce;
- two independent instances use separate arenas and provider contexts;
- fake DMA and fake NFC can be replaced independently without core changes;
- an immutable source/toolchain/seed/trace result manifest is published.

All tests are unprivileged and require no kernel module, KVM or real device.

## Still stopped

C3.1 does not authorize protocol command layouts, NVMe queues/opcodes/status,
durable-write claims, NAND geometry or operations, FTL/GC, persistent media,
raw block, pinning, DMA hardware, IRQ, BAR/PAT, PCI, QEMU, native binding,
firmware-runtime containment or performance claims.

## Rejected alternatives

- Reuse the V1/A-prime record, state engine or error values.
- Expose queue/CID fields instead of an opaque origin token.
- Retain command data pointers supplied by the caller.
- Allow provider inline callback or provider-thread entry into the core.
- Allocate command objects from a heap.
- Accept a command before completion storage is reserved.
- Implement reset as table clearing without provider drain.
- Treat abort as rollback.
- Pass provider-native errors directly to firmware status.
- Freeze NAND geometry through the C3.1 fake NFC.
- Branch in the core for fake, model, Host or Guest providers.

## Consequences

The first portable implementation is intentionally headless and mechanically
bounded. It can establish ownership, reset and stale-event correctness before
protocol and media complexity. HIF retains complete transport identity and
publication responsibility. ADR-0007 can model persistence independently, and
later integration can combine its facts with live command state without
creating a dependency cycle.
