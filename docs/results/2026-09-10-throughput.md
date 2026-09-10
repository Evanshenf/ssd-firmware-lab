<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Performance evidence: separate layers, builds and working sets

Adopted source tip: `a6ee009`. These are archived September 2026 development
measurements, not new benchmarks performed for publication. **The measured
native read result is about 0.589 decimal GB/s, not 10 GB/s.** Isolated ARM NAND
and NFC rates must not be substituted for the native NVMe result.

All 30 native samples, durations, bytes, errors, completion latency, pair order
and computed medians are in [the machine-readable data](2026-09-10-throughput.toml).
The [separate ARM data](2026-09-10-arm-throughput.toml) retains all 80 physical
media/NFC transfer samples and all 80 FTL phase records, including per-process
resource use, separately timed lifecycle intervals and exact build identities.
No samples were dropped or rerun to obtain a desired result. MiB/GiB denote
binary sizes; MB/s and GB/s in this report are decimal rates.

## Native x86-64: three separate paired comparisons

Common environment: Ubuntu 26.04, kernel `7.0.0-30-generic` package
`7.0.0-30.30`, 4-KiB pages, GCC 15.2 `-O2`, fio 3.41, local tmpfs, normal
logging and `FWLAB_MEDIA_EXCLUSIVE=0`. Real FTL, PAGE2, physical NAND checks,
CRC, synchronization and locks remain. The firmware process is pinned to CPU
7; the Host producer is on CPU 6. This is **not one CPU for the entire system**.

Each sample uses `psync`, 128-KiB reads, QD1, one job, direct I/O and no file
creation: 64 MiB of successful reads repeated over the same warmed 1-MiB extent
at offset 4 MiB. `invalidate=0`. Every sample completed 512 reads with error 0
and zero writes. Full before/after data oracles ran outside the timed interval.
Per-sample times are only 113–406 ms across the three sets. These are bounded
warm-read diagnostics, not long-duration steady-state device benchmarks.

| Pair/order | CRC generic A / SSE4.2 B MB/s | Large A / MQ2 B MB/s | MQ2 A / rejected combined-control B MB/s |
|---|---:|---:|---:|
| 1 / AB | 168.192641 / 444.429562 | 438.620026 / 588.674245 | 588.674245 / 588.674245 |
| 2 / BA | 169.466828 / 435.771844 | 435.771844 / 559.240533 | 588.674245 / 593.883752 |
| 3 / AB | 165.292768 / 441.505684 | 438.620026 / 588.674245 | 563.940033 / 593.883752 |
| 4 / BA | 165.700898 / 444.429562 | 441.505684 / 563.940033 | 593.883752 / 588.674245 |
| 5 / AB | 167.353775 / 419.430400 | 435.771844 / 593.883752 | 583.555339 / 550.072655 |
| Separate A / B medians | 167.353775 / 441.505684 | 438.620026 / 588.674245 | 588.674245 / 588.674245 |
| Median of paired B/A ratios | 2.642384110 | 1.342105262 | 1.000000000 |
| B wins / ties / losses | 5 / 0 / 0 | 5 / 0 / 0 | 2 / 1 / 2 |

These are three experiments, not one continuous A/B/C run. In particular, do
not multiply unrelated medians into a claimed cumulative speedup.

### Existing CRC ISA selection (source `ab81faa` unchanged)

Both arms use Large Host profile 2. B adds `-msse4.2` to the existing flags on
a verified supporting CPU; the same implemented CRC path preserves bytes and
checks. The 48635-case oracle and 1024 shift-table entries passed. The generic
worker has no CRC32 instructions; B does. Default portable builds remain
generic. No new checksum algorithm or global build default was introduced.

The criterion of at least four wins and paired median ratio >=1.2 was met.
This supports explicit ISA selection on a suitable CPU, not a universal
architecture gain or proof that CRC was the only remaining bottleneck.

### Serial-credit MQ2/progress (adopted `a6ee009` production intake)

Both arms use the same SSE4.2/strict0/media/logging choices. A uses profile 2;
B uses profile 3 with two queues and three vectors. CPU 6 maps to Q1 in A and
Q2 in B. The measured gain belongs to this complete construction change, not
solely to one progress function. B mean completion latency ranges from
220.148 to 232.463 microseconds; paired median mean-latency reduction is
24.964263%. All ten samples and twenty data oracles passed.

Two queues share one global I/O frame and one serialized storage executor.
This comparison does not demonstrate parallel FTL or multi-queue throughput
scaling. Application 1-MiB transfers may split into eight 128-KiB Host commands;
this experiment explicitly used 128-KiB commands.

### Combined control call: not adopted

The separate `7a2e258` experiment combined adjacent PUMP/STATUS calls. Its
corrected fixed-profile checks passed, but the predeclared four-wins/ratio>=1.05
performance criterion did not. It remains excluded from the adopted source.
The result is retained because fewer control calls did **not** establish a
meaningful performance gain. No rescue optimization or favorable rerun followed.

The data file now binds both full source revisions, the corrected candidate's
11-path source manifest and the actual archived A/B worker ELFs. Those files
were rehashed during report maintenance; no performance run was repeated and
the rejected code remains outside the adopted branch.

## ARM64 isolated layers: different experiments, not native PCI

