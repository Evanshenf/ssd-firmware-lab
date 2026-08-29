<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3.2 executable persistence policy

This component is a transport-free, in-memory symbolic model of the frozen
ADR-0007 policy. It does not link the C3.1 implementation and does not publish
command completions.

The public boundary consists only of value-shaped persistence facts and pure
policy functions:

- `include/fwlab/contracts/persistence_facts.h` defines versioned profiles,
  requests, facts, PLP envelopes, obligations, witnesses and invariant IDs;
- `include/fwlab/portable/persistence_policy.h` exposes validation and witness
  functions with no allocator, thread, clock, transport or device dependency.

Tiny-model geometry, physical-operation ledgers, checkpoints, recovery state,
scenario grammar and broken variants remain private to this directory. The
recovery path is intentionally split in two:

```text
model state at a cut
  -> physical settlement (may read B/A/C ledger and protected PLP)
  -> logical image projection (ledger, RAM and Host cache removed by type)
  -> logical recovery
  -> 13 independent invariant checks
```

The deterministic BFS covers 11 closed scenario families. Every canonical
base state is checked at controller-reset, simulated-power-loss and
daemon-crash cuts; the Host-crash case is added to the Host-authority
metamorphic family. Canonical bytes are explicitly little-endian and hash
collisions are resolved by comparing the complete canonical byte sequence.
Capacity or depth exhaustion fails the gate rather than pruning a transition.

Each of the 13 named broken variants is searched by the same level-order state
machine. The first matching failure is emitted as a machine-readable shortest
counterexample with its complete action trace, cut, frozen physical outcomes,
durable floors, allowed sets and recovered atoms.

Run the component independently with:

```sh
make -C core/c32 check
make -C core/c32 check-clang
make -C core/c32 check-sanitize
make -C core/c32 check-thread
make -C core/c32 check-cross
```

C3.2 defines no NAND geometry, NFC operation, FTL layout, file/raw media or
protocol field. Passing this symbolic gate is not evidence of real PLP hardware
or physical power-cut behavior.
