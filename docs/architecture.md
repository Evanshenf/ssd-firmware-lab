<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture baseline

## Layering

```text
┌──────────────── transport / trusted HIF ────────────────┐
│ PCI/config/BAR · doorbells · queue capture · IRQ        │
│ address-graph validation · bounded capabilities · CQE    │
└──────────────────────────┬───────────────────────────────┘
                           │ versioned async ABI
┌──────────────────────────▼───────────────────────────────┐
│ portable controller firmware                            │
│ protocol policy · lifecycle/dependencies · FTL/GC/WL    │
│ metadata · checkpoint · recovery                        │
└──────────────────────────┬───────────────────────────────┘
                           │ NFC descriptor ABI
┌──────────────────────────▼───────────────────────────────┐
│ NFC model / future RTL                                  │
│ resources · timing · ECC · retry · fault outcome        │
└──────────────────────────┬───────────────────────────────┘
                           │ physical PPA operations
┌──────────────────────────▼───────────────────────────────┐
│ persistent media                                        │
│ pages/OOB · erase generations · wear/bad blocks · WAL   │
└──────────────────────────────────────────────────────────┘
```

The firmware source is portable C with explicit platform/HIF/NFC contracts. Native and ISA-target builds share source, not necessarily a binary. Only an ISS and endpoint implementing the same SoC profile can be expected to run the same ELF.

## Safety and protocol truth

The trusted HIF retains raw submissions and all transport addresses. Before portable policy it validates only a maximum structural memory-safety envelope; it does not yet mint the final data capability. The firmware receives an address-free canonical protocol descriptor and remains the sole authority for protocol legality, actual transfer length and completion status. After firmware returns the exact transfer shape, HIF resolves the same immutable address capture and issues the exact command-scoped capability.

A capability binds exact range/direction plus controller instance, owner, reset, per-queue generation and command identity. Reset, unmap, queue recreation or owner change revokes it. Effectful use of an old capability cannot DMA, publish a completion/interrupt or mutate new firmware state. Idempotent old-owner cleanup may only reduce its old ledger and release old resources; it cannot resolve reused identifiers into new-owner objects.

The expanded owner/queue identity is HIF-private. Per ADR-0006, HIF binds it
into an opaque origin token; portable firmware interprets only its own instance,
controller epoch and command UID and never parses QID, CID or ring layout.

Cycle 04 further distinguishes the address-free policy from its headless
memory-transport reference. Doorbells, memory queues, data-pointer graphs and
physical completion placement are not transport-neutral. The generalized
`c4_command_graph_v1` will own multi-action protocol commands; frozen C31/C35
remain unchanged regression references and are not a Cycle 04 runtime
dependency. See [ADR-0008](adr/0008-generalized-nvme-command-graph-boundary.md).

C4.3 implements that separation through sanitized typed requests and the sole
`c4_command_graph_v1` outer lifecycle. Data-bearing commands remain held until
their later-profile witness mask is complete; a validation-only fake cannot
produce success. Queue/target integration uses a non-reentrant mailbox bridge
to the frozen C4.2 HIF. See [ADR-0011](adr/0011-c4-command-graph-v1.md).

## Command identity and lifecycle

```text
(instance_nonce, owner_epoch, controller_epoch,
 qid, ring_generation[qid], cid, cmd_uid)
```

- a rebuilt controller instance gets a new nonce;
- revoking an owner increments `owner_epoch`;
- reset-begin increments `controller_epoch`;
- queue creation/recreation increments that QID's ring generation before use;
- each submission gets a unique `cmd_uid`.

```text
ACCEPTED → DISPATCHED → HELD/RUNNING → CANCEL_PENDING
         → COMPLETION_READY → PUBLISHED → ACKED
```

This diagram describes the full HIF-plus-firmware path. The portable core owns
command state through immutable completion intent and a one-use completion
lease; HIF alone owns physical PUBLISHED/ACKED queue and notification state.

Held asynchronous events, Abort, queue-delete barriers, reset acknowledgements and forced daemon cancellation use this state machine. Firmware produces completion intent; only HIF publishes the physical queue entry and interrupt, preserving:

```text
data visible → completion visible → interrupt visible
```

## Trust profiles

- `Trusted-Monolithic`: a headless or optional `vfio-user` HIF and firmware may share a process. This is a functional baseline and makes no daemon-containment claim.
- `Isolated-B*`: kernel/HIF and firmware runtime are separate capability domains. Containment is claimable only after death, revoke, stale-event and bounds tests pass.

## Ownership transition

Normal Host-to-Guest transition:

```text
stop application writes; Flush, unmount and close holders
→ under the transition lock, close ordinary admission/enqueue
→ enter QUIESCING; owner_epoch++ makes old effect authority non-current
→ allow only idempotent old-ledger teardown/control operations
→ request Host driver unbind and wait for remove/workqueue completion
→ controller_epoch++ at reset-begin
→ retire queue/map generations; close doorbells; mask interrupt sources
→ revoke capabilities; cancel and drain DMA/commands/CQE/IRQ work
→ clear old routes/PBA; prove every old ref, pin and token is zero
→ perform destructive reset and receive reset-ack
→ bind upstream vfio-pci, create a fresh IOMMUFD/IOAS, publish Guest owner
→ Guest enables controller and creates fresh queue generations
```

Failure to prove zero references enters `QUARANTINED`; Guest binding is forbidden. Guest-to-Host is symmetric. This is a destructive ownership reset, not live migration.

## Portability boundary

Portable: protocol/media policy, request/dependency logic that uses the fixed contract, FTL, GC/WL and recovery.

Replaced by hardware: PCIe link/config/BAR, requester DMA, queue walkers, completion writers, interrupt generation, NFC PHY and ECC/LDPC engines.

Platform-specific: boot, RTOS/runtime, linker map, interrupt controller, timers, cache/coherency and atomics.

Detailed decisions are frozen in [ADR-0001](adr/0001-system-architecture.md), [ADR-0002](adr/0002-power-domains-and-persistence.md), [ADR-0003](adr/0003-firmware-hardware-contract.md), [ADR-0004](adr/0004-kernel-baseline.md), [ADR-0005](adr/0005-synchronous-ioas-copy-gate.md), [ADR-0006](adr/0006-portable-command-lifecycle-contract.md), [ADR-0007](adr/0007-command-durability-and-persistence-policy.md), [ADR-0008](adr/0008-generalized-nvme-command-graph-boundary.md), [ADR-0009](adr/0009-upstream-vfio-route-and-milestones.md), [ADR-0010](adr/0010-linux-hif-portable-executor-contract.md) and [ADR-0011](adr/0011-c4-command-graph-v1.md).
