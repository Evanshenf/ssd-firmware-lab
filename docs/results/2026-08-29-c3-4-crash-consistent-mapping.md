<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3.4 crash-consistent mapping and ordinary-file media result

- Date: 2026-08-29
- Disposition: **PASS for the narrow C3.4 finite mapping/file-media gate**
- Immutable source commit: `9cc2f9093585e1dc382b93570a8cff536225bb6e`
- Frozen prerequisites: ADR-0002, ADR-0003, ADR-0006 and ADR-0007
- Evidence capture: `2026-08-29T16:00:53Z`
- Execution profile: unprivileged native tests, disposable regular files and
  user-mode cross-ISA runners

## What passed

C3.4 adds a private portable coordinator that makes one frozen C3.1 provider
operation own an entire firmware command graph. The graph drives one or more
frozen C3.3 NFC operations, turns exact physical completions and generation-
bound physical receipts into C3.2 facts, and asks the frozen C3.2 public policy
for the required volatile or durable witness before exposing one C3.1 terminal
event. C3.1 still owns the completion intent/lease, and HIF still owns physical
publication.

The finite firmware profile implements two 16-byte logical atoms, three
four-page data blocks, one recyclable four-page journal, two firmware
checkpoint slots, read/write/trim/fixed-frontier fence and one single-live-page
relocation. Its explicit little-endian page format uses a 96-byte main area and
64-byte OOB header with CRC-32C, monotonic logical version/state/record/commit
identities and exact predecessor/data dependencies.

Recovery scans raw C3.3 page/OOB and block truth, selects a complete firmware
checkpoint/anchor pair, replays strict journal successors and derives L2P/P2L.
It has no parameter for pre-crash RAM, a Host cache, decoded file metadata or
the simulator physical WAL. A relocation destination remains orphaned until
its journal commit; source erase is allowed only after relocation authority
and source-lease release.

The new ordinary-file backend implements the existing `fwlab_nand_media`
contract plus a C3.4-private operation binding. Its exact 64-KiB image contains
two physical superblocks, two self-contained physical checkpoints, three
fixed WAL segments, double-buffered PPA candidates and double-buffered block
health candidates. Physical BEGIN, APPLIED/NO_EFFECT and COMMIT records are
explicitly encoded and replayed. The physical format contains no decoded
logical mapping.

## Three truth domains stayed separate

```text
firmware NAND pages/OOB
  DATA + MAP/TOMBSTONE/RELOCATION + firmware CHECKPOINT/ANCHOR
  -> firmware recovery -> derived L2P/P2L

physical file container
  raw page/OOB/health candidates + physical B/A/C WAL + physical checkpoint
  -> physical recovery -> raw page/OOB/health projection

Host/HIF
  opaque command origin + C3.1 completion lease/publication
```

The physical file parser never includes the firmware decoder and never emits
L2P. The firmware scanner never receives a file WAL record. Deleting volatile
overlays and rebuilding a new coordinator instance from the raw projection
preserves the recovered firmware result.

## Command and durability evidence

The directed coordinator suite passes:

- cache-disabled durable write, NFC readback, durable trim and absence read;
- cache-enabled no-PLP volatile success followed by background C_map drain;
- a fixed-frontier fence over a retained volatile obligation;
- explicit rejection of a validated-PLP profile at this gate;
- dual-atom firmware checkpoint/anchor and journal erase/recovery;
- one deterministic single-live-page relocation followed by safe victim erase;
- controller reset that discards an uncommitted same-epoch overlay and blocks
  stale delivery.

The integrated crash-conformance suite uses the real C3.1 implementation,
C3.2 public witness functions, C3.3 NFC model, C3.4 coordinator and file
backend. It cuts every B/A/C barrier in DATA and MAP physical transactions:

- a DATA candidate or DATA C without MAP remains an orphan and recovers old;
- a durable MAP BEGIN is settled by physical restart and recovers new;
- volatile success cuts recover only old or new;
- acknowledged self-durable success always restarts at new.

This is 17 directed C3.2/file conformance cases. It does not assume
multi-atom command-wide crash atomicity.

## Ordinary-file safety and restart evidence

The POSIX adapter receives an already-open fd. Formatting requires a newly
created, empty, unlinked regular file; restart requires the exact 64-KiB size.
Linked existing files, wrong sizes and directories are rejected. There is no
path open, raw block/character device, ioctl, discard, mount or mmap path.

The full POSIX integration test runs:

```text
new anonymous regular file
  -> C3.1/C3.2/C3.3/C3.4 self-durable write
  -> duplicate fd and close original
  -> rebuild file media, NFC, coordinator and lifecycle instances
  -> raw firmware recovery
  -> C3.1/C3.3 readback of the original 16 bytes
```

