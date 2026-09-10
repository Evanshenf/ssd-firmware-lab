<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3 NAND flash controller reference

This directory is the retained C3 NFC model. The tagged tiny native worker and
original scaled format-1 construction use it; current scaled/large/MQ2 native
workers select [PAGE2-R0](../core/nfc-page-v2/README.md) instead. The two models
have separate contracts and evidence, not interchangeable feature coverage.

PAGE2-R0 executes one physical page group at a time with accepted payload/result
ownership and physical page/OOB/health/generation validation. It rejects
unsupported nonzero timing, retry and injected-fault settings. It does not
inherit the C3 features described below. The actual selected binding is in the
[architecture matrix](../docs/architecture.md#actual-storage-bindings) and
[source map](../docs/source-map.md).

C3.3 implements a caller-owned, transport-free programmable NAND/NFC model.
It provides staged read/program operations, exact channel/LUN/plane resources,
page and OOB truth, block erase, ECC/read-retry outcomes, bad blocks, wear,
integer virtual time and deterministic seeded faults.

The release-v1 media profile is deliberately narrow: abstract SLC, full main
page with optional full OOB, and one program opportunity per erase. The model
does not implement an FTL, mapping, GC policy, file/raw media, protocol command,
DMA, PCI or Host address.

Fake and model engines use one semantic provider contract. A private adapter
maps the frozen C3.1 opaque request token and lifecycle identity to that
contract while preserving the detailed physical completion in a sidecar.

Run this C3 reference layer independently with:

```sh
make -C nfc check
make -C nfc check-clang
make -C nfc check-sanitize
make -C nfc check-thread
make -C nfc check-cross
```

All timing values are normalized functional ticks. Fault seeds are replayable,
not calibrated probability or physical endurance claims. The model does not
claim pin-level ONFI conformance or real power-loss behavior.
