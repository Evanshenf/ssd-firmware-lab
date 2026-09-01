<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Host-assignable SSD firmware design and M4/M5 graduation contract

> **Historical PoC review snapshot.** This document records the adversarial
> design review that followed the disposable donor experiment. The current
> public authority is [ADR-0009](adr/0009-upstream-vfio-route-and-milestones.md)
> plus [ADR-0010](adr/0010-linux-hif-portable-executor-contract.md). This file
> is useful context, not a claim that its gates are implemented or that this
> branch is suitable for merge into `main`.

- Status: adversarially reviewed design baseline; M4/M5 graduation remains HOLD
- Date: 2026-09-01
- Applies to: portable firmware, M3-P, Linux HIF, synthetic PCI, M4 and M5
- Does not modify: the active C4.3 worktree or frozen C4.1/C4.2 evidence

## 1. Purpose and claim boundary

This document defines the normal engineering sequence for turning the existing
software semantics and synthetic-PCI feasibility work into a Host-assignable
SSD firmware system.  It prevents three incorrect shortcuts:

1. a successful native `nvme` bind is not M4 graduation;
2. two successful Host→Guest→Host cycles are not M5 graduation;
3. upstream `vfio-pci` does not manufacture project-local DMA, IRQ, reset,
   media or stale-authority correctness.

The 2026-08-31 nested PoC establishes mechanism feasibility only.  Its minimal
kernel NVMe executor and ordinary-file media fixture are replaceable test
providers, not the portable firmware or M3-P data path.

The exact identity of that result is:

```text
core      = minimal in-kernel NVMe/media fixture
frontend  = synthetic PCI transport PoC
owner     = two clean, cold Host→Guest→Host cycles
trust     = trusted monolithic kernel fixture
evidence  = Profile-Nested
```

Core maturity, Host frontend, owner mode, trust profile and evidence platform
are orthogonal dimensions.  A later milestone number does not inherit an
unproven dimension from an earlier one.  The current `m4_frontend_services v1`
and `m4_media_ops v1` are `POC_INTERNAL_NO_COMPAT`: they have no compatibility
promise and must not enter Linux-profile-v1 or a support matrix.

## 2. Terms and profiles

| Term | Meaning |
|---|---|
| C4 fixed profile | Tiny transport-independent software oracle: 8 × 512-byte LBAs, 4-KiB transfer, queue depth four |
| C4.3 | Typed NVMe policy/control plus sole multi-action command graph |
| C4.4 | Exact owner-bound capability, bounded data movement and action witnesses |
| C4.5 | Headless integration of queue HIF, graph, data actions and completion intent |
| M3-P | Scalable 512-byte-LBA FTL/NFC/media block-action provider |
| B*-ABI | Versioned authority boundary between Linux HIF, portable executor and M3-P |
| Linux-profile-v1 | Real Linux-driver command/queue/transfer requirements; never retrofits the fixed C4 profile |
| M4-N | Bounded nested Host-native admission result |
| M4-B | Bare-metal Host-native graduation on a published support profile |
| M5-N | Nested upstream-VFIO assignment plus P7 canaries |
| M5-B | Bare-metal M5 graduation after M4-B and owner-cycle fault injection |
| P7 | Explicit cross-owner stale-authority canary matrix |

Project status words are restricted to `DESIGN_ONLY`, `POC_MECHANISM`,
`CONTRACT_FROZEN`, `INTEGRATED_FUNCTIONAL`, `GRADUATED_REFERENCE`,
`SUPPORTED_PROFILE` and `QUARANTINED`.  A test PASS does not implicitly promote
one status to another.

## 3. Target architecture

