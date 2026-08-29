<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3.3 programmable NAND/NFC result

- Date: 2026-08-29
- Disposition: **PASS for the narrow C3.3 functional-model gate**
- Immutable source commit: `1e3594ad3068ed3991e28589db14420ef59ca6c2`
- Frozen prerequisites: ADR-0002, ADR-0003, ADR-0006 and ADR-0007
- Evidence capture: `2026-08-29T13:30:39Z`
- Execution profile: unprivileged native tests plus user-mode cross-ISA runs

## What passed

C3.3 implements a new independent `nfc/` portable layer. It has caller-owned
fixed arenas, staged READ/PROGRAM transfer and execute operations,
generation-checked plane caches, configurable channel/LUN/plane/block/page
geometry, page and OOB byte truth, exact block erase, factory/runtime bad
blocks, successful-erase wear limits, behavioral ECC/read-retry, deterministic
integer fault injection and FCFS virtual-time resource scheduling.

The release-v1 model is deliberately narrow: abstract SLC, a full main page,
optional full OOB programmed with that page, and one program opportunity per
erase. A normal success is complete. An interrupted or injected partial
physical effect is reported as failed, applied and torn; it is never promoted
to logical mapping authority or command durability.

The semantic NFC provider is independent from C3.1. A private adapter joins a
frozen C3.1 opaque request token and exact lifecycle identity to an immutable
C3.3 descriptor. Generic C3.1 events retain only normalized lifecycle faults;
the exact PPA, OOB, ECC, wear and torn result remains in a generation-bound
C3.3 sidecar. Neither C3.1 nor C3.2 source changed.

## Boundary and replaceability

```text
C3.1 public lifecycle envelope
  -> private token/identity adapter
  -> C3.3 semantic provider
       |-- scripted fake
       `-- programmable model
              -> physical media contract
                   `-- in-memory test backend
```

Only the private adapter may include the frozen C3.1 provider header. The NFC
model, scheduler, fault engine and media coordinator do not include C3.1 or
C3.2. NFC completions are physical facts only; C3.3 cannot emit `C_map`, a
durability witness, a fence result or a Host completion.

The physical media contract contains raw PPA page/OOB, erase generation,
health and wear operations but no file path or decoded logical map. C3.4 can
replace the memory fixture with a file-backed implementation without changing
the NFC scheduler.

## Runtime exit evidence

| C3.3 exit item | Evidence |
| --- | --- |
| checked geometry/version/arena validation | contract unit cases and overflow rejection PASS |
| rejection/backpressure ownership boundary | rejection leaves model and media hashes unchanged |
| exact operation/cache identity | completed-token replay rejected; cache PPA/epoch/generation/retry checked |
| staged PROGRAM and READ | transfer/execute/trigger/transfer round trip PASS |
| non-erased program rejection | second page program fails with NO_EFFECT |
| bit direction and full-page rule | memory backend applies `old & payload`; partial ranges rejected |
| page and OOB truth | main+OOB program/read/erase round trip PASS |
| exact erase scope | sentinel in sibling plane remains byte-identical |
| factory/runtime bad blocks | mutation attempts fail NO_EFFECT; health remains monotonic |
| wear threshold | exactly two successful erases allowed; third marks runtime bad |
| ECC and retry | clean/corrected/uncorrectable plus explicit retry recovery classes PASS |
| reproducible seeded faults | published seed and two deterministic vector hashes PASS |
| virtual-time resource serialization | same-LUN serial, plane-parallel, cross-channel overlap and priority ordering PASS |
| controller reset | queued old operation drains as RESET failure before quiescence |
| modeled SSD power loss | queue/cache/event loss with committed torn program prefix retained |
| injected torn effects | PROGRAM and ERASE fail with APPLIED+TORN, never SUCCESS |
| fake/model replaceability | one C3.1 adapter consumer uses both providers without a branch |
| two-instance isolation | distinct arena/media/seed instances retain independent hashes and bytes |
| explicit codec | request/completion/trace LE round trips and literal prefix vector PASS |
| no platform dependencies | include/string/unresolved-symbol architecture gate PASS |
| repository integration | root `make check`, `nfc` layer fake ELF, policy, links, SPDX and REUSE PASS |