ARM64 Linux VM, GCC 15.2 `-O2`/native ISA, one CPU 7, explicit mapped tmpfs and
exclusive development build (`FWLAB_MEDIA_EXCLUSIVE=1`). Physical-only and
NFC-only fixtures each use a 128-MiB main working set with reusable windows.
Initialization, prefault, warmup/erase and reopen/readback execute outside the
timed transfers. Full main/OOB checks and reopen passed. Each shape has one
five-pair alternating set; all timed samples recorded zero major faults.
The ARM data preserves the 40 media and 40 NFC Write/Read records separately
from buffer setup, format, erase preparation, close and recovery durations.
An erase-preparation interval is not counted as bytes programmed or read.

| Isolated experiment and shape | Baseline median W / R GB/s | Candidate median W / R GB/s | Paired median W / R ratio |
|---|---:|---:|---:|
| Physical media, 8 KiB (`fc303a2` → `f27b590` core) | 4.487367 / 11.843810 | 4.831880 / 12.171548 | 1.080250 / 1.067281 |
| Physical media, 256 KiB | 12.544006 / 12.481724 | 12.675227 / 12.468737 | 1.012172 / 1.000123 |
| NFC, 8 KiB (`f27b590` → `6f4b53f` core) | 3.480464 / 6.887139 | 3.749414 / 7.790588 | 1.087963 / 1.131256 |
| NFC, 256 KiB | 8.398219 / 8.689920 | 10.093410 / 9.583585 | 1.193214 / 1.090647 |

The media change removes immediate decoding of a just-generated private INTENT;
persistent decoding and generated CRC still run. Its Read implementation did
not change, so noisy Read-control differences are not claimed as caused gains.
The NFC change aligns owned windows and honors queried arena alignment; it
does not remove the input snapshot or grant a caller-buffer loan.

These are physical PROGRAM/READ transfer intervals, not sustained FTL/GC or
calibrated electrical NAND timing. Do not compare them directly with the x86
native rates to compute a percentage of overhead: platform, layer, build and
measurement interval differ.

### Real FTL parent workload, ARM64

The `8d8a1a1` → `80023c6` validator-zero-scan experiment used the same real
FTL/PAGE2/media path, 64-MiB logical volume, common non-LTO harness and five
paired full fixtures. All work counters and data oracles matched. These rates
divide Host payload by **accumulated service time**, not total fixture wall
time; setup, recovery and outer verification have separately logged durations.
Service intervals still contain the invoked software/harness checks. They are
not pure FTL instruction rates or a native-driver benchmark.
Each of the ten complete fixtures contributes eight phase records. The ARM
data retains their Host/substrate byte counts, operation counters, setup and
service times, the seven separately logged lifecycle durations and the whole
journey wall time. These overlapping intervals are not summed into one rate.

| Phase | Baseline / candidate GB/s | Paired median ratio |
|---|---:|---:|
| 64-MiB fill | 3.152337 / 3.228230 | 1.027607 |
| Interleaved 4-KiB overwrite | 0.245032 / 0.263476 | 1.073453 |
| 1-MiB parent requiring GC | 0.473723 / 0.512628 | 1.087123 |
| GC readback | 2.617252 / 2.847792 | 1.094063 |
| Full fragmented read | 2.528422 / 2.759987 | 1.097996 |
| Recovered full read | 2.540963 / 2.780661 | 1.080884 |
| Continued 1-MiB write | 0.474334 / 0.515061 | 1.089259 |
| Continued 1-MiB read | 5.451963 / 5.727420 | 1.022668 |

The fifth GC-readback pair had ratio 0.592757; it remains in the five-pair
median. Peak process RSS was 92712 KiB. The separate 64-GiB campaign at
`cec2c5d` is functional capacity/recovery evidence, **not** a performance result
for this code or current native MQ2.

## Identity, reproduction and limitations

The native and ARM TOML files contain source labels, exact A/B executable,
benchmark-source and archive hashes. The ARM data names the measured product
file hashes separately from later adopted commits: private pre-commit benchmark
exports and final source/README/test integration are not the same artifact.
Additional retained ARM artifact identities are:

| Artifact | SHA-256 |
|---|---|
| Physical media final evidence | `86e12a915d45bb6250f6dbffeec5d6290081155de8a3be19059f78f4ee79ae67` |
| Physical media candidate bench ELF | `7179057d832d6bfd5f9fe19fe20b7627bab5741fb9d40017f08e7231fb5d387e` |
| NFC final evidence | `a8fdff3c1ccf84a5218410517a91b165c1b362e4cec24193a751be7b9460b071` |
| NFC candidate bench ELF | `34931896c0b090eb3b49723db3e01df8773ce42ed2bda01a7df66922cb3500e3` |
| FTL service-rate summary | `376975e88b9d44eb0315ee1f4ff6696d4b211863ee90903bcf8c3ad17c98b9de` |
| FTL integrated functional evidence | `82be32e899f7b6eb510135da53ec3e9bc020b8002c10bb7daec3cc8d912f83d4` |

These hashes are custody references to private archives, not public download
links or proof of independent reproduction. Raw infrastructure logs and model
transcripts are not included. Sanitized standalone layer benchmark harnesses
are not yet a public reproduction package. Existing
[functional entries](../getting-started.md) reproduce semantics, not these
exact performance measurements. The [earlier performance plan](../performance-spine.md)
retains historical amplification and substrate diagnostics with their own
scopes; an mmap-copy baseline is not a NAND or NVMe result.

No reported rate proves disk persistence, host power-loss survival, real NAND
timing, PCIe 4/5 saturation, <=3% overhead, production endurance or an optimal
implementation. No extra full-capacity or performance campaign was run for
this documentation publication.
