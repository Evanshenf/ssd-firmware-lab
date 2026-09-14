<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0026: per-runtime channel workers in native L1

## Decision and compatibility

Refine the cooperative-first construction in
[ADR-0025](0025-native-channel-construction.md), using the existing execution
transport from [ADR-0022](0022-channel-worker-execution.md).
The same MQ2 executable can opt in with `--nand-workers 1|4`, only alongside
`--nand-profile channel-lab4k` and the 64-MiB preset. Omission preserves the
cooperative channel path; omission of the channel profile preserves mapped R0.
No new protocol, FTL, NAND engine, media format or kernel ABI is introduced.

One worker owns all four channel actors; four workers own one whole channel
each. The channel remains the execution partition. LUN/plane resources, bus
serialization, array overlap and timing stay in the existing NAND scheduler,
not in a thread-per-plane or thread-per-die hierarchy. This selection neither
adds NAND resources nor guarantees a throughput multiplier. Affinity/NUMA,
vendor geometry and wall-clock pacing are not selected.

## Resource ownership

The locked physical volume and construction factory remain process-lived.
Each volatile J0 runtime gets a **new** worker executor. A retained native
runtime-media association exists before worker preparation and remains until
actual resource cleanup, even when failed J0 construction leaves no J0 pointer.
Final media close and replacement both refuse an unresolved association.

Linux-private resource hooks prepare, release and wait. Worker preparation
allocates primitives without threads; each startup step creates one thread
or collects one synchronized startup acknowledgement. Admission and the
factory executor are published only after all requested acknowledgements.
Snapshots cannot read unacknowledged startup fields.

Normal close keeps the existing order: FTL work drains; NFC actor jobs and
frame acknowledgements return; hub shutdown requests STOP and obtains actual
thread joins; J0 can then finish and release its arenas. Native destroys the
executor allocation before publishing `last_closed`/`EPOCH_DRAINED` and clearing
the association. After J0 fini, native must never step J0 or its old hub again.
An exit notification, timeout or observed exit flag is not a join certificate.

## The narrow pre-step exception

`scale_storage_options.executor_pre_step_cleanup_by_caller` defaults false
and is valid only with an explicit external executor. Its choice is copied
with the executor before arena allocation. If opted in and **never stepped**,
runner release frees unused FTL/NFC arenas but leaves actual worker cleanup
with the retained native owner. Constructors and format/recover-start only
select state; jobs are submitted by later runner advancement.

This is not a general abort API. Every runner advance entry records `stepped`
before invoking lower work. Once stepped, successful normal fini/close is
still mandatory; outstanding jobs cannot lose their arenas. Default headless
pre-step release still stops and joins its workers synchronously.

## Control service and scope

The polling executor requests STOP once and attempts at most one actual
`pthread_tryjoin_np` per shutdown call. EBUSY is pending, not an error or
progress; other join failures retain ownership. The old blocking convenience
APIs remain available to their existing headless callers.

Native services kernel PUMP between bounded startup, recovery and drain
turns. Only an explicit unchanged worker-lifecycle wait or the hub's real
all-posted external-wait state can avoid consuming the local iteration budget.
Short waits request at most 1 ms. Threaded startup/recovery/drain has a
60-second operational deadline: expiry reports unresolved failure, never
permits resource release or replacement. Allocation, pthread creation, mutexes,
join polling and OS scheduling have no hard realtime latency guarantee.

This first threaded native slice is **L1-only**. `--owner-dir` combined with
workers is rejected before device access. The existing owner grant calls
runtime creation synchronously; servicing PUMP cannot process peer disconnect
inside that call. No recursive owner socket polling is added. Async owner
grant/disconnect and new-construction M5 remain separate work.

## Evidence and stop

The [bounded software result](../results/2026-09-14-native-worker-lifetime.md)
uses the existing native-loop fixture, real worker jobs and real NAND media.
It covers startup service, mixed I/O/Flush/recovery, real join-pending service,
reset reconstruction and partial/pre-step construction failures. The older
default cleanup fixture remains a regression. One exact-source confirmation
closes this software slice; real native driver operation is a later journey.
No new test framework, large-capacity campaign, performance claim or media
conversion is part of this decision.
