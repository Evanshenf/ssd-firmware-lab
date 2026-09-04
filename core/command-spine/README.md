<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Command-spine contracts

This component is the provisional S0-A seam between profile adapters, a future
profile-neutral lifecycle, typed action drivers and HIF construction.  S0-A
contains only the five versioned contract families, structural validators, a
literal construction manifest and bounded fake-adjacent checks.

The current C43 implementation remains unchanged.  The component does not
implement an NVMe controller, lifecycle executor, M3-P, NFC/media data path,
PCI transport, owner switching or physical completion publication.  Its fake
binding is `HARNESS/PROVISIONAL`, never a reusable or M4 binding claim.

Run the bounded contract gate with:

```sh
make check-s0a
```

The gate creates exactly three durable artifacts under `build/s0a`: the
contract archive, public-contract test and fake-adjacent link executable.
