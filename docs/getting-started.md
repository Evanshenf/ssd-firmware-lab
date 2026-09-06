<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Getting started with NVMe SSD firmware simulation

[Project overview](../README.md) · [中文介绍](../README.zh-CN.md) ·
[Published results](results/2026-09-05-vertical-spine-preview.md)

NVMe FWLab has two different entry points. Start with the software check unless
you specifically need the experimental host-visible PCI/NVMe function.

| Entry | Needs | What it demonstrates |
| --- | --- | --- |
| Headless firmware/storage check | Ordinary Linux user, compiler and Make | Real firmware → FTL → NFC → temporary file-NAND behavior, with test Host bindings |
| Native NVMe experiment | Dedicated supported Linux VM, reserved BAR memory, kernel headers and privileged setup | Linux `nvme` driver → synthetic PCI/HIF → the shared firmware/storage path |

Neither entry flashes a commercial SSD. The released namespace is 1 MiB and
runtime budgets are finite. A successful software test is not a native PCI,
real NAND, large-capacity or performance result.

## 1. Prerequisites

Use Linux with Git, GNU Make, GCC or Clang with C11 support, and Linux/POSIX
development headers. The broader aggregate also uses Python 3, binutils
(`ar`, `nm`) and GNU coreutils (`sha256sum`). The published hosted checks use
Ubuntu 24.04; that hosted environment is separate from the native module's
pinned experimental kernel.

On Debian/Ubuntu, the usual development dependencies are:

```sh
sudo apt-get update
sudo apt-get install git build-essential python3
```

Package installation is the only privileged operation in this software-only
example. Run builds and tests as your ordinary user in a writable checkout.
Do not install or load a kernel module for this first check.

## 2. Clone the published preview

```sh
git clone --branch v0.1.0-spine-preview.1 https://github.com/Evanshenf/ssd-firmware-lab.git
cd ssd-firmware-lab
git rev-parse HEAD
```

The expected release commit is
`4b2a56272e567a5c8071819f506e4a7bd0acac24`. The tag intentionally leaves you in a
detached-HEAD checkout for reproduction. Create your own branch before editing.
Keep full Git history: some repository checks resolve historical source objects.

This guide may be newer than the tag; the commands above target the unchanged
published firmware implementation, not unreviewed future capacity work.

## 3. Run the first firmware/storage check

```sh
make -C frontends/linux-m4 check-runtime
```

The target builds `frontends/linux-m4/build/j1_runtime_matrix` from the portable
firmware, FTL, NFC, media and test Host bindings. It creates temporary NAND image
files; it does not need root, KVM, kernel-module loading or a raw block device.

Successful output includes these existing markers:

```text
EMPTY_FLUSH_PASS
J1 runtime prerequisite: PASS
COVERED_GC_RECOVERY_PASS
PRACTICAL_STORAGE_PASS
REFERENCE_BUDGET_PASS
```

Markers carry additional fields. Require the **whole command to exit with code
0**, not just an early line containing `PASS`. The prerequisite marker includes
`native_hif=not_connected`: this is expected, since this executable tests the
software runtime with test Host bindings. It does not create `/dev/nvme*`.

The existing workload exercises two profiles, normal completion/retirement,
partial-page I/O, repeated full-namespace overwrites, automatic foreground GC,
metadata recovery and continued writes, GC interruption cases, and defined
resource-exhaustion behavior. Exhaustion is deliberately tested; it is not an
indefinite-operation claim.

For a second compiler, install Clang if needed and run:

```sh
make -C frontends/linux-m4 check-runtime CC=clang
```

## 4. Optional: run the broader existing software aggregate

```sh
sh scripts/check_current_spine.sh
```

This rebuilds and executes the existing lifecycle and FTL/NFC/media matrices
with their actual object/archive/executable digests. It also builds the native
worker and client, but **does not load or attach the native device**. The runtime
check from step 3 is included, so this is optional additional coverage, not a
second required onboarding step.

## 5. Native Linux NVMe and QEMU experiments

For an actual synthetic namespace visible to `nvme list`, follow the
[native PCI/HIF guide](../kernel/m4-native/README.md) and
[firmware binding guide](../frontends/linux-m4/README.md). This requires a
disposable x86-64 Linux VM, the tested kernel/headers, a deliberately reserved
16-KiB BAR aperture and explicit device/media identity checks. Do not load the
experimental modules on a production host or reuse a physical SSD as a target.

First creation uses explicit `--format`; subsequent startup must recover the
same UUID-bound image without that flag. Guest assignment is an exclusive
ownership transition, not concurrent Host/Guest use. Its current DRAIN_ONLY
policy requires prior explicit Flush/FUA and holder cleanup. See the native
guide rather than globally unbinding an NVMe driver.

The [release results](results/2026-09-05-vertical-spine-preview.md) distinguish
headless checks from executed native/QEMU cases and list remaining risks.

## What to report if the software check fails

[Open an issue](https://github.com/Evanshenf/ssd-firmware-lab/issues) with:

- `git rev-parse HEAD`, the compiler version and Linux distribution;
- the exact command and exit code;
- the first failure and relevant surrounding output;
- whether you ran the software-only check or a separately configured native lab.

Redact usernames, internal hosts, private paths and credentials. Do not upload
NAND images or raw infrastructure logs. Do not work around a failure by
disabling test assertions or pointing the test at another disk.

## 中文速读

先以普通 Linux 用户运行第 2、3 节命令，即可检查现有固件、FTL、NFC 与文件 NAND 的真实软件路径。
这不是创建系统可见 NVMe 设备的命令；输出 `native_hif=not_connected` 是预期现象。
判断成功需要完整命令退出码为 0，不能只看中途的 PASS。
原生设备实验另需专用虚拟机和明确预留的 BAR 空间。当前仍是 1 MiB、有限运行预算的软件预览，不适合存放真实数据。
