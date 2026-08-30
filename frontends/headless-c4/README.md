<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C4 headless memory-transport reference

C4.1 captures one caller-owned 64-byte command value, retains raw queue and
address fields privately, and emits an address-free canonical value. It also
encodes and decodes a 16-byte completion value from a private publication
context plus a portable completion intent.

This is a headless memory-transport reference, not a transport-neutral layer.
The portable policy boundary is transport-neutral; queue memory, command IDs,
data-pointer values, completion phase and publication placement remain here.

The raw-address/CID mutation corpus proves that changing private identity and
address values does not change the canonical bytes while their presence facts
remain unchanged. Literal byte vectors, not encode/decode round trips, are the
primary codec oracle.

```sh
make check-c41
make fake-link-c41
make check-all
```

C4.1 has no queue walker, doorbell, PRP graph, DMA engine, interrupt, PCI
register, QEMU adapter or storage backend.