```text
                      L1 native Linux nvme owner
                                │
                                ▼
┌──────────────── trusted Linux HIF / transport ────────────────┐
│ PCI config/BAR · raw SQE capture · queue identity · FLR       │
│ PRP/SGL structural preflight · IOMMU/map/pin leases           │
│ capability minting · CQE phase-last publication · MSI-X/PBA   │
│ owner/reset/queue/mapping generations · quarantine            │
└──────────────────────────────┬─────────────────────────────────┘
                               │ B*-ABI v1
                               ▼
┌──────────────── portable NVMe executor ────────────────────────┐
│ typed request · protocol legality · command graph · Abort      │
│ action dependencies · terminal winner · completion intent      │
│ no QID/CID/BDF/PRP/IOVA/PFN/Linux pointer/file path             │
└──────────────────────────────┬─────────────────────────────────┘
                               │ typed block actions + witnesses
                               ▼
┌──────────────── M3-P portable storage firmware ────────────────┐
│ scalable L2P · allocator · Trim · GC/WL · metadata             │
│ journal/checkpoint · durability · recovery                     │
└──────────────────────────────┬─────────────────────────────────┘
                               │ NFC descriptor ABI
                               ▼
┌──────────────── NFC provider / future RTL ─────────────────────┐
│ channel/LUN/plane resources · timing · ECC/read-retry · faults │
└──────────────────────────────┬─────────────────────────────────┘
                               │ physical PPA operations
                               ▼
                      persistent NAND/media truth

M5 alternate owner path:

synthetic PCI → upstream vfio-pci → VFIO cdev/IOMMUFD → QEMU → L2 nvme
```

The Host-native and VFIO paths share one function and one controller state.
They are exclusive owners, never two simultaneously published controllers.
Every externally effectful operation carries or is checked against the full
identity tuple `(instance_nonce, owner_epoch, controller_epoch,
queue_generation, command_uid, mapping_generation, irq_route_generation)`.
Old work may release only its old ledger; it cannot move data, publish CQE,
signal IRQ/PBA or mutate a new generation.

## 3.1 Current PoC blocker inventory

The present mechanism PoC intentionally does **not** satisfy this production
architecture.  Before any M4/M5 wording, close at least these blockers:

- no implemented `owner_epoch` in the kernel transport;
- DMA requests carry raw IOVA plus direction/length, not an owner/reset/queue/
  command/mapping-bound capability;
- DMA admission does not yet check PCI Bus Master Enable at every start/effect;
- the software IOMMU is an xarray plus CPU `memcpy`, not requester DMA, IOTLB or
  hardware DMA-remapping evidence;
- cache coherency is unconditionally reported by the PoC provider and is not a
  calibrated platform fact;
- the MSI-X source injects a Linux virq but does not implement a complete
  per-vector PBA state machine;
- pending IRQ work has no owner/route/controller generation binding;
- FLR does not yet prove synchronized IRQ-work drain, complete config/BME/MSE/
  MSI-X reset and mapping/capability revocation as one owner fence;
- the function advertises a 16-MiB BAR while only a 16-KiB implemented aperture
  is actively modeled and scrubbed;
- the current disposable M5 script directly unbinds its known unmounted test
  namespace and does not implement a general mount/swap/dm/LVM/md/open-holder/
  udev/application shutdown transaction;
- the current M5 smoke opens a diagnostic VFIO cdev/IOAS/eventfd owner before
  starting QEMU, but does not create a separate diagnostic owner epoch and
  reset/zero-proof fence between those two authority grants;
- no cross-owner P7 IOVA-ABA, route-reuse or stale-CQE canary is authoritative;
- fixture `reset` and `stop` are void callbacks without a completion token,
  result, timeout or quarantine outcome;
- FLR currently invokes fixture reset under a config spinlock, while endpoint
  stop can run in the PCI rescan/remove lock interval; a future callback that
  sleeps or waits cannot reuse either call context;
- P2 historical wording mentions stale-epoch behavior although its DMA test has
  no epoch, and P3 wording mentions PBA although the test does not observe PBA;
- the current L2 profile uses `x-no-mmap=on` and does not prove revocation of a
  live VFIO BAR mapping.

These are expected PoC limitations, not bugs that upstream VFIO can repair.
Either implement and prove each fact or reduce the exposed profile/claim.

The local PoC branch is not the authority for C4.2 closure state.  Architecture
integration must bind the current mainline C4.2a reviewed-closure source and its
evidence child; it must not infer that status from this older feasibility base.

## 4. Normal development sequence

The dependency graph is deliberately not a single percentage-complete line:

