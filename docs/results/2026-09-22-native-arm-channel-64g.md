<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ARM64 channel64GiB: native full-volume data result

- Source: `4fe70514cfb298fceda46dbab1dee65d2eee2cd9`; documentation baseline
  `cedebdafb6ad0e50e7fee8182518ed0dfb45dd93`.
- One actual native Linux full-volume episode completed successfully. The
  firmware/kernel/storage code was unchanged from the preceding
  [256 MiB native episode](2026-09-22-native-arm-channel-256.md).
- This closes the new channel path's previously unexecuted64GiB **bounded
  full-volume data/GC/cold-recovery** case. It does not repeat or inherit every
  group from the older R0 [native acceptance](2026-09-13-native-arm64.md).

## Construction and actual data path

ARM64 Ubuntu26.04, Linux7.0.0-30-generic,4KiB system pages,8CPUs,96GiB RAM,
and the already prepared16KiB BAR. One MQ2 worker selects
`--namespace-mib 65536 --nand-profile channel-lab4k --nand-workers 4`.
There is one coordinator plus four NAND workers, not four concurrent Host
parents. No owner socket or M5 route is selected.

Linux nvme -> synthetic PCI/HIF -> existing firmware/profile/lifecycle ->
Block -> mutableFTL3 -> IPR channel NFC -> strict POSIX physical-v2 shards ->
HIF completion/IRQ. No direct LBA-file bypass, skipped synchronization or
substituted RAM storage engine was used.

Identify NSZE/NCAP/NUSE is134217728 with512-byte LBAs:68719476736 logical
bytes. Geometry remains4channels/1LUN/2planes/40960blocks/64pages,4KiB main
plus128B OOB. Each of four shards is22822273024bytes; the complete volume
including its1024-byte manifest is91289093120bytes. Capacity adds blocks,
not extra parallel resources or a claimed throughput multiplier.

An existing90GiB local tmpfs was reused after the user approved retirement of
one completed old test image. No archive disk, VM reinstall/reboot, raw backend
format or other VM change was needed. Programs, scripts and logs stayed on
disk; tmpfs recovery is not host-power-loss durability.

## Executed checks

The existing native six-shape client first passed, including the last8KiB of
the64GiB namespace, FUA/Flush and logical1MiB transfers split into8x128KiB
wire commands. The full-volume phases then reused the earlier D205 fio
`pattern_hdr`/absolute-offset oracle: fio3.41,128KiB,one libaio job,QD32,
direct I/O,4KiB verification intervals,CPU6,and end_fsync after writes.
Firmware workers were not restricted to the old single-CPU7 placement.

| Completed operation | Exact logical bytes | fio runtime | Observed decimal MB/s |
|---|---:|---:|---:|
|A full write|68719476736|1173.008s|58.584|
|A full read/verify|68719476736|155.278s|442.558|
|B overwrite, alternating128KiB stripes|34359738368|1685.411s|20.387|
|After cold recovery: B stripe read/verify|34359738368|86.664s|396.471|
|After cold recovery: complementary A read/verify|34359738368|80.832s|425.076|

All four fio invocations exited0, with exactly the expected read/write byte
counts and I/O counts. Total bulk native data is224GiB. The complementary
reads cover the entire64GiB address space after overwrite and cold recovery;
this is not a small fixed working set advertised as a full disk.
The original fio JSON payloads are published with only two SPDX comment lines:
[A write/read](data/2026-09-22-native-arm-channel-64g/A-fill-read.jsonc),
[B overwrite](data/2026-09-22-native-arm-channel-64g/B-stripe-write.jsonc),
[B recovered read](data/2026-09-22-native-arm-channel-64g/B-stripe-read.jsonc), and
[A recovered read](data/2026-09-22-native-arm-channel-64g/A-complement-read.jsonc).
Strip the first two lines to parse or hash each unchanged JSON payload.

