<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Architecture and implemented bindings

This page maps the current source onto the existing architecture; it does not
expand the frozen preview or historical C3/C4 evidence. Use the
[construction/status matrix](current-status.md) for selected capacities,
profiles and evidence, and the [source map](source-map.md) for code ownership.

## Layering

```text
┌──────────────── transport / trusted HIF ────────────────┐
│ PCI/config/BAR · doorbells · queue capture · IRQ        │
│ address-graph validation · bounded capabilities · CQE    │
└──────────────────────────┬───────────────────────────────┘
                           │ versioned async ABI
┌──────────────────────────▼───────────────────────────────┐
│ portable controller firmware                            │
│ protocol policy · lifecycle/dependencies · FTL/GC      │
│ metadata · checkpoint · recovery                        │
└──────────────────────────┬───────────────────────────────┘
                           │ NFC descriptor ABI
┌──────────────────────────▼───────────────────────────────┐
│ selected NFC model / future hardware adapter            │
│ physical operation · payload/result ownership           │
└──────────────────────────┬───────────────────────────────┘
                           │ physical PPA operations
┌──────────────────────────▼───────────────────────────────┐
│ persistent media                                        │
│ pages/OOB · erase generations · health · transactions  │
└──────────────────────────────────────────────────────────┘
```

The firmware source is portable C with explicit platform/HIF/NFC contracts. Native and ISA-target builds share source, not necessarily a binary. Only an ISS and endpoint implementing the same SoC profile can be expected to run the same ELF.

### Actual storage bindings

These are constructor selections, not automatic fallback chains:

| Binding | Protocol/lifecycle and Block consumer | NFC and physical medium |
|---|---|---|
| Tagged preview / ordinary native `worker` | Shared `command-spine` and reference `core/m3p` | C3 model in `nfc/`, file-NAND-v0 |
| Original scalable headless format 1 | Same profile/lifecycle seam, `core/ftl-scale` | Scaled C3 construction in `core/nfc-runtime`, original compact file-NAND-v1 qualification |
| Current scaled/large/MQ2 native defaults | Same profile/lifecycle seam, retained scalable FTL parents and format-2 windows | `core/nfc-page-v2` PAGE2-R0, physical NAND v2, explicitly selected mapped-tmpfs byte adapter |
| Explicit cooperative channel LAB | Same profile/lifecycle/Block and serial format-2 FTL | WAVE4 PAGE2 hub, independently owned timed NFC and physical-v2 shard per channel; [ADR-0020](adr/0020-cooperative-nand-channel-domains.md) |
| Explicit multi-head format3 LAB | Same profile/lifecycle/Block, physical head domains and ordered DATA-wave/MAP FTL | Same cooperative hub and real v2 channel shards; no OS threads/native selection; [ADR-0021](adr/0021-multihead-ftl-write-waves.md) |
| Explicit channel-worker LAB | Same format3 FTL and portable actors, optional construction-time executor | Cooperative/one/four Linux data workers over the same timed NAND and real shards; actual join before release, no native selection; [ADR-0022](adr/0022-channel-worker-execution.md) |
| Explicit independent-plane READ LAB | Same protocol/lifecycle and existing format2 parallel-read pool; readonly after normal preparation | Policy-selected channel actors: READ plane registers, whole-LUN mutations, same bus/media; [ADR-0023](adr/0023-independent-plane-read.md) |
| Explicit mutable READ/WRITE LAB | Same protocol/lifecycle, one writable format3 instance with disjoint existing pools and one Host parent | Same IPR actors/physical shards, cooperative or selected worker; existing multihead factory with PARALLEL option; [ADR-0024](adr/0024-mutable-format3-read-write.md) |
| MQ2 opt-in channel LAB | Same actual native constructor/loop and mutable format3 factory, no protocol/lifecycle rewrite | Process-lived strict POSIX channel volume and cooperative IPR actors; [bounded actual native x86-64 L1](results/2026-09-14-native-channel-l1.md), not new M5/thread/ARM-native evidence; [ADR-0025](adr/0025-native-channel-construction.md) |
| Same MQ2 channel option with `--nand-workers 1\|4` | Same semantic implementations; native-private prepare/release/wait composition | Fresh per-runtime workers, actual joins before zero/replacement, same process-lived media; [offline native-loop evidence](results/2026-09-14-native-worker-lifetime.md), not real threaded native/M5/NUMA or throughput proof; [ADR-0026](adr/0026-native-worker-lifetime.md) |

`j0_construction.c` binds a ready volume and its actual Block service;
`scale_storage.c` constructs the selected FTL/NFC pair. Both reside below
historically named `frontends/headless-*` directories, but they are also linked
into the native worker and are **production composition code**, not fake
storage. See the [source map](source-map.md) and
[ADR-0013](adr/0013-scalable-ftl-and-page-windows.md).

The original C3 model implements functional resource/timing, ECC/retry and
seeded-fault behavior. Its ticks are not calibrated physical NAND timing.
PAGE2-R0 is instead a one-slot functional batch executor: page/OOB, health,
generation and failure facts participate, but unsupported nonzero timing,
retry and injected-fault settings are rejected. A 64-page group is not 64-way
NAND parallelism. Neither the current path nor this diagram implies advanced
wear leveling or a real NAND/RTOS implementation.

