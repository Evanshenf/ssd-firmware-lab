<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Physical file-NAND v2: reservation, direct homes and terminal

This unreleased physical NAND format removes the previous per-page payload redo
and metadata-sector postimage duplication. It is not an LBA store: commands use
physical pages, block generations, program order, main/OOB and erase/health facts.
The old [compact v1](../file-nand-v1/README.md) and its images are unchanged;
there is no implicit conversion, fallback or raw-device binding.

One synchronous transaction programs up to 64 full contiguous pages in one
block. The compatibility NAND interface also supports known singleton partial
programs, full/partial erase, bad marking and reads. The three required barriers
are:

```text
INTENT + sync → main/page/block homes + sync → COMMIT + sync
```

INTENT binds the physical reservation and a validated base block record. Its
unresolved successor is recovered from that base, before trusting homes the
successor may have partially overwritten. Recovery installs absolute abort
facts, syncs, then writes/syncs ABORT before reuse. The other bank retains the
previous resolved transaction. COMMIT and ABORT match their exact intent and
sequence; a stale terminal cannot finish a newly reused bank.

An aborted program reservation becomes explicitly unknown/TORN, never valid
erased space. Known partial programs retain their actual bytes and CRCs. The
internal unknown state is distinct from legitimate CRC-zero data; its FF
placeholders are accompanied by TORN and cannot become a successful NFC read.
Erase counters/generations use absolute recovery facts, so another interruption
does not increment twice. Partial erase preserves its untouched suffix.

Full erase uses the existing generational NAND representation: a durable
generation/cursor change makes old page records stale and subsequent reads
return erased bytes. It does not perform or claim an unmeasured whole-main FF
write. All actual metadata writes and synchronization remain in cost accounting.

## Cost and evidence boundary

With 256-byte page records, 64-byte block records, a 1024-byte intent and a
512-byte terminal, a successful full-page batch writes `4352*n + 1600` bytes.
The direct 64-page case measured 280128 written bytes, five write calls and
three syncs for 262144 bytes of main data. GCC and Clang ASan/UBSan pass the
bounded byte-substrate cases for partial writes, terminal publication, repeated
abort recovery, bank reuse, neighbors and absolute counters.

The POSIX adapter retains private regular-file checks, exact identity/length,
exclusive OFD ownership, complete/partial I/O handling and real synchronization.
Formatting creates a new file only; recovery neither creates nor resizes one.
The failure model permits partial effects within a requested byte range; it
does not qualify arbitrary physical-sector collateral damage or host power loss.
Tmpfs is functional/process-recovery evidence, not durable storage hardware.

The existing C3 NFC can consume the compatibility interface one page at a time.
That connection is not evidence that it uses the 64-page batch function. A new
typed NFC binding and within-parent FTL windows are still needed for that
performance path. Physical-NAND health/generation persistence on real hardware
remains a separate portability contract, not a backend-only replacement claim.

## Run the bounded checks

The adjacent byte test creates no NAND file:

```sh
make -C media/file-nand-v2 check
```

Real C3/FTL journeys use the existing independently provisioned, capped tmpfs
described in the [scaled FTL guide](../../core/ftl-scale/README.md):

```sh
make -C frontends/headless-scale -f ftl.mk check-media-v2-cuts
make -C frontends/headless-scale -f ftl.mk check-media-v2-cost
```

The cost entry reports actual API calls/bytes and maintenance, with instrumented
timing. It is not a paired 1-MiB throughput benchmark or a 10-GB/s/1–3% claim.
Both entries create fresh owned images; neither operates on an existing image.
