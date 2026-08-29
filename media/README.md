<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Persistent physical media

C3.4 provides a BSD-3-Clause ordinary-file prototype under `c34-file/`. It
stores page/OOB bytes, erase generations, wear/bad-block state, an explicit
physical-operation B/A/C WAL and self-contained physical checkpoints in an
exact 64-KiB image.

The engine accepts physical operations only. It cannot contain an authoritative
decoded logical map or silently repair firmware state. Its POSIX adapter accepts
an already-opened, newly created, unlinked regular file; it rejects devices,
directories, linked existing targets and wrong-sized restart images. No path,
raw-block initializer, discard or device ioctl is implemented.

`fdatasync()` is used only as an ordinary-file substrate barrier. Modeled crash
outcomes come from explicit persistent transitions and restart images; no test
equates Host synchronization with NAND completion, firmware `C_map`, power-loss
protection or real power failure. The separation follows
[ADR-0002](../docs/adr/0002-power-domains-and-persistence.md).

Run:

```sh
make -C media check
make -C media/c34-file check-all
```
