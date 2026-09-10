<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0014: Construction-selected native profiles and serial-credit MQ2

- Status: Recorded implemented decision; integration/release validation is separate
- Date: 2026-09-10
- Implementation baseline: `a6ee009bbca5932857d51c3a5f265e0b60183a76`
- Refines: ADR-0009's shared endpoint/owner route and ADR-0010's HIF binding
- Preserves: exclusive ownership, one SQ consumer/CQE publisher, old profile limits and separate bare-metal graduation
- Related: ADR-0012 physical media; ADR-0013 scalable FTL/PAGE2

## Decision

Connect the scalable firmware path to native Linux and upstream VFIO/QEMU using
explicitly matched construction identities. Keep the same synthetic PCI
function and persistent media across owner transitions while allowing drained
volatile firmware objects to be reconstructed. Extend the Host-facing surface
to two queue pairs without implying two concurrent storage executors.

### Construction, not a runtime fallback

The kernel build chooses one producer and one fixed Host profile. The worker
attaches the same producer/profile, physical-media format identifier, media UUID
and implementation-binding digest. The attachment pins those values and
rejects incompatible retries; it does not negotiate arbitrary combinations.
The supplied binding digest identifies the selected build but is not, by
itself, independent proof of the bytes that were executed. Source and ELF
attestation remain separate release evidence.

The profile constants in
[m4_profile_native.h](../../include/fwlab/unstable/m4_profile_native.h) are:

| Host profile | Maximum I/O | I/O pairs / depth | MSI-X vectors | Large-frame admission |
|---|---:|---:|---:|---|
| 1 SMALL | 8 KiB | 1 / 32 | 1 | Legacy shared 32-slot metadata envelope |
| 2 LARGE_SERIAL | 1 MiB | 1 / 32 | 1 | One global I/O + one Admin |
| 3 LARGE_MQ2_SERIAL | 1 MiB | 2 / 32 | 3 | One global I/O + one Admin |

Profiles 2 and 3 use a 1-MiB I/O frame, separate 4-KiB Admin reserve, 4-KiB
controller pages, at most 257 captured data-page references and two PRP-list
pages. These are complete fixed contracts, not additive knobs. In particular,
MQ2 is **not** two Admin credits plus a pool of I/O credits per queue; two Host
queues share one global I/O frame/credit and the same serialized FTL/PAGE2 path.
Queue depth is Host ring capacity, not the number of admitted large buffers.

Namespace capacity is not a field of this transport limit table. The current
scaled native media constructor explicitly selects 131072 512-byte LBAs
(64 MiB) over 80 MiB of modeled main area. Recovered FTL volume authority still
supplies Identify/range policy. Selecting a large transfer profile does not
resize a namespace, and a larger backend alone does not create a larger one.

[ADR-0015](0015-capacity-presets-and-mapped-budgets.md) later parameterizes this
construction with shared capacity presets, preserving the default64MiB and
the same ready-volume authority. The transport limit table is unchanged.

The unqualified default kernel build remains SMALL/BAR and the original
`worker` target remains the historical reference. `scaled-worker`,
`scaled-pump-worker`, `large-worker` and `mq2-worker` are separate explicit
targets. No target failure selects a different profile or reformats media.

### One producer and one data path

BAR means the compatibility kernel polling thread invokes the HIF service.
PUMP means the firmware loop invokes the private bounded `FWLAB_M4_PUMP`
operation; the BAR thread is not started in that construction. Both use
`native_service_locked`, the same capture rules and the same publication code.
`NEXT` only transfers a retained capture; it is not a second queue walker.
PUMP service errors are distinct from attachment/transport errors and enter the
existing fault/reset handling rather than being mistaken for a lost FD.

The authoritative kernel object list is exactly
`m4_pci_main.o m4_pci.o m4_hif.o`, with the IOMMU object in its separate module.
`m4_hif.c` defines the strong `fwlab_authoritative_sq_consumer_v0` and
`fwlab_authoritative_cqe_publisher_v0` anchors. The historical `m4_frontend`,
`m4_nvme` and direct-LBA media fixture are not linked into this construction.
The new producer therefore changes who schedules the HIF, not who implements
protocol, FTL or CQE policy.

The actual scaled command journey is:

```text
Linux nvme (L1 or exclusive L2 owner)
-> m4_hif native_capture / retained NEXT
-> native_worker decode_capture / j0_runtime_admit_referenced
-> Linux profile + command-spine lifecycle + typed action drivers
-> native Host data mover / controller-buffer lease
-> Block service -> retained scalable FTL -> PAGE2-R0
-> physical NAND v2 -> mapped POSIX bytes
-> aggregate result / immutable intent
-> native_publish -> phase-last CQE -> per-vector MSI-X/PBA
```

Write DMA-in precedes Block mutation; Read Block completion precedes DMA-out.
Transport owns raw SQE/PRP/IOVA, mapping leases, queue/CID and IRQ routes.
FTL owns LBA mapping, OOB, GC, metadata and durability. The private media file is
never accessed by an NVMe logical offset in HIF.

