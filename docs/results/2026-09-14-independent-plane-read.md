<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Independent-plane READ through real NAND and the existing FTL

Source `8d1b39ca4ae44f38b3def070636b4b0d7b9b5b2b`, base `71e3075`.
NAND core `3590d94`, frontend/J0 `6b5fcab`, real lower fixture `8d1b39c`.
Profile **IPR-LAB4K-D**; [ADR-0023](../adr/0023-independent-plane-read.md).
One independent read-only exact-source confirmation returned **NoRequired /
STOP** for the four fixed D obligations. It verified15 source identities,21
selected logs and10 executable hashes without rerunning tests.
Not a release-wide freeze, native deployment or vendor throughput result.

## Measured model behavior

Both policies use identical geometry, bytes and explicit synthetic costs:
1,000ns command,10,000ns read array and1,000,000,000B/s channel transfer.
A page is4,096B main+128B OOB; its output costs4,224ns. Plane parallelism is2
in both profiles. Geometry alone does not activate independent reads.

| Two reads on the same channel/LUN | LUN-exclusive | Independent-plane |
|---|---:|---:|
| Different planes, model ns |30,448|19,448|
| Same plane, model ns |30,448|30,448|
| Different-plane peak active arrays |1|2|

The independent array intervals overlap by9,000ns; shared-channel transfers
do not overlap. Per-LUN array work is20,000ns in both cases, illustrating why
summed work time is not elapsed utilization. Actual main/OOB reads, CRC,
generation and physical-media checks remain; READ leaves media hash/sequence
unchanged. These results are **model time, not wall-clock bandwidth**.

## Actual vertical path

The new explicit factory connects unchanged Linux-profile/lifecycle → existing
format2 FTL/read pool → policy-selected channel actor → unchanged real
physical-v2 shard. No logical-file backend, edited map or fake NAND payload.

One1MiB namespace uses1channel/1LUN/2planes/16blocks per plane/64pages per block.
Metadata occupies global blocks0–11.128 legal8KiB J0 writes fill DATA12–15;
one legal4KiB FUA overwrite of LPN0 allocates block16. The resulting reads are:

| Legal8KiB J0 read | Actual physical runs |
|---|---|
| LBA0/count16 | PPA1024: plane1/block0/page0; PPA769: plane0/block12/page1 |
| LBA504/count16 | PPA831: plane0/block12/page63; PPA832: plane0/block13/page0 |

Each produces two count1 PAGE2 requests and two contiguous controller-buffer
publications. Exact payload/OOB validation and the model times above pass.
The transition drains the final MAP ACK in7 bounded calls without changing
provider, UID, record sequence, media or durable frontier. Preparation exhausts
the existing finite trace; it is not cleared or rebound to obtain evidence.

Close and same-format recovery preserve mappings/data/frontier129. The prepared
and recovered physical semantic hashes match between policies. READ-era media
contents/frontier remain unchanged. D's Host consumer is explicitly read-only
after preparation; B3's mutable write-pool construction still reads serially.

## Resource and execution evidence

Fresh lower-media tests also execute:

- Reads before PROGRAM, a complete two-page PROGRAM before another-plane READ,
  and ERASE before another-plane READ: whole-LUN exclusion remains.
- Actual PROGRAM main/OOB and ERASE generation effects.
- Unstarted cancel without media read; started cancel drains only its current
  page while the other plane completes normally.
- Reset with two active plane reads, returning no valid read publication and
  reaching zero plane/LUN/channel ownership.

The adjacent core fixture additionally checks cap1, old default constructors,
confirmed-mutation cancellation and quarantine skipping later media callbacks.
GCC and Clang ASan/UBSan pass. Both real fixtures also compile and execute as
AArch64, RISC-V64 and big-endian s390x static ELFs under QEMU user mode.
Local tools: GCC13.3, Clang18.1.3, QEMU-user8.2.2 on x86-64 Linux6.8.0-138.
No ARM VM, native PCI, hardware-port or NUMA execution is implied.

Affected GCC checks include the old core R0/R1/RW/channel cases, C's actual
worker/close/J0 fixtures, B multihead J0 and N1 parallel J0. MQ2 compile/link
passed without starting it. Policy/links/SPDX/REUSE passed. No new D TSan claim
is made; C's unchanged worker runtime keeps its separately recorded evidence.

All D media runs are serialized as UID1000 in capped1GiB local tmpfs. Lower
images are4,473,856B; each J0 shard is8,931,328B. They are new files and are
closed/identity-checked before successful cleanup. No existing image, raw
device, VM or slow-disk fallback was used. All fixed runtime cases passed
first execution; retained initial attempts were build-only fixture issues.

| Lane | Lower whole-fixture seconds / RSS KiB | J0 seconds / RSS KiB |
|---|---:|---:|
| GCC |0.03 /3,092|0.33 /12,044|
| Clang ASan/UBSan |0.04 /11,520|0.50 /64,640|
| AArch64 emulation |0.10 /10,776|1.01 /22,392|
| RISC-V64 emulation |0.06 /9,760|0.74 /21,272|
| s390x emulation |0.06 /9,456|1.10 /20,788|

RSS excludes tmpfs page cache. These durations are budget evidence, not a
benchmark or disk/host-power-loss proof. No old large-capacity test was replayed.

With an already prepared capped tmpfs, run these as an ordinary UID1000 user,
serially:

```sh
make -C media/file-nand-v2 check-nfc-plane
make -C frontends/headless-scale -f ftl.mk check-plane-read-j0
```

Native IPR, full mutable format3 parallel reads, vendor calibration,6-plane
geometry,16KiB/4TB,8GB/s and NUMA placement remain unqualified. The existing
hardware erase-generation persistence contract gap is not resolved by D.
