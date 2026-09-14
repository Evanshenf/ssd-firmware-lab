<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# One writable format3 runtime with parallel READ and multi-head WRITE

Source `03cd40db3694f40fa2ed59f18436a1464a1e367e`, base `f6cac35`.
Core `62e6459`, construction `97dddf9`, fixed real cases `03cd40d`.
Profile **WAVE4-RW-IPR-LAB4K**; [ADR-0024](../adr/0024-mutable-format3-read-write.md).
One independent read-only exact-source confirmation returned **NoRequired /
STOP** for the four fixed obligations. It verified11 source identities,17
selected logs and11 executable hashes without rerunning tests.
The earlier full-file Pro design advice returned GO with no new required design
correction; it did not approve the subsequent code or rerun tests.

## Actual path

The existing multihead factory's PARALLEL option connects unchanged
Linux-profile/lifecycle → one mutable format3 FTL with disjoint existing read
and write pools → existing IPR channel actors → real physical-v2 shards.
There is one Host parent at a time; internal NAND operations may overlap.
Commands do not toggle readonly, replace providers or bypass NAND/OOB/CRC/syncs.

The fixture has4channels/1LUN/2planes/3blocks/64pages,1MiB namespace and6MiB
physical main. Metadata occupies0–11; DATA domains2/3 remain, not four active
DATA heads. Four ordinary POSIX v2 images total6,751,744B. All are fresh capped
local tmpfs files; no original image or raw device is used.

## Fixed real observations

| Case | Observed result |
|---|---|
| Disjoint pools | Actual write/read ranges fit without overlap in the constructor-owned arena. Format3 remains writable and parallel READ stays enabled. |
| Real intermediate publication | A Block READ at LBA1/count2047 reaches1535 completed LBAs with read pool active and workNONE at an API-call boundary. A valid new bufferless Flush parent is backpressured; GC/CP are rejected. Map bytes/sequences, UID, maintenance counters and physical shard sequences do not change from those attempts. |
| Mixed operations | Same instance completes1MiB WRITE, parallel READ, overwrite, Flush, full read, real GC/CP, another WRITE/Flush/READ. No manual inter-command ACK drain. |
| Actual GC-created IPR | Ordinary writes plus one real relocation move victim20 into15. LPN1/2 become PPA897/960: channel2/LUN0 on different planes. Actual two-page READ takes19,448 synthetic ns with exact data/OOB participation. |
| Cancellation and continued use | A partial READ retains exactly63 published LBAs, without adding a previous write pool's prefix. After real drain/retirement, another WRITE/Flush/READ succeeds. No quarantine flag is cleared. |
| Accepted READ close | Close during actual accepted NAND RUN suppresses publication, reaches zero, and recovers the complete byte oracle. |
| Accepted WRITE close | Its accepted1024-LBA prefix finishes required MAP, without SELF; frontier remains4. After zero and recovery, the full namespace matches the old/new prefix oracle, and later writing succeeds. |
| Legal J0 and worker binding | Cooperative and one data worker each pass128 ordinary8KiB fill writes, READ→RMW7/16→Flush→READ, actual GC/CP, continued writes and same-format recovery to frontier131. GC/final physical semantic hashes match across modes; actual joins precede release. |

The J0 byte oracle checks both replacement data and retained LBA0–6 and23,
before/after GC and recovery. The Block0/24-LBA overwrite is whole-page coverage,
not itself an RMW proof. Actual READ submissions/results/OOB facts and hub
counters—not scalar window counters—establish the parallel path.

Natural ACK backpressure occurs between command types. Its attempt count
depends on execution scheduling and is not a stable metric. Flush success
does not claim lower ACK quiescence. The real partial-publication test uses a
between-step state, not callback reentrancy or a manually edited map.

## Execution boundary

Both fixtures pass GCC, Clang ASan/UBSan and actual static AArch64, RISC-V64 and
big-endian s390x execution under QEMU user mode. The J0 cooperative/one-worker
episode also passes GCC TSan with process-local no-ASLR; host/global ASLR was
not changed. Affected GCC B Block/J0, D plane J0, N1 parallel J0 and C worker
J0 pass. MQ2 compiles/links only; its native binding was not started or changed.
Policy/links/SPDX/REUSE checks pass.

All media runs are serial, UID1000, on the existing capped1GiB local tmpfs,
without slow-disk fallback or altered durability calls. Success files are
closed and identity-checked before cleanup. All runtime cases passed; no DUT
repair followed testing. A first successful Block log retained an inherited
16MiB main-size label; the helper now derives the actual6MiB, preserving the
old fixture's16MiB default. The generic J0 preflight estimates one aggregate
image at6,702,592B; actual four-shard size is6,751,744B, within its additional
64MiB free-space margin. No old capacity test or retained image was replayed.

| Lane | Block whole-fixture seconds / RSS KiB | J0 seconds / RSS KiB |
|---|---:|---:|
| GCC |0.08 /13,312|0.73 /14,624|
| Clang ASan/UBSan |0.14 /51,584|1.11 /76,188|
| GCC TSan |not run (no worker)|4.44 /58,028|
| AArch64 emulation |0.24 /23,520|2.24 /27,772|
| RISC-V64 emulation |0.23 /22,412|1.65 /24,420|
| s390x emulation |0.30 /22,064|2.47 /23,804|

Local tools: GCC13.3, Clang18.1.3, QEMU-user8.2.2 on x86-64 Linux6.8.0-138.
RSS excludes tmpfs page cache. Durations are execution budgets, not SSD
bandwidth.19,448ns is unpaced model time, not measured wall-clock throughput.
No native PCI/ARM VM, NUMA, vendor timing,16KiB/4TB or hardware-port proof.

With a prepared capped local tmpfs, run these serially as UID1000:

```sh
make -C frontends/headless-scale -f ftl.mk check-unified-rw-parent
make -C frontends/headless-scale -f ftl.mk check-unified-rw-j0
```

Old constructors retain their schedules and format expectations; the fixed
preview and prior native R0/format2 selection remain separate. The hardware
erase-generation persistence gap is still disclosed, not solved by composition.