The supervisor completed in54m14s on one guest clock, including smoke checks,
counter snapshots and cold reconstruction. These are integrity-run rates and
costs, **not** matched benchmark medians, steady-state SSD performance, a
thread-speedup proof or the10GB/s target. In particular, overwrite performance
under low-free conditions remains an optimization concern, not a solved goal.

## GC, recovery and resources

After initial A write/read: GC0, checkpoints41, free_blocks59338,
ready1/fault0/quarantined0. After B overwrite: **GC143446**, checkpoints60,
free_blocks1 and victim_blocks118698, still ready1/fault0/quarantined0.
Thus reclamation actually ran while accepting overwrite traffic; its presence
is not inferred just from selecting a large geometry.

The original runtime drained epoch1, reconstructed epoch2 during unbind,
then drained the latest epoch2 with zero authorities/DMA/buffers/Block/NFC and
exited0. Only PCI was recreated; the IOMMU module remained loaded. A fresh
worker recovered the same shards/UUID/manifest without `--format`. All six
file identities/sizes and manifest bytes matched across the cold boundary.
Map sequence3143243 and durable frontier786452 matched before and after.
GC/checkpoint statistics of the new runtime are volatile counters and must
not be mistaken for lost media history.

Initial format took50.463061701s. The unbind-time reconstruction took
32.714732158s, and fresh-process cold recovery33.910180784s. The existing60s
operational deadline was not changed; these observations do not prove a
universal latency bound on other hosts or profiles.

The format worker cgroup peaked at92003708928bytes, including charged tmpfs
pages. Its process RSS was approximately348MiB, which is **not** total RAM
consumption. The full-test cgroup peak was120688640bytes. Sampled live memory
events were zero; completed format/test cgroups had been removed at final
collection, so their final event counters are recorded unavailable, not zero.
The recovered worker remained live, bound to the64GiB native namespace.

Both long data phases crossed their20-minute inspection point. Actual CPU and
completed-I/O progress were checked; the same runs continued without restart.
No DUT repair or retry occurred. One post-test collector needed to tolerate
normally removed cgroup files; the first partial collection was retained.
An ERR trap also logged an expected no-mount `findmnt` result inside command
substitution during retirement; the containing guard and retirement completed
successfully. Neither observation is reported as a firmware failure.

## Evidence and limits

| Artifact | SHA-256 |
|---|---|
|ARM worker|`d4e8a3208125f7f0ae77cb81c4dd12196e85dad7dcc2778920f5ce3d55f2642d`|
|Native client|`d4f995a754f7cfdd894f778cfb085f610e1207caa2b372e916e34e092348f74d`|
|fio executable|`5699289d11cbe25be813f05584b20ccad8a0223f2b773a5861e35f92bcedf642`|
|Binary/profile binding|`b8992bfc913610319d258a208213f615fcb8ee51e7d99aa39be4bbcc1f97013b`|
|Finite full-data operator|`6f6ccfe9a7067641cfe68dd02a92dc61b76a73cc8529cb4edf2851684d963a2d`|
|82-file evidence manifest|`2406e9b6de0a9e31817bc4a275a080ad12e75467498b053214023ee8c3aae3d0`|
|Full phase log|`d57fce3b88bc763d34033e0f909a3a4efacc47c445c87dc61c0a1077bbf5ad24`|
|Collected archive|`65712ba4656738d7f974c10a4324fa95c5d77e8824bcc558a7c8eacd24e87975`|

All82 hashes were checked after transfer. The still-running recovery worker
log is represented by a frozen byte-counted prefix, not an unstable live-file
digest. Private archive hashes do not imply public availability of lab paths.
No new independent review, Pro consultation or hosted CI was run for this
unchanged-source execution; this report does not claim otherwise.

This is native L1 full-volume data/GC/normal-cold-recovery evidence, not a new
filesystem/ENOSPC matrix, pending-I/O reset/power-cut test, M5 qualification,
NUMA-locality result, vendor NAND calibration, physical SSD benchmark or new
frozen release. Kernel taint was12288 at final observation; retained ring
output is not a complete warning-absence certificate. P01 real-NAND
erase-generation persistence and the previously disclosed limitations remain.
