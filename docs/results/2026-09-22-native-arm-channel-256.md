<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ARM64 native channel path: bounded 256 MiB result

- Firmware source: `4fe70514cfb298fceda46dbab1dee65d2eee2cd9`;
  documentation baseline: `c660f767d7f8ee34f61ba2c6e1cfc82165523624`.
- One actual Linux native episode passed on ARM64, using the existing MQ2
  worker, `channel-lab4k`, a 256 MiB namespace and four NAND data workers.
- This adds native-driver evidence to the
  [software capacity checks](2026-09-15-channel-capacity.md). It is not a new
  frozen release, a full-capacity campaign or a throughput result.

## Actual path and environment

Ubuntu ARM64, kernel `7.0.0-30-generic`, 4 KiB system pages and the previously
prepared 16 KiB BAR aperture. The original verified ARM PCI/IOMMU modules were
reused; the kernel/UAPI source difference from their earlier corrected source
to this firmware source is documentation only. The existing ARM firmware ELF
was reused and the existing large native client was rebuilt with GCC 15.2.
No firmware, kernel, FTL, NFC or media engine was changed for this episode.

Linux `nvme` -> synthetic PCI/HIF -> MQ2 firmware/Linux profile -> shared
lifecycle/Block -> mutable FTL format3 -> IPR channel NFC -> four strict POSIX
physical-v2 NAND shards -> completion intent -> HIF CQE/MSI-X.

The geometry is four channels, one LUN/channel, two planes/LUN, 160 blocks/plane,
64 pages/block and 4096-byte main plus 128-byte OOB. Each shard is 89,165,824
bytes; four shards and their 1024-byte manifest total 356,664,320 bytes. The
256 MiB namespace reports NSZE/NCAP/NUSE 524288 with 512-byte LBAs. BAR state
is memory-backed; there is no direct LBA-to-file storage shortcut.

Fresh media was placed on an existing, capped 1 GiB local tmpfs. Programs and
logs stayed on disk. Synchronization, CRC/OOB, locks and NAND semantics were
unchanged. Image lengths are not resident memory: the files are sparse and
this episode wrote only selected extents, not the whole namespace.

## Fixed episode

The existing `j1_native_large_io` client used these six non-overlapping shapes:

| LBA start | Bytes | User-buffer offset |
|---:|---:|---:|
|128|512|0|
|129|4096|0|
|137|8192|512|
|524272|8192|0|
|8192|1048576|0|
|16384|1048576|512|

The third write uses FUA; each write is followed by Flush. Each client run
checks ordinary and readahead data and performs 64 continued first-case reads.
The actual L1 wire limit was 128 KiB: each logical 1 MiB transfer was split
into eight commands, not one unsplit 1 MiB command.

Eight device commands exited zero, in this order:

1. Identify Namespace.
2. Write all six shapes, with the existing FUA/Flush and read checks.
3. Independent verification of all six shapes after all writes completed.
4. One quiet controller reset, with no active test client or in-flight block I/O.
5. Verification after reset and runtime reconstruction.
6. Verification after normal close, PCI recreation and same-media cold recovery.
7. Another write run of the same A patterns, not a different data generation.
8. Final independent verification.

The two write runs total 4,236,288 logical bytes. This is not a full-fill,
sustained-overwrite, ENOSPC, GC-pressure or new-filesystem qualification.

Five tasks were observed at each stable ready point: the coordinator and four
data workers. The quiet reset kept the coordinator and replaced all four
worker PID/starttime tuples. The format runtime progressed epoch1 -> 2 at the
explicit reset, then 2 -> 3 during unbind. Its final epoch3 drain had zero
authorities, DMA, buffers, Block and NFC work, and the process exited zero.
The cold-recovered runtime progressed epoch1 -> 2 during unbind, drained that
latest epoch to zero and exited zero. Cold recovery retained the global/child
UUIDs, all six file identities/sizes and the exact manifest; no second format.

PCI recreation was necessary after HIF close quarantined an attached function.
The IOMMU module remained loaded. Each recreated function had a new nonce:
this is not M5 same-function ownership switching or a concurrent-reset test.

## Preservation and evidence limits

An existing 64 GiB R0 instance was normally drained/stopped, retaining its NAND
image, then restored with its original executable, arguments and service and
bound to Linux `nvme` again. Its image identity was unchanged. A previously
recorded 4 MiB file was read without mounting or journal replay before and
after; both hashes matched. This is a bounded old-data preservation witness,
not re-verification of every old file. The temporary service exit-receipt
setting was removed and the original service configuration restored.

One episode was run, with no DUT repair or retry. The supervisor exited zero.
The kernel ring rolled during the episode; the retained suffix has no matched
warning/error marker, but whole-episode warning absence is **not** established.
The pre-existing kernel taint remained 12288. Firmware phase/exit receipts are
retained independently of that ring. Guest wall timestamps differ from the
controller host's clock; they are not mixed into a latency measurement.

No 64 GiB channel-native readiness or full-volume result follows from this
256 MiB run. Nor does it establish one-worker native operation, NUMA locality,
thread speedup, hardware NAND timing, ARM M5, physical power loss, real-device
portability or SSD bandwidth. The existing P01 erase-generation persistence
porting gap remains open. Tmpfs process recovery is not disk durability.

## Exact bindings

| Artifact | SHA-256 |
|---|---|
|649-source manifest, verified before and after|`1a4fecaa768e8a933cad03afff310e781d241b2022a3eb15453b3704edabf711`|
|ARM firmware ELF|`d4e8a3208125f7f0ae77cb81c4dd12196e85dad7dcc2778920f5ce3d55f2642d`|
|ARM native client ELF|`d4f995a754f7cfdd894f778cfb085f610e1207caa2b372e916e34e092348f74d`|
|PCI module|`f369400d591c409b29adb031cdd41d999130227084889261647b061f01e8023f`|
|IOMMU module|`d275888735339e89c622817debed4f04cd64f6d523929d44e29534456a334b19`|
|Five-file binary/profile binding|`ace9a07deb1b5a78902ad97e566c8c7e58fbfa377d3af4bed9dbbcca11a65d71`|
|Finite operator|`cf3a48324f72ca1a0609227e0ef1a5b7aaa3644dbd285db3b174d5408f34d23a`|
|99-file collected evidence manifest|`22d7e05a897b9bc302b3cc41ab4b0fa38c51b6bf9c8b7aeafd514bfaeee0ecbb`|
|Episode log|`bf018143362be4ae89e96ca16cc9e26be1ce042f69c9de94625627c3c5a58205`|
|Collected archive|`6f6dc7754865ec8756fb23e3a875257fb89c4af7244dcc048cb6389f342080b3`|

The private archive includes lab identities and the retained probe, so hashes
are provenance anchors, not a claim that the complete archive is published.
Reproduction uses the existing [native deployment entry](../native-scaled-usage.md)
and client, with an independently approved lab/media preservation plan. Never
copy historical PIDs or image paths into another deployment.