## Deterministic outputs

```text
C3.3 contract unit: PASS (4 cases)
C3.3 NAND legality unit: PASS (2 cases)
C3.3 explicit codec unit: PASS (2 cases)
C3.3 fake/model provider replaceability: PASS
C3.3 virtual scheduler unit: PASS (3 cases)
C3.3 ECC/retry/wear unit: PASS
  cases=3
  seed=9b6d3e7a4c2158f1
  vector-hash=85da006eb21da96f
C3.3 reset/power unit: PASS
  cases=3
  effect-vector-hash=5b684e06e9612070
C3.3 two-instance isolation: PASS
C3.3 bounded model: PASS
  families=13 states=402 terminals=41 cuts=804
  duplicates=99 collisions=0 max-depth=17
  actions=00003fff invariants=0003ffff
  hash=29d99027066df3bf
C3.3 broken variants: PASS
  shortest-counterexamples=18
  hash=11e3d3f785592714
```

The runtime unit suite has 19 directed cases. Separately, the bounded model
exhausts every enabled topological interleaving in the 13 frozen grammar
families and injects controller-reset and modeled-power-loss checks at every
canonical grammar state. This is not a claim that all states of every accepted
large geometry were enumerated.

The ten program outputs (contract, legality, codec, replaceability, scheduler,
ECC/wear, reset/power, isolation, positive model and negative model) are
byte-identical under native x86-64 and:

- AArch64 GCC 13.3 through the AArch64 user-mode runner;
- RISC-V GCC 13.3 through the RISC-V user-mode runner;
- big-endian s390x GCC 13.3 through the s390x user-mode runner.

## Shortest named counterexamples

| Invariant | Broken mutation | Minimum actions |
| --- | --- | ---: |
| N-GEOMETRY | `BM_GEOMETRY_ALIAS_OOB` | 1 |
| N-IDENTITY | `BM_CACHE_MATCH_PLANE_ONLY` | 7 |
| N-PROGRAM-ERASED | `BM_PROGRAM_CHECK_SUBMIT_ONLY` | 11 |
| N-BIT-MONOTONIC | `BM_PROGRAM_ASSIGN_BYTES` | 9 |
| N-NO-PARTIAL-CLAIM | `BM_PARTIAL_REPORTS_SUCCESS` | 7 |
| N-ERASE-SCOPE | `BM_ERASE_IGNORES_PLANE` | 5 |
| N-PAGE-OOB | `BM_PROGRAM_DROPS_OOB` | 10 |
| N-BAD-BLOCK | `BM_ERASE_CLEARS_BAD` | 3 |
| N-ECC | `BM_ECC_STRICT_LT` | 6 |
| N-RETRY | `BM_RETRY_OMITS_STEP` | 9 |
| N-WEAR | `BM_WEAR_GT_NOT_GE` | 11 |
| N-SEED-REPLAY | `BM_FAULT_XORS_VIRTUAL_NOW` | 3 |
| N-TIME-SERIAL | `BM_FORGET_LUN_TAIL` | 3 |
| N-CUT | `BM_CUT_ROLLBACK_COMMITTED` | 9 |
| N-EPOCH | `BM_EVENT_MATCH_SLOT_ONLY` | 7 |
| N-ISOLATION | `BM_GLOBAL_MEDIA_CONTEXT` | 4 |
| N-PROVIDER-EQUIV | `BM_FAKE_LOSES_RAW_STATUS` | 5 |
| N-NO-FTL-HOST | `BM_HOST_CACHE_SELECTS_PPA` | 1 |

Each broken run enables one named mutation and uses the same property checker.
FIFO level-order traversal proves no shorter grammar path reaches the target
violation. Counterexamples include family, geometry/profile/seed hashes,
complete action path, cut kind, media/event/oracle hashes and violation mask.

## Commands executed on the immutable source commit

