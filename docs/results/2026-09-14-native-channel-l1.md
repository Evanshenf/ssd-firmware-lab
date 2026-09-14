<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Cooperative channel NAND through the real native Linux driver

Source: `b34328a7b9614fbf2ab3557d76e6595c260566a8`. This is the existing
[channel construction](../adr/0025-native-channel-construction.md), following
its [offline native-loop result](2026-09-14-native-channel-construction.md).
Only a comment was clarified after that source gate; no FTL, NFC, media,
kernel, protocol or lifecycle repair was needed for this native episode.

## Actual binding and environment

An x86-64 Linux test VM used Ubuntu 26.04, kernel `7.0.0-30-generic` (package
`7.0.0-30.30`), 4 KiB system pages and GCC 15.2.0. The existing synthetic M4
endpoint/software-IOMMU modules were built with producer2 and Host profile3.
The production MQ2 worker explicitly selected `channel-lab4k`,64MiB,
cooperative jobs and strict POSIX channel media. The installed Linux `nvme`
driver, not a fake ioctl producer, performed the data operations.

```text
Linux nvme -> M4 PCI/HIF -> production native MQ2 worker
 -> unchanged Linux profile/lifecycle/data mover -> aggregate Block
 -> mutable format3 FTL -> IPR cooperative channel hub
 -> four real physical-v2 NAND shards -> Host completion
```

The namespace has 131,072 × 512-byte LBAs: 67,108,864 B. Identify NSZE/NCAP/NUSE
and the Host client's BDF/driver/exclusive-open guards agree. Geometry is
four channels, one LUN per channel, two planes per LUN, forty blocks per
plane and64pages per block, with4096B main+128B OOB. It remains the explicit
LAB geometry, not a vendor NAND part,16KiB page or4TB construction.

The existing factory selects both FTL3 pools, writable parallel READ and
IPR. Each channel has its real independently owned physical-v2 engine.
There is no direct-LBA storage shortcut, mapped-shard optimization or external
OS executor in this option. Modeled timing is synthetic and unpaced; it does
not cap wallclock execution or establish SSD bandwidth.

## Fixed native checks

The unchanged `native-io-large` Host client exercised these six shapes:

| Starting LBA | Bytes | Host buffer offset |
|---:|---:|---:|
|128|512|0|
|129|4096|0|
|137|8192|512|
|131056 (namespace tail)|8192|0|
|8192|1048576|0|
|16384|1048576|512|

The third write uses FUA; each write is followed by Flush. Every case has
byte-exact ordinary and readahead-hint reads;64continued small reads follow
each write/verify invocation. This is not a full64MiB scan or the offline
fixture's exact1MiB head/tail recipe. Continued writes repeat the fixed data.

All three finite groups passed:

1. Actual native attachment/readiness and Identify, then initial write and
   verify using those shapes.
2. One explicit quiescent `nvme reset`: epoch1 zero drain -> recovery ->
   epoch2 ready, native queue reconstruction and exact verification.
3. Normal unbind/close, cold reopen of the same media **without format**,
   verify, continued write/verify, clean close and restoration of the prior
   isolated test instance. All eight operator command exit records are0.

Observed Linux L1 `max_hw_sectors_kb` was 128. A 1 MiB logical transfer therefore
used eight 128 KiB commands; this is not unsplit 1 MiB or strict-L2 evidence.
Host profile3 remains one global I/O frame. Ordinary successful operations
do not qualify an additional concurrent-MQ or active-NAND-reset matrix.

The initial function was `18221104047802580248`. Explicit reset advanced1->2;
normal driver unbind also drained/rebuilt2->3 before final drain3. Cold
process/module reconstruction used a new function `11366952293380984425`,
then normal unbind1->2 and final drain2. These extra unbind transitions are
not hidden or counted as additional injected reset tests. There is no M5
same-function ownership-switch claim across the cold module recreation.

## Media, preservation and identity

One fresh volume was created and retained after successful close. UUID
`27f1b4135ee5447e8665fc3ea3dd19e9`; four child files each22,303,744B,
plus1024B manifest and empty lock: total89,216,000B. This is80MiB physical
main, not64MiB of direct logical storage. The immutable manifest, child UUIDs
and all six device/inode/size identities matched initial/cold/final snapshots.
Final per-file hashes are retained with the private evidence.

All CRC, NAND semantics, synchronization and exclusive locks remained.
The medium was a bounded1GiB tmpfs; binaries/logs stayed on disk. The original
test image's hash, source and binaries were verified unchanged and its worker
restored unbound with the existing maintenance lock. The separate read-only
raw disk remained unmounted with unchanged read/write counters. No VM reboot,
filesystem format, raw-device write or retained-image deletion occurred.

Three earlier **operator preparations** failed, separately recorded:
an omitted `driver_override=none` value; a `cmp -s` sysfs-size fast-path
misclassification; and a process-exit observation race after the old worker
had cleanly exited. The last case required an explicit restoration-only
transaction before continuing. None reached the new candidate or its media.
The observer was corrected and checked with30ordinary short-lived local
processes, a reaped PID and wrong live identity. Only the final invocation
ran the new native episode. Failed records were preserved; no firmware
change, automatic reformat or successful-test replay was used to get a pass.

## Evidence and disposition

All 645 source hashes matched local exact source and remote export before/
after build and execution. Four actual binaries plus source/profile identity
verify against binding manifest
`67e1a303d3159741efb465e1944c6da9542bedc58ee16f767dbefc22fbf4a37f`.
The matched private native archive has SHA256
`5ec6cb90cce3de8225063c0677acd24dba3c80ad6476f18fa566d5784e9e18dc`;
17selected evidence hashes are bound by
`b2a85f64e345e6d076f1e7f863b09a6dd7c0f2886fcb294ed1e3e9a91935a83c`.
The public summary does not distribute all private raw logs or bulk media.

The kernel build retained its compiler-name,1448B stack-frame warning and
missing-vmlinux BTF notes. Kernel taint12800 existed before and remained.
IOMMU logging rolled the kernel ring during this short episode, so saved
before/after snapshots do **not** prove absence of every kernel warning.
Command, worker-close and media identity evidence is separate from that limit.

Fixed native checks complete. One independent read-only exact-source/evidence
confirmation returned **NoRequired / STOP**, verifying all 645 source objects,
the six binding entries, 17 selected evidence hashes and four bounded
source/path/closure/restoration obligations. It did not rerun the tests.
No wallclock throughput or peak-RSS
benchmark was measured. Tmpfs does not prove reboot/power-loss persistence.
Bare-metal, ARM native channel mode, M5, threaded-native/NUMA, larger capacity,
vendor calibration and hardware generation recovery remain separate work.
