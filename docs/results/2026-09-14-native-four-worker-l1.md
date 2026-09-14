<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Four-channel workers through native Linux L1

## Exact construction

Source `435c0b0ecc7182ea0fef33f9c22f90af0e5f4803`, with unchanged code from
`68a77302becc14147b45645a94867d80578f1df4`. This is the real-native follow-up to
the [software lifetime result](2026-09-14-native-worker-lifetime.md) and
[ADR-0026](../adr/0026-native-worker-lifetime.md), not another firmware engine.

The actual Ubuntu26.04/Linux7.0.0-30.30 x86-64 L1 `nvme` driver crossed M4/PUMP,
the existing MQ2/Linux profile, lifecycle/data mover, mutable FTL3, IPR channel
hub, **four real Linux data workers**, and strict-POSIX physical-v2 NAND shards.
The candidate selected `channel-lab4k`,64MiB and `--nand-workers 4`, without
`--owner-dir`. One coordinator and four data workers were observed. The same
four-channel/one-LUN/two-plane/40-block/64-page geometry,4KiB+128B pages and
synthetic unpaced timing were retained. No affinity or NUMA policy was added.

## One bounded native episode

All three fixed groups passed in one invocation, with no DUT correction or
operator retry:

1. Native binding/Identify and exact bounded I/O. NSZE/NCAP/NUSE were131072,
   with512-byte LBAs:67,108,864bytes. Six shapes covered512B,4KiB,8KiB with
   buffer+512,namespace-tail8KiB,and two1MiB transfers including buffer+512.
   The third write issued FUA; every write was followed by Flush. Ordinary,
   readahead and64continued small reads checked exact data.
2. One quiescent controller reset. The same coordinator remained; the four
   predecessor worker `(tid,start)` tuples were replaced by four fresh tuples
   after the old epoch drained and the successor became ready. Data verified.
3. Normal close, same-volume cold recovery, continued write/read, and normal
   final close. The original pre-existing lab instance, binaries and media
   were restored; its function remained unbound and its media hash unchanged.

Eight commands—Identify, initial write/verify, reset, post-reset verify, cold
verify, continued write/verify—returned actual exit0. Both candidate processes
closed exit0 with zero-reference drain records. Normal driver unbind also
caused runtime reconstruction: the format process went1→2(explicit reset)
→3(unbind)→drain3; the cold process went1→2(unbind)→drain2.

The task observations are not an exit-alone join proof. They combine with the
exact implementation and prior real-tryjoin software evidence to establish the
declared lifetime ordering. Cold module recreation uses a new function identity;
it is not an M5 same-function owner switch.

The Host exposed a128KiB wire limit, so each1MiB logical transfer used eight
commands. This is not unsplit1MiB NVMe-command evidence. FUA was exercised with
subsequent Flush, not as an isolated power-loss experiment.

One new bounded-tmpfs volume was formatted once, then reopened without format.
Its six files, global/child UUIDs and immutable manifest remained identical
across reset and cold recovery. Four physical images total89,214,976bytes plus
a1024-byte manifest and empty lock. The closed volume and evidence were
retained. Existing images and the independent read-only backing disk were not
changed; the latter's I/O statistics remained unchanged.

## Provenance and limitations

Native build used GCC15.2.0-16ubuntu1 and the installed4KiB-page kernel headers,
producer2/profile3. Clang21.1.8 was inventoried, not used. The retained warnings
are the compiler-name comparison,1448-byte native-ioctl frame versus1024-byte
warning threshold, and skipped BTF because `vmlinux` was unavailable.

The archive binds648source hashes, the selected profile, four actual ELFs,
operator/launcher and79record files. Preserved working artifacts are not a
public artifact download or an external attestation:

- native archive SHA256 `124d556212a0fe48a854aa6b2a4b3736158bcf364eb6a5d82b12a6fcc1eb7156`;
- record manifest SHA256 `e823882c0d325d45a578668cb211665e026b67dacfbb56a86909bd822ac926bc`;
- source manifest SHA256 `5790f7c2606daf990298e4455f5963ee6d1fee005dcee9bb44c9ecc9cecbd20f`;
- source/profile/four-ELF binding SHA256 `174d137b7eceb8231bbc4ebb1f6d2b746c584acbd8c2d5bc788f4d159430eab3`.

Before/after kernel snapshots were retained, but the ring moved; no
whole-episode warning-absence claim is made. Pre-existing taint12800 remained.
This result does not establish performance, peak RSS, speedup, NUMA locality,
wall-clock pacing, full-capacity/endurance, filesystem, accepted-NAND reset
races, ARM native channel operation, M5, real-NAND portability or disk/whole-
machine power-loss persistence. The one-worker native option has software
evidence, not a separate real-native run here.

The single independent exact-source host-evidence confirmation returned
**NoRequired / STOP** for the three fixed native groups, with no open findings.
Its decision SHA256 is `06e8815a922ddb5f3707725b18bbb6e757f2058d88c6822b6a9343bab8e52f29`.
It checked648source entries,79records and six bindings covering four ELFs,
without rerunning tests or making remote changes. This bounded native journey
is closed; no additional discovery or reexecution is required.
