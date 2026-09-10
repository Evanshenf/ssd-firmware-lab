<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Development status and supported constructions

This page describes the adopted development source through `a6ee009`, not an
expansion of the immutable `v0.1.0-spine-preview.1` tag. The project remains
**pre-alpha research software**. Publishing source is not a production release
or a claim that every historical test was rerun on the newest revision.

## Capacity is not one global constant

| Construction | Namespace / transfer | Storage and evidence boundary |
|---|---|---|
| Tagged preview / ordinary `worker` | 1 MiB / 8 KiB, one I/O pair | Reference M3-P + C3 NFC + file-NAND-v0; [frozen results](results/2026-09-05-vertical-spine-preview.md) |
| Scalable headless FTL | 64 / 256 MiB qualification profiles; explicit retained parents up to 1 MiB | Real FTL/NFC/physical-media path; original full disk and tmpfs regressions are distinct records |
| Historical ARM64 scale campaign at `cec2c5d` | 64 GiB namespace | Full fill, half-volume overwrite and full recovered readback on tmpfs; not current MQ2, disk persistence or native ARM PCI evidence |
| `scaled-worker` / `scaled-pump-worker` | 64 MiB / 8 KiB | Scalable FTL format 2 + PAGE2-R0 + mapped physical-v2 on bounded tmpfs |
| `large-worker`, Host profile 2 | 64 MiB / 1 MiB, one I/O pair | Same storage stack, explicit PUMP construction, bounded PRP graph |
| `mq2-worker`, Host profile 3 | 64 MiB / 1 MiB, two depth-32 I/O pairs, three MSI-X vectors | Same storage stack, **one global I/O frame plus one Admin reserve**; serialized FTL execution, not parallel storage throughput |

Native workers intentionally select a fixed 64-MiB capacity. The headless FTL
is not still hard-coded to 1 MiB; nor does a 64-GiB headless result automatically
make the native namespace 64 GiB. Logical capacity, physical geometry, arena
budget, media-format version and Host transfer profile are separate choices.
See [ADR-0013](adr/0013-scalable-ftl-and-page-windows.md) and
[ADR-0014](adr/0014-native-profile-and-serial-mq2.md).

## Which real path executes?

```text
Linux nvme / alternate QEMU guest
 -> synthetic PCI/HIF -> native worker -> Linux profile
 -> shared spine lifecycle -> aggregate Block operation
 -> scalable FTL (mapping, RMW, foreground GC, journal/checkpoint/recovery)
 -> PAGE2-R0 NFC -> physical NAND main/OOB/health/transaction records
 -> Block result -> completion intent -> HIF CQE / MSI-X
```

BAR and controller state use volatile memory. The storage file contains
physical NAND state, not executable firmware or a logical LBA shortcut.
Physical-v2 uses reservation records and direct physical homes with ordered
barriers; it is not the old payload-redo format. NFC PAGE2-R0 is a functional
one-slot batch executor. It **does not inherit C3's calibrated timing, ECC/retry
or injected-fault coverage** merely because both are called NFC.

One kernel SQ consumer and CQE publisher remain authoritative. The old PoC's
whole NVMe executor and logical-file fixture are not part of that construction.
M5 changes the exclusive owner of the same function/media/implementation;
drained volatile runtime objects may be rebuilt under a new epoch.

## Platform and persistence support

| Environment | What has evidence | What is not established |
|---|---|---|
| Linux x86-64 userspace | Current software paths with GCC/Clang; existing targeted sanitizer runs | Arbitrary operating systems or production endurance |
| Linux ARM64 userspace | Native scalable-storage/lower-layer checks and the separately versioned 64-GiB campaign | ARM kernel M4/M5, every current profile/configuration |
| x86-64 native test VM | Ubuntu kernel `7.0.0-30-generic` (package `7.0.0-30.30`), 4-KiB pages, reserved 16-KiB BAR; named native/reset/owner/MQ2 cases | Bare-metal requester DMA, arbitrary kernels, an ARM native endpoint |
| Hosted cross CI | Historical C4 portable matrix; current lower PAGE2/media fixture entries are separately named | Full current-stack portability from the old C4 matrix alone |
| Regular-file disk backend | Earlier exact-source small-volume persistence/restart cases | Physical host power loss or performance of current mapped-tmpfs workers |
| Local tmpfs | Functional recovery after process interruption and explicitly scoped software throughput | Survival of reboot/power loss, physical NAND or SSD bandwidth |
| NFS backed by remote tmpfs | No deployed/qualified route claimed | Treating a non-tmpfs client mount as persistent disk evidence |

The mapped native construction requires tmpfs; its startup rejects another
filesystem rather than falling back. All modeled CRC, ordering, synchronization
and locking remain. Default `FWLAB_MEDIA_EXCLUSIVE=0` retains host-file checks.
The explicit `=1` development build narrows protection against out-of-band host
file changes; it does not remove NAND checks, synchronization or owner gates.
ISA-targeted CRC builds are also explicit, not the portable default.

## Remaining limits

- One namespace. No SGL, DSM/TRIM advertisement, extended SMART/AER, firmware
  update/Format, RAID, NVMe-MI or RTOS port is implied by the interfaces.
- Two queues do not mean two concurrent FTL requests: profile 3 has one global
  I/O payload credit. Full 1-MiB writes are not all-or-nothing atomic writes.
- The tested Linux Host may split a 1-MiB workload into eight 128-KiB commands;
  literal/guest evidence separately establishes actual single 1-MiB SQEs.
- Physical erase-generation recovery remains a hardware-port contract gap
  (P01). Real NAND is not a proven backend-only replacement. The previously
  disclosed concurrent-FLR publication-window question (N01) is not closed by
  successful named reset cases.
- The native worker still uses a bounded file, not an exclusive raw block
  device. A larger backend alone changes neither namespace nor FTL budgets.
- No 10-GB/s end-to-end, PCIe 4/5 saturation, 1–3% overhead or production-ready
  claim. See [separated performance results](results/2026-09-10-throughput.md).

Start with the [software guide](getting-started.md). Use the
[native construction guide](native-scaled-usage.md) only in a disposable,
explicitly prepared lab. The [development evidence index](results/2026-09-10-scaled-storage-mq2.md)
distinguishes old frozen results, adopted development evidence and pending
release-wide validation.
