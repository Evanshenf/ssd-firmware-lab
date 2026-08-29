<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3.5 integrated headless firmware graduation result

- Date: 2026-08-29
- Disposition: **GRADUATED_FIXED_PROFILE / REVIEW_PENDING**
- Immutable source commit: `48567dae4f3246c2eddb83a28a30c526947dbc86`
- Frozen prerequisites: C3.1 through C3.4 and ADR-0002/0003/0006/0007
- Evidence capture: `2026-08-29T17:43:59Z`
- Execution profile: unprivileged native and user-mode cross-ISA tests using
  memory or newly created, immediately unlinked regular files

## What graduated

C3.5 composes the frozen command lifecycle, persistence policy, programmable
NAND controller, crash-consistent mapping and file-media layers behind one
transport-free headless interface. The new layer owns admission, complete
command/request identity, completion publication, reset ordering, teardown and
normalized evidence. It does not own mapping truth, NAND state or the physical
file format.

The same deterministic firmware archive is consumed by four separately linked
lanes:

| Lane | Composition | Graduated comparison |
| --- | --- | --- |
| S | C3.1 and scripted lifecycle binding | lifecycle only |
| M | C3.4 + C3.3 + memory media | lifecycle, semantic result and raw NAND projection |
| B | C3.4 + C3.3 + byte-image file engine | lifecycle, semantic/raw and deterministic container |
| P | C3.4 + C3.3 + anonymous POSIX fd | lifecycle, semantic/raw, container and target Linux ABI |

The comparisons passed as exact byte equality before hashing:

```text
E_life:      S == M == B == P
E_sem/raw:       M == B == P
E_container:         B == P
```

S remains a completion-only lifecycle fixture. It is not a storage provider
and contributes no durability, raw-NAND or restart evidence. M has no file
container. This tiering is part of the result rather than a qualification
added after the tests.

## Generic ownership transactions

The generic headless source has no scripted/model/memory/file/POSIX branch and
includes no C3.2/C3.3/C3.4 private header. It enforces:

```text
C3.1 submit -> binding register -> first lifecycle step

completion acquire -> copy semantic sidecar -> encode canonical publication
  -> consume C3.1 lease -> acknowledge binding result

close admission -> epoch-first reset and bounded drain to RESET_ACK
  -> recover binding while still closed -> prove quiescent
  -> reset ACK -> update owner epoch -> reopen

close admission -> bounded teardown/ACK -> prove bundle quiescent
  -> release the coherent bundle and exact caller-owned objects
```

A failed registration rolls back the still-ACCEPTED C3.1 command without a
step between submit and register. A dedicated mutation fixture observed the
command in ACCEPTED state, rejected registration, and then proved that later
commands could reuse the capacity.

The media and physical-receipt providers form one storage-owned bundle. Their
context must match, the bundle permits one live claimant, restart refuses a
live claim, and release requires physical quiescence. A directed test rejects
a second runtime over the same mutable image.

The binding checks instance nonce, complete command identity, controller epoch
and owner epoch. Wrong nonce, UID, epoch, slot, slot generation, lease
generation, released lease, consumed lease, slot reuse and pre-reset identity
probes all failed without changing peer state.

## Directed storage and reset evidence

The graduation workload runs fake DMA capture, durable write/read, volatile
write, background C_map, fixed-frontier fence, durable trim, absence read and
rebuild/readback. Fake DMA and the semantic storage request are two separate
transport-neutral commands; this result does not claim a single hardware
DMA-to-FTL data path.

The reset sweep first measured 60 C3.1 unit steps for the fixed durable-write
trace. M and B were reset after every cut `k=0..60`. Of the 61 outcomes in each
lane, 50 recovered the old value and 11 recovered the new value after raw
C_map authority existed. No third value appeared. Additional READY/LEASED and
consume-before-binding-ACK cuts invalidated the old lease, command and sidecar.

```text
C3.5 tiered S/M/B/P graduation: PASS (14 cases)
C3.5 reset/identity: PASS
  reset-executions=126 D_M=60 D_B=60
  outcomes M=50-old/11-new B=50-old/11-new
C3.5 ownership: PASS
  M/B backpressure-cancel + submit/register rollback
C3.5 POSIX restart: PASS
  dup fd -> close original -> rebuild -> readback -> exact-fd cleanup
C3.5 geometry seam: PASS
  two standalone C3.3 geometries; non-profile full C3.4 rejected
```

The POSIX case uses a newly created, immediately unlinked regular file. It
duplicates the fd, closes the original, destroys the volatile runtime, restarts
from the retained exact file, reads the durable atom and verifies that the
remaining exact fd is closed. A rejected non-profile initialization leaves the
bundle unclaimed and closes its test-owned fd.

## Composition model and negative evidence

Thirteen closed composition families were traversed with lexical level-order
state exploration. The new model does not reopen the frozen C3.1 state model,
C3.2 persistence lattice, C3.3 NFC scheduler/fault model or C3.4 crash matrix.
It checks the new registration/publication/reset/quiescence/identity seams.

```text
C3.5 composition model: PASS
  families=13 states=243 transitions=350 stale-probes=74 max-depth=12

C3.5 broken variants: PASS
  shortest-counterexamples=16
  vector=26665ac74fd7c4f9
```

Each named mutation produced a shortest counterexample within depth 16:

