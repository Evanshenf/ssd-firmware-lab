<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0022: Channel-owned actors with a separate Linux execution transport

Status: accepted for bounded C at `bbadc16414c3fe6bb777216b6212d59531924a52`.
One independent exact-source confirmation returned NoRequired/STOP; see the
[result](../results/2026-09-14-channel-workers.md).
Implements C from [ADR-0020](0020-cooperative-nand-channel-domains.md), using
[B's format3](0021-multihead-ftl-write-waves.md), not native selection or D.

## Same NAND resources, different execution placement

One logical channel actor owns one timed NFC model and one physical-v2 shard.
One Linux worker may own all four actors, or four workers own one each. The
cooperative binding advances those same actors without OS data threads. The
four total DATA credits, NAND topology, timing, FTL and persistent bytes do not
change with worker count. LUN array state and shared-channel arbitration stay
inside the existing NAND engine. Die/package membership and plane addresses
do not independently create a thread or performance multiplier.

The existing hub's shared `run_one(h)` was not thread-safe. Extracting an
actor/job boundary is required; dispatching that whole function to threads or
putting a global mutex around media calls would not establish this ownership.

## Finite job and frame ownership

The [private executor](../../include/fwlab/private/nfc_channel_v2_job.h) has
submit, poll and shutdown. Each channel has one stable job mailbox, independent
of DATA credits. Only four commands exist:

| Command | Meaning |
|---|---|
| PREP | Raise the local admission floor and admit the channel's sorted input, without NAND stepping. All actors participate, including empty ones. |
| RUN | Advance the unchanged local engine and collect actual results. An empty actor has no NAND work. |
| RETIRE | Relinquish exactly the consumed reports/frames and return an acknowledgement. |
| CLOSE | Reset/drain the local actor after its frames and reports have gone. This is not OS-thread termination. |

Every PREP reply precedes every RUN dispatch. JOIN waits for actual RUN
reports and commits their maximum modeled time. Arrival order and thread IDs
cannot alter NAND scheduling inputs. Cached stats/trace getters never inspect
an executing actor. The existing finite trace/drop policy remains.

Frame ownership is:

```text
coordinator snapshot -> PREP grant -> actor execution -> immutable RUN report
-> caller take/discard -> RETIRE_ACK -> coordinator reuse
```

Returning a job does not implicitly return its frame. The coordinator retains
token/credit state and a copied result; caller delivery does not modify the
actor-held report. Workers never mutate the hub's global registry or counters.

`actor_job_step()` is the only job interpreter. Cooperative poll invokes one
bounded quantum; a worker loops the same function until its job completes.
Each quantum invokes at most one child step of budget1 or collects one result.
The hub budget bounds coordinator/cooperative work, not aggregate worker CPU
or the wall time of a synchronous backend callback. An unchanged asynchronous
poll is not progress, completion or zero ownership.

## Linux transport and finalization

[Linux runtime](../../frontends/headless-scale/nfc_channel_workers.c) owns
pthreads, atomics, condition variables, eventfd and optional affinity. Job
publication and reply consumption use release/acquire. Sleep predicates and
signals share a worker wake mutex; no wake mutex spans actor/media execution.

The completion eventfd is only a wake hint. The composition may wait only
after the hub has posted every job in the phase and has no local progress;
drain notification, recheck eligibility/visible replies, then bounded poll.
Waiting during partial dispatch could strand an unsent channel. Timeout
never manufactures an idle or drained certificate.

Reset closes admission but drains accepted DATA, required MAP, reports and
RETIRE_ACKs. Then actor CLOSE replies, worker STOP and actual `pthread_join`
must precede PAGE2 quiescence and arena/media release. An exited flag does not
replace join. Partial creation stops/joins created idle workers before ordinary
failure returns. Release before the first step does no physical IO and joins
idle workers. Failed joins retain ownership; no cancel/detach/forced free.

`scale_storage_options.channel_executor` is optional and valid only for the
explicit multihead factory. The caller owns its transport context until runner
release. The generic constructor imports no pthread header/library. Native
entries keep R0/format2 and do not link this Linux worker implementation.
Protocol/lifecycle/FTL algorithms, LAB event engine and physical-v2 sources
are unchanged by C. No on-media conversion is introduced.

## Scope and STOP

Close C after matched cooperative/1/4-worker real Block and J0 journeys,
byte-equivalent media/model outcomes, controlled real-call overlap,
constructor/accepted-close/ACK ownership, affected checks and one bounded
exact-source confirmation. No unbounded test framework or clean-review streak.

The [measurements](../results/2026-09-14-channel-workers.md) separate coordinator,
worker and process CPU from model/wall time. This small ordinary-POSIX workload
does not demonstrate a speedup. NUMA allocation/pinning, independent-plane
READ, mapped-shard cost work, native worker selection, vendor geometry/timing,
16-KiB pages, 4-TB scale and hardware generation persistence remain separate.