```text
C4.2a reviewed closure
  → C4.3 policy/command graph ── freeze storage_action_v1 ──→ M3-P
  → C4.4 capability/data movement
  → C4.5 fixed-profile headless integration ───────────────→ Linux HIF v1
                     M3-P + C4.5 + Linux HIF v1
                                  ↓
                       integrated portable core
                                  ↓
                       M4-N → M4-B → M5-N/P7 → M5-B
```

M2/vfio-user remains an optional differential oracle; it is not on the main
graduation path.  The mechanism PoC stops growing protocol/media features now;
only safety, reproducibility and evidence defects may be fixed in that branch.

### Stage A — freeze software semantics

1. Close C4.3 policy, control transactions and command graph.
2. Close C4.4 bounded capability/data action semantics.
3. Close C4.5 headless end-to-end integration.
4. Preserve frozen C4.1/C4.2 source and claims.
5. Produce the finite C4 evidence contract before privileged Host work.

No PCI, VFIO or Linux-driver result can replace these software gates.

### Stage B — freeze the integration authority

Define executable fake-adjacent B*-ABI gates:

- Linux HIF with fake owner, IOMMU/mapper, IRQ/PBA and executor;
- portable executor with fake HIF effects and fake block provider;
- M3-P with fake command graph and fake NFC/media;
- owner lifecycle with fake Host-native and VFIO-assigned adapters;
- NFC and media with independently replaceable adjacent fakes.

Only after these gates pass may a real adapter replace a fake.

Each production boundary has exactly five fake-adjacent lanes: real producer
with fake consumer, fake producer with real consumer, hostile fake, reset at
every irreversible ownership point, and one real-real vertical integration.
Fake/reference code must not share the DUT decoder/state transition under test.

An evidence freeze preserves one result but promises no compatibility.  A
contract freeze fixes version, ownership, state machine, backpressure,
terminal/cancel/query, teardown reserve, quiescence, counter exhaustion,
threading and incompatible-version behavior.  A support freeze additionally
fixes the tested platform and operations profile.  An ops table alone is not a
contract freeze.

### Stage C — develop M3-P and Linux-profile-v1 in parallel

M3-P owns scalable FTL, GC/WL and persistent recovery.  Linux-profile-v1 owns
the real driver command subset, queue depths and transfer constraints.  They
meet only through the frozen typed block-action and witness interface.

Transport must not use a file or raw block offset as an FTL shortcut.

### Stage D — M4-N nested Host admission

Run the native Host driver through the real Linux HIF and portable executor,
but label all results `Profile-Nested`.  Exit only on exact-source cleanup,
reset, capability-revoke and negative-fault evidence.

### Stage E — M4-B bare-metal graduation

Repeat the complete path on a published bare-metal profile with the real
IOMMU/interrupt-remapping configuration.  M4-B requires the portable
executor→M3-P→NFC/media path; a kernel file fixture is prohibited.

### Stage F — M5-N and P7

Exercise nested upstream assignment and explicitly reuse old identifiers and
routes.  Successful cold cycles alone are insufficient.

### Stage G — M5-B bare-metal graduation

Start only after M4-B.  Repeat P7 and inject faults at every transition state,
including QEMU, runtime and owner-orchestrator death.

## 5. Owner state machine

```text
ABSENT
  → FUNCTION_PRESENT_NO_OWNER
  → NO_OWNER_RESET
  → NO_OWNER_CLEAN

NO_OWNER_CLEAN --fresh grant--> HOST_BOUND_DISABLED → HOST_LIVE
NO_OWNER_CLEAN --fresh grant--> VFIO_DIAGNOSTIC (optional owner term)
NO_OWNER_CLEAN --fresh grant--> VFIO_BOUND → GUEST_LIVE

Any live owner
  → SOURCE_FENCING
  → SOURCE_DRAINING
  → REVOKING(source)             [owner-revoke linearization]
  → NO_OWNER_DRAIN
  → RESETTING                    [controller-reset linearization]
  → NO_OWNER_CLEAN               [atomic zero-reference certificate]

Any unproved post-revoke fence → QUARANTINED
```