| Invariant | Mutation |
| --- | --- |
| one terminal | `BM_DOUBLE_TERMINAL` |
| complete sidecar identity | `BM_SIDECAR_UID_ONLY` |
| witness not invented | `BM_C31_SUCCESS_IS_DURABLE` |
| provider-neutral lifecycle | `BM_HEADLESS_BRANCH_PROVIDER_TAG` |
| container excluded from semantics | `BM_FILE_CONTAINER_IS_SEMANTIC` |
| backpressure retains ownership | `BM_DROP_REGISTRY_ON_BACKPRESSURE` |
| instance isolation | `BM_GLOBAL_ACTIVE_INSTANCE` |
| full identity | `BM_MATCH_SLOT_ONLY` |
| lease generation | `BM_ACCEPT_RELEASED_LEASE` |
| reset removes old registry | `BM_RESET_RETAINS_OLD_REGISTRY` |
| reset preserves raw authority | `BM_RESET_ROLLBACK_CMAP` |
| sound quiescence | `BM_IGNORE_PHYSICAL_RECEIPT` |
| new post-reset epoch | `BM_REUSE_EPOCH` |
| schedule equals solo | `BM_GLOBAL_STEP_CURSOR_SEED` |
| raw-only recovery | `BM_RECOVER_HOST_L2P` |
| canonical trace encoding | `BM_TRACE_NATIVE_STRUCT` |

## Schedule and thread isolation

For each of the MM, BB and MB provider pairs, both frozen six-action twin
families replay every order-preserving interleaving:

```text
2 families x 3 pairs x C(12,6)
  = 5,544 complete macro schedules
  = 20,586 distinct exact prefixes
```

Every exact prefix updates one actor's full canonical trace/raw projection and
checks that the peer projection remains byte-identical. Thirty-six selected
schedules also run two live firmware/media instances and compare each prefix
against a separately executed solo reference. The exhaustive claim applies to
this frozen macro grammar, not to arbitrary C call or thread schedules.

The native pthread driver performs 64 barrier-started repetitions for MM, BB
and MB, alternating twin-write and asymmetric workloads. Each thread owns one
complete instance, arena, buffer, media/image and UUID. All 192 twin runs match
single-thread solo trace/raw bytes. Clang TSan reports no race. This proves
cross-instance/global-state isolation only; calls on one instance remain
caller-serialized. P/P is covered by a separate two-fd directed case and is not
part of the 192-run claim.

## Fixed profile and true geometry boundary

The full C3.4 stack remains fixed at one channel, one LUN, one plane, six
blocks, four pages per block and 96+64 bytes per page. Its builder rejects a
different profile before claiming media.

A separate test instantiates two real C3.3 models with different
channel/plane geometry and fault seeds. Programming and resetting one leaves
the other's state hash, media hash, programmed bytes and erased sentinel
unchanged. This is NFC provider/media isolation, not multi-geometry C3.4 FTL
support.

## Archive, link and portability evidence

`libfwlab_firmware_c35.a` contains exactly 12 objects: frozen C3.1 core/codec,
C3.2 policy and the nine C3.4 production objects. Its member order and
dependency files match the explicit allowlist; deterministic reconstruction is
byte-identical. It has no global writable/BSS/common symbol and no heap,
thread, clock, random, file syscall or transport dependency.

The S link pulls only C3.1 objects from the archive. M/B/P pull the complete
fixed firmware graph. M contains no file engine, B contains no POSIX adapter,
and only the isolated P adapter references `pread`, `pwrite`, `fstat`,
`ftruncate`, `fdatasync` and `errno`. The adapter does not open, close, unlink,
map or ioctl a file; the headless test harness owns fd lifecycle.

GCC and Clang strict builds, ASan+UBSan, Clang TSan, GCC `-fanalyzer` and Clang
static analysis passed. Canonical outputs are byte-identical between GCC and
Clang and across native x86-64, AArch64, RISC-V 64 and s390x 64 under user-mode
runners. The cross checker verifies ELF64 and verifies s390x big-endian before
execution.

```text
firmware archive SHA-256
b1f95f2787fe9a3d585f467d4ba72e6de32938c51273d4ad7f411dc1e644cf6f

E_life SHA-256
0a5528a5f0f58ea165c88e8bdb4396c8c8902b794afa49f5187704db32dc3f8f

E_sem/raw SHA-256
8676761e2f91a8617983629e57af0b7376179271d76d6f699664143ca704ebea

E_container SHA-256
d039a57e1adba3270ac8cc53a4376bca48218905a2bea0d72f0655b5b2a246b2
```

## Toolchain

| Tool | Version |
| --- | --- |
| Host GCC | Ubuntu 13.3.0 |
| Clang | Ubuntu 18.1.3 |
| AArch64/RISC-V/s390x GCC | Ubuntu 13.3.0 cross |
| user-mode runner | 8.2.2 |
| Python | 3.12.3 |
| REUSE tool | 2.1.0 |
| incidental Host kernel | 6.8.0-138-generic |

The source commit's sorted `SHA-256  path` manifest has SHA-256
`ecfe0c7c7031435f6d8b50f320e5c48ab86de07726b6b4429a6fb276f17957e8`.
The C3.5 component-file submanifest is
`511310a14c64d8e08e0ba433681b585b63cf14551cff5e483441d435af992de2`;
the four checker-file submanifest is
`e57865d604fa0ea598460434fe1fd6e1d9f938b35f7aa06a18abab010976bcf9`.

## Exact claim boundary

This result does not add or graduate an NVMe protocol/opcode/status, queue,
BAR, PCI function, device DMA, MSI-X/IRQ, VFIO path, QEMU device, raw block
backend, real power-loss/PLP behavior, filesystem-independent durability,
same-instance concurrency, 32-bit/freestanding target, physical NAND or
bare-metal endpoint.

User-mode P execution proves the target Linux libc/syscall ABI against the
same Host kernel/filesystem; it is not QEMU storage-device evidence. No root
privilege, KVM, debug machine, extra SSD or raw media was used.

The fixed-profile result remains `REVIEW_PENDING` until the scheduled Cycle 03
ChatGPT Pro second-opinion review. That review is not certification,
recognition, endorsement or an independent test reproduction.
