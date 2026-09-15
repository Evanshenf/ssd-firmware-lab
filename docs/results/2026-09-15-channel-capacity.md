<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Channel capacity construction: bounded software result

- Source: `4fe70514cfb298fceda46dbab1dee65d2eee2cd9`.
- Source commits: `abb3b2a` shared presets, `bc60822` native selection,
  `4fe7051` existing fixture/CI entry.
- Status: fixed construction tests passed; new64GiB native device operation
  remains unqualified. Not a new frozen release or throughput benchmark.
- Design: [ADR-0027](../adr/0027-channel-capacity-presets.md).

## What changed

The opt-in channel path now connects64/256/65536MiB through one shared capacity
constructor, native media geometry, format/recovery expectations and the ready
FTL volume. The existing FTL3/NFC/channel workers/physical-v2 implementations,
timing, CRC, barriers and locks are unchanged. R0 default geometry is preserved.

| Logical capacity | Blocks/plane | Four images + manifest, bytes | Evidence here |
|---|---:|---:|---|
|64MiB|40|89216000|Actual cooperative native-runtime fixture|
|256MiB|160|356664320|Actual fourworker native-runtime fixture|
|64GiB|40960|91289093120|Construction/sizing only, no large media allocation|

These are image lengths, not process RSS or extra RAM provided by tmpfs.
All presets retain4channels/1LUN/2planes and64pages/block with4KiB+128B pages.

## Executed checks

The existing `native_progress_offline` includes the production native worker
constructor/loop, real J0/lifecycle, FTL3, NFC and four POSIX NAND shards. Its
ioctl boundary simulates Host requests/memory; no Linux NVMe device is opened.
The64MiB cooperative and256MiB fourworker cases passed with GCC and
Clang ASan/UBSan, both locally on x86-64 and on actual ARM64 Linux.

Each checks literal capacity/geometry/shard sizes, ready FTL and Identify
NSZE/NCAP/NUSE, tail Write/Flush/Read, one past-end READ returning0x80/SCT0
without Host DMA, retained-media reconstruction and lock exclusion,
clean wrong-capacity recovery rejection, cold recovery and continued I/O.
The256MiB cases each created and actually joined25threads in the existing
seven-generation lifetime fixture; no reset/test campaign was added.

| Actual ARM64 whole fixture | Seconds | Peak process RSS KiB |
|---|---:|---:|
|GCC15.2,64MiB cooperative|0.16|22984|
|GCC15.2,256MiB fourworker|1.95|24044|
|Clang21 ASan/UBSan,64MiB cooperative|1.51|94172|
|Clang21 ASan/UBSan,256MiB fourworker|3.16|114072|

These are functional-test costs including format/recovery and checks, not
matched storage throughput. RSS excludes tmpfs pagecache. ARM builds used
explicit `-march=armv8-a+crc`; timing/model semantics were not changed.

Two affected default R0 local cases also passed. Seven sizing-plan executions
passed: local GCC/Clang, actual ARM GCC/Clang and qemu-user AArch64/RISC-V64/
s390x. The latter three also cross-built the complete production MQ2 worker;
they did not run it against devices. Production CLI probes accepted all three
channel presets up to a deliberately absent device, while invalid capacity,
threaded owner mode and R0 workers retained their expected rejection.

## Reproduce the bounded software cases

Prepare an ordinary-user-owned tmpfs capped at1GiB with at least512MiB free
and sufficient RAM; keep programs/logs on disk. Run serially:

```sh
make -C frontends/linux-m4 progress-runtime mq2-worker
frontends/linux-m4/build/scaled-offline/native_progress_offline --capacity-plan
FWLAB_TEST_MEDIA_DIR=/run/fwlab-test-media \
  frontends/linux-m4/build/scaled-offline/native_progress_offline \
  --nand-profile channel-lab4k --namespace-mib 64
FWLAB_TEST_MEDIA_DIR=/run/fwlab-test-media \
  frontends/linux-m4/build/scaled-offline/native_progress_offline \
  --nand-profile channel-lab4k --namespace-mib 256 --nand-workers 4
```

The current-spine script includes the plan and these two cases in its existing
job. Hosted CI/full aggregate execution was not run for this local source
slice. The earlier large R0 and native-kernel results keep their original
source/profile identities; they are not relabeled as channel64GiB evidence.

## Evidence bindings and stop boundary

Full649-entry source manifest:
`1a4fecaa768e8a933cad03afff310e781d241b2022a3eb15453b3704edabf711`.
Seven changed-source manifest:
`6e44a8e62ee0af6100b635042dc6e40b99511897557c54820878cb040165cbfa`.
ARM GCC fixture:
`11110d4be50ccd71296bfe8d9dc27b8bc5c42bb9a05f2608de04d87b9ddf1674`.
ARM Clang fixture:
`c150254376e231e999083bfbcee08869a7be9246e82c73de2986e3cbc19bfc84`.

Only fresh small test media was created and cleaned up after close. Existing
large media, native services, VMs and raw devices were not changed. No failed
DUT run or retry occurred. One independent exact-source confirmation found
no Required issues in the four fixed construction/evidence obligations; it
checked the source and artifact bindings, not a second DUT execution.
No large native readiness, ARM channel PCI, M5,
NUMA locality, physical power-loss, vendor-NAND or performance proof follows
from this construction result. New64GiB runtime testing needs its own resource
admission and native timeout evidence, not another round of these small tests.