An active VFIO admission probe is an owner, not a harmless read-only check.
Production either omits it or returns through `NO_OWNER`, advances the owner
epoch, resets and proves zero state before QEMU receives a fresh Guest term.
`NO_OWNER` may remain indefinitely.  A timeout is never permission to grant a
new owner, and `QUARANTINED` is sticky until explicit offline recovery.

### Required linearization points

1. **close admission**: no new SQ capture, DMA start, CQE or IRQ enqueue;
2. **owner revoke**: advance `owner_epoch`; old effect authority is non-current;
3. **reset begin**: independently advance `controller_epoch`;
4. **queue/map retire**: advance ring and mapping generations;
5. **drain complete**: zero old DMA, pin, command, lease, CQE and IRQ work;
6. **route clear**: eventfd/vector/PBA and BAR-visible volatile state are clean;
7. **new owner publish**: only after every preceding proof succeeds.

Making old authority stale and publishing the new owner are separate events.
Every endpoint instance has an unpredictable `instance_nonce`; every revoke,
reset, queue creation, mapping and IRQ route creates a fresh generation.
Counter exhaustion or wrap fails closed.  A destination that fails after grant
is revoked as a real owner and cannot be relabeled in place as the source.

The zero-reference certificate is one transaction-consistent snapshot, not a
sequence of shell observations.  It covers: owner/admission/BME, commands and
provider operations, queue/CQE leases, DMA/map/pin/domain references, VFIO
cdev/VMA authority, IRQ work/routes/PBA/eventfd, media-owner references and a
cold BAR/controller digest.

## 6. Host shutdown and holder closure

Before unbinding Host `nvme`:

1. stop application writes and new opens;
2. issue filesystem/application durability barriers as applicable;
3. issue NVMe Flush and wait for completion;
4. unmount the exported synthetic namespace;
5. deactivate or close dm-crypt, LVM, md, multipath, swap and filesystem holders;
6. close namespaces/controller character-device users and admin tools;
7. prove zero mountpoints and zero holders through sysfs plus independent tools;
8. freeze ordinary admission and enter `QUIESCING`;
9. unbind the Host driver and wait for remove/workqueue completion.

This also covers other mount namespaces, raw block/character FDs, registered
io_uring files, loop/slave relationships and udev/automount races.  Normal
handoff never uses lazy/forced unmount.  An unknown holder returns `EBUSY`; the
default policy neither kills applications nor automatically remounts.

Guest→Host is symmetric: Guest application fence, Flush, normal unmount,
Guest shutdown acknowledgement, QEMU/cgroup reference closure, VFIO revoke,
reset and zero proof.  Guest panic, guest-agent loss or QEMU `SIGKILL` is a
dirty recovery lane, not a clean owner switch.

The private NAND/backend disk is never the exported namespace and must never be
mounted.  A script that confuses the two identities stops before any write,
discard, reset or unbind.

## 7. BAR and PCI configuration contract

- Every exposed BAR byte is implemented, reset/scrubbed or not exposed.
- BAR must not overlap System RAM or another resource.
- PAT/memory type aliases are forbidden.
- Config writes use explicit writable masks; BAR probing and command/status
  restoration are deterministic.
- FLR, controller reset and owner reset are distinct named operations.
- Reset preserves only explicitly persistent media/retention regions.
- MSE=0 suppresses all BAR and doorbell side effects.
- BME disable closes new DMA admission; re-enable creates a new generation.
- BME=0 suppresses both DMA and MSI/MSI-X memory-write effects.
- The function cannot enter VFIO while an uncontrolled Host driver is bound.

Nested BAR admission does not graduate bare-metal resource containment or
write-combining/order behavior.

## 8. IOMMU and DMA contract

### IOMMU association

- The endpoint has one deterministic requester identity and normal IOMMU group.
- `enable_unsafe_noiommu_mode` remains disabled.
- A fwnode-less software provider is a pinned nested exception, not a portable
  bare-metal association design.
- Competing providers or ambiguous group membership stop the adapter.

### Capability and map lifetime

An effectful data capability binds:

- instance and owner epoch;
- controller/reset and queue generation;
- command and DMA-admission generation;
- mapping/pin reference generation;
- exact byte range, direction and one operation lease.