### Queue and progress lifetime

MQ2 captures SQ/CQ incarnations and holders with each origin. Delete closes
the exact queue incarnation, can return accepted/in-progress while holders
drain, and retains its exact result for reply-loss retry. A repeated Number of
Queues request returns the retained paired count/DW0 result. It cannot silently
renegotiate a different topology beneath live queues.

I/O grant preference alternates between Q1 and Q2; a queue whose CQ has no
capacity cannot consume the only large frame and starve an otherwise runnable
peer. This is serial-credit admission fairness, not a concurrent FTL speedup.
Queue recreation receives a new incarnation; Delete CQ also retires only that
queue's IRQ route/PBA after pending work and holders drain. IRQ route generation
and pending state are per vector, so masking or retiring one vector does not
silently clear another queue's notification.

The worker uses private `advanced`/`runnable` progress from J0/FTL to decide
whether to apply its existing idle sleep. Budget consumption or an occupied
slot alone is not progress. Owner polling still occurs before the next retained
command is received. This signal is private scheduling information, not a new
public lifecycle observer ABI or a reason to skip reset/owner service.

### Same function/media, reconstructed volatile epochs

`native_scaled_media` is a process-lived holder, kept while its runtime is NULL
during reset or NO_OWNER. Its physical medium, exclusive lock, UUID, geometry
and factory outlive reconstructed J0/FTL/NFC runtime allocations. Final media
close requires the owner server stopped and the runtime finished; NULL runtime
alone is not permission to close a holder that a future grant will reuse.

Owner revoke closes old effect authority before drain. Grant follows the old
epoch's terminal/drained/zero certificate, not merely a driver unbind or IOAS
detach. Stable identity means the same PCI function/nonce, media UUID and
implementation bindings, not the same heap addresses or preserved volatile
controller state. Fresh nonce/epoch construction on recovery is intentional.
This refines the implementation of ADR-0009, not its exclusive two-LP rule.

## Compatibility and claim limits

- Old profile/source/image evidence remains valid only for its original scope.
  The new 64-MiB native construction does not rewrite the 1-MiB tagged preview,
  old C3/C4 oracles, or old 64-GiB headless qualification into MQ2 evidence.
- The selected native backend is the bounded mapped-tmpfs physical v2 path.
  Default `FWLAB_MEDIA_EXCLUSIVE=0` retains operation-boundary property checks;
  exclusive ownership and ISA-specific CRC builds must be selected and labeled
  explicitly. Neither changes FTL/NFC ordering or removes synchronization.
- A 1-MiB fio/application request is not proof of one 1-MiB NVMe SQE. The tested
  Linux Host can split it into smaller commands. Only evidence observing the
  actual SQE shape supports a wire-size claim.
- Native software transport was exercised on its named x86/Linux profile.
  MQ2 compilation requires the Linux 7 MSI parent-domain interface. Userspace
  ARM64 tests do not qualify ARM native M4/M5, arbitrary kernels, bare-metal
  requester DMA, RTOS or a real NAND controller.
- Two usable I/O queues do not imply parallel firmware, calibrated NAND timing,
  sustained whole-SSD performance, a 10-GB/s result or a whole-system single-CPU
  result. Layer microbenchmarks and native measurements keep separate labels.
- P01 physical-NAND generation persistence and the previously disclosed N01
  concurrent-FLR interleaving are not declared closed by this ADR. Named reset,
  rebind and owner-cycle evidence is narrower than all possible races. See
  [preview limitations](../results/2026-09-05-vertical-spine-preview.md#review-and-remaining-limits).

## Source anchors

- [m4_internal.h](../../kernel/m4-native/m4_internal.h),
  [kernel Makefile](../../kernel/m4-native/Makefile): fixed producer/profile
  guards, vector count and authoritative object list.
- [m4_hif.c](../../kernel/m4-native/m4_hif.c): `native_capture`,
  `native_service_locked`, `native_queue_effect_mq2`, `native_publish`,
  `native_owner_exchange` and the only SQ/CQE anchors.
- [m4_pci.c](../../kernel/m4-native/m4_pci.c): producer startup selection,
  `fwlab_m4_prepare_msix_vector`, `fwlab_m4_retire_msix_route` and PBA state.
- [native_attach.c](../../frontends/linux-m4/native_attach.c),
  [native_worker.c](../../frontends/linux-m4/native_worker.c): explicit attach,
  real runtime construction, command admission, reset and scheduling.
- [native_scaled_media.c](../../frontends/linux-m4/native_scaled_media.c),
  [native_scaled_media.h](../../frontends/linux-m4/native_scaled_media.h):
  process-lived physical holder and fixed-capacity storage factory.
- [frontends/linux-m4/Makefile](../../frontends/linux-m4/Makefile): separate
  production worker targets and explicit source inventories. The reply-loss
  test worker is a named test-only executable, not a performance binary.

This record captures the adopted architecture only. Rejected control-call
combining experiments do not change this profile. Release readiness requires
the separate exact-candidate checks and publication decision, not another
open-ended test or architecture framework.
