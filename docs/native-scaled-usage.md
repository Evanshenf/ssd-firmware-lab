<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Native scaled / MQ2 construction: operator sequence

This is a manual, experimental deployment guide, not a one-command installer
for arbitrary hosts. Start with [software checks](getting-started.md) and the
[current support matrix](current-status.md). Do not load these modules on a
production machine or point them at a physical SSD.

## 1. Choose matching builds

The tested native environment is x86-64, Ubuntu kernel `7.0.0-30-generic`
(package `7.0.0-30.30`), 4-KiB system pages and an explicitly reserved 16-KiB BAR
aperture. See [kernel preparation and owner control](../kernel/m4-native/README.md).
An ARM64 userspace result does not establish ARM native support.

For the adopted serial-credit MQ2 construction, build in a fresh checkout of
one recorded commit, as an ordinary user:

```sh
git rev-parse HEAD
make -C kernel/m4-native FWLAB_M4_PRODUCER=2 FWLAB_M4_HOST_PROFILE=3 W=1
make -C frontends/linux-m4 mq2-worker native-io-large
sha256sum kernel/m4-native/ssd_fwlab_native_pci.ko \
  kernel/m4-native/ssd_fwlab_native_iommu.ko \
  frontends/linux-m4/build/scaled-offline/fwlab_native_mq2_worker \
  > native-binding.sha256
sha256sum native-binding.sha256
```

Retain the manifest and use its digest as the worker's `--binding-sha`. It is
an operator-supplied build identity, not independent attestation. Do not mix
workers/modules from different profile or producer builds. A fresh build
directory avoids stale compiler-flag outputs. `mq2-replyloss-worker` is a test
executable and must not replace the production worker.

The portable/default CRC build does not request CPU-specific instructions.
The published native cost experiment explicitly added `-msse4.2` to the normal
compiler flags on a supporting x86 CPU; its rate is not a generic-build promise.
`FWLAB_MEDIA_EXCLUSIVE=0` remains the default. Lower-layer `=1` measurements
must not be silently attributed to this default native build.

## 2. Provision media separately from code and logs

This selected native constructor uses a private `nand.bin` on tmpfs. An
administrator provisions a dedicated tmpfs capped at 1 GiB, with private
ownership for the worker's effective user and `nodev,nosuid,noexec`. The
directory must exist before startup. Keep binaries, manifests and logs on
disk. Verify filesystem type, free space and available RAM before running;
do not substitute an unmounted directory, a slow system-disk directory or NFS.

The image is 89165824 bytes: 80 MiB physical NAND main area plus physical
records/OOB/health/transaction metadata, exposing **64 MiB** logical capacity.
Those are three different sizes. Recovery requires the same UUID and compatible
format/geometry. Moving to a larger file or `/dev/sdb` is not a capacity option
in this worker. Raw block backing is not implemented here.

Tmpfs supports process-restart testing while the mount survives. It does not
survive reboot or prove physical power-loss persistence. Never move, truncate,
copy over or replace an image while any worker holds it. The simulator's
exclusive lock and validation do not authorize an external writer.

## 3. First initialization versus recovery

Record, outside tmpfs, the source/ELF manifest, selected profile, media UUID and
media directory. Choose one nonzero 128-bit UUID and retain its 32-hex spelling.
The kernel's actual BDF determines `/dev/fwlab-native-BDF`; do not guess it from
an unrelated `/dev/nvme*` device name.

After the prepared modules are loaded with the reserved `bar_start`, the
function initially has no native driver bound. Start the selected worker in a
foreground terminal with the actual device, private directories and manifest:

```text
fwlab_native_mq2_worker --device /dev/fwlab-native-BDF \
  --media-dir PRIVATE_MEDIA_DIRECTORY --uuid RETAINED_32_HEX_UUID \
  --binding-sha MANIFEST_64_HEX_SHA --owner-dir PRIVATE_OWNER_DIRECTORY \
  --format
```

The placeholders are intentionally not a runnable command against an unknown
device. The native interface is currently root-only. All directories must be
owned appropriately for that effective user. `--format` creates a **new** image
exclusively and then initializes the FTL; it does not convert or overwrite an
existing `nand.bin`. Failed initialization may preserve a partial image for
diagnosis. Do not delete it and retry formatting as an automatic recovery step.

On every subsequent startup, use the same arguments **without `--format`**.
Physical format/UUID/geometry and FTL roots/capacity are read and checked; a
missing, damaged, incompatible or mismatched image fails instead of formatting.
Old preview images require their old construction. No automatic upgrade exists.

Wait for `NATIVE_READY` before binding Linux `nvme` to the exact synthetic BDF.
Verify sysfs ancestry and the advertised identity/capacity before accessing the
namespace. Never globally unload/unbind all NVMe devices. The
`native-io-large` client is the matching bounded 64-MiB test client; the ordinary
`native-io` client is the old 1-MiB reference and is not interchangeable.

## 4. Normal use, owner transfer and shutdown

Application reads/writes go through the synthetic `/dev/nvmeXn1`, not through
`nand.bin`. Both queue pairs share one I/O frame and serialized storage service.
The tested Linux Host can split application 1-MiB I/O into 128-KiB wire commands;
do not infer wire shape from an application block-size flag.

Before shutdown or L1/L2 transfer, stop writers, complete explicit Flush/FUA,
unmount any test filesystem and close namespace holders. Use the existing
owner coordinator for revoke → drain → zero certificate → grant. A successful
driver unbind alone is not the certificate. The owner socket is a coordinator
lease: connecting and disconnecting merely to inspect status can revoke the
owner. Do not use it as a health-probe socket.

For shutdown, unbind only the synthetic BDF **while the worker is still alive**,
allow the worker to terminate gracefully, then unload the PCI and IOMMU modules
in that order. An attached worker dying can quarantine the function; retain
the image, recreate the prepared module instance, then recover without format.
Do not free unresolved internal work or bypass quarantine to obtain a green
startup. No service manager, automatic retry/autoformat or broad device-cleanup
script is supplied by this guide.

## Evidence and unresolved deployment work

The [development results](results/2026-09-10-scaled-storage-mq2.md) identify the
executed native and ownership cases. Full bare-metal qualification, ARM native
M4/M5, raw-device admission, larger native capacity, real-NAND generation
recovery and unattended deployment packaging remain separate work. Named reset
tests do not close the previously disclosed concurrent-FLR question.