Map objects are explicitly `LIVE`, `REVOKING` or `DEAD` and contain direction,
owner/generations, pin owner and an active reference count.  Direction names
are `DEVICE_READS_HOST` and `DEVICE_WRITES_HOST`, never an ambiguous `READ`.

DMA rules:

1. validate the complete range before the first byte moves;
2. acquire an exact map/pin lease;
3. check owner, controller, BME and mapping generation at every effect/retry;
4. unmap first closes new acquisition;
5. stop BME/engine, drain active references, complete required IOTLB/ATC
   invalidation, then unpin and permit IOVA reuse;
6. stale cleanup may release only old bookkeeping;
7. never convert an unvalidated supplied address directly to a page.

PRP/SGL structure and every mapping reference are captured as one immutable
command address graph.  Retrying a segment must not resolve a raw IOVA against
whatever domain happens to be current.  The portable executor receives only an
opaque bounded capability, never IOVA/GPA/HPA/PFN or a Linux object.

Cache coherency must be established by the exact platform profile.  A software
copy being coherent in nested x86 does not prove non-coherent architectures,
IOTLB ordering, ATS/PASID, P2P or requester-DMA behavior.

## 9. MSI-X, eventfd and PBA contract

- Vector allocation belongs to an isolated, device-specific domain.
- Setting an isolation flag alone is not evidence; delivery, mask, pending,
  unmask, reset and revoke are independently tested.
- Masked completion sets the exact vector PBA bit and emits no IRQ.
- Unmask consumes pending state and replays exactly once.
- CQE data becomes visible before status/phase; IRQ/PBA follows phase publish.
- Owner reset blocks new enqueue, drains IRQ/irq-work, clears route and PBA,
  then permits vector/eventfd reuse.
- Closing eventfd or unbinding VFIO alone does not prove project IRQ work gone.

Every route has owner/controller/route generation and a reference count.  Route
destruction orders: stop source, mask/disable, close new notify, synchronize
producer/IRQ/irq-work, clear PBA, detach eventfd, then free virq.  A bare virq
number is never retained across route life because Linux may reuse the number.

The software MSI source proves nested function, not physical interrupt
remapping.  M4-B/M5-B require a real platform IOMMU/interrupt-remapping tuple.

## 10. CQE and completion publication

```text
data visible
→ CQE non-phase fields visible
→ release/DMA barrier
→ status/phase published last
→ current-route IRQ or PBA effect
```

Completion intent is immutable and bound to owner/controller/ring generation.
It cannot publish into a recreated queue or a new owner's CQ slot.  Consume is
one-use and unknown responses retain a queryable tombstone.

## 11. Reset, teardown and quiescence

Every asynchronous provider supports:

- explicit acceptance/ownership transfer;
- exactly one terminal event;
- idempotent cancel;
- response-loss query using the same token;
- bounded stop/quiescence;
- zero-reference observation.

Formal reset/stop return a token and terminal result, have a finite timeout and
enter quarantine on an unprovable failure.  A boolean `quiescent` check or a
warning followed by free is not sufficient.

Transport teardown must call synchronous provider `stop` after workers stop and
before destroying bindings.  It must not rely on a coincidental final poll.

Potentially blocking executor/provider callbacks never run under config/IRQ
spinlocks or the global PCI rescan/remove lock.  The production adapter first
captures and fences state in atomic context, schedules teardown in sleepable
context, waits there, and only then publishes reset/stop acknowledgement.

Reset and teardown use protected, independently reserved identity domains so
ordinary counter/capacity exhaustion cannot prevent cleanup.

## 12. M3-P and media ownership

M3-P, not transport, owns:

- namespace capacity and LBA→PPA mapping;
- free-page/block allocation;
- GC victim selection and relocation;
- dynamic/static wear leveling;
- over-provisioning and emergency reserves;
- journal/checkpoint recycling;
- hot/cold separation and write-amplification policy;
- bad-block retirement and metadata placement;
- NAND page/subpage adaptation and durable recovery.

