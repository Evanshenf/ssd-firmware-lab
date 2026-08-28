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

The trusted HIF retains raw submissions and all transport addresses. It validates only the maximum memory-safety envelope from a single machine-readable schema and issues command-scoped capabilities. The firmware receives an address-free canonical protocol descriptor and remains the sole authority for protocol legality, actual transfer length and completion status.

A capability binds exact range/direction plus controller instance, owner, reset, per-queue generation and command identity. Reset, unmap, queue recreation or owner change revokes it. Old events may release old resources but cannot DMA, publish a completion/interrupt or mutate new firmware state.

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

Held asynchronous events, Abort, queue-delete barriers, reset acknowledgements and forced daemon cancellation use this state machine. Firmware produces completion intent; only HIF publishes the physical queue entry and interrupt, preserving:

```text
data visible → completion visible → interrupt visible
```

## Trust profiles

- `Trusted-Monolithic`: `vfio-user`, HIF and firmware may share a process. This is a functional baseline and makes no daemon-containment claim.
- `Isolated-B*`: kernel/HIF and firmware runtime are separate capability domains. Containment is claimable only after death, revoke, stale-event and bounds tests pass.

## Ownership transition

Normal Host-to-Guest transition:

```text
stop application writes; Flush, unmount and close holders
→ enter QUIESCING under a transition lock
→ allow only bounded old-owner teardown/control operations
→ request Host driver unbind and wait for remove/workqueue completion
→ close all Host doorbell/MMIO entries; owner_epoch++
→ mask interrupts and revoke capabilities
→ prove every old ref, pin and command token is zero
→ controller_epoch++ at reset-begin
→ force-cancel queues, perform destructive reset, receive reset-ack
→ bind custom VFIO, create a fresh IOAS, publish Guest owner
→ Guest enables controller and creates fresh queue generations
```

Failure to prove zero references enters `QUARANTINED`; Guest binding is forbidden. Guest-to-Host is symmetric. This is a destructive ownership reset, not live migration.

## Portability boundary

Portable: protocol/media policy, request/dependency logic that uses the fixed contract, FTL, GC/WL and recovery.

Replaced by hardware: PCIe link/config/BAR, requester DMA, queue walkers, completion writers, interrupt generation, NFC PHY and ECC/LDPC engines.

Platform-specific: boot, RTOS/runtime, linker map, interrupt controller, timers, cache/coherency and atomics.

Detailed decisions are frozen in [ADR-0001](adr/0001-system-architecture.md), [ADR-0002](adr/0002-power-domains-and-persistence.md), [ADR-0003](adr/0003-firmware-hardware-contract.md) and [ADR-0004](adr/0004-kernel-baseline.md).
