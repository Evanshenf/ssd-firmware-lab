<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Vertical-spine experimental preview

Status: **reviewed fixed-profile software preview**. Source candidate:
[`735aaeb632b4155f44890bbc51fbd3b8b9d59cc1`](https://github.com/Evanshenf/ssd-firmware-lab/commit/735aaeb632b4155f44890bbc51fbd3b8b9d59cc1),
tree `40c39a54ac32100a6e305a26926a912204935b52`.
The release closure adds documentation only; its firmware and test source are
identical to this candidate. This is not production readiness, a performance
result, full M3/M4/M5 feature graduation or physical-hardware certification.

## What is connected

```text
native Linux nvme / QEMU guest nvme
 -> synthetic PCI/HIF queue capture
 -> Linux-profile-v1 -> shared command-spine lifecycle
 -> aggregate Block -> M3-P FTL/GC/recovery -> C3 NFC
 -> persistent file-NAND pages/OOB/health/WAL
 -> completion intent/lease -> HIF CQE/IRQ
```

The same software function switches exclusively between Host and Guest through
upstream vfio-pci, IOMMUFD and QEMU/KVM. The portable firmware, FTL, NFC and media
path remain the same implementation. There is one linked SQ consumer and one
CQE publisher; old whole NVMe/media fixtures are excluded. The separate C43-P1
reference binding also executes through the shared lifecycle and storage path.
Its presence does not make the retired C43 graph the native executor.

BAR/controller state is volatile memory. `nand.bin` stores simulated physical
page/OOB data, block-health state and persistence/recovery records, not executable
FTL/NFC firmware. The worker contains that code. Namespace I/O does not bypass
FTL/NFC to access the image by logical LBA.

## Fixed delivery envelope

- One 1-MiB namespace, 512-byte LBAs, one I/O queue pair, depth 32 and 8-KiB maximum
  transfer; direct PRPs and the bounded two-entry PRP-list shape.
- Basic Linux initialization, Identify/SMART/queue setup, Read/Write/Flush and
  write FUA; no complete NVMe command-set, SGL or arbitrary-geometry claim.
- Persistent file-NAND only. Exclusive raw-block backing and capacity growth
  are separate later tasks; no raw disk was formatted or used for these results.
- Finite LAB budgets, not indefinite operation or endurance: 65536 NFC/record
  identifiers, 4096 Host mutation sequences, corresponding lifecycle limits and
  65536 physical-media transactions. Persistent sequences are not cleared by
  reopening an image. Resource exhaustion is a defined terminal outcome.
- Target native integration: disposable x86-64 Linux VM with the tested Ubuntu
  `7.0.0-30-generic` kernel and explicitly reserved 16-KiB BAR aperture. No other
  kernel, bare-metal DMA-master or real NAND portability claim is made.
- Ownership transfer currently uses DRAIN_ONLY: stop writers and use explicit
  Flush/FUA before transfer. Volatile runtime state may be reconstructed after
  reset; same-function claims bind function/media/build identity, not RAM objects.

## Executed evidence

| Boundary | Result on the candidate |
|---|---|
| Current software path | Fresh GCC/Clang S0-B/J0-A/J0-B semantics, worker/client builds and runtime pass |
| Empty Flush | Repeated after format and same-image recovery; valid zero frontier, no fabricated NAND operation |
| Continued storage use | Fill 256 pages, two complete overwrites, free=0/1/2 reclamation boundaries, Flush and 4096 reads |
| Recovery and reuse | Same-image all-256-page readback, checkpoint-covered GC restart, 64 further ordinary overwrite/Flush cycles and full comparison |
| GC interruption | Active-GC close and exits before switch / after switch-before erase; existing 24-live and single-replacement recovery also pass |
| Budget boundary | Reference: 1002 reads followed by SC6/SCT0/DNR1, successful final Flush and zero-holder close |
| Native Linux boundary | Actual ioctl: 10721 reads then 0x4006/DNR1; error buffer unchanged, final Flush successful, no timeout/reset used for that boundary |
| Native transport | 512 B/4 KiB/offset-512 8 KiB, 64 continuing reads, DMA-in/out/pre-CQE reset cuts, PCI BME/MSE, masked-PBA/unmask |
| Same-function ownership | L1 writes A, QEMU L2 reads A/writes and Flushes B, L1 reads B |
| Coordinator interruptions | Pre-grant QEMU termination and post-grant coordinator termination, automatic revoke and data readback |
| Four J3 cases | Old DMA/map/publication/IRQ reject; old bytes/CQ/PBA/eventfd unchanged; current Identify/CQE/IRQ succeed |
| Final cleanup | Every named epoch drain has zero Host/DMA/buffer/Block/NFC holders; final NO_OWNER |

Local software tests are not relabelled as native or physical power-loss tests.
The native/QEMU rows were separately executed on the target. Private raw lab
logs are retained; the following digests identify that evidence, not independent
third-party attestation.

| Artifact | SHA-256 |
|---|---|
| C2 source archive | `3c5142a50fccacb9058587b07c314894bad485438246685c60a03b9e203afa4a` |
| All tracked-source manifest | `eac3dc204f28c6c31b1d5e78b52856478a3f6b91082f4a6d96af4583935fd72b` |
| Target worker | `81465def6f2cba6a2231b4258f551c0e33b26711dd5322fc69167095f8eb5349` |
| Target module/worker binding | `f123cf293e18e1e9ca36df0957724bf55d35c7d5ea5c675335bc8d37a367f8bd` |
| Target runtime log | `4d2e08fdc73c7af7b33925ccdf68a8fb0e2dac255c01a013f317af03e955bfcf` |
| Native budget log | `c352a6cc4ac32c00cf010cbf7d5821e9ab467baf9a98dd99c1051e64eefb5276` |
| J3 log | `b3ecb3a5fe5f62097b5166e0c99ab0b2def2e435b0db0d4f6daaec33b833b327` |
| QEMU A/B log | `bd2a2b2d0da7049a4ecb1761dece329c9b3000bc629dacc96d33fc28c2bb7037` |
| Worker/epoch-drain log | `cc97a49b40db39b7f2915def72b02208fe43a7d67a30c34e0b7da6d4694bf258` |

Exact-candidate hosted results all succeeded:
[current-spine 33970404160](https://github.com/Evanshenf/ssd-firmware-lab/actions/runs/33970404160),
[policy-smoke 33970404181](https://github.com/Evanshenf/ssd-firmware-lab/actions/runs/33970404181),
[c4-portable 33970404229](https://github.com/Evanshenf/ssd-firmware-lab/actions/runs/33970404229).
The metadata-only release closure receives local policy/ancestry/source-integrity
checks, not another full semantic matrix.

## Review and remaining limits

The restored review focused on ordinary supported use. It found and closed empty
Flush failure, missing production GC admission, zero-live GC/recovery defects,
permanent-credit backpressure and checkpoint-covered DATA/RESERVE role loss.
The latter was reproduced as the 41st post-restart overwrite failing far below
all budgets; the final recovery helper and 64-write regression close that path.
Finite trace storage was handled by an NFC-owned, operation-free diagnostic
consumer; trace sequence, operation UID, cache, epoch and media are not reset.

Independent exact-source confirmations and a ChatGPT web Pro targeted second
opinion accepted the original/induced fixes. Pro's decision was
`TARGETED_CONFIRMED_CLOSED / ENOUGH`, conditional on final exact-candidate CI;
that condition is now satisfied. The review is AI-assisted engineering evidence,
not vendor approval or a guarantee of absence of bugs. There is no additional
discovery round or required clean-review streak for this preview.

Two explicit boundaries remain:

1. Real NAND is not a backend-only replacement. Current erase-generation truth
   comes from simulated health slots and the media WAL/checkpoint engine. Durable
   ownership/recovery of that information on physical NAND, including blank
   blocks and interrupted erase, remains a future contract.
2. A concurrent PCI FLR between publication's initial check and MSI-X preparation
   remains an unconfirmed worker-rejection/quarantine risk. The exact interleaving
   lacks a full-system witness and is not claimed closed by earlier reset cuts.
   Named reset/rebind/owner tests passed; exhaustive concurrent-FLR coverage is
   not asserted. No speculative production patch was used to manufacture closure.

The existing 1440-byte ioctl-frame compiler warning is retained (tested target
kernel stack: 16 KiB); it is not itself a demonstrated stack overflow.

## Reproduce and continue

From a Git checkout, run `sh scripts/check_current_spine.sh`; `CC=clang` selects
the other compiler. This invokes existing semantic ELFs with real digest inputs.
The old frozen whole-stage leaf/cap checks retain their historical scope and
are not silently widened to approve evolved code.

For native lab prerequisites, build, explicit first-format versus recovery and
cleanup, see the [native guide](../../kernel/m4-native/README.md) and
[firmware binding](../../frontends/linux-m4/README.md). Do not load this
experimental endpoint on a production host or point its tests at real data.
Capacity growth, raw backing, richer FTL/WL/NAND and protocol breadth remain
separate bounded future work; they do not reopen this fixed preview by themselves.
