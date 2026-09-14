<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Multi-head format3 through real channel NAND and the shared firmware

Source: `3bce6ab19ab4613e300345335ddf223ccd3bf670`, core `54c8150`, baseline
`ce6ad58f2867de8f9491038f007468ba309fe38a`.
Profile: **WAVE4-LAB4K-B**, cooperative NFC, at most four write domains/runs,
ordinary 4-KiB main/128-B OOB and exact FTL format3. See
[ADR-0021](../adr/0021-multihead-ftl-write-waves.md).
One independent read-only exact-source/evidence confirmation returned
**NoRequired / STOP** for B, verifying all 22 source identities, 14 selected
logs and 10 executables without rerunning tests. This is not a native, threaded,
large-capacity or whole-release qualification.

## What actually executes

`scale_storage_multihead_lab_factory_init()` connects the unchanged J0/Linux
profile and lifecycle to the existing aggregate Block service and new format3
write pool. DATA goes through the existing cooperative channel hub, timed NFC
and real physical-v2 shard files. The NFC/media implementation from A is not
replaced. Mapping, OOB, CRC, health, generation and synchronization participate.
No direct LBA image backend or additional command executor is introduced.

Legal 8-KiB NVMe-profile requests prove two-head writes. Larger requests in the
second fixture start at the existing Block/controller-buffer interface, not at
an enlarged NVMe profile. Native worker construction still selects R0/format2.

## Fixed observations

| Case | Result |
|---|---|
| Legal J0 write/RMW | An aligned 8-KiB write maps its two pages into different domains; partial RMW spans three. FUA/Flush, frontier and exact reopen pass. Both 64-block geometry and metadata-skewed 32-block geometry pass; the latter correctly avoids metadata-only domain0. |
| Four uneven runs | A 52-KiB Block write creates 4+3+3+3 physical-page runs across four distinct domains. Real later-run completion occurs at callback10 before the first run at13. Four logical MAP pairs remain ordered; one aggregate parent completes with SELF/frontier1. |
| Smaller lower credit | The same four-run wave with a lawful two-credit before-admission wrapper completes across multiple actual hub batches. Later completion6 precedes first7; local BP74 and ACK BP88 are observed. All DATA resolves before any MAP admission. |
| Normal close | With one of two runs accepted, the unaccepted suffix stops and the accepted prefix maps; base frontier remains. With both accepted, both finish. No accepted PROGRAM cancel is sent. Exact readback and existing zero close certificates pass. |
| DATA uncertainty | A real first DATA program completes physically, then its API certainty is deliberately lost; a healthy later sibling also completes. The wave emits no MAP, drains results/reset/ACKs before recovery-required, and has no SELF. Reopen preserves old logical data and seals orphans. |
| MAP uncertainty | A real second MAP's A rail completes physically, then certainty is lost. The first MAP is known committed: 8-LBA known prefix, UNKNOWN_PREFIX and no SELF. Recovery replays the valid rail and returns the actual new data/frontier. This is not an atomic rollback claim. |
| Rollover and GC | A 1-MiB Block parent uses four 64-page DATA runs and closes full heads. A bounded overwrite and one forced relocation move one real page; full read and reopen are exact. |
| Low-free progress | On that same small assembly, 11 bounded whole-volume overwrites reach one global emergency free block. The next 1-MiB parent completes in four waves with automatic reclamation; full recovered readback is exact. No per-domain reserve or four-head requirement falsely exhausts capacity. |

Readiness wrappers are installed at construction and only introduce lawful
before-admission BP around the real hub. Fault wrappers return uncertainty
after actual physical programming; they do not supply fake NAND payloads or
skip barriers. Failure cases use explicit child-process recovery boundaries,
not a claimed successful owner grant from a quarantined runtime.

The low-free episode initially exposed an old **test-only phase assumption**:
the shared parent fixture expected the serial `work.phase` MAP-wait marker.
Format3 keeps this phase inside its owned wave. Its test alternative now checks
the actually completed final MAP/current map sequence and final aggregate span,
while keeping format1/2's original phase witness. No product patch was made for
that assertion; the failed log and media were retained.

## Execution scope

Both new fixtures pass GCC and Clang ASan/UBSan. AArch64, RISC-V64 and big-endian
s390x executables were statically linked, checked for correct ELF identity and
actually executed under local QEMU user mode. This is not hosted CI or native
PCI execution. A channel J0, N2a timed-mutation J0 and N1 parallel-read J0 received
affected GCC regressions. A small pure-memory head probe includes legacy1/2;
the old 64/256-MiB full campaigns were not rerun. MQ2 userspace compiled/linked
only. Policy, link, SPDX and REUSE checks passed.

All new physical files use the existing capped 1-GiB **local tmpfs**, serially
as UID1000, with ordinary POSIX synchronization. No mapped-profile switch,
existing image conversion, raw media, VM change or slow-disk fallback occurs.
The larger fixture has 1-MiB namespace and 16-MiB physical main, in four files
totaling 17895424 bytes. Smaller skewed J0 cases use four files totaling
8980480 bytes. These are fixture capacities, not an imposed new native limit.

Whole-test durations and peak process RSS are budget evidence only, not storage
bandwidth or architecture-performance comparisons:

| Execution | J0 seconds / RSS KiB | Block seconds / RSS KiB |
|---|---:|---:|
| GCC x86-64 | 0.07 / 12016 | 0.16 / 11108 |
| Clang ASan/UBSan | 0.23 / 153728 | 0.30 / 66560 |
| AArch64 QEMU user | 0.31 / 22332 | 0.47 / 21152 |
| RISC-V64 QEMU user | 0.28 / 21188 | 0.43 / 20168 |
| s390x QEMU user | 0.40 / 20856 | 0.60 / 19704 |

## Reproduce and stop

Provision the existing isolated tmpfs as described in the
[FTL guide](../../core/ftl-scale/README.md), then run serially as the ordinary lab
user (the Block fixture expects UID1000):

```sh
make -C frontends/headless-scale -f ftl.mk check-multihead-j0
make -C frontends/headless-scale -f ftl.mk check-multihead-parent
```

The entries reject missing/non-tmpfs/insufficient media space, create only new
owned sets, and clean verified successful files after closing holders. They do
not run the old large-volume test main inherited for helper reuse.

B stops at these fixed witnesses, affected checks and one exact-source
confirmation. C threads, D independent-plane READ, modern NAND geometry, 4-TB,
vendor calibration, hardware generation persistence and native deployment remain
separate. There is no guaranteed thread multiplier or new 8/10-GB/s result.
