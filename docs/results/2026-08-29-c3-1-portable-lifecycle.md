<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3.1 portable command-lifecycle result

- Date: 2026-08-29
- Disposition: **PASS for the narrow C3.1 lifecycle gate**
- Immutable source commit: `df13d3747ed01b1d7895f9c7a0ccc63072410dd4`
- Frozen prerequisites: ADR-0006 and ADR-0007 at `5c45bd9`
- Evidence capture: `2026-08-29T10:00:13Z`
- Execution profile: unprivileged x86-64 Host tests plus user-mode cross runs

## What passed

The source commit implements a caller-owned fixed arena, one serialized
executor per instance, budgeted provider polling, generational command and
operation identities, immutable completion intents, one-use completion leases,
abort tickets, epoch-first reset, bounded provider drain, teardown and a fixed
deterministic trace ring.

Each command owns zero or one lifecycle-only provider operation. DMA and NFC
use the same value-only ownership envelope but have independently replaceable
fixtures. The dedicated DMA fixture validates exact capability, instance,
epoch, origin, direction and ranges. Host-to-controller data is committed only
after full success; controller-to-Host failures report none, full,
exact-prefix or unknown-prefix effects without invented rollback.

No provider calls back into the core. Even zero-delay terminal events appear
only in a later poll. A wrong-instance event has no new-state effect. If the
provider then cannot produce the exact accepted terminal event, reset remains
in drain instead of falsely acknowledging quiescence.

## Exit evidence

| ADR-0006 C3.1 exit item | Evidence |
| --- | --- |
| no platform/protocol/persistence dependency | repository policy, constrained-header compile and unresolved-symbol audit PASS |
| no heap, lock, worker or wall-clock progress | object audit PASS; only codec and `memset` remain as normal unresolved core symbols |
| GCC and Clang strict builds | GCC 13.3 and Clang 18.1.3 PASS with `-Werror -Wpedantic` |
| x86-64, AArch64 and RISC-V | native x86-64 plus AArch64/RISC-V cross binaries executed PASS |
| explicit codec and cross-endian proof | 96-byte literal vector PASS on x86-64 and real big-endian s390x |
| ASan/UBSan and TSan | unit, bounded-model and fuzz binaries PASS in both lanes |
| bounded lifecycle model | 1,944 exhaustive bounded traces PASS |
| deterministic envelope/event fuzz | 5,000 iterations at the fixed seed PASS |
| DMA effect classes | none/full/exact-prefix/unknown-prefix data and intent checks PASS |
| reset in every live state | ACCEPTED, DISPATCHED, HELD, RUNNING, CANCEL_PENDING, READY and LEASED injection PASS |
| stale, duplicate and wrong-instance events | zero-effect stale handling, duplicate fail-closed and false-quiesce rejection PASS |
| pool and reduced-counter exhaustion | command, UID, operation, lease, ticket, slot and epoch cases PASS/fail-closed as specified |
| two-instance isolation | equal slot numbers under separate arenas/nonces remain isolated |
| provider replacement | scripted and dedicated DMA/NFC providers substitute without core changes |
| repository integration | `make check`, layer fake ELF execution, policy, links, SPDX and REUSE PASS |

## Deterministic results

```text
C3.1 unit tests: PASS (17 cases)
C3.1 bounded model: PASS (1944 traces, hash=a8b8770a7a6ce007)
C3.1 deterministic fuzz: PASS
  seed=9b6d3e7a4c2158f1
  iterations=5000
  hash=2842aa9bd174fa6f
```

The unit, model and fuzz output is identical under:

- x86-64 GCC and Clang;
- AArch64 GCC 13.3 through the AArch64 user-mode runner;
- RISC-V GCC 13.3 through the RISC-V user-mode runner;
- big-endian s390x GCC 13.3 through the s390x user-mode runner.

## Commands executed on the immutable source commit

```sh
make -C core check
make -C core check-clang
make -C core check-sanitize
make -C core check-thread
make -C core check-cross
make check
reuse lint
gcc ... -fanalyzer -c core/c31.c
clang --analyze ... core/c31.c core/c31_codec.c
```

All commands completed successfully. The static analyzers produced no finding.

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

The Host kernel is incidental to this unprivileged portable-core gate and is
not a kernel compatibility claim.