```sh
make -C nfc check
make -C nfc check-clang
make -C nfc check-sanitize
make -C nfc check-thread
make -C nfc check-cross
make check
reuse lint
gcc ... -fanalyzer -c nfc/*.c
clang --analyze ... nfc/*.c
```

All commands completed successfully. Both static analyzers produced no
finding. The existing layer-fake policy automatically discovered `nfc/`, built
its own Makefile and executed its fake ELF without changing the frozen root or
`core/` Makefiles.

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
| Host kernel used for unprivileged execution | 6.8.0-138-generic |

The Host kernel is incidental to this transport-free model. No debug machine,
KVM guest, physical SSD, file image or raw device was needed or exercised.

## Source and test manifest

| Path | SHA-256 |
| --- | --- |
| `include/fwlab/contracts/nand_media.h` | `16b14aa4d777ac9341458de24172c9d0ae0cbe058bb237e438428b2f36be05d7` |
| `include/fwlab/contracts/nfc_provider.h` | `d8e5385ecd02bb1b43b56dfbffd359020c40c57bda2154133cd77b8e9515a6a6` |
| `include/fwlab/portable/nfc_codec.h` | `4a0f67a3f004804ec50abfb911f4ddb65531425c916096aae5a786ca3ad12a75` |
| `include/fwlab/portable/nfc_model.h` | `43c63d3c208bf7d2e21226d7cf0cce522c57606f86eed6ed308ccc162ca9fa2b` |
| `include/fwlab/portable/nfc_types.h` | `4baae32a519c220d40c9c09bd6b80236d073c4d602c231ee117a201d19d9a1ca` |
| `nfc/Makefile` | `767beec0472d61f8e43fe7b77da4f78b432e4f4dd8dc6afadf19b46ec5c2b1cf` |
| `nfc/adapters/nfc_c31_adapter.h` | `dae35bbfb61159c92fcca8f803ed83c983813ce9d8888f2e197345631988b1a2` |
| `nfc/fakes/nfc_buffer.c` | `96cb3924db69228440fdcb588470771e9366291fabd7058bf3ad15fd01f4bdf0` |
| `nfc/fakes/nfc_buffer.h` | `75496bcff72007882a173f20d915ab7bff515b42214f78380bc91b9b592fc52f` |
| `nfc/fakes/nfc_fake_main.c` | `65d9aef8d785f9a3e89178a36cfe7509849f072f444935e4c31952a27dfa3026` |
| `nfc/fakes/nfc_memory_media.c` | `0dd5d5ec00ae93741501a999d4f9667d2bf1f431c20bf57afe64d6308bcf3e6d` |
| `nfc/fakes/nfc_memory_media.h` | `a68dadcc9a95cd609e8db87731a30905cc28e1699d9fbab7bc28d9ba6c69b2f8` |
| `nfc/fakes/nfc_scripted.c` | `02e7e7088768011fa72c77e6a15e8704caf6bbee68f3c6ebf1dac5ff8980c3cb` |
| `nfc/fakes/nfc_scripted.h` | `5c0e0d6c8a42365c7744acfaa8285a573b05be1056a3a0e51bd6ef04ce6a54d2` |
| `nfc/nfc_adapter.c` | `3b56acbf8103129e51e9e79fee23a2ac95472dabfca33f4dc54b5cc8b83ac983` |
| `nfc/nfc_codec.c` | `6c3e2a57ab70129c9e3948aac19167c0f2978f0fa93f0ca0e6a97ca5fee0b822` |
| `nfc/nfc_fault.c` | `978d459ca13db66ac6c1f156af493e3b54f787e4e4228bbef78c686cb9922c8e` |
| `nfc/nfc_internal.h` | `888ab428b9d2a81dbc40bc156e394dcef296815613406cc24e73beee0aa4dbd1` |
| `nfc/nfc_media.c` | `58aeb225f8571c29d8029586ab1f8292b62d41679d26d0e116a008d83f62dec9` |
| `nfc/nfc_model.c` | `b882699c454b4b79a8655f79f20e097a5c08e1f402167a06f94385afe337944b` |
| `nfc/nfc_scheduler.c` | `780453e5c2f290af52e9cc6bbd8b4765b8fa06e61b3ab7500ae9b270cc242f6d` |
| `nfc/tests/broken_c33.c` | `1c7243eeea4432458332a4b518701502b6f0136213f7906088ce557b43fab0e9` |
| `nfc/tests/c33_oracle.c` | `6b1d79545e9b248a62f79923a7c23951eb2ef4ea4a71e24e551b30d235998c02` |
| `nfc/tests/c33_oracle.h` | `4a49f8ee09e4739fcfdbd0d7978dc28d312913572e46eb48b022ac75082fe127` |
| `nfc/tests/c33_test_support.c` | `d21a7a541f32c410d58b6adbeeba4f4e2fe9ba5ce1a575b1ebe5f871893acb1b` |
| `nfc/tests/c33_test_support.h` | `a7cab7acdfb11d53108fe25b3505e5fa7ae5f76fcf2b0e3367407aff7fbf4fef` |
| `nfc/tests/model_c33.c` | `9371bf5562a192efc95015a39890af173e0e65c65bb5293f319c98c1fa18f8d8` |
| `nfc/tests/test_codec.c` | `846a37a657813b0a086605545fbe83dc3e741a6c4feb37b159c8d0da52415285` |
| `nfc/tests/test_contract.c` | `73bb789e8bb7e67402e668cebf447e709fd9decbd2557046528aa77ed4b670e0` |
| `nfc/tests/test_ecc_wear.c` | `05d0d5f7f6e1f1cbdc3a9d4e93300bf7471d1631626508af766fb17faae0d2af` |
| `nfc/tests/test_isolation.c` | `2d9ec994d2fd257e7e248b8a2d8e0d8668a05edbb2d1cebb6a3450cbbf9a72f0` |
| `nfc/tests/test_legality.c` | `945898c0b29a6065c9cb82aa702e2c046ce224524374b21980947ecd2a692f89` |
| `nfc/tests/test_replaceability.c` | `668f9630dd8f918dda6f25e2635a54579dad1abffea3278fdb62fc419ea2e0c1` |
| `nfc/tests/test_reset_power.c` | `a4a351b14e366724f6861211cb3dfeb886d5c877864dbc2dbff1b644b0a9c5f0` |
| `nfc/tests/test_scheduler.c` | `9dd0e3075b53ef5a750784ae0a29be5bd91f78602d4ece22604a35ba87e8dbcf` |
| `scripts/check_c33_architecture.py` | `f6de8d387d72ec356e0ded3adbcff7bf025849214bfc1e8f7853ebbae4289dc5` |
| `scripts/check_c33_cross.py` | `526e28a2503b6c9fe30f2193db0dcd3127b60ed4c4d296aca6e7ccebefb2423c` |

