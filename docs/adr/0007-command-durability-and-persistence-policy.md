<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0007: Command durability and executable persistence policy

- Status: Accepted executable-policy baseline; C3.2 implementation remains gated
- Date: 2026-08-29

## Context

ADR-0002 separates firmware mapping truth, physical NAND truth and simulator
infrastructure truth and defines durable physical-operation B/A/C/S ordering.
It does not yet give an executable command-level answer for volatile success,
durable success, write cache, power-loss protection, durability fences,
multi-atom recovery or mapping/checkpoint commit points.

Those meanings must be fixed before a headless core, FTL or media prototype can
return success. Otherwise each implementation could invent a different
durability contract.

This ADR is an executable refinement of ADR-0002, not a parallel source of
truth. It uses the identity and event envelope in ADR-0006 but does not depend
on its lifecycle implementation. Persistence produces monotonic evidence; it
does not change command state or publish a completion.

## Scope

This decision freezes:

- volatile and durable success eligibility;
- write-cache, no-PLP and validated-PLP profiles;
- transport-neutral self-durable and fixed-frontier fence requests;
- logical atomicity unit and recovery outcome sets;
- data, mapping and checkpoint commit points;
- the relationship between physical B/A/C/S and logical recovery truth;
- trim, relocation, GC, checkpoint, reset and late-effect ordering;
- the exhaustive tiny-model gate for C3.2.

It does not define a protocol bit, opcode or status mapping; NAND geometry,
ECC or timing; an FTL layout; journal/OOB binary format; file/raw media layout;
or Host persistence batching.

## Truth domains remain separate

1. Firmware truth: logical versions, mapping/tombstone records, firmware
   checkpoints and recovery policy written through NFC into NAND/OOB truth.
2. Physical truth: page/OOB data, erase generation, physical outcomes, wear and
   bad blocks.
3. Simulator infrastructure truth: physical-operation ledger, media container
   generation and substrate I/O completion. It never contains an authoritative
   decoded logical map.

Deleting volatile firmware RAM and any Host-side decoded cache must not change
the recovered mapping result.

## Physical and logical terminology

ADR-0002's historical B/A/C/S ordering is retained with explicit names:

```text
B_phys: durable BEGIN for one physical operation
  → A_phys: APPLIED or NO_EFFECT outcome fixed
  → C_phys: that outcome durably published in commit-sequence order
  → OUTCOME_DELIVERED: outcome delivered to the still-live firmware token
```

`OUTCOME_DELIVERED` is the old S point. It is not a logical mapping commit and
not an externally published command success.

Logical mutation terms are separate:

- `CAPTURED`: complete logical-atom payload and mutation intent are owned by
  the controller domain;
- `DATA_STABLE`: all required data physical operations reached valid
  `C_phys(APPLIED)`;
- `C_map`: a valid mapping, tombstone or relocation record became recoverable
  authority;
- `LOGICAL_DURABLE`: recovery is required to apply that logical transition;
- `PLP_ADMITTED`: a complete redo envelope entered a validated protected
  domain with reserved drain budget;
- `C_ckpt`: a valid firmware checkpoint and anchor became recoverable;
- `VOLATILE_SUCCESS`: an external success was published without a durable
  witness;
- `DURABLE_SUCCESS`: an external success was published with a valid durable
  witness.

The persistence layer emits evidence such as CAPTURED, C_map,
LOGICAL_DURABLE or PLP_ADMITTED. A later integration layer may publish success
only if the command remains live and its required evidence is satisfied.
Persistence itself never publishes a CQE, status or interrupt.

`PROGRAM complete`, C_phys, C_map, C_ckpt and external success are not
interchangeable.

## Portable logical atom

A `logical_atom` is the smallest unit for an independent recovery choice. It is
not defined by a protocol LBA field and has no byte size in C3.2. A later
protocol/namespace profile selects its size.

For one atom, recovery chooses exactly one complete old value, one complete new
value or a tombstone. Torn data or metadata is never authoritative.

Logical atomicity does not imply NAND page, main-plus-OOB or multi-page
atomicity. Every metadata record is versioned, checksummed and independently
valid or invalid.

The first policy provides no multi-atom group atomicity:

- a durable command success requires every atom in that command to have a
  durable witness;