All modeled crash tests maintain separate working and stable byte images.
Each barrier publishes the modeled stable image; restart uses a new substrate
object. The test never kills a process or guesses what a filesystem retained.
A focused independent test decoder verifies the selected physical checkpoint,
the first WAL BEGIN and its selected candidate without calling the DUT file
decoder.

`fdatasync()` is only the POSIX substrate barrier. POSIX permits a successful
positioned I/O call to transfer fewer bytes than requested, so the adapter uses
complete `pread`/`pwrite` loops. Neither fact makes Host synchronization a NAND
program, C_phys, C_map, durability fence or PLP guarantee.
([positioned I/O](https://man7.org/linux/man-pages/man2/pwrite64.2.html),
[POSIX fdatasync](https://man7.org/linux/man-pages/man3/fdatasync.3p.html))

The B/A/C complete-marker and idempotent replay rules were behaviorally
calibrated against general journal commit/replay principles, not copied from a
filesystem implementation.
([Linux ext4/JBD2 documentation](https://docs.kernel.org/6.17/filesystems/ext4/journal.html))

## Deterministic outputs

```text
C3.4 firmware codec/recovery unit: PASS (3 cases)
C3.4 coordinator flows: PASS (7 cases)
C3.4 memory/file replaceability: PASS
C3.4 C3.2/file crash conformance: PASS (17 cases)
C3.4 full POSIX file restart integration: PASS

C3.4 bounded integration model: PASS
  families=13 states=138 cuts=414 terminals=13
  invariants=0003ffff hash=740bf45bf09f289e

C3.4 broken integration variants: PASS
  shortest-counterexamples=18 depth-sum=40 hash=d30fb7acf83193fb

C3.4 file contract: PASS (2 cases)
C3.4 file crash/recovery: PASS (11 cases)
C3.4 POSIX disposable-file adapter: PASS (2 cases)

C3.4 file persistent-prefix model: PASS
  families=4 prefixes=257 restarts=257 cuts=771
  hash=ea03610d91cd230a

C3.4 broken file variants: PASS
  shortest-counterexamples=16 depth-sum=16 hash=6a93980261b7ea13
```

The integration model exhausts every reachable action subset in 13 closed
scenario grammars and injects controller-reset, modeled-power-loss and
daemon/substrate cuts at every canonical state. It abstracts the internal NFC
scheduler already frozen by C3.3; actual directed tests use the real C3.3
implementation.

The file model checks all 257 byte prefixes, including the complete record, of
the first WAL BEGIN against a staged candidate and restarts each image. A torn
final record is ignored only when its complete envelope is absent and every
later WAL location is erased. A complete corrupted record, an interior torn
record, a bad selected candidate, a bad selected checkpoint/superblock chain
or a commit-order/reference error fails closed. This is not a claim that every
byte in every possible long image was exhaustively corrupted.

## Portability and analysis

The portable program outputs are byte-identical under native x86-64 and:

- AArch64 GCC 13.3 through the AArch64 user-mode runner;
- RISC-V GCC 13.3 through the RISC-V user-mode runner;
- big-endian s390x GCC 13.3 through the s390x user-mode runner.

The cross gate covers firmware codec/recovery, actual C3.1/C3.2/C3.3 flows,
file crash conformance, positive/negative integration models and positive/
negative file models. The POSIX real-file test remains a native Host test;
the byte-image engine itself is cross-ISA.

GCC and Clang strict builds, ASan+UBSan, TSan, GCC `-fanalyzer` and Clang
static analysis produced no finding. Root repository policy, layer fake links,
relative links, SPDX checks, `git diff --check` and REUSE 3.0 lint passed.

## Toolchain

| Tool | Version |
| --- | --- |
| Host GCC | Ubuntu 13.3.0 |
| Clang | Ubuntu 18.1.3 |
| AArch64 GCC | Ubuntu 13.3.0 cross |
| RISC-V GCC | Ubuntu 13.3.0 cross |
| s390x GCC | Ubuntu 13.3.0 cross |
| user-mode runner | 8.2.2 |
| Python | 3.12.3 |
| REUSE tool | 2.1.0 |
| incidental Host kernel | 6.8.0-138-generic |

No root privilege, KVM, debug machine, physical SSD or raw media was needed.

## Source and test manifest

| Path | SHA-256 |
| --- | --- |
| `core/c34/Makefile` | `2d974c3c0e6603e6ea79382ca69e66dfcf4b4694262f684d35ccc420cf9c0181` |
| `core/c34/README.md` | `c61d99343c1724bb465ca7177037532c7d69403009a8ce8473e517f2c21e5ced` |
| `core/c34/c34.h` | `ebffc12b3fb802f29d6cc81c31a7c514fdac15b1e9331812e17c2a0202480a2e` |
| `core/c34/c34_checkpoint.c` | `881c2310e3ac690d6af84a6f6c1bf3600d4faf520d704bb62b0ace35ddc54a8e` |
| `core/c34/c34_codec.c` | `a1c269afeb275b42a51052898232d896d55e261579bfc948efbb9a96165f0992` |
| `core/c34/c34_coordinator.c` | `6b1aa52784e4b896ec307f1d6fb791ac3006e56444173f39476cd77489a0d648` |
| `core/c34/c34_drive.c` | `428a06d97c6fa15f7930435e4b1a4afea1f8f65336145de37fbc95f2e2777214` |
| `core/c34/c34_internal.h` | `2a712b30e2b9401169446e4a91e8206a854c3011fade2627561d7cad3cab6dfb` |
| `core/c34/c34_journal.c` | `1c5783b982d31527eb010e967130671730f691a2633a21ac1d2efa76914d7a57` |
| `core/c34/c34_mapping.c` | `20abb026f1ecbeb85f19a3cfd80efd969e572e26290747655d1bc07c828e44d9` |
| `core/c34/c34_nfc_graph.c` | `a20b76a216f8406dfa33956adcf8fa516e1635561eece5924857cccefbab83e1` |
| `core/c34/c34_provider.c` | `32a9e82c16af4b3bcb3f531103906ea830c19f99275468973aceb79ead5a9149` |
| `core/c34/c34_recovery.c` | `5eaa2475d3cbdab9a6682cb23d826a869eef7ee35f48c95f77a887275fd5f592` |
| `core/c34/fakes/c34_buffer.c` | `b0b396dd92465cd295fffc283985bd98cd7af36d918ba3ea1b0915ed3926d8bc` |
| `core/c34/fakes/c34_buffer.h` | `a6d29752e0a19efe455b586487cc0d59b3b88aac362ee039068ced5a5c413bb3` |
| `core/c34/fakes/c34_fake_main.c` | `514e40863d70455dfc0bd82330de24381eb24611ad9c2183eb93778db9cf216e` |
| `core/c34/fakes/c34_memory_media.c` | `4eb823390ff881def6e3ca188e8403520a8639006815f168d9909826be216005` |
| `core/c34/fakes/c34_memory_media.h` | `ef27af4ec7028841a8a98bb864e693414eed38df08f064ba1fb7dfbf70510202` |
| `core/c34/tests/broken_c34.c` | `d2bc590e2cdf7442cadae0bf8d8fb864fff8cff782581a701fb7d93bb75e4936` |
| `core/c34/tests/c34_oracle.c` | `ff164327ed2c3ada5970536432892b592b965f7087704812a44875086ba18371` |
| `core/c34/tests/c34_oracle.h` | `80d3b5000fea5b1e38f403a74f9597246e5b176dddd0acb3b797f100553deced` |
| `core/c34/tests/c34_test_support.c` | `6e1461f6c24dcfe4361d9deb20be65731cc0a5fb06bbdca6f655ec033f0b2f0a` |
| `core/c34/tests/c34_test_support.h` | `6c8542450605fecbc39f0960401ab1b5bdbde12cd36564425130be5acf817a71` |
| `core/c34/tests/model_c34.c` | `50c755f0bac55c7e93e1ea75faf042feaa79bf4535fbdf07c4d3e318785ed50a` |
| `core/c34/tests/test_codec_recovery.c` | `11962083789d21cda2b723d1825bf669647c1e3edc5be250e90df8ae51ffe909` |
| `core/c34/tests/test_crash_conformance.c` | `9ec3ac461307e8d2bd16f8645ba97a53f5f7c2649cbb65b6901401dbebcf632d` |
| `core/c34/tests/test_flows.c` | `67a08dd47db5d977791eac55de187509feb0ca2e42eb2d60aefd1c5ee6039698` |
| `core/c34/tests/test_posix_integration.c` | `597a3155b52e0dea88422c462eadc38158d08a3a3ed4770968c24ab79cdb591e` |
| `core/c34/tests/test_replaceability.c` | `d3a8faf400a4df827c47ae1495e9f27d1d1add1dbcc017fbcb5f8e47f601da2f` |
| `include/fwlab/private/c34_physical_txn.h` | `5425052834768e25f1a137dd297e025c2603c9951c445fd102606162160231d9` |
| `media/Makefile` | `93a89a5f32805dadefbb9dcd7a87d36f515fb7ccc38625d12cd1dfeee3104db7` |
| `media/README.md` | `416683fd2e891a10b299f880bededbc1071e7dd1d85f8bfc86ae18daea09a048` |
| `media/c34-file/Makefile` | `6d635ebfda7e3b4992f425b792624cf9c7d53cbb86e0e0ad3d9ae01cbbfbb4b6` |
| `media/c34-file/c34_file_codec.c` | `39bf93ebdde1a23b8e4989f53ec032fb8934fa02bb8b6e221f030225164bf2f3` |
| `media/c34-file/c34_file_engine.c` | `835b54ba56b3008ba46427c85d67426a266b0361d43138f17d7092900dcfc5e0` |
| `media/c34-file/c34_file_fake_main.c` | `b7907827ee0d9640c4dd7e2324ef50bfac716356a918b80a8834bc43646cb25c` |
| `media/c34-file/c34_file_internal.h` | `de759c311b8186927cde34e47f60f2e0ae402f0890556abf98dd7f99ca56601f` |
| `media/c34-file/c34_file_media.c` | `486839b605f6884e786885c44f62e0c87458227d6a4d4fc7284aad4e6a5fc3b7` |
| `media/c34-file/c34_file_media.h` | `3ddb087d707c8d72a90f843398bf340c5d2c8efac52aeba6ef1aaebd65402db6` |
| `media/c34-file/c34_file_posix.c` | `a90780edd417cf03a274a013e1d003dfb2c4368ebfedfb96775e7584fc6ee1a2` |
| `media/c34-file/c34_file_recovery.c` | `ac0d4fb03a323aa68db69b8da068ac8de80bef46f6e58828ec286fd4ba5688d5` |
| `media/c34-file/tests/broken_c34_file.c` | `ee316c7d6e6fcaba4c7761913c5b6a2df48208b87c7db3335d2aa6e215714391` |
| `media/c34-file/tests/c34_file_oracle.c` | `ad178490042ed1c73654d6a94dede0ab96fda1a1369d290093fe952c12c58140` |
| `media/c34-file/tests/c34_file_oracle.h` | `43501922c66eaf35e6963a5fa2893aede56552d021e842837d04d3c2f36cd746` |
| `media/c34-file/tests/c34_file_test_support.c` | `a4f1c859876ec52afbbf0be17c6e8edc0158a3b1fee44c2456543ba1d8dfdce2` |
| `media/c34-file/tests/c34_file_test_support.h` | `f523dcb33b1f937585fefb926f95e0def7786ee810aba370971f4e3916780889` |
| `media/c34-file/tests/model_c34_file.c` | `8f02ae520e55aa1c89b4e58fe43c72524fa9ee14eb651b334e61d0f98ee412d3` |
| `media/c34-file/tests/test_file_contract.c` | `79ea7da213128c97817cda12851877c464f02c5b2f4f5e42331943fe11facf3e` |
| `media/c34-file/tests/test_file_crash.c` | `e3f2408235d185d0f8afc3d9483bf4454583a88b0ad01feaae7d7ac128a7acdf` |
| `media/c34-file/tests/test_file_posix.c` | `5036063d154976b7d3053da161dbddd8b0a1c18e96fc0185ea2babeb2e655687` |
| `scripts/check_c34_architecture.py` | `ca4fa6c988e3cd532290b632d0b548c569b66f95d16fd3f84ae748ecfe5402ef` |
| `scripts/check_c34_cross.py` | `3cce28638f8045cc9aa8d2df56c0f3d2c54d47be7481c02b3756e6ab59fb4fdc` |

These hashes bind the exact implementation and test inputs at the immutable
source commit. The result document and repository policy freeze them in the
following evidence commit.

## Claim boundary

C3.4 does not establish:

- a production or long-running FTL, general GC/WL, multi-victim relocation or
  unlimited journal/checkpoint recycling;
- an NVMe command, opcode, queue, namespace, status or conformance result;
- Host DMA, pinning, BAR, MSI-X, IRQ, PCI, VFIO, QEMU or owner switching;
- raw-block media, real SSD/NAND writes or actual electrical power-loss tests;
- validated PLP, filesystem-independent persistence, physical NAND atomicity,
  endurance, calibrated latency, IOPS or production suitability;
- recovery from arbitrary corruption outside the explicitly tested finite
  profiles.

The Host kernel version is incidental. No debug/lab machine was used because
this gate is deliberately transport-free and uses only disposable ordinary
files.

## AI assistance and human responsibility

- AI-assisted: yes
- Model family: OpenAI GPT-5.6, 2026-08-29
- Input categories: frozen project ADRs/contracts and public POSIX/Linux
  behavioral documentation
- Third-party implementation source copied or transformed: no
- Responsible owner and final reviewer: Evanshenf
- Human review remains required before any release, hardware or durability
  claim
