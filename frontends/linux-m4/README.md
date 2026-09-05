<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Native Linux firmware binding

This work area connects the synthetic PCI/HIF to the portable firmware,
M3-P, NFC and physical file-NAND path. The current implementation supplies
the runtime prerequisites; native PCI/HIF is not connected yet.

The shared lifecycle has a private publication extension. A completion lease
retains the immutable intent until publication has a known outcome. Profile
retirement then allows command-slot reuse. Host queue occupancy and pending
notification remain HIF responsibilities. Old lease and command identifiers
are not reused when a storage slot is recycled.

The runtime accepts a Host binding at construction. Its default headless
binding snapshots supplied input bytes. A referenced Host binding receives an
origin and exact shape with no input array; the later DMA action reads the
Host bytes through that binding. Firmware, FTL and NFC retain their existing
address-free interfaces.

Run the current prerequisite matrix with:

```sh
make -C frontends/linux-m4 check-runtime
make -C frontends/linux-m4 check-runtime CC=clang
```

It uses the real two-profile firmware/media fixture, publishes and reclaims
64 commands, discards one lease during close, and checks a referenced Host
binding whose bytes change between admission and DMA. That binding is an
adjacent test provider, not native DMA evidence. The final marker explicitly
reports `native_hif=not_connected`.

Kernel integration will provide the referenced Host binding, queue effects and
CQE publication. The kernel owns PCI/BAR, mappings, Host transfers and IRQ;
the userspace process owns portable firmware and physical file-media policy.
