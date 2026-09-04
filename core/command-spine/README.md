<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Command-spine contracts

This component is the provisional S0 seam between profile adapters, a
profile-neutral lifecycle, typed action drivers and HIF construction. S0-A
contains the five versioned contract families, structural validators, a
literal construction manifest and bounded fake-adjacent checks. S0-B adds one
fixed-capacity lifecycle implementation plus independently authored C43-P1 and
Linux-profile-v1 adapters.

Adapter argument and normalized-result slots are private to this component.
The fake lower layer resolves those slots through construction-time bindings,
copies payload bytes through PAYLOAD_FILL, and writes terminal results back by
exact reference. Abort candidates are appended to a fake-owned mailbox and are
read by a later lifecycle step; a driver callback never reenters lifecycle.

The current C43 implementation remains unchanged and is not linked into S0-B.
The shared lifecycle executes immutable action programs but contains no opcode,
namespace, queue, DMA, Block, FTL, NFC or media policy. The S0-B drivers and
tiny profile are test fakes. This component does not implement an NVMe
controller, M3-P/NFC/media data path, PCI transport, owner switching or
physical completion publication, and makes no Rule-of-Two, J0, M4 or M5 claim.

Run the bounded contract gate with:

```sh
make check-s0a
make check-s0b
```

The gate creates exactly three durable artifacts under `build/s0a`: the
contract archive, public-contract test and fake-adjacent link executable.
The fresh S0-B gate likewise leaves exactly three durable artifacts under
`build/s0b`: one lifecycle archive, one two-profile adapter archive and one
profile-matrix executable. Both gates are local and unprivileged.