Transport and Linux HIF are forbidden from owning `struct file`, paths, VFS
operations, logical LBA→file offsets, FTL maps or durability policy.

The production media provider selects the private backend by stable WWN/serial
and a destructive-test allowlist, never by `/dev/sdX` numbering.  It refuses a
system disk, mounted filesystem, swap, holder or unexpected geometry.  Raw-disk
alignment, discard and Flush semantics belong to that provider profile.  The
exported synthetic NVMe namespace and private NAND/backend identity can never
name the same block object or be simultaneously exposed to Linux storage users.

## 13. P7 cross-owner canary matrix

At minimum, reuse each identity after an owner transition:

| Canary | Old action | Reused new object | Required result |
|---|---|---|---|
| IOVA | delayed DMA/read/write | same IOVA, different page | old effect is STALE/REVOKED; new page unchanged |
| mapping reference | delayed release/query | new mapping generation | old cleanup reduces only old ledger |
| QID/CID | old completion intent | new command with same QID/CID | no new CQE/data/IRQ mutation |
| CQ slot/phase | delayed publisher | reused slot and phase | old phase never becomes valid |
| MSI-X vector | delayed IRQ work | new route/eventfd | old eventfd and new counter unchanged |
| PBA | old masked pending | reset/reused vector | old pending bit cannot replay |
| BAR VMA | retained old mapping/write | new owner BAR/doorbell/table | access is revoked/faulted; new state unchanged |
| VFIO process | fork/dup/cgroup child holds cdev/VMA | QEMU exits | zero certificate blocks a new grant |
| Abort target | delayed target result | reused opaque reference | exact old ticket reaches STALE/TOO_LATE |
| media operation | delayed terminal | new owner command | old action cannot mutate media |

Run canaries across Host→Guest, Guest→Host, controller-only reset and failed
transition→quarantine.

## 14. Failure and recovery matrix

Inject failure at every owner state:

- application/holder refuses shutdown;
- Host driver unbind timeout;
- portable executor crash/hang;
- M3-P/NFC/media provider crash;
- QEMU normal exit, SIGKILL and host crash;
- VFIO cdev close before/after IOAS detach;
- IOMMU unmap blocked by a live lease;
- IRQ arrives during mask/route replacement;
- FLR fails or returns with nonzero volatile BAR state;
- host reboot during HOST_LIVE, NO_OWNER and GUEST_LIVE.

Recovery never guesses ownership.  If the durable/observable fence cannot prove
one safe owner and zero old authority, the function remains `QUARANTINED` until
an operator-controlled destructive recovery.

Before owner revoke, a failed preparation may restore the source only after a
fresh validation.  After driver teardown begins or owner revoke commits, there
is no in-place rollback: finish drain/reset/zero proof, then grant even the old
source as a fresh owner epoch.  A destination that was already granted and then
failed must itself be revoked.  NAND/media effects already accepted cannot be
rolled back; recovery only prevents them from producing a new-owner DMA, CQE or
IRQ effect.

## 15. Evidence and test governance

### Per-change development gates

- component unit and fake-adjacent tests;
- bounded state/model tests for affected invariants;
- strict GCC/Clang and sanitizers where applicable;
- architecture/dependency checks;
- targeted privileged smoke only when the mechanism changed.

Every permanent test records a requirement/hazard ID, claim level, independent
oracle, CI tier/budget and its addition/retirement trigger.  New permanent tests
are admitted only for a new requirement, interface/profile/platform change,
escaped defect or uncovered high-risk invariant.  Once a gate covers its finite
obligations, later defects reopen only their owning gate and add the smallest
regression.

Recommended budgets are: PR for static/unit/contract/mutation smoke; nightly for
sanitizers, bounded models, fuzz and cross-ISA; weekly for privileged nested;
release for bare-metal, owner-failure cuts, power and stress.  An unchanged
frozen component is identified by its source manifest instead of rerunning all
historical combinations.

### Candidate freeze

1. seal one immutable source commit;
2. build all artifacts from that commit;
3. run hosted unprivileged and privileged exact-profile gates in parallel;
4. hash source, config, compiler, modules, QEMU/initramfs and logs;
5. add an evidence-only child commit;
6. obtain adversarial review against the frozen claim wording.