- before durable success, a crash may recover any per-atom old/new combination
  allowed by version and dependency order;
- a command whose effects may have partially committed reports indeterminate,
  not no-commit.

Orphan data may be created. Logical metadata must not publish a candidate until
all data it references is complete and valid.

## Durability requests

The portable policy uses three generic requests:

- `DURABILITY_DEFAULT` applies the configured cache/power profile;
- `REQUIRE_SELF_DURABLE` waits only for the command's own dependency closure;
- `DURABILITY_FENCE(scope, frontier)` captures a fixed order frontier and waits
  for all covered volatile obligations.

No protocol name, bit or command encoding appears here. A future frontend maps
its protocol to these requests.

Every mutation receives controller epoch, opaque scope and monotonically
ordered sequence at acceptance. A fence captures one fixed frontier; commands
accepted later do not expand it. The first version does not coalesce one
volatile obligation into a later overwrite. Such optimization needs a separate
proof.

A covered operation may close as provable no-commit. If its outcome is
indeterminate, the fence fails rather than claiming durability. Successful
fence publication is itself a durable success.

## Cache and power-profile policy

| Write cache | Power profile | Earliest modifying-command success | Success class |
|---|---|---|---|
| disabled | no PLP | every atom LOGICAL_DURABLE and evidence delivered to the live request | durable |
| enabled | no PLP | every atom CAPTURED and visible to same-epoch reads | volatile |
| disabled | validated PLP | every atom LOGICAL_DURABLE | durable |
| enabled | validated bounded-drain PLP | every atom PLP_ADMITTED or LOGICAL_DURABLE | durable |
| enabled | claimed but unvalidated PLP | illegal configuration | none |

Self-durable and fence requests always wait for LOGICAL_DURABLE or a validated
PLP_ADMITTED witness.

Volatile success creates an obligation that remains outstanding after command
resource retirement. Reset may discard volatile cache only according to its
declared event profile and recovery set; it cannot silently call the obligation
durable.

## Validated PLP witness

PLP_ADMITTED can support durable success only if all of these are true before
publication:

- the complete payload, atom/version identity, dependency and order information
  form a versioned, integrity-checked redo envelope with a commit marker;
- capacity and power-drain budget are reserved;
- reset or modeled power loss drains envelopes in persistent order to C_map
  before READY;
- the protected domain survives every event claimed by the profile, including
  daemon or substrate crash when those are in scope;
- firmware/HIF interprets the envelope; simulator infrastructure does not
  create a second decoded mapping truth.

Insufficient capacity causes backpressure, waiting for LOGICAL_DURABLE or
failure before success publication. A profile never silently downgrades durable
success to volatile success.

C3.2 may model a validated PLP domain symbolically. C3.4 cannot make an early
PLP durability claim until its provider demonstrates the protected-domain and
drain properties for the events named in that result.

## Logical commit points

| Mutation | Logical commit rule |
|---|---|
| write | all referenced data reach valid C_phys(APPLIED), then a valid mapping record reaches C_phys and forms C_map |
| trim | a higher logical-version tombstone record reaches C_phys and forms C_map |
| relocation | destination copy reaches valid C_phys, then relocation record reaches C_phys and forms C_map; only then may old source erase |
| firmware checkpoint | valid inactive checkpoint reaches C_phys, then valid anchor reaches C_phys and forms C_ckpt |
| PLP write | PLP_ADMITTED is a durability witness, not NAND mapping commit; recovery must drain it to C_map |

C_map is LOGICAL_DURABLE. Mapping records may provide durability before a
firmware checkpoint when recovery replays their journal tail.

A firmware checkpoint compacts only existing C_map records. It cannot turn a
volatile RAM mapping into durable truth. A future batch-commit checkpoint would
require another ADR.

Simulator physical checkpoints recover physical bits and unresolved B_phys
operations only. Firmware checkpoints recover mapping/tombstone truth. Neither
may substitute for the other.

## Crash lattice