The separately selected cooperative channel LAB uses a closed-batch JOIN and
monotonic admission floors around independent timed child engines. It does not
make R0 timed or the native FTL parallel. Worker/NUMA placement and actual plane
capabilities remain distinct from NAND address geometry.
The optional Linux executor transports PREP/RUN/RETIRE/CLOSE jobs, not FTL
commands. Its threads share no actor/media engine concurrently; only the
coordinator publishes JOIN results and retains the global token registry.
IPR is an explicit resource policy in that same NAND event engine, not another
thread hierarchy. Existing constructors remain LUN-exclusive. Its readonly
FTL consumer is not a silent extension of B3 or the native R0 binding.
The later mutable construction selects both FTL pools once, without changing
NAND/job/worker semantics. Parent ownership and active-pool maintenance exclusion
protect read snapshots; `parallel_reads` is distinct from readonly permission.
The MQ2 channel opt-in reuses this construction. Its physical assembly and
factory survive reset/NO_OWNER while drained volatile runtimes are rebuilt.
It defaults to cooperative jobs. The explicit Linux-worker option reconstructs
its executor after actual shutdown/join, not by reusing a process-lived worker
pointer. Native retains a separate resource association across NULL-J0 startup
failure and never calls J0/hub again after fini. Only a never-stepped runner can
return worker cleanup responsibility to that native owner; normal stepped
shutdown is unchanged. This later software integration has its own evidence.

Physical versions v0/v1 retain their own redo-based engines. Physical v2 orders
INTENT, physical homes and terminal COMMIT; interrupted reservations recover
to an explicit abort outcome instead of reconstructing payload redo. The byte
adapter still sits below PPA/main/OOB operations, never below a direct-LBA
shortcut. [ADR-0012](adr/0012-versioned-physical-nand-media.md) fixes this
version-specific persistence distinction.

## Safety and protocol truth

The trusted HIF retains raw submissions and all transport addresses. Before portable policy it validates only a maximum structural memory-safety envelope; it does not yet mint the final data capability. The firmware receives an address-free canonical protocol descriptor and remains the sole authority for protocol legality, actual transfer length and completion status. After firmware returns the exact transfer shape, HIF resolves the same immutable address capture and issues the exact command-scoped capability.

A capability binds exact range/direction plus controller instance, owner, reset, per-queue generation and command identity. Reset, unmap, queue recreation or owner change revokes it. Effectful use of an old capability cannot DMA, publish a completion/interrupt or mutate new firmware state. Idempotent old-owner cleanup may only reduce its old ledger and release old resources; it cannot resolve reused identifiers into new-owner objects.

The expanded owner/queue identity is HIF-private. Per ADR-0006, HIF binds it
into an opaque origin token; portable firmware interprets only its own instance,
controller epoch and command UID and never parses QID, CID or ring layout.

The historical Cycle 04 design distinguishes address-free policy from its
headless memory-transport reference. Doorbells, memory queues, data-pointer
graphs and physical completion placement are not transport-neutral. Its
`c4_command_graph_v1` and frozen C31/C35 implementations remain regression
references, not the current native executor. See
[ADR-0008](adr/0008-generalized-nvme-command-graph-boundary.md).

The earlier C4.3 `c4_command_graph_v1` implementation and ADR-0011 remain bounded
references, not the native executor. The current vertical path uses
`core/command-spine/spine_lifecycle.c`, two real profile adapters and aggregate
Block operations. GC, RMW, metadata and NAND child work stay inside the selected
FTL/NFC, not in the shared command graph.
The native PCI/HIF performs queue capture and completion publication through
`frontends/linux-m4`; no old whole NVMe/media fixture is linked into that path.

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

Physical NAND is not yet a drop-in backend. The reference M3P recovery consumes
NFC `final_erase_generation` in `core/m3p/m3p_recovery.c`; `nfc/nfc_media.c`
obtains it from file-NAND block-health state. The current scaled path consumes
the same fact through `core/ftl-scale/ftl_scale_nfc_v2.c` and
`ftl_scale_recovery.c`; PAGE2 obtains it from physical-v2's independent
persistent block records. It also participates in DATA/OOB identity checks.
A future raw-block byte adapter can preserve the simulated format and health
metadata, but that adapter is not implemented. Real NAND needs a
separate, still-open contract specifying who durably owns erase/wear generations,
how blank blocks recover them and how interrupted erases are reconciled. The
current simulator's explicit health metadata is not itself evidence of data
corruption, nor evidence of lossless physical migration by replacing one backend.

Historical decisions and frozen scopes are indexed in the [ADR index](adr/README.md).
The adopted development decisions are
[ADR-0012: physical NAND versions](adr/0012-versioned-physical-nand-media.md),
[ADR-0013: scalable FTL and PAGE2](adr/0013-scalable-ftl-and-page-windows.md) and
[ADR-0014: native profiles and serial-credit MQ2](adr/0014-native-profile-and-serial-mq2.md).
Their explicit refinements/partial supersessions do not rewrite old images,
source hashes or test results. Recording an implemented decision is separate
from freezing a new release.
