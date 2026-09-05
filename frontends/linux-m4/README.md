<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Native Linux firmware binding

This work area connects the synthetic PCI/HIF to the portable firmware,
M3-P, NFC and physical file-NAND path. The native worker and owner-control
binding are implemented. The integration is experimental; implementation and
lab execution do not themselves establish reviewed architecture graduation.

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

The native binding provides the referenced Host transfer, queue effects and
CQE publication through the private kernel interface. The kernel owns PCI/BAR,
mappings, Host transfers and IRQ; the userspace process owns portable firmware,
FTL/NFC and physical file-media policy. See the [native integration guide](../../kernel/m4-native/README.md)
for the required lab environment, media identity, startup and cleanup rules.

`worker` builds the firmware process; `native-io` builds the separate native
integration client. Its `owner-stale` mode runs exactly four J3 stale-authority
checks using two sequential VFIO/IOMMUFD attachments on the same function.
It reuses fixed IOVAs, completion slot/lease number and eventfd descriptor,
then requires both old-key rejection and successful current completion. The
private canary controls are inert unless explicitly armed by the exclusive
firmware/control owner. The producer submits Identify only and contains no
NVMe command executor, FTL or logical-media shortcut.