Evidence output cannot feed back into production source or build recipes.
Canonical wire/trace bytes may be cross-compiler invariants.  Object/archive
bytes and GNU make internal databases are toolchain-dependent and are compared
only under the same frozen toolchain; they are not firmware semantic oracles.

### Required support tuple

- distribution/kernel package and source revision;
- config digest and compiler/linker;
- QEMU, vfio-pci and IOMMUFD versions;
- CPU/IOMMU/interrupt-remapping mode;
- synthetic module and portable executor identities;
- M3-P/NFC/media profile and format hashes;
- exact disk/controller/NAND identity and destructive-test policy.

### Formal M4/M5 gates

| Gate | Positive exit | Required negative canary |
|---|---|---|
| D0 | production capability, generation, state and lock-order contracts frozen | portable code importing BAR/IOVA/eventfd fails architecture gate |
| M4-G0 | deterministic Linux fwnode/requester/IOMMU association | ambiguous provider, RID or group fails closed |
| M4-G1 | complete BAR/resource/PAT/MSE/FLR lifecycle | MSE=0 doorbell and unimplemented aperture have no effect/leak |
| M4-G2 | blocked-default domain, exact group, map/pin/ref accounting | detached/wrong-domain DMA and partial map failure are contained |
| M4-G3 | immutable capability DMA with BME/direction/generations | pause old DMA, revoke, reuse IOVA, release old; new page is unchanged |
| M4-G4 | table/PBA/mask/unmask/route-generation lifecycle | reuse virq/vector/eventfd after queued old IRQ; new route is unchanged |
| M4-G5 | reset/unbind/unload-under-load and atomic zero certificate | cut at DMA-in/media/DMA-out/CQE/notify; no effect crosses reset ACK |
| M4-G6 | native Linux I/O traverses real C4.5 and M3-P | fixture path is absent and unsupported commands fail deterministically |
| M4-G7 | exact bare-metal synthetic reference tuple | repeat G1–G6 on physical Host with no warning/fault/leak |
| M5-G1 | graceful Host↔Guest holder/Flush/owner transaction | hidden holder, Flush error or preflight owner blocks handoff |
| M5-G2 | full bidirectional P7 authority revocation | reuse IOVA/QID/CID/CQ/vector/eventfd/VMA identities after revoke |
| M5-G3 | coordinator/QEMU/Guest failure recovery | kill at every state; result is one owner, NO_OWNER or quarantine |
| M5-G4 | bounded soak with stable zero certificates | repeated forced identity reuse has no resource growth or stale effect |

M4-G7 is still a software-synthetic profile: CPU-mediated copy is
`SW_CPU_MEDIATED`, not physical endpoint requester DMA.  Real PCIe endpoint,
hardware DMA/IOTLB/interrupt-remapping and link behavior form a later M6 gate.

## 16. Temporary headless backend migration

The local PoC may use a non-authoritative shadow backend pinned to the pulled
C4.3 Phase-2 commit `edaed32`.

```text
fixed synthetic prepare key (offline scaffold)
                  │
                  ▼
C4.3 Phase-2 shadow backend
  reserve/query/observer only; success_eligible=false

live native-NVMe fixture remains separate and authoritative for P0–P6
```

Rules:

- shadow output never changes data, CQE, IRQ or Host-visible status;
- no live SQE or observation mailbox is implemented at this checkpoint;
- source commit/profile is explicit and immutable;
- response differences are diagnostic, not authority;
- no live raw address enters the portable graph;
- cutover is prohibited until C4.3 policy and C4.4 capability/data contracts
  freeze and a C4.5 mailbox bridge passes fake-adjacent tests.

The executable scaffold lives in `experiments/m4-headless-backend-poc` and
reports `authoritative=0`.

### Source-of-truth receipt

Every bridge candidate records both histories:

- portable mainline closure: C4.2a candidate `4bb4e56` and its reviewed
  `TARGETED_CONFIRMED_CLOSED` evidence record;
- pulled C4.3 checkpoint: Phase-2 commit `edaed32`;
- mechanism donor: local PoC branch and exact transport implementation commit.

