<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Cycle 03 portable firmware evidence manifest

- Date: 2026-08-29
- Cycle result: five narrow portable/headless sub-gates passed
- Current disposition: **REVIEW_HOLD / C3.5a REQUIRED**
- Post-review erratum:
  [C3.5 review hold](2026-08-29-c3-5-review-hold.md)
- Evidence profile: unprivileged behavioral/Host-native portable firmware and
  user-mode cross-ISA execution

This manifest freezes the five-gate Cycle 03 chain without extending any
result into an NVMe transport, hardware DMA/interrupt, PCI endpoint, physical
power-loss or raw-media claim.

The five recorded sub-gate runs completed, but Cycle 03 is not graduated. A
post-review source audit found three Critical C3.5 failure-path defects; C3.5a
must close them before the cycle can leave `REVIEW_HOLD`.

## Immutable gate identities

| Gate | Scope | Source commit | Evidence freeze |
| --- | --- | --- | --- |
| C3.1 | portable command lifecycle | `df13d3747ed01b1d7895f9c7a0ccc63072410dd4` | `c0fd2b3b8af03a8c01c215fb2192e6c14223637f` |
| C3.2 | executable persistence policy | `73ec2419103d202ce8f270f2f20ce34c292f73f9` | `f08e3c236675d1e13fe5480a489efff5a1853dbb` |
| C3.3 | programmable NAND/NFC model | `1e3594ad3068ed3991e28589db14420ef59ca6c2` | `e487e09d7bb320e36d439ce4f4c507ed10789b27` |
| C3.4 | crash-consistent mapping and ordinary-file media | `9cc2f9093585e1dc382b93570a8cff536225bb6e` | `9f22e6ddebfcc77a720a761e3b07b302716f9334` |
| C3.5 | integrated fixed-profile headless graduation | `48567dae4f3246c2eddb83a28a30c526947dbc86` | this evidence transaction |

Each gate consumed earlier source as frozen input. C3.5 added only a headless
frontend, private bindings, test fixtures/models and checkers; it did not
modify C3.1-C3.4 source.

## Evidence chain

### C3.1 lifecycle

- caller-owned fixed arena and serialized per-instance executor;
- full generational command/operation/lease/ticket identities;
- immutable completion intent and one-use publication lease;
- epoch-first reset, bounded drain and teardown;
- 17 unit cases, 1,944 bounded traces and 5,000 deterministic fuzz iterations;
- deterministic cross-ISA/cross-endian codec and no heap/thread/clock
  dependency.

### C3.2 persistence policy

- value-only public facts, obligations, PLP envelopes and witnesses;
- logical recovery receives no physical ledger, Host cache or firmware RAM;
- 39 runs, 637 states and 1,912 recovery cuts across 11 closed grammars;
- all 13 invariant bits covered and 13 shortest broken counterexamples;
- positive hash `b399ec240bd8838f`, negative hash `216e7d7d0d5a88ed`.

### C3.3 programmable NFC

- staged READ/PROGRAM, configurable geometry, main/OOB truth, erase/wear/bad
  block behavior, ECC/retry, deterministic faults and virtual-time resources;
- 13 model families, 402 states, 804 cuts and 18 shortest counterexamples;
- two-instance model/media isolation and explicit little-endian codecs;
- functional in-memory model only, not physical NAND timing/endurance.

### C3.4 mapping and file media

- two-atom crash-consistent mapping derived from raw page/OOB authority;
- actual C3.1 lifecycle, C3.2 witnesses and C3.3 NFC operations;
- exact 64-KiB ordinary-file B/A/C container and full anonymous-fd restart;
- 138 integration states/414 cuts and 257 file prefixes/771 restart cuts;
- no raw device, real filesystem power-loss or PLP claim.

### C3.5 integrated headless graduation

- separately linked S/M/B/P lanes consume one deterministic frozen-source
  firmware archive;
- exact tiered byte equality for lifecycle, semantic/raw and container views;
- 126 M/B reset executions, with old/new recovered outcomes determined by raw
  authority and no third value;
- 13 composition families, 243 states, 350 transitions and 74 stale probes;
- 924 unique actor-choice strings forming 5,544 labeled reference-product
  cases and 20,586 within-matrix unique prefix labels, with only 36 selected
  live twin schedules in the historical C3.5 source;
- 16 shortest new broken variants, vector `26665ac74fd7c4f9`;
- 192 barrier-started MM/BB/MB pthread twin runs under Clang TSan;
- native/GCC/Clang/ASan/UBSan/static-analysis gates and exact
  AArch64/RISC-V/s390x output equivalence.

Canonical C3.5 hashes:

```text
archive       b1f95f2787fe9a3d585f467d4ba72e6de32938c51273d4ad7f411dc1e644cf6f
life          0a5528a5f0f58ea165c88e8bdb4396c8c8902b794afa49f5187704db32dc3f8f
semantic/raw  8676761e2f91a8617983629e57af0b7376179271d76d6f699664143ca704ebea
container     d039a57e1adba3270ac8cc53a4376bca48218905a2bea0d72f0655b5b2a246b2
```

## Two explicit limitations

First, the scripted S lane proves only the C3.1/headless lifecycle envelope. It
does not write NAND/OOB, produce a physical receipt or participate in
storage/restart equivalence.

Second, the full C3.4 firmware remains one fixed geometry. The separate C3.3
dual-geometry test proves NFC provider/media isolation only. A versioned
multi-geometry FTL profile is future work.

Before post-review this was labeled `GRADUATED_FIXED_PROFILE / REVIEW_PENDING`.
That label is withdrawn; the current disposition remains
`REVIEW_HOLD / C3.5a REQUIRED` until a separate remediation closure.

## Architecture and safety closure

The deterministic firmware archive has an exact 12-object allowlist, no global
writable/BSS/common symbol, and no heap, thread, clock, random, file or
transport symbol. Link-map checks prove that S pulls only C3.1, M has no file
engine, B has no POSIX adapter and the isolated P adapter does not own fd
lifecycle.

All test files are caller-owned memory or newly created, immediately unlinked
regular files. Successful test transactions close exact fds and remove build
artifacts through component clean targets. No privileged operation, KVM,
debug/lab machine, physical SSD or raw device participated in Cycle 03.

## Review and next-step lock

Cycle 03 has reached its five-gate review cadence. The next action is the
scheduled file-based ChatGPT Pro architecture second opinion. The private raw
review is not public evidence and cannot be described as independent
reproduction, certification, endorsement or official recognition.

A Critical review finding places the cycle in `REVIEW_HOLD` and requires a
separate C3.5a correction. Without a Critical finding, Required and Advisory
items become an explicit future backlog. This manifest does not authorize
Cycle 04 or select its transport boundary.

Still outside the graduated scope: NVMe protocol/queues, hardware DMA,
IRQ/MSI-X, BAR/PCI, VFIO, QEMU device integration, Host-native binding, raw
media, real PLP/power failure, physical NAND/FPGA, same-instance thread safety,
32-bit and freestanding execution.