## Source and test manifest

| Path | SHA-256 |
| --- | --- |
| `core/Makefile` | `2f56d74b4997003b5cb4aec42c36866f2dc97c7e818a9ca73b42a4391e274178` |
| `core/c31.c` | `9535dd53f52a4736cee53a4ca9f9536f5916ef8b54581f32e767d313993e6b48` |
| `core/c31_codec.c` | `98766c7a39b75b723352c024815b72928a6f80ea2d9bf17a909bc6c21b76dcd4` |
| `core/c31_internal.h` | `b25ed32f0384393dc4268dd9c921aecbb149415bfc22924de9e092bedef997d9` |
| `core/fakes/c31_fake_dma.c` | `8fd0edefd709a010568b3fe6aab95a4b7f3268973a2e5e829f241f22e3dbd6de` |
| `core/fakes/c31_fake_dma.h` | `8f9f29fc0e0c318361dfb799621540d3f7d26c77021907f6a6827d75e9eaf071` |
| `core/fakes/c31_fake_main.c` | `0dc56ba62865e5694e775b9c5d672f90b671fd3a2569accf9b87fa3919cca0fe` |
| `core/fakes/c31_fake_nfc.c` | `1b95f0f1c38125e3b3b74fe1db3ab9b86e02d2dca5492a7ea96c100ec6eecbd5` |
| `core/fakes/c31_fake_nfc.h` | `0bb41eb6ec1aa112bce000f4e7c5877f750c41ae568fdcc2befbacc97ca91c3e` |
| `core/fakes/c31_fake_provider.c` | `20870983bb219df7b70626c0d024c62299bd617516d6110aaa321b3cd1ae61a6` |
| `core/fakes/c31_fake_provider.h` | `54f1395b78db86fa11bdcf7ca81155592483c1bc1aa5c64135dc1a29dd980cd9` |
| `core/tests/fuzz_c31.c` | `25975b950153f1b0db72b4a5f0410e04aa553f9b744e286840cdd96e27117d21` |
| `core/tests/model_c31.c` | `e1e4474e7c4ff8e135103d99f6f624a24803b709988826f94aa8ca1ff505ad4c` |
| `core/tests/test_c31.c` | `a4732e2e24ea821353ec07a0552db5a9572dd949a3742e80bdc1a25b3be96380` |
| `include/fwlab/contracts/c31_provider.h` | `6a48aee94834d50b7b67d1f6d7fc9cc43b4f00f10e50927970a800ac59ffdc66` |
| `include/fwlab/portable/c31.h` | `c2dced10b4965cb026e208d8dd4650b7fcd213486b48bfb4e9026f1fd2cb73db` |
| `include/fwlab/portable/c31_codec.h` | `c4ede29686cb548f4a04cea72b7de56abea2f4f173791bb15b4e783e19334ba4` |
| `include/fwlab/portable/c31_types.h` | `b1ee94f66203b096e85b3afa55fa790246348c01749c42d9df27a0a2e1a21c7c` |
| `scripts/check_c31_cross.py` | `bc1d5c798c8a0152e02e4014acf24d519ed2b0ebd25e8d162fac894830430cbf` |

These hashes bind the build inputs tested at the immutable source commit. The
result document and repository policy freeze them in the following evidence
commit.

## Claim boundary

C3.1 `SUCCESS` is a lifecycle result only. The completion intent deliberately
contains no durability or persistence enum. This result does not authorize or
claim:

- any NVMe queue, command, status or conformance behavior;
- NAND geometry, page/OOB operations, ECC, timing, bad blocks or wear;
- mapping, FTL, garbage collection or recovery correctness;
- file-backed or raw persistent media;
- device DMA, BAR, interrupt, PCI, QEMU or native-driver behavior;
- performance, power-loss or physical-hardware fidelity.

C3.2, C3.3, C3.4 and C3.5 remain separately gated.

## AI assistance and human responsibility

- AI-assisted: yes
- Model family: OpenAI GPT-5.6, 2026-08-29
- Input categories: frozen public ADRs and repository-owned policy/build files
- Third-party implementation source copied or transformed: no
- Responsible owner and final reviewer: Evanshenf
- Human review remains required before any release or external protocol claim
