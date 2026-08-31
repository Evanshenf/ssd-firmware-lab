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

## C4.2 headless queue HIF

C4.2 adds a caller-serialized, fixed-arena queue HIF around the frozen C4.1
codec. The fixed profile has one Admin queue pair and one I/O queue pair,
physical depths from 2 through 32, and usable SQ/CQ capacity `depth - 1`.
Queue memory and the future command graph remain separate providers:

```text
fake 64-byte SQ / 16-byte CQ memory
                 |
                 v
capture-once queue HIF -- opaque origin / CID generation map
                 |
                 v
address-free command port -- graph-owned handle / ticket / lease
```

The HIF advances an SQ head only after the command port proves admission
committed. A CQ entry stages bytes 0..13 and byte 15 first, publishes byte 14
(status/phase) last, and releases the active CID only after the command port
also proves consume committed. Host CQ-head events that arrive in the physical
marker/reconcile window are latched and applied only after that cross-layer
proof. Queue reset ends at `COLD_NO_QUEUES`; the caller must create, scrub and
enable fresh Admin queues explicitly.

```sh
python3 ../../scripts/run_c42_gate.py
make check-c42
make fake-link-c42
make check-all
```

`run_c42_gate.py` is the authoritative native C4.2 execution boundary. It
invokes one fixed Makefile with a clean environment and then independently
requires 13 distinct, fresh ELF programs, their expected runtime markers and
an exact receipt. Direct Make targets remain useful defense-in-depth and
developer workflows, but dry-run/touch/extra-makefile invocations are not
evidence.

The C4.2 gate covers exact depths 2/3/4/32, 12 bounded-model families, 31
production-source mutations, nine architecture mutations, 11 provider
variations, 33 reset cut points, 64 deterministic fuzz executions and 64
different-instance thread repeats.
It does not implement NVMe command legality, Admin commands, PRP/data DMA,
storage, BAR/MMIO, interrupts, PCI, QEMU or vfio-user.