The local PoC was forked from an older mainline state and cannot overwrite or
reinterpret newer portable evidence.  Integration copies or links reviewed
interfaces only after provenance and semantic-diff review; it never merges a
dirty donor branch wholesale.

## 17. Graduation claims

The fixed software oracle and current Linux probe are intentionally different:
`Oracle-Min-v1` is 8 × 512-byte LBAs with a 4-KiB maximum and depth four;
`Linux-Probe-P0` is the 1-MiB fixture with depth 32 and up to 8-KiB transfers.
Neither is silently reinterpreted as Linux-profile-v1.  That profile freezes a
machine-readable trust/CPU/coherency/kernel/QEMU/IOMMU tuple, PCI and queue
shape, namespace/LBA/MDTS/alignment, command/feature/log support, unsupported
behavior, owner policy, filesystem scope, non-goals and revalidation triggers.
If safe mounted handoff is claimed, its M3-P capacity must support a real
filesystem and the profile must include mount/write/Flush/unmount evidence.

### Current source and claim matrix

| Item | Source of truth | Current permitted claim |
|---|---|---|
| C4.2a | mainline candidate `4bb4e56`, `TARGETED_CONFIRMED_CLOSED` | reviewed fixed queue/CQ/identity contract |
| local PoC C4.2 base | older feasibility snapshot | cannot override current mainline closure |
| C4.3 | mainline development; pulled Phase 2 `edaed32` | no graduation; shadow reservation/query only here |
| C4.4/C4.5 | not complete | no integrated portable claim |
| M3-P | not complete | no scalable FTL/NFC claim |
| P0–P6 | local Profile-Nested fixture evidence | transport mechanism and cold-cycle feasibility only |
| frontend seam | local source dependency split | no B*-ABI or drop-in compatibility claim |
| P7 | not complete | no stale-authority-free claim |
| bare metal/Linux-profile-v1 | not complete/frozen | no supported Host or assignment profile |

| Result | Permitted wording |
|---|---|
| synthetic mechanism PoC | exact nested mechanism feasibility only |
| frontend seam PoC | transport/executor/media dependency separation only |
| C4.5 PASS | fixed-profile headless software oracle only |
| M4-N PASS | exact nested Host-native admission profile only |
| M4-B PASS | Host-native support on the published bare-metal tuple |
| M5-N PASS | exact nested assignment plus P7 profile only |
| M5-B PASS | assignment support on the published bare-metal tuple |

Never state “M4 is done” after a bind, block-device read/write or nested cycle.
Never state “M5 is done” without P7 and owner-cycle fault injection.

## 18. Hard stop conditions

Stop or quarantine on:

- BAR overlap or memory-type alias;
- ambiguous device/backend identity;
- unsafe no-IOMMU or unsafe interrupt mode;
- untracked IOVA/map/pin or direct supplied-address dereference;
- live capability after owner/reset/map/queue generation change;
- stale CQE, IRQ, eventfd or PBA mutation;
- stale BAR VMA/doorbell mutation after owner revoke;
- inability to close mounts/holders or drain workers/providers;
- nonzero old reference at new-owner publication;
- reset failure, timeout, incomplete cold-state proof or generation wrap;
- concurrent source/destination access or preflight/QEMU sharing one owner term;
- kernel warning, lockdep report, IOMMU fault or unexplained transition timeout;
- transport-side VFS/LBA/FTL/durability truth;
- a second protocol or media implementation in QEMU/VFIO;
- a need to weaken a frozen claim/test solely to make integration pass.

## 19. Open design decisions

- byte-level B*-ABI layouts and mailbox ownership;
- Linux-profile-v1 Admin command and queue/transfer limits;
- scalable M3-P geometry, L2P cache, GC/WL and metadata layout;
- supported bare-metal IOMMU and interrupt-remapping profiles;
- dynamic provider/process lifetime leases and runtime death recovery;
- real endpoint/SoC selection and requester-DMA semantics;
- offline media-format migration between incompatible NAND geometries.

These decisions require separate finite gates.  They are not silently decided
by the mechanism PoC.
