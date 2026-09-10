<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Portable firmware core

This BSD-3-Clause layer owns transport-neutral firmware policy and state. It
consumes only versioned, address-free contracts and must not include
Linux-private, QEMU, VFIO, filesystem or simulator-WAL interfaces.

## Current and reference implementations

| Role | Entry | Boundary |
|---|---|---|
| Current shared lifecycle and profiles | [command-spine](command-spine/README.md) | Address-free profile policy, finite aggregate actions and completion ownership |
| Current scalable storage | [ftl-scale](ftl-scale/README.md) | Mapping, retained Block parents, private GC/checkpoint/recovery |
| Current scaled native NFC | [nfc-page-v2](nfc-page-v2/README.md) | PAGE2-R0 one-slot functional page-group execution, not C3 timing/fault equivalence |
| Reference storage / legacy native constructor | [m3p](m3p/m3p.h) and [C3 NFC](../nfc/README.md) | Small fixed-profile path and existing image formats |
| Historical protocol oracle | [c4-nvme](c4-nvme/README.md) | Frozen C4/C43 scope; not the current native executor |
| Historical lifecycle oracle | C3.1 files in this directory | Narrow standalone lifecycle tests described below |

The native runtime also links shared construction/action code under
`frontends/headless-j0` and `frontends/headless-scale`; those files are not
test-only merely because of their directory names. The
[source map](../docs/source-map.md) distinguishes production code from adjacent
test drivers and historical references. No directory move or duplicated
runtime is required to use the current path.

## C3.1 reference

C3.1 passed its narrow [lifecycle gate](../docs/results/2026-08-29-c3-1-portable-lifecycle.md)
and remains a regression reference. Its original
[opening record](../docs/results/2026-08-29-c3-1-opening.md) is historical, not
the current project status. This directory's original reference contains:

- `c31.c`: fixed-arena command ownership, provider polling, completion leases,
  abort, reset, teardown and deterministic traces;
- `c31_codec.c`: explicit 96-byte little-endian descriptor codec;
- `fakes/c31_fake_dma.c`: capability-checked DMA with staged controller writes
  and bounded external prefix effects;
- `fakes/c31_fake_nfc.c`: lifecycle-only NFC fixture with no NAND geometry;
- `fakes/c31_fake_provider.c`: a second scripted provider implementation used
  for replacement, fault and duplicate-event tests;
- `tests/`: unit, bounded-model and deterministic fuzz gates.

The public native types and provider contract live under `include/fwlab/`.
Provider contexts are stable initialization bindings; command descriptors,
tokens, completion intents and traces contain no external address.

Every command has at most one lifecycle-only provider operation in C3.1. This
does not freeze a future command graph. The core has no worker thread, lock,
heap allocation, wall-clock progress or provider callback. All progress is
driven by serialized, budgeted `fwlab_c31_step()` calls.

## C3.1 reference validation

Run the unprivileged gates from the repository root:

```sh
make -C core check
make -C core check-clang
make -C core check-sanitize
make -C core check-thread
make -C core check-cross
make check
```

The cross gate executes these same C3.1 unit, model and fuzz binaries on
AArch64, RISC-V and big-endian s390x user-mode runners. It does not qualify
every newer FTL/PAGE2/native path on those architectures. For selected current
software-path checks, use [getting started](../docs/getting-started.md) and
`scripts/check_current_spine.sh`; named evidence remains in the
[results index](../docs/results/README.md).

## C3.1-only boundary

C3.1 defines no protocol queues, commands or statuses; no durability class;
no NAND page/OOB, ECC or timing behavior; no FTL, mapping, GC or persistent
media; and no BAR, DMA hardware, interrupt, PCI or virtualization claim.

The implementation is governed by the frozen lifecycle contract in
[ADR-0006](../docs/adr/0006-portable-command-lifecycle-contract.md). The
independent persistence policy in
[ADR-0007](../docs/adr/0007-command-durability-and-persistence-policy.md) is not
part of C3.1 alone. Its later fixed-profile integration is recorded in the
[Cycle 03 closure](../docs/results/2026-08-30-cycle-03-reviewed-closure.md).