| Cut | Physical recovery | Logical consequence |
|---|---|---|
| before B_phys | reclaim inactive payload | no candidate |
| after B_phys, before A_phys | replay the frozen input/profile/outcome deterministically | depends on whether a valid later mapping record forms C_map |
| after A_phys(APPLIED), before C_phys | idempotently roll forward to one C_phys | data alone is orphan; mapping C_phys may form C_map |
| after A_phys(NO_EFFECT), before C_phys | preserve old physical truth and publish NO_EFFECT | cannot form the intended C_map |
| after data C_phys, before outcome delivery | data physically exists | remains orphan without C_map |
| after metadata C_phys, before outcome delivery | metadata physically exists | C_map and logical durability already hold |
| after outcome delivery, before external success | firmware observed the fact | command remains externally unacknowledged |
| after volatile success | current epoch must read the new value | reset/power recovery may select an allowed old/new outcome |
| after durable success | new durable floor exists | no later mutation: recovery must select new |

An old-epoch physical operation may reach C_phys or NO_EFFECT under its profile.
It cannot deliver an old outcome, mutate new firmware state or publish an old
completion after reset fencing.

## Recovery outcome sets

Let `F` be the last externally acknowledged durable floor for one atom and `N`
the next candidate, with no later acknowledged mutation:

| Observation | Allowed recovered set |
|---|---|
| no success and provable no-commit | `{F}` |
| externally unacknowledged but actual cut already reached C_map or PLP_ADMITTED | `{N}` |
| volatile success for N | `{F, N}` |
| durable success for N | `{N}` |
| failed no-commit | `{F}` |
| failed indeterminate | `{F, N}` |
| volatile trim | `{F, TOMBSTONE}` |
| durable trim | `{TOMBSTONE}` |
| durable trim followed by an unacknowledged higher-version write | `{TOMBSTONE, N}` |

Across all possible cutpoints before the exact cut is known, an unacknowledged
operation may have the policy upper bound `{F,N}`. At one concrete recovered
physical state, the deterministic oracle chooses the one outcome implied by
valid metadata.

With multiple later unacknowledged mutations, allowed states begin at the
durable floor and follow only persisted version/dependency order. Recovery
never goes below the durable floor. A lower-version mapping, data copy or
checkpoint cannot resurrect a value hidden by a durable tombstone.

For a multi-atom command, the allowed set is the product of each atom's set,
restricted by per-atom version ordering. There is no command-wide crash
atomicity claim.

## Trim, relocation and checkpoint rules

- A durable tombstone has a higher logical version than the mapping it removes.
- Old data, mapping and GC copies with lower versions cannot override it.
- Tombstone reclamation waits until checkpoint and erase-generation evidence
  proves older records cannot replay.
- A GC destination is not authoritative before relocation C_map.
- The old source cannot erase before relocation C_map and release of active
  leases.
- Relocation preserves logical version and uses a later implementation-private
  physical-copy discriminator; its encoding is not fixed here.
- Firmware checkpoint anchor flip retains the previous valid slot until the new
  slot and anchor are both valid.
- Recovery chooses the highest valid firmware checkpoint and replays valid tail
  records in persistent version/commit order.
- A checkpoint never uses Host-decoded mapping state as authority.

## Epoch, reset and event separation

Controller epoch, owner epoch and instance nonce fence stale events; they do not
define media version or recovery order. Old-epoch metadata that reached C_map
may remain valid physical truth.

The stable reset profile holds READY until:

- every old B_phys operation reaches C_phys or durable NO_EFFECT;
- every validated PLP envelope is drained or fail-closed;
- no old event can deliver an outcome or completion to the new epoch.

Controller reset, firmware reset, simulated SSD power loss, daemon/substrate
crash and Host crash are distinct cuts. A result names the event injected. An
experimental late-effect profile cannot serve as release persistence evidence.

## Executable invariants

C3.2 fixes these invariant identifiers:

1. `P-UNIQUE`: at most one authoritative logical state per atom.
2. `P-NO-TORN`: torn or invalid data/metadata is never authoritative.
3. `P-DEPEND`: mapping commit never references data without valid C_phys.
4. `P-DURABLE-FLOOR`: recovery never falls below the latest durable success.
5. `P-VOLATILE-BOUND`: volatile-success recovery stays within its declared set.
6. `P-TRIM`: durable tombstone cannot be bypassed by old data/copy/checkpoint.
7. `P-GC`: old source remains authoritative and unerased until relocation
   C_map.
8. `P-CHECKPOINT`: interrupted rollover selects complete old or complete new,
   never a mixture.
