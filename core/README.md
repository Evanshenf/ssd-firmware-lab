<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Portable firmware core

This BSD-3-Clause layer owns transport-neutral firmware policy and state. It
consumes only versioned, address-free contracts and must not include
Linux-private, QEMU, VFIO, filesystem or simulator-WAL interfaces.

## C3.1 implementation

C3.1 is explicitly open under the narrow scope and exit matrix in the
[gate-opening record](../docs/results/2026-08-29-c3-1-opening.md). It currently
contains:

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

## Validation

Run the unprivileged gates from the repository root:

```sh
make -C core check
make -C core check-clang
make -C core check-sanitize
make -C core check-thread
make -C core check-cross
make check
```

The cross gate executes the same unit, model and fuzz binaries on AArch64,
RISC-V and big-endian s390x user-mode runners. The gate-opening record remains
non-pass evidence until the final immutable result manifest is published.

## Still stopped

C3.1 defines no protocol queues, commands or statuses; no durability class;
no NAND page/OOB, ECC or timing behavior; no FTL, mapping, GC or persistent
media; and no BAR, DMA hardware, interrupt, PCI or virtualization claim.

The implementation is governed by the frozen lifecycle contract in
[ADR-0006](../docs/adr/0006-portable-command-lifecycle-contract.md). The
independent persistence policy in
[ADR-0007](../docs/adr/0007-command-durability-and-persistence-policy.md) is
not consumed until a later integration gate.
