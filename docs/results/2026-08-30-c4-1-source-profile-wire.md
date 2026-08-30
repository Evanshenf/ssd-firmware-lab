<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C4.1 source/profile/wire result

- Date: 2026-08-30
- Disposition: **PASS for the fixed C4.1 software-oracle gate**
- Immutable source commit: `31e07d8006911b3bb2838c816caa23ded856c4b6`
- Frozen design: ADR-0008 and the Cycle 04 five-gate sequence
- Execution profile: unprivileged native tests plus user-mode cross-ISA runs

## What passed

C4.1 adds an independently authored, address-free command/completion/profile
ABI and a common typed-action envelope. A separate headless memory-transport
component captures a caller-owned 64-byte command value, retains command IDs
and pointer values privately, and produces canonical bytes containing only
protocol operands, opaque origin and address-presence facts.

Physical 16-byte completion encoding combines an address-free immutable intent
with HIF-private queue identity and phase. The portable completion intent does
not contain queue ID, command ID, queue phase or Host/guest address.

The project-authored fixed profile is one controller, one namespace with eight
512-byte LBAs, maximum 4-KiB transfer, one I/O queue pair, integration depth
four and queue-engine hard maximum 32. These are oracle parameters, not PCI
register claims.

## Exit evidence

| C4.1 item | Evidence |
| --- | --- |
| explicit byte codecs | literal 128-byte command, 64-byte intent/profile and 16-byte completion vectors PASS |
| raw/private isolation | 65,536 CID/pointer mutations produce identical canonical bytes when presence facts are unchanged |
| versioned action boundary | token/envelope/submit-result/terminal validation PASS; no action engine is claimed |
| bounded canonical model | 4,096 states and 32,768 transitions PASS at depth 12/successors 8 |
| strict native builds | GCC, Clang and an additional conversion/shadow/prototype lane PASS |
| sanitizers | ASan+UBSan and TSan unit/model/fuzz lanes PASS |
| analyzers | four production sources PASS GCC `-fanalyzer` and Clang analyzer |
| compiler determinism | six complete program outputs are byte-identical under GCC and Clang |
| cross-endian execution | native, AArch64, RISC-V and big-endian s390x outputs are byte-identical |
| repository integration | root `make check`, fake layers/components, policy, links, SPDX and REUSE PASS |
| C3 coexistence | C31-C34 archive and all C35 canonical lane hashes remain unchanged |

The clean C4.1 matrix started with both C4 build directories absent and
completed in 17.25 seconds with exit status zero. The subsequent root check
completed in 18.44 seconds with exit status zero.

## Canonical fake-adjacent bytes

Complete bytes are compared before hashing:

```text
portable profile + command + intent: 256 bytes
SHA-256: 68e0e63851ead696982aa42780b2788ffffe7185699eafa1932452176a4af697

headless profile + canonical command + intent + physical completion: 272 bytes
SHA-256: e91d57671d935d63869f6d8b1b9bae891941b21d5dbdef0f4b2ed20017198b14
```

Frozen C3.5 hashes remain:

```text
archive:      b1f95f2787fe9a3d585f467d4ba72e6de32938c51273d4ad7f411dc1e644cf6f
life:         3b38adcdaa0f3a6b8c2ab5ea54d4a134826a49a456f3e32672126ae811ce5842
semantic/raw: 68d49de982b70406c3c1e1baf39549502c384afe5827eb3acc7f155026040508
container:    b6e9125697ed85894aa4ac26c5167b3c05ccbf51f8c8d0f41fb6bbb1f77d1399
two-atom:     dc7e66f4a627939eddac9979ca3e46f5de7b4bc61eff7180ef3f48ad076b3b92
```

## Commands executed on the immutable source commit

```sh
make -C frontends/headless-c4 check-all
make check
reuse lint
```

The full component matrix includes standalone `check-c41` and `fake-link-c41`
for both portable and HIF components, strict compiler flags, analyzers,
determinism and cross-ISA execution.

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

The Host kernel is incidental to this unprivileged gate and is not a kernel
compatibility result.

## Source and test manifest

