<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# NVMe FWLab — SSD Firmware Development and Simulation

[English](README.md) · [简体中文](README.zh-CN.md) ·
[Project website](https://evanshenf.github.io/ssd-firmware-lab/) ·
[Getting started](docs/getting-started.md) ·
[Preview release](https://github.com/Evanshenf/ssd-firmware-lab/releases/tag/v0.1.0-spine-preview.1)

**NVMe FWLab (`ssd-firmware-lab`) is an open-source NVMe SSD firmware development
and simulation lab on Linux.** Study and modify portable C firmware, a flash
translation layer (FTL), garbage collection (GC), a NAND flash controller (NFC)
model, and persistent NAND page/OOB storage without a dedicated SSD development
board. 开源 NVMe SSD 固件开发与仿真实验室：在 Linux 中研究 FTL、垃圾回收、NAND 控制器与持久化恢复。

An experimental software PCI/NVMe controller already connects the native Linux
NVMe driver to this firmware/storage path. A QEMU guest can become the alternate,
exclusive owner of the same software function. This is **pre-alpha research
software**, not firmware to flash onto a commercial SSD.

The current release, **v0.1.0-spine-preview.1**, is a reviewed, fixed-profile
software preview: **one 1-MiB namespace, file-backed NAND and finite runtime
budgets**. Large-capacity and production-readiness claims are not made. See the
[exact-source results and remaining limits](docs/results/2026-09-05-vertical-spine-preview.md).

**Development source has advanced beyond that tag.** Adopted changes through
`a6ee009` add scalable FTL, physical NAND v2/PAGE2, and an explicit 64-MiB native
profile with up to 1-MiB I/O, two I/O queues and three MSI-X vectors. MQ2 still
has one global I/O frame and serialized storage execution. Capacity construction
now shares 64/256/65536-MiB presets. The current PAGE2/v2 path passed a full
64-GiB ARM64 headless journey, and a 256-MiB native namespace passed Linux
reset/recovery and ext4 mount checks. Subsequent ARM64 native work connected the
Linux driver to a 64-GiB namespace and exercised full-volume data, controls and
fixed-extent performance, followed by ext4 fsync/reset/rebind, true ENOSPC,
reallocation and final file-hash checks. These are bounded native L1 results,
not production or ARM guest-owner qualification. See
[capacity evidence](docs/results/2026-09-10-current-capacity.md) and the
[exact-source ARM native results](docs/results/2026-09-13-native-arm64.md).
See [current constructions and platform limits](docs/current-status.md),
[development evidence](docs/results/2026-09-10-scaled-storage-mq2.md) and
[performance samples](docs/results/2026-09-10-throughput.md). This is development
publication, not a new frozen release or a change to the old tag.

The newer opt-in channel/worker path now has64/256/65536MiB capacity
construction. Its [current64/256MiB software checks](docs/results/2026-09-15-channel-capacity.md)
ran with real FTL3/NFC/shards on x86-64 and ARM64, but simulated Host ioctls.
The subsequent [256 MiB ARM64 native episode](docs/results/2026-09-22-native-arm-channel-256.md)
passed real-driver I/O, four-worker reset/reconstruction and cold recovery.
New64GiB channel-native operation remains unqualified; the older R0 result
above does not substitute for it.

## Try the software path on Linux

Start with an ordinary, unprivileged Linux user. You need Git, GNU Make and a
C11 compiler with Linux/POSIX development headers. No FPGA, kernel module, KVM
or raw disk is required for this first check.

```sh
git clone --branch v0.1.0-spine-preview.1 https://github.com/Evanshenf/ssd-firmware-lab.git
cd ssd-firmware-lab
make -C frontends/linux-m4 check-runtime
```

This builds and runs existing headless integration checks through the real
firmware, FTL, NFC and temporary file-NAND. It covers ordinary I/O, repeated
overwrites, GC, Flush, recovery and bounded resource handling. It **does not
create `/dev/nvme*`**; `native_hif=not_connected` is expected in this test.

The [getting-started guide](docs/getting-started.md) explains dependencies, output,
the broader software check, and the separate privileged native NVMe experiment.

## What works in the tagged preview

| Capability | Executed software behavior |
| --- | --- |
| NVMe command path | Linux initialization, Identify, minimal SMART, queue setup, Read, Write, Flush and write FUA |
| FTL and reclamation | Logical-to-physical mapping, partial-page read-modify-write, out-of-place writes and foreground GC |
| NAND controller model | Staged physical operations, channel/LUN/plane scheduling, modeled ECC/retry and deterministic faults |
| Persistent media | NAND pages, out-of-band (OOB) data, health state and physical-operation write-ahead log |
| Recovery | Same-image recovery, checkpoint/GC interruption cases and continued writes after restart |
| Native and guest ownership | Native Linux and sequential Host → QEMU Guest → Host journeys through the same firmware/storage implementation |

These are scoped results, not a full NVMe implementation, silicon-accurate timing
or real hardware power-loss qualification. Detailed case coverage and tested
build identities are in the [release results](docs/results/2026-09-05-vertical-spine-preview.md).

## The NVMe-to-NAND data path

```text
Native Linux nvme / alternate QEMU guest nvme
  → synthetic PCI/HIF: BAR, queues, Host transfers, completion and IRQ
  → NVMe profile + shared command lifecycle
  → Block service → FTL / GC / mapping journal / recovery
  → NAND flash controller model
  → persistent physical NAND pages + OOB + health/WAL
  → completion intent → HIF CQE / IRQ
```

BAR and controller state live in volatile memory. `nand.bin` is the persistent
**simulated physical NAND medium**, not executable firmware and not an LBA image
that bypasses FTL/NFC. Logical namespace reads and writes traverse the firmware,
FTL, NFC and NAND model.

The native adapter consists of Linux kernel PCI/HIF modules and a userspace
firmware process. Guest assignment uses upstream `vfio-pci`, IOMMUFD and QEMU,
not a second QEMU NVMe executor or a custom VFIO ABI. Host and Guest never own
the function concurrently. Ownership transfer requires stopping writers,
explicit Flush/FUA, holder cleanup, revoke/drain and zero-reference checks
before the next grant. See the [native guide](kernel/m4-native/README.md).

## Where to read and change the firmware

| Area | Source entry |
| --- | --- |
| NVMe profile policy and payloads | [Linux-profile-v1 adapter](core/command-spine/profiles/linux_profile_v1_adapter.c) |
| Command lifetime and completion | [Shared lifecycle](core/command-spine/spine_lifecycle.c) |
| FTL mapping and media metadata | [Mapping](core/m3p/m3p_mapping.c), [codec](core/m3p/m3p_codec.c) |
| Garbage collection and restart | [GC](core/m3p/m3p_gc.c), [recovery](core/m3p/m3p_recovery.c) |
| NAND controller behavior | [NFC model](nfc/README.md) |
| Persistent NAND substrate | [File-NAND implementation](media/file-nand-v0/) |
| Linux runtime and device binding | [Firmware frontend](frontends/linux-m4/README.md), [PCI/HIF](kernel/m4-native/README.md) |

For the newer path, start at [scalable FTL](core/ftl-scale/README.md),
[PAGE2 NFC](core/nfc-page-v2/README.md), [physical NAND v2](media/file-nand-v2/README.md)
and [native scaled/MQ2 startup](docs/native-scaled-usage.md). The table above
retains the tagged preview's reference implementation.

## Current limits and future work

- The preview has one 1-MiB namespace, 512-byte LBAs, an 8-KiB transfer limit and
  one I/O queue pair of depth 32. It is not a general-purpose disk for real data.
- Command/record/media-operation budgets are finite. Reopening an image does
  not erase persistent history or establish indefinite endurance.
- The tagged native integration was tested on a disposable x86-64 Linux VM with Ubuntu
  `7.0.0-30-generic` and a specifically reserved 16-KiB BAR aperture. It is not
  a drop-in module for arbitrary Linux kernels or production hosts. Development
  ARM64 evidence has its own EFI preparation and scope in the current result page.
- Larger native capacity, richer FTL/wear-leveling, exclusive raw-block backing,
  RTOS ports and real FPGA/SoC/NAND adapters are future work, not released features.
- Physical NAND requires a concrete metadata/erase-generation recovery contract;
  simply replacing the file backend does not prove hardware portability.
- The release records an unconfirmed concurrent-FLR publication-window risk;
  successful named reset tests do not establish exhaustive race coverage.

The worker creates a new NAND image only with explicit `--format`; normal
startup recovers an existing UUID-bound image. Do not point experiments at a
physical SSD or unrelated data. Read [SECURITY.md](SECURITY.md) before native work.

## Contribute or report a reproduction

[Open an issue](https://github.com/Evanshenf/ssd-firmware-lab/issues) with the
release/commit, Linux environment, command, expected result and actual output.
Please redact host identities and private paths. Useful contributions include
reproducible examples, documentation and bounded firmware/FTL improvements.
Follow [CONTRIBUTING.md](CONTRIBUTING.md) for source boundaries and sign-off.

Development is AI-assisted; see [AI_ASSISTED.md](AI_ASSISTED.md). AI assistance
does not replace source review, executed tests or accurate capability claims.

## Architecture and historical records

Start with the [current release evidence](docs/results/2026-09-05-vertical-spine-preview.md).
Earlier component checkpoints are historical references, not additional current
capability claims.

- [Requirements](docs/requirements.md), [architecture](docs/architecture.md) and [roadmap](docs/roadmap.md)
- [Architecture decisions](docs/adr/README.md)
- [Upstream VFIO route](docs/adr/0009-upstream-vfio-route-and-milestones.md) and [Linux HIF boundary](docs/adr/0010-linux-hif-portable-executor-contract.md)
- [Earlier lifecycle](core/README.md), [persistence](core/c32/README.md), [headless firmware](frontends/headless-c35/README.md), [NVMe policy](core/c4-nvme/README.md) and [queue/HIF reference](frontends/headless-c4/README.md)
- [Initial risk plan](docs/m0-plan.md) and [nested-KVM topology](docs/lab/pve-nested-kvm.md)

## License and independence

Original userspace source, schemas, scripts and tests use **BSD-3-Clause**;
Linux kernel source under `kernel/` uses **GPL-2.0-only**; Markdown documentation
uses **CC-BY-4.0**. See [LICENSES.md](LICENSES.md) and per-file SPDX identifiers.

This independent project is not affiliated with, endorsed by or certified by
NVM Express. It uses no official logo and makes no certification claim.
