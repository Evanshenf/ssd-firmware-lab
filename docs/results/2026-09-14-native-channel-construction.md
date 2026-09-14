<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Cooperative channel NAND: actual native construction, offline Host

Source `08d91f610e922960c03b60b9b710fd0bbcab4e41`, production parent
`381d9775688c990323fc6217f258b9770332e450`, on top of the mutable read/write
construction `e48022c15a51649a8fd81a68db44e8a76c16cbfe`.
[ADR-0025](../adr/0025-native-channel-construction.md) records the option.
This is not a new native-kernel or M5 qualification, release or performance
benchmark. Earlier native R0 evidence is not relabeled as channel evidence.

## What ran

The existing `mq2-worker` gains explicit `--nand-profile channel-lab4k`.
The existing `test_scaled_runtime.c` includes the actual production worker
and calls its constructor and `firmware_loop`; only the Host ioctl boundary
is fake. There is no substitute storage executor or direct-LBA fixture.

The selected64MiB namespace uses four channels, one LUN per channel, two
planes per LUN, forty blocks per plane and64pages per block, with4KiB main
and128B OOB. The same mutable format3 FTL, IPR channel hub/cooperative actors
and four real physical-v2 engines execute. The explicit NULL executor means
no OS data workers. Timing is synthetic and unpaced. Physical files use the
existing **strict POSIX** adapter, not the default native mmap adapter.

Each child image is22,303,744B; the four sum to89,214,976B plus a1024B
manifest. This is80MiB physical main versus64MiB logical capacity. Startup
preflight checks that total with filesystem/RAM margins. It is not an
existing-volume resize or a rerun of the full64-GiB campaign.

All fixed cases passed:

- Identify capacity; two existing finite1MiB head/tail episodes with write,
  SELF/Flush, exact reads and continued writing after recovery. This does
  not fill or validate the entire64MiB namespace.
- Actual writable FTL3 with disjoint read/write pools and IPR; all four
  children show real READ/PROGRAM/sequence/materialized-byte and array/bus
  facts. This is not a new same-LUN plane-overlap or throughput witness.
- Existing96turn no-progress test: five idle sleeps, none after actual
  storage advancement, six PUMP and STATUS visits. Reset/drain/recovery/ready
  ordering is preserved. Its reset occurs before the first command capture,
  not during accepted NAND work.
- Same process-lived volume while FTL/NFC identities are recreated. A
  competing open is rejected by the volume lock during runtimeNULL. The
  final close/reopen preserves six-file identities and child UUIDs, and
  existing zero-reference close checks pass.
- New-only reformat, wrong capacity and opposite R0 recovery reject without
  changing the clean closed assembly. Dirty-redo recovery is not claimed
  universally non-mutating. Success cleanup checks exact file identity after
  close; failed initialization is never automatically deleted or reformatted.

The old default R0 native-loop episode and one unchanged mutable J0 adjacent
episode also passed. Production MQ2 and scaled-worker builds linked; two
invalid CLI choices returned usage2 before HIF access. No valid online CLI,
module load, native driver, real DMA/IRQ or owner-switch run was performed.

## Execution and resource budgets

Linux x86-64 development environment: GCC13.3, Clang18.1 and QEMU user8.2.2.
Static ELF identities were checked before actual AArch64, RISC-V64 and
big-endian s390x execution. No TSan case is needed for this cooperative-only
change; it does not replace the earlier threaded construction's TSan proof.

| Fixed channel fixture | Result | Whole-fixture seconds | Peak process RSS KiB |
|---|---|---:|---:|
| GCC |PASS|0.13|20976|
| Clang ASan/UBSan |PASS|0.42|92928|
| AArch64 qemu-user |PASS|0.64|32136|
| RISC-V64 qemu-user |PASS|0.57|30144|
| s390x qemu-user |PASS|0.92|29856|

These are test budgets, not data-path bandwidth. The old R0 episode was also
0.13s, with99360KiB peak RSS; different substrate allocation makes this
neither a controlled speedup nor a memory-optimization comparison.

All media jobs ran serially as an ordinary user in fresh directories on the
existing1GiB tmpfs. Binaries/logs remained on disk. All sync/CRC/locking calls
remained enabled. The mount's usage returned to its prior183,902,208B;
other retained images were not modified. Image sizes above are logical
footprint bounds, not sampled allocation peaks, and RSS excludes tmpfs.
There were no build/runtime failures, DUT repairs or whole-campaign retries.

Exact local manifests: five source files
`ee67bdc5b1521c96d724ddc077c2379daabbc088214903c855efa835eb6ebc49`,
25logs `191d1e3be11394379a05d325db3bf0939e3454f8b0eff5b47ae854b55c93f26b`,
eight ELFs `c98df0f02be593d207ff95c7d0e672778c13b15e80c24a950ab90dd99487b01d`.
The public summary is not distribution of every private raw log.

## Disposition

Fixed implementation/tests complete. One independent read-only exact-source
confirmation returned **NoRequired / STOP**, verifying the five source Git
objects,25logs/eight ELFs and the four bounded construction/lifetime/path/
identity obligations. It did not rerun tests or qualify the online kernel.
No kernel, protocol/lifecycle,
FTL algorithm, NFC/worker or physical-engine semantic code changed. The
default R0 path and old image formats remain unchanged.

Later threaded native use requires a fresh executor per firmware runtime,
actual old joins and a bounded waiting policy during startup/drain. Mapped
shards, NUMA, vendor calibration, pacing, native qualification and larger
capacity are separate tasks, not reasons to extend this construction test.
