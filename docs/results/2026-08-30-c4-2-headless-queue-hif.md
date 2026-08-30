<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C4.2 headless queue/CQ/identity result

- Date: 2026-08-30
- Disposition: **PASS for the fixed C4.2 software-semantic gate**
- Immutable source commit: `905a01e9e140a7bda2810db92118f5693b196ac1`
- Base/frozen C4.1 HEAD: `3d19bb58f54dbf08458454f971094ff324432656`
- Execution profile: unprivileged native tests plus user-mode cross-ISA runs

## What passed

C4.2 adds a caller-serialized, fixed-arena headless queue HIF around the frozen
C4.1 byte codec. The fixed topology is one Admin queue pair plus one I/O queue
pair, with no shared CQ. Physical SQ/CQ depths are 2 through 32 and usable
capacity is always `depth - 1`.

The HIF captures each 64-byte SQ value once, mints an opaque origin and
preallocates local reconciliation records, then asks an address-free command
port to reserve the future graph capacity and mint the graph-owned command
handle. The SQ head moves only after stable admission query proves the port
commit and the local generation-qualified CID map is installed.

A CQ publication acquires a graph lease only after proving CQ capacity, samples
SQHD at reserve time, prepares a stable cross-layer consume token, stages bytes
0..13 plus byte 15, and publishes byte 14 (status/phase) last. Physical marker
visibility alone does not release the CID or create a notification. Those
effects occur only after the command port proves consume committed. Host
CQ-head events in that reconcile window use a preallocated cumulative latch
and are applied atomically after the cross-layer commit.

Queue-memory capabilities, graph handles, HIF active generations, ring
generations, mapping generations and frozen action tokens remain distinct
domains. SQ delete uses PREQUIESCE to drain the last accepted Host tail. Reset
advances the epoch first and ends at `COLD_NO_QUEUES`; callers must explicitly
map, scrub, create and enable new Admin queues. Protected teardown can take
over RESETTING and remains available when the controller epoch is exhausted.

## Exit evidence

| C4.2 item | Evidence |
| --- | --- |
| create contract | malformed role/bytes/depth/association rejected without state change; unknown CQ scrub stayed PREPARED; same-token query/abort proved the terminal result |
| SQ capture/admission | single and batched tails, wrap, stale/same-tail no-effect, overrun/out-of-range reset-required, immutable capture across backpressure, duplicate CID rejection |
| identity | graph-owned handles, independent opaque origins, exact raw-copy seam, target-before/after-commit ordering, CID reuse before old CQ ACK, epoch-first target revoke |
| CQ reserve/publication | full check before lease, delayed and reverse ready order, reserve-time SQHD, 15-ordinal body, phase-last marker, unknown query and partial-marker poison |
| cross-layer ownership | physical marker before consume proof retained CID/lease; cumulative ACK and same-CID tail latched safely; notification and cleanup used independent records |
| cleanup fairness | fixed round-robin admission/ready/publication/consume lanes and per-ledger cursors allowed an unrelated command to complete while old graph cleanup remained pending |
| delete/reset/teardown | PREQUIESCE drained doorbelled SQEs; tombstone blocked QID reuse until ACK; 33 reset cut points; explicit Admin rebuild; teardown takeover and epoch-exhausted teardown |
| exact boundaries | depth 2 usable 1, depth 3 usable 2, depth 4 usable 3 and depth 32 usable 31; full-before-lease and ACK-resume/wrap checked |
| bounded model | 12 named families, 147 unique states, 190 complete transitions, maximum depth 8 and successors 4 under hard caps 32768/262144/20/8 |
| dynamic negative model | all 20 named broken variants found lexicographically first shortest counterexamples at depth 3; no cap hit |
| architecture negatives | eight temporary-source mutations detected: raw-derived origin, HIF-minted graph handle, missing stable query, cap/action interchange, shared generation, writable global identity, intent-derived context and raw default trace |
| deterministic fuzz | 64 executions and 4096 actions PASS; normalized value hash `575d55d4d7a84fda` |
| different-instance threads | 64 paired thread repeats PASS under TSan; same-instance APIs remain caller-serialized |
| compiler/analyzer matrix | GCC, Clang, conversion/shadow/prototype strict lane, ASan+UBSan, TSan, GCC `-fanalyzer` and Clang analyzer PASS |
| deterministic/cross-ISA | eight complete program outputs match GCC/Clang and native/AArch64/RISC-V/big-endian s390x |
| repository integration | root policy/layers/links/SPDX, root `make check`, frontends fake-link and REUSE 362/362 PASS |
| C4.1/C3 coexistence | all 17 C4.1 frozen paths remain byte-identical; C31-C34 archive and C35 canonical lane hashes remain unchanged |

