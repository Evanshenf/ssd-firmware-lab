<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Native channel-worker lifetime: bounded software integration

## Source and scope

Source **`68a77302becc14147b45645a94867d80578f1df4`**, based on
`bb07fea008df8180beae1105936e016020c78c15`. Thematic commits are worker transport
`5b7d3429f59073c8665e035db534885d3a626abf`, native composition
`1ba02dccbc63da92eedc4b59c85ab1ea13358611`, then the existing fixture at the
source above. Ten source/build/test files changed. Portable FTL, NFC actors,
NAND media format/engine, lifecycle/protocol and kernel sources are unchanged.

[ADR-0026](../adr/0026-native-worker-lifetime.md) records the lifetime choice.
The existing MQ2 binary selects `--nand-profile channel-lab4k --nand-workers
1|4`: 64-MiB mutable format3/IPR, four strict-POSIX physical-v2 shards, unchanged
4-channel/1-LUN/2-plane/40-block/64-page geometry and synthetic unpaced timing.
Threaded `--owner-dir`, R0, non-64-MiB and invalid worker-count combinations
are rejected before device access. Cooperative and R0 defaults remain intact.

This result executes the actual native constructor and firmware loop with
**fake Host ioctls**. Data jobs, FTL/NFC and NAND media are real implementations;
thread/allocation wrappers impose the stated finite conditions and never
substitute actor results or successful joins. It is not an online kernel,
ARM native machine, M5, full-capacity or throughput qualification.

## Fixed observations

- A real thread's entry is held before startup ACK. Four PUMP service visits
  continue without READY, Host admission or early executor publication.
- Existing exact mixed I/O, Flush and recovery execute actual worker jobs
  through the same FTL/NFC/physical-media path. Real channel ownership is mask15
  for one worker, or one channel per worker for four workers.
- Real thread final return is held after worker exit notification. libc
  tryjoin actually returns EBUSY; PUMP service continues without publishing
  a closed epoch or releasing media. Actual final return and join follow.
- Two existing service-fault reset episodes reconstruct workers on the same
  volume and continue I/O. All predecessor threads actually join before the
  next creation. The ledger does not use pointer inequality as freshness proof.
  These resets are before first Host capture, not accepted-NAND reset races.
- Four-worker mode covers second-create EAGAIN and a separate pre-step NFC
  arena ENOMEM. NULL J0 retains native cleanup ownership; replacement and media
  close refuse until all1/4created threads actually join. No NAND job, child
  sequence/file-identity change or new epoch certificate occurs. Existing
  headless pre-step release still joins4 with unchanged media hash.

The one-worker fixture totals5creates/5joins; the four-worker fixture totals
25/25including failures, with zero blocking joins in the native path. No
runtime failure or DUT repair occurred. One initial fixture build failed on
a redundant include; it was corrected before the first runtime invocation.

## Executed matrix and resource budgets

Build host: Linux x86-64, kernel6.8.0-138-generic; GCC13.3.0, Clang18.1.3,
crossGCC13.3.0 and QEMU-user8.2.2. All15 invocations ran serially as an ordinary
user in fresh directories on an existing 1-GiB-capped local tmpfs. Sync, CRC,
locks and the NAND model were unchanged. Logs and programs stayed on disk.
Own successful fixtures were closed and identity-checked before cleanup;
unrelated retained images were not modified.

| Whole fixture | elapsed seconds | peak RSS KiB | result |
|---|---:|---:|---|
| GCC, worker1 / worker4 |0.26 /0.26|21108 /21088|PASS both|
| Clang ASan/UBSan+leaks, worker1 /worker4 |0.46 /0.45|95932 /104936|PASS both|
| Clang TSan, worker4 |1.14|81816|PASS|
| AArch64 static, qemu-user, worker1 /worker4 |0.79 /0.81|34656 /36720|PASS both|
| RISC-V64 static, qemu-user, worker1 /worker4 |0.76 /0.77|31248 /31616|PASS both|
| s390x static, qemu-user, worker1 /worker4 |0.94 /0.98|30548 /31736|PASS both|
| GCC existing cooperative / R0 native-loop |0.13 /0.11|20976 /99368|PASS both|
| GCC existing headless worker J0 / worker fixture |0.14 /0.72|12388 /12484|PASS both|

Times include controlled waits, initialization, recovery and fixture work.
They are **not bandwidth or speedup measurements**. RSS excludes tmpfs file
cache; tmpfs process recovery is not reboot/power-loss persistence. Cross
ELFs were actually run, but qemu-user does not emulate the native PCI/IOMMU/IRQ
stack. TSan disabled ASLR only for its own launched process.

GCC/Clang-sanitized/AArch64-static production MQ2 builds and the GCC legacy
worker build passed. Four CLI guard cases returned usage exit2. Source policy,
links, SPDX and REUSE641 passed. No hostedCI or public release is implied.

## Artifact identity and disposition

The preserved working-artifact manifests bind10source/build/test files,
31logs and12ELFs. They are not an external attestation or a bundled public
artifact download:

- source-set SHA256 `4cc363d3f1f5da1a0caa27e573a34bfe49b91b378d3b0bb68057af0f718002c6`;
- log-set SHA256 `ba6236366ad3903290dbf1ca62a1b31d5b93982d0f23755c666b04435715134e`;
- binary-set SHA256 `387206107091d6d72d5dd2f361526365b0f390e1493deccc787594ed7e118dc9`.

The single independent exact-source confirmation returned **NoRequired / STOP**
for this H and the five fixed obligations, with zero open findings. Its decision
record SHA256 is `8c9bb759fcdc01b4f00b7be5e9fa9b328e42e11b72ad525dbd93dc468737cf28`.
It checked10source files,31logs and12ELFs without rerunning the tests. This
software integration slice is closed; no additional discovery is required.
The preceding cooperative
[native L1 evidence](2026-09-14-native-channel-l1.md) keeps its own source and
scope; it is not relabeled as threaded native proof. Next is a separately
bounded real-native worker journey, not additional generic test infrastructure.
