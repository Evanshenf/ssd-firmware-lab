<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# NAND flash controller

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

Run the layer independently with:

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
