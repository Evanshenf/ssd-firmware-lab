<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0023: Explicit independent-plane READ on the existing NAND engine

Status: accepted for bounded D at `8d1b39ca4ae44f38b3def070636b4b0d7b9b5b2b`;
one independent exact-source confirmation returned NoRequired/STOP. Implements D from
[ADR-0020](0020-cooperative-nand-channel-domains.md). See the
[bounded result](../results/2026-09-14-independent-plane-read.md).

## Capability, not a topology or thread multiplier

Independent-plane READ is a real device capability—for example, Micron
explicitly describes it in its [232-layer NAND announcement](https://investors.micron.com/news/press-release/2022/Micron-Ships-Worlds-First-232-Layer-NAND-Extends-Technology-Leadership-07-26-2022/default.aspx).
That does not supply our timing parameters or prove compatibility with a
particular NAND chip. Grouped multi-plane commands can have device-dependent
address restrictions; [ONFI4.2 section3.1.1](https://onfi.org/files/onfi_4_2-gold.pdf)
describes those separately. This profile is a synthetic resource model, not an
ONFI command-sequence emulator or a commercial mixed-mode NAND qualification.

The existing event engine now accepts a closed private READ policy:
`LUN_EXCLUSIVE=0` or explicit `INDEPENDENT_PLANE=1`. Existing constructors
still select0 even when geometry already contains multiple planes. Two new
policy-aware constructors select model/hub policy independently of the
optional [C execution transport](0022-channel-worker-execution.md).
No live setter, profile registry, new event interpreter or plane/LUN thread.

## Resource ownership

| Operation | Acquisition | Release |
|---|---|---|
| Default READ | Whole LUN free | Current page DATA_END |
| IPR READ | Whole LUN free, addressed plane free, held registers below configured plane parallelism | Current page DATA_END |
| PROGRAM/ERASE | Whole LUN free or already owned by this group, every plane register free | Existing mutation terminal boundary |

A plane READ holds its register through command, array wait and output. It
does not release at array-ready and imply a cache-register pipeline. Different
planes may overlap arrays, but command/data/status traffic still arbitrates
the same channel bus. A mutation's self-owner exception permits its next page
without dropping whole-LUN ownership between pages.

Keep existing event rank/UID ordering, durations, conservative admission-time
budget, real materialization/effect callbacks and confirmed-group drain.
Unstarted READ cancellation takes no plane or media effect. Started READ
drains its current page and releases only its own register; later pages do not
start. Reset/quarantine retain their existing publication/effect rules and
drain plane ownership before quiet. No foreign owner is cleared to report zero.

Private existing stats append READ policy, held plane registers, active read
arrays and per-plane array/register intervals. Both policies count plane work.
`held_luns` counts distinct LUNs with either ownership type. Per-LUN interval
sums can exceed elapsed model time during IPR; they are not utilization ratios.
The existing256-entry trace remains bounded with explicit drops.

## Real FTL consumer, unchanged algorithms

`scale_storage_parallel_channel_lab_factory_init()` connects the same hub and
physical-v2 assembly to the existing generic format2 parallel-read pool.
Ordinary writes prepare data using the always-timed RW model. A private
frontend transition requires upper/FTL idle and drains at most one lower
retirement-control step per retry, then enters FTL read-only mode.
There is no FTL idle callback, timing/UID reset, provider replacement or hidden
checkpoint. N0's older timed-read transition stays unchanged.

For the finite two-plane journey, normal1MiB fill plus one4KiB overwrite
produces actual two-plane mappings. Existing grouping splits two count1 reads
without map edits; same-plane reads crossing a physical block form the control.
Preparation fills the old trace, so J0 uses committed time/counter deltas and
actual payload/PPA validation. Fresh lower fixtures verify exact intervals.

B's format3 write pool still has serial READ. D does not silently combine it
with the readonly pool or turn the native worker into an IPR device. Protocol,
lifecycle, FTL algorithms, C job/worker semantics and media format/IO are unchanged.

## Scope and STOP

Stop after policy/default and cap checks, paired different/same-plane real
reads, whole-LUN mutation exclusion, cancel/reset/quarantine drain, real FTL/J0
placement/recovery, affected checks and one bounded exact-source confirmation.
No new framework, unrelated large-capacity campaign or clean-review streak.

The page remains4KiB+128B OOB; fixture costs are synthetic and unpaced. Further
mutable format3 read/write composition, NUMA allocation, native selection,
6-plane/vendor geometry,16KiB/4TB and hardware-generation persistence need
separate work. Lower model-time overlap is not measured Host throughput.
