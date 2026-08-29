<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# C3.4 crash-consistent mapping coordinator

This private portable component joins the frozen C3.1 lifecycle envelope,
C3.2 persistence witnesses and C3.3 physical NFC completions. One C3.1
provider operation owns an entire firmware command graph; individual NAND
transfers remain C3.3 operations. The coordinator never publishes a queue
completion or interrupt.

The deliberately finite proof profile contains two 16-byte logical atoms,
three four-page data blocks, one four-page journal block, two firmware
checkpoint blocks and one single-live-page relocation. Firmware metadata uses
an explicit little-endian 96-byte main plus 64-byte OOB encoding with CRC-32C.
Recovery starts from raw page/OOB and block state only; RAM maps, Host caches
and the file backend's physical WAL are absent from the recovery API.

Supported semantic flows are read, write, trim, a fixed-frontier durability
fence, two-slot checkpoint rollover and one bounded relocation. Only no-PLP
cache-enabled and cache-disabled profiles are accepted. This is a finite
correctness profile, not a production FTL, performance result or real-NAND
format.

Run the component gates with:

```sh
make -C core/c34 check
make -C core/c34 check-all
```

The native flow suite composes the unmodified C3.1, C3.2 public policy and C3.3
model sources. The cross gate executes portable codec, flow, bounded-model and
file-byte-engine programs on AArch64, RISC-V and big-endian s390x runners.

Still excluded are protocol commands and queues, general GC or wear leveling,
validated PLP, Host addresses, DMA, IRQ, BAR, PCI, VFIO, QEMU, raw block media
and any physical power-loss claim.
