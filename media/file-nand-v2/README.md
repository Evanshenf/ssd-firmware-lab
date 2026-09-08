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

The existing C3 NFC consumes the compatibility interface one page at a time;
it does not implicitly use the batch function. An explicit
[NFC page-v2](../../core/nfc-page-v2/README.md) / FTL-window construction now
consumes batches through `fwlab_file_nand_v2_batch`. Scalar and batch callbacks,
geometry and UUID come from the same physical instance. A contiguous 64-page
READ uses three reads totaling 278592 bytes, retaining page/main/OOB CRC,
generation and physical-state checks. No writes or syncs are added to READ.
Physical-NAND health/generation persistence on real hardware
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
make -C frontends/headless-scale -f ftl.mk check-parent-window-v2
```

The cost entry reports actual API calls/bytes and maintenance, with instrumented
timing. It is not a paired 1-MiB throughput benchmark or a 10-GB/s/1–3% claim.
Both entries create fresh owned images; neither operates on an existing image.

## Optional operation-boundary validation

`fwlab_file_nand_v2_posix_operation_batch(media)` explicitly selects a different
POSIX validation interval. It checks the complete regular-file, current owner,
mode, link count, identity and size predicate before and after one synchronous
physical operation. Its byte callbacks retain range checks, complete IO, EINTR
handling and every sync while reusing those validated file facts. The default
descriptor still checks before each byte callback; format/recovery, resize,
diagnostic hash and close remain outside reuse.

This profile requires exclusive backend control throughout the operation. OFD
locks exclude cooperating owners, not external truncate/chmod/link operations.
Persistent changes before entry or at exit are detected, but in-flight truncation
followed by file regrowth can escape the boundary checks. Do not claim the old
per-callback detection interval. A failed exit check returns error/quarantine,
even after COMMIT synced; NFC reports UNKNOWN rather than a successful write.
The descriptor retains the same media context, geometry, UUID and physical engine.

Small real-POSIX checks and the existing actual consumers can select it explicitly:

```sh
make -C media/file-nand-v2 check-operation
make -C frontends/headless-scale -f ftl.mk check-parent-window-v2-operation
make -C frontends/headless-scale -f ftl.mk check-window-v2-operation
```

Use only the documented capped tmpfs for these fresh-image checks. No deployment
switches automatically. The optimization changes neither physical write bytes
nor barrier count, and does not add calibrated NAND hardware timings.

## Optional fixed mapped BYTE profile

`fwlab_file_nand_v2_posix_mapped_format/restart` explicitly selects bounded
mapped copies beneath the same physical engine. The constructors require tmpfs
on the actual opened FD and cap the entire image, including all metadata and
transaction banks, at 600 MiB and the platform's size/pointer-difference limits.
The existing ordinary-I/O constructors remain available without this profile.

Strict ordinary-I/O format/recovery and directory synchronization run first.
Recovery may repair an unresolved INTENT even if later mapped preparation fails;
no external PROGRAM has been accepted at that point. With resizing frozen, the
constructor preallocates the complete current extent using `FALLOC_FL_KEEP_SIZE`,
creates one R/W `MAP_SHARED` mapping and requires `MADV_POPULATE_WRITE` to succeed.
A final identity/EOF check precedes publishing the media or holder outputs.
Admission failure publishes no media, cleans up resources and retains a newly
created partial image for diagnosis. It does not fall back to ordinary I/O.

Every mapped read/write checks the exact mapped extent before forming a pointer
and copies to/from ordinary caller-owned buffers. No mapped pointer leaves the
adapter. The existing operation-boundary descriptor selects the same validation
interval on either BYTE implementation; the strict descriptor and diagnostic
hash still validate each callback on a mapped instance. All encoded records,
CRC/OOB checks, snapshots and three real `fdatasync` boundaries remain unchanged.
There is no extra `msync`, altered erase operation or zero-copy claim.

The file, FD and mapping require one executor and exclusive control through
close: no independent writes, truncate, resize, hole punch or unlink. OFD locks
coordinate cooperating owners and cannot prevent unrelated file modification.
Preallocation and prefaulting do not guarantee immunity from later memory
pressure or mapped-access faults. Returned validation/sync errors retain the
existing UNKNOWN/quarantine and failed-Read-output rules. A mapped-access fault
terminates the process; ordinary restart determines the state of the surviving
image. No signal-to-error handler or completed-copy prefix is supplied.

After the existing drain/close preconditions, cleanup unmaps first and closes
the retained FD once. The same path handles partially prepared constructors.
An unmap failure emits a diagnostic and terminates the process so a live mapping
cannot remain behind a closed media instance. Linux close/EINTR handling is
unchanged. Tmpfs results concern process restart while the filesystem survives;
they do not establish disk or whole-machine power durability.

The source policy permits only this POSIX adapter's literal
`<linux/magic.h>` include for its actual-FD tmpfs check. Linux filesystem types
and constants remain outside the physical engine and shared contracts. This
bounded profile does not select 64-GiB mappings, arbitrary filesystems, raw
storage or any native deployment automatically. Initialization and teardown
costs must be reported separately from runtime throughput.

Use the existing small capped tmpfs and run these entries serially:

```sh
make -C media/file-nand-v2 check-mapped
make -C frontends/headless-scale -f ftl.mk check-parent-window-v2-mapped
make -C frontends/headless-scale -f ftl.mk check-window-v2-mapped
```

These add only mapped-copy interruption/access-fault and construction-cleanup
checks to the existing POSIX test. The existing real Linux 64-MiB recovery leg
also hands the image back to ordinary I/O and continues reading/writing without
reformatting. The 256-MiB journey remains mapped through recovery. The map is
fully allocated/prefaulted, so resident and tmpfs usage reflect the whole image,
not just the small subset written by a smoke test. Neither these checks nor the
entry's diagnostic timings constitute the paired performance-adoption result.