9. `P-EPOCH`: old-epoch outcome/completion cannot enter new firmware state.
10. `P-FENCE`: every successful fence obligation has a C_map/PLP witness.
11. `P-PLP`: durable-by-PLP envelope is complete, capacity-reserved and drains
    to C_map.
12. `P-CONSERVE`: symbolic free/live/stale/orphan state is conserved.
13. `P-NO-HOST-AUTHORITY`: removing volatile/decoded Host cache does not
    change recovery.

Every invariant has positive traces and at least one deliberately broken model
that emits a deterministic minimal counterexample.

## Deterministic recovery oracle

For one concrete cut, the oracle:

1. completes every durable B_phys record to its frozen C_phys or NO_EFFECT;
2. blocks old outcome delivery under reset/power profiles;
3. drains valid PLP envelopes in persistent order when the profile requires;
4. selects the highest complete firmware checkpoint and valid anchor;
5. replays only NAND/OOB mapping/tombstone/relocation records after its
   watermark;
6. accepts a write mapping only when data dependency, checksum, version and
   predecessor are valid;
7. applies tombstones before every lower-version record;
8. retains the GC source until a valid relocation record commits;
9. computes recovered-state hash and compares it with volatile/durable/fence
   observations;
10. never reads old firmware RAM, a decoded Host mapping cache or the physical
    outcome ledger to choose L2P truth.

B_phys freezes deterministic input/profile/seed/outcome, so one seed and
cutpoint produce one recovered result rather than a random choice from the
policy upper bound.

## C3.2 tiny-model gate

C3.2 uses an abstract in-memory geometry, not an NFC or FTL implementation:

- two logical atoms;
- three symbolic erase groups with three candidate slots each;
- two firmware checkpoint slots;
- at most two outstanding mutations and two in-flight physical operations;
- one- and two-atom commands;
- at least two controller epochs;
- no-PLP, validated PLP capacity two and PLP capacity-one exhaustion;
- reduced version widths that fail closed at exhaustion.

Every modeled persistent transition receives cuts for controller reset,
simulated SSD power loss and daemon/substrate crash across:

- data program then mapping commit;
- overwrite old/new;
- mapping commit then checkpoint/anchor rollover;
- volatile and durable trim;
- GC copy, relocation commit and old erase;
- default write followed by fixed-frontier fence;
- self-durable with unrelated prior work;
- PLP admission and interrupted drain;
- C_phys before outcome delivery and delivery before external success;
- multi-atom partial commit;
- old-epoch late C_phys with blocked delivery.

The model exhaustively enumerates the bounded transition space; random sampling
does not replace enumeration. Every failure reports the shortest deterministic
counterexample with profile, initial hash, event sequence, cutpoint, frozen
physical outcomes, recovered hash and invariant identifier.

The same trace and seed produce the same result and hash across runs. The gate
uses no filesystem image, raw block, VFIO, PCI, QEMU or protocol field.

## Still stopped

C3.2 does not implement or authorize NAND geometry, NFC operations, ECC/timing,
FTL mapping structures, journal/checkpoint/OOB encoding, GC policy, file/raw
media layout, protocol Flush/FUA/VWC mapping, raw media, real power-failure
claims, device DMA, IRQ, BAR, PCI or QEMU.

## Rejected alternatives

- Treat Host `fsync` as NAND power-loss durability.
- Treat simulator physical ledger or Host-decoded L2P as firmware authority.
- Use one generic COMMITTED state for physical, mapping and checkpoint commit.
- Return success without a named volatile/durable evidence class.
- Assume atomic multi-page or main-plus-OOB update.
- Let abort/reset imply rollback of an already applied physical effect.
- Allow GC to erase old source before relocation commit.
- Let a checkpoint make arbitrary volatile RAM mapping durable.
- Silently downgrade durable success when PLP capacity is exhausted.
- Use raw block or real media in the policy-model gate.

## Consequences

Command durability becomes an explicit, executable policy rather than an
accidental property of a provider or Host file operation. C3.1 can remain a
non-persistent lifecycle gate. C3.2 can exhaustively validate tiny crash states
without implementing NAND or an FTL. C3.3 supplies physical outcomes later,
and C3.4 integrates minimal mapping/media code against a recovery contract that
is already fixed.