The clean C4 build started with both C4 build directories absent. Full
`check-all` completed in 112.23 seconds with exit status zero. The subsequent
root `make check` completed in 20.82 seconds. These are functional test times,
not controller-performance results.

## Remote source CI

The immutable source commit passed both public GitHub Actions workflows:

- `c4-portable` run
  [33311999466](https://github.com/Evanshenf/ssd-firmware-lab/actions/runs/33311999466):
  success;
- `policy-smoke` run
  [33311999470](https://github.com/Evanshenf/ssd-firmware-lab/actions/runs/33311999470):
  success.

## Complete normalized program-output hashes

GCC and Clang produced identical complete output for each program before these
SHA-256 values were computed. All three cross targets matched the same native
output.

```text
c42_queue_unit         efbbdf7eafafd3707399d9c1e9355cbf8fb5557425b732e774346fc45ff5ae77
c42_publication_unit   56f7bfa457b378257d2c344950409017cb1d75d429f57b1804e25d1c2c9634ac
c42_identity_unit      d5853e4eec276210a0aab04f8370c71bdf65d5c202a85240d67ded8fe6ec43bf
c42_reset_delete_unit  9e61f807916192b26079090e47ab7f12214cabc2ed77a6a4bd56d4ceaa31d23c
c42_model              d00aa047c8f63503153308b278b505ae64f60445c901d3253bfb83467fd2b63f
c42_broken             0beb3d3b1dc5c858fe9c48fd0a11c96c2050d5e8f7247e7333bea6555fbaabb6
c42_fuzz               7d10127eb9894a1e43a7c98356e4e653500c8ef7777a09c4f49bb6f64c72cf9f
c42_fake_link          6fd3c0b32846a04f830e04f2629b2d03b301ee777d8b707466955d2a0394fd85
```

## Commands executed on the immutable source tree

```sh
make -C frontends/headless-c4 check-all
make check
reuse lint
make -C frontends fake-link FWLAB_FAKE_OUTPUT=/tmp/c42-frontend-fake
```

## Toolchain

| Tool | Version |
| --- | --- |
| Host GCC | Ubuntu 13.3.0 |
| Clang | Ubuntu 18.1.3 |
| AArch64/RISC-V/s390x GCC | Ubuntu 13.3.0 cross |
| user-mode runners | QEMU 8.2.2 |
| Python | 3.12.3 |
| REUSE tool | 2.1.0 |
| incidental Host kernel | 6.8.0-138-generic |

The Host kernel is incidental to this unprivileged software gate and is not a
kernel-compatibility or hardware result.

## Immutable source/test manifest

| Path | SHA-256 |
| --- | --- |
| `docs/legal/c4-2-source-boundary-review.md` | `a31b0df5e067abedce68b55d0f0d7fcf7a817bfaacfbcb21acdf94747fecf195` |
| `include/fwlab/contracts/hif_command_port.h` | `42670216147192d82e7edb4d154d2acd566731d2e6a7b031bcef6fafbef07519` |
| `frontends/headless-c4/hif/c42.h` | `0cb232dc8a16281a04e40cfc5db01fc96abb1f47faed3bc15e9bcf81e905dd77` |
| `frontends/headless-c4/hif/c42_internal.h` | `2b4a00bdbba8f1efe40a80a845492e8a1183fc186ceea207febfd953089907da` |
| `frontends/headless-c4/hif/c42_memory_port.h` | `0399355f7325b91b0f059bf92388328d0d168088e4df81d52ad57712af420a9f` |
| `frontends/headless-c4/hif/c42_identity.c` | `34c6ff9c2aee48f09605610368759aa4c1a9f63e519f0db7112e7fa5620991b8` |
| `frontends/headless-c4/hif/c42_queue.c` | `bb3053e03fa4a4b1f84d3452a3cdb330c48c2173346b139defa432355857fa7a` |
| `frontends/headless-c4/hif/c42_publication.c` | `a8c9f13266af12bcc1aca37b36fe519c6e634365d09ccb717d9531e7bd2eab77` |
| `frontends/headless-c4/hif/c42_runtime.c` | `429ad8c87bbffd376aa0e98f5791d117ca47d6ace633990f924703885d291990` |
| `frontends/headless-c4/fakes/c42_command.c` | `165eed7a6bac5f08c5f34e37dccb5fa3d0fe365819bdf22d35380b48f807c79f` |
| `frontends/headless-c4/fakes/c42_command.h` | `2e4d450da9ae274ae8986013cdde5a5db0f4218cb779d7e1af8ab31e24ba9cdd` |
| `frontends/headless-c4/fakes/c42_memory.c` | `01e00a2e07b3a07944ed3466b6f12d0fa3d00dd48826bb274e0c391342ffe124` |
| `frontends/headless-c4/fakes/c42_memory.h` | `e2e5f2a2056269c4e22f4da8bd65347637fb729d37e1c5192b67f3234b317179` |
| `frontends/headless-c4/fakes/c42_fake_main.c` | `e0479faa676ac64e4083b637ef8b965ffcc2ad019d9382ab60db49c1771594a1` |
| `frontends/headless-c4/tests/c42_support.c` | `d30daf39b5035abefe05465f55168c3bffd531cd09c1265d942c6a4393fed68f` |
| `frontends/headless-c4/tests/c42_support.h` | `a81a807396b3a27f32198fd2666bdcbf9a9321300c585e22e6203239fa05dda5` |
| `frontends/headless-c4/tests/test_c42_queue.c` | `426e53dbfd36c191fa146224baf86ad0f9ab7d1e4a7db8da13fe83884341802a` |
| `frontends/headless-c4/tests/test_c42_publication.c` | `7d2ed3ea48e198dd066e404b3c78c41043bef8c3c72a9a7a0a8ca548a6bb3795` |
| `frontends/headless-c4/tests/test_c42_identity.c` | `504d15ff5e03b23298f6709db916dc8fcdb6100d9103311a282559360cd099d2` |
| `frontends/headless-c4/tests/test_c42_reset_delete.c` | `4ec8050b602e9df6d5287e86eef07eba06a6b4d424889fa75df74a2b80670b2e` |
| `frontends/headless-c4/tests/test_c42_thread.c` | `6fc1d6ce28cbdb2cbb837d3665d93e05429548eede5374ef9d39d57020be7a94` |
| `frontends/headless-c4/tests/c42_model.c` | `94a2c29bf95fefdf6e72a5f4dd3a6bee360a2fb43962f576936fe4311bd307af` |
| `frontends/headless-c4/tests/c42_model.h` | `a401463e23a6d659f2464bd5a2d6b38b8d94ff36f8639588cd3f56aa210b0214` |
| `frontends/headless-c4/tests/model_c42.c` | `e7ccd0067e5f4674c4e91560b10015d4c337fb8e0e23816aad42f76dc64cf64f` |
| `frontends/headless-c4/tests/broken_c42.c` | `fbcef3fcacf6fcfd2ddeb2a4fb5a12e16c8aad4e3c3189f3dc315720658571b8` |
| `frontends/headless-c4/tests/fuzz_c42.c` | `f6edbcffc331830f1359f786c5d30c597dfbaaaa98b8980a05c2033dcfa50884` |
| `scripts/check_c42_architecture.py` | `37328141bdf485b9ec8a03477455c5622c01eae274624a0532f6dc901bd100d0` |
| `scripts/check_c42_analysis.py` | `bdb9bfddf04383e3279685b9c48463f54cf97be8be55fd9190f66bfd5de44fb2` |
| `scripts/check_c42_determinism.py` | `a64b41e8df69f18773d583ddcb986892a079af98c362e5317895aadba093f383` |
| `scripts/check_c42_cross.py` | `24f8ed722b02eb0039d9b729844153f84dffacb34c1427a59b175eca5378e57b` |
| `frontends/Makefile` | `b487f14657331a37102baafdd52b0e139abfde5e3600a68ee4cfcebd73db486b` |
| `frontends/headless-c4/Makefile` | `a1434df61c4ebeb6c45fe9832cf345557a6d7f285159c5f16119c42bbd34ee31` |
| `frontends/headless-c4/README.md` | `a310b71e319dffd91b2dfcf2cd8599a673bfc81fdd6a27ded3235b39fb465594` |

The evidence commit freezes the C4.2-specific production, fake, test, checker,
source-boundary and result files. Shared Makefiles and the cumulative C4 README
remain evolvable for C4.3-C4.5. All 17 `freeze.c4_1` paths remain independently
protected.

## Claim boundary

C4.2 is not a working or conformant NVMe controller. It does not implement or
prove:

- opcode/status legality, Admin command behavior, Identify data or AER;
- Read/Write/Flush execution, PRP/data DMA, namespace storage, C34/C35 binding,
  NAND/NFC effects or persistent media;
- PCI/config/BAR/MMIO, DMA ordering/coherency, MSI-X/IRQ, QEMU/vfio-user or an
  unmodified Linux NVMe driver;
- same-instance concurrency, weak-memory correctness, malicious/torn Host
  writes, 32-bit/freestanding execution, electrical timing or performance;
- NVM Express certification, endorsement or official recognition.

The next independent gate is C4.3 typed protocol/control policy and the real
multi-action command graph behind the address-free C4.2 port.

## AI assistance and human responsibility

- AI-assisted: yes
- Model family: OpenAI GPT-5.6, 2026-08-30
- Third-party implementation source copied or transformed: no
- Human/source/legal review remains required before a release or external
  conformance claim