These hashes bind the exact implementation and test inputs at the immutable
source commit. The result document and repository policy freeze them in the
following evidence commit.

## Claim boundary

C3.3 does not establish:

- an FTL, L2P/P2L map, GC/WL policy, trim, journal or checkpoint;
- C3.2 logical durability, `C_map`, PLP or fence satisfaction;
- an NVMe command, queue, status, register or conformance implementation;
- file-backed or raw-block persistence, substrate recovery or Host `fsync`;
- real NAND bad-marker format, BCH/LDPC layout, paired-page constraints,
  retention, read disturb, cache/copyback/multi-plane commands or suspend;
- DMA, BAR, MSI-X, PCI, VFIO, QEMU or native-driver behavior;
- calibrated latency, fault probability, endurance, physical power loss,
  electrical behavior, ONFI conformance or performance.

The modeled SSD power loss is an in-memory deterministic event. It is useful
for controller/NAND state-machine correctness but is not real media durability
evidence.

## AI assistance and human responsibility

- AI-assisted: yes
- Model family: OpenAI GPT-5.6, 2026-08-29
- Input categories: frozen public ADRs, repository-owned contracts and public
  Linux/vendor behavioral documentation
- Third-party implementation source copied or transformed: no
- Responsible owner and final reviewer: Evanshenf
- Human review remains required before any release or hardware claim
