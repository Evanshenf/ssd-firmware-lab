<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Same physical NAND and FTL on cooperative, one and four workers

Source: `bbadc16414c3fe6bb777216b6212d59531924a52`; base `207700c`.
Portable actor commit `9af3ba5`, Linux transport/composition `c7939bc`.
Profile **WAVE4-LAB4K-C**; [ADR-0022](../adr/0022-channel-worker-execution.md).
One independent read-only exact-source confirmation returned **NoRequired /
STOP** for C's four fixed obligations. It verified16 source identities,18
selected log hashes and12 executable hashes without rerunning tests.
This is not a new release-wide freeze, native deployment or throughput win.

## What actually runs

The unchanged Linux-profile/lifecycle and aggregate Block interface use B's
format3 FTL. Its physical PAGE2 requests cross the same portable channel actor
implementation in all modes, then the unchanged timed NAND engine and four
ordinary POSIX physical-v2 shard files. CRC, OOB, health/generation, sync calls
and locks remain. Threads do not replace NAND with direct LBA memory access.

Four logical channels and four total group credits are fixed. Cooperative
execution has no OS data worker; one worker owns all channels; four workers
each own one. The tested page is4KiB main+128B OOB, with synthetic explicit LAB
timings, not a modern vendor geometry or wall-clock rate limiter.

## Fixed observations

| Observation | Result |
|---|---|
| Same Block workload | Actual1MiB write uses four physical DATA runs; subpage RMW, exact readback and actual GC pass. Then4MiB overwrite and8MiB verified read complete. |
| Model/media equivalence | Same complete channel stats, per-shard hash/sequence, frontier6, model time154967031ns and aggregate media hash `77befaaa65c1232d`. After close/join/reopen/final close, all six files are byte-identical between cooperative/1/4-worker modes. |
| Actual concurrency | A controlled first-DATA-program rendezvous records four distinct pthreads simultaneously owning independent real channel calls. It is a call-overlap witness, not parallel CRC throughput. No two callbacks enter one shard concurrently. |
| Partial construction | One actual pthread is created; the second create returns EAGAIN. The first is actually joined before ordinary failure returns no runtime handle. No media job is submitted. |
| J0 composition | One/four workers through the real optional-executor factory pass Identify,8KiB multihead writes, RMW,FUA,Flush and exact recovery. Closing during accepted two-head DATA drains DATA/MAP, reaches the existing zero certificate and actually joins workers; recovery reads the new data/frontier3. |
| Pre-step release | An initialized storage runner can be released before any step; all four idle threads join and the physical media hash is unchanged. |

The lower observation helper drains only pending ACKs after FTL parent/IO/
metadata/work are empty. Normal FTL completion intentionally leaves the last
MAP ACK for the next admission/reset; no new FTL idle callback was added.

## Execution and limits

Both real fixtures pass GCC, Clang ASan/UBSan and GCC ThreadSanitizer. Initial
TSan startup failed before the fixture with an unexpected mapping; a bounded
process-local no-ASLR launch then passed both fixtures, with no race report.
This is not proof of every possible interleaving or a global ASLR change.
Local tools were GCC13.3.0, Clang18.1.3 and QEMU user8.2.2 on x86-64 Linux
6.8.0-138; GCC's ordinary build used `-O2`, sanitizer builds used `-O1`.

AArch64, RISC-V64 and big-endian s390x statically linked executables were
checked for ELF identity and actually run under QEMU user mode. These are not
native PCI/kernel, ARM VM, NUMA or bare-metal results. B's real J0/Block and
A's channel J0 passed affected GCC checks; the core fake-adjacent channel suite
passed GCC/Clang sanitizers, including a failed shard and healthy sibling.
MQ2 compiled/linked only and was not started. Policy/links/SPDX/REUSE passed.

Media uses fresh directories in the existing capped1GiB local tmpfs, with
17,895,424B total shard image size and1MiB namespace per fixture. No existing
image, raw device, VM or slow-disk fallback is involved. Success images are
removed only after close and identity checks. A first observation-helper
failure and its image remain recorded; the fix changed only that helper.
The old core assertion that one tick retires an ACK was updated to require
actual actor-job acknowledgement, including no immediate credit recycling.

TSan and the RISC-V worker fixture briefly overlapped in orchestration, using
disjoint images; all other media runs were serial. This policy deviation is
not a throughput sample. Maximum observed individual process RSS100840KiB
excludes tmpfs page cache. No old large-capacity campaign was rerun. Local
tmpfs/reopen evidence cannot establish disk persistence or host power survival.

## Preliminary software cost — no demonstrated acceleration

One unreplicated x86-64 GCC subphase per mode uses4MiB write+8MiB read and
includes harness data fill/verification. Ordinary POSIX checks and syncs remain;
this is not the earlier optimized mapped ARM backend. The controlled overlap
rendezvous is outside this cost subphase. Model delta is72567602ns in all modes.

| Data workers | Wall ms | Coordinator CPU ms | Summed data-worker CPU ms | Process CPU ms |
|---|---:|---:|---:|---:|
| Cooperative (0) |79.391036|79.391160|0|79.391105|
|1|84.479613|37.364554|46.854287|84.218910|
|4|83.601207|44.418432|59.920107|104.338810|

These short samples do **not** establish a wall-time gain. Timers are sampled
sequentially, so rounded CPU components need not sum exactly. Four CPUs are
schedulable on one NUMA node; data workers are pinned/read back on CPUs0..3,
while the coordinator remains unpinned. CPU accounting does not hide it or
claim a whole-system single-core run. Exact values and per-worker CPU are in
the [recorded cost data](data/2026-09-14-channel-workers.jsonc).

Reproduce only in an already prepared capped local tmpfs, as UID1000, with at
least four allowed CPUs and sufficient RAM/space; run these serially:

```sh
make -C frontends/headless-scale -f ftl.mk check-channel-workers
make -C frontends/headless-scale -f ftl.mk check-channel-workers-j0
```

The first begins at Block/buffer, not an enlarged NVMe profile. Independent
plane READ, NUMA placement, native selection and vendor/16KiB/4TB performance
remain separate work. No thread-count multiplier,8GB/s or10GB/s is claimed.