| Path | SHA-256 |
| --- | --- |
| `core/c4-nvme/Makefile` | `6fe47d58b6ba6b5bc930fb332280e47c2835bbb5a9810156b7380c394507e097` |
| `core/c4-nvme/README.md` | `fe3e6636efd8a2df060d3c67640dc158f7b2e07395a18f7ee7327ad08ce935d7` |
| `core/c4-nvme/c41_action.c` | `8932d127ae3603b48cfdd0143c675bff7e3fd3e062ae286308307ef4f70f3963` |
| `core/c4-nvme/c41_codec.c` | `2fb0b5acd94266f023c300e6ab120607e322c230bc52ff12674fa0fea7dc50ac` |
| `core/c4-nvme/c41_profile.c` | `b975f04eddb38a0d08c6c23c4007f8bb90ac719f5513132ebf2e38df739a82ec` |
| `core/c4-nvme/fakes/c41_fake_main.c` | `6073f06e770cc2dd94ea73bfea93525835f7ac4716e4ebba39a888de30b7fc8b` |
| `core/c4-nvme/tests/model_c41.c` | `d2f18c925c4f64384dc539db5547bab4858275d3a18bc7749370ae462956940f` |
| `core/c4-nvme/tests/test_c41_portable.c` | `392b3991fed86a55316ce3a7a3546aa186ce3cbddddac28e25ce5539ffb05b97` |
| `frontends/headless-c4/Makefile` | `5937b989dd79f18b40d7f6fcfafd05c0801eeb2d6796f26fc150be80d06058fd` |
| `frontends/headless-c4/README.md` | `c7e66d247ffbf07f95505f3c5924d4aabd546b715631585390927430680c3d6b` |
| `frontends/headless-c4/c41_wire.c` | `3b9f682bcc253cfcd4b384a29d8c279363a8203c98c5c63ee133445dc322671b` |
| `frontends/headless-c4/c41_wire.h` | `0c0f188312cb67e7f57d19697fbf32767b3dcf2b161c0670a29de8882937ebea` |
| `frontends/headless-c4/fakes/c41_fake_main.c` | `8bb93e73c7458bc066de2d2e085cd3d22b15f685232ff5e108f37b2a189e1542` |
| `frontends/headless-c4/tests/fuzz_c41.c` | `946a0cb16c2512e37bdc2cde8f858c942640f734e4fe641aaa7ca23d7624cd58` |
| `frontends/headless-c4/tests/test_c41_wire.c` | `32258f426a86bbf2d4080b7e4d3c0a2b8ebc46288dc2586071530bb116477a49` |
| `include/fwlab/contracts/hif_action.h` | `c1afc74c228f1d467671faba256d94a452bd948d1c2fe0f94765b1db3b878956` |
| `include/fwlab/portable/nvme_codec.h` | `5ae3d488412bbfd69fa8cc1f441472f0cb9d3da8b3ba833177aaeaa671f8370f` |
| `include/fwlab/portable/nvme_types.h` | `f65e9313e33206c9c98b2b485a5cae74a0523cf4062403b18658516f2e08019f` |
| `scripts/check_c35_architecture.py` | `94eb3cb058fa5791e12464b58e3b381ad8f5e4c7572a074a1c8e2a070fc8d5b7` |
| `scripts/check_c4_analysis.py` | `3b88637b2f37f5aa871011e0ff0fff710757b4330b0af6b456e9ffabcd14ac02` |
| `scripts/check_c4_architecture.py` | `6f2a3517cd89d9bfbd941c6738cf6b77ca9aa08419b211d6219ed83010b3c12b` |
| `scripts/check_c4_cross.py` | `9668450610e3a6c06e7f68266b7029b96f1b5fdbbe812ce00aa0378951373653` |
| `scripts/check_c4_determinism.py` | `a0722a1e470feab3d5e18be12e3e04469a12fae4ccb49fbb818d52ecbe2b572f` |
| `docs/adr/0008-generalized-nvme-command-graph-boundary.md` | `2ee8f526bff088d96b0c9669ae9187ecdb4c8b36f50be7439f97f50bfe8b8db0` |
| `docs/legal/c4-1-source-boundary-review.md` | `952b09bb63fbbad7d52ac2c87ac10d77ef9f441cf3ef55ebc4ec74d252dedcbf` |

These are the exact build/test inputs at the immutable source commit. The
following evidence commit freezes the permanent C4.1 implementation, ABI,
tests and decisions while leaving shared build/check aggregators evolvable for
later gates. The complete manifest remains bound to the immutable source
commit; the evidence commit does not alter a listed source file.

## Claim boundary

C4.1 is not a working NVMe controller. It does not implement or prove:

- command legality, Identify contents, Read/Write/Flush execution or status
  conformance;
- SQ/CQ state, doorbells, queue creation/deletion, CID reuse or Abort;
- PRP walking, capability minting, bounce DMA, IOMMU/pinning or requester DMA;
- persistent namespace, C34/C35-backed 512-byte LBAs, raw block media or power
  failure;
- PCI/config/BAR/MMIO, MSI-X/IRQ, QEMU/vfio-user or an unmodified Linux driver;
- same-instance concurrency, 32-bit/freestanding execution, performance,
  certification or endorsement.

The next independent gate is C4.2 queue/CQ/identity over fake adjacent layers.

## AI assistance and human responsibility

- AI-assisted: yes
- Model family: OpenAI GPT-5.6, 2026-08-30
- Third-party implementation source copied or transformed: no
- Human/source/legal review remains required before a release or external
  conformance claim
