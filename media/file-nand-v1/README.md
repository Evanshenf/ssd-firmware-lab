<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Compact physical NAND: SCALE-A

This is an experimental second physical-media binding, not an expanded-capacity
release. The existing `file-nand-v0` format and published preview are unchanged.
No image migration, raw device, real NAND, performance or full-capacity result
is claimed. The first integrated consumer uses the same 1 MiB FTL, production
NFC and command lifecycle as the preview.

## Format and owner

The NAND media owns only physical PPA bytes, program/erase state and persistent
physical operation outcomes. Its format contains no namespace LBA map. The FTL
owns mapping journals/checkpoints and logical durability above NFC.

The v1 file has 4 KiB sectors, two immutable superblocks, two applied-sequence
records and two 32 KiB redo banks. Each physical page has one 4096-byte main home
and a separate 256-byte record containing 128-byte OOB plus physical identity,
program state and integrity checks. Each block has a 64-byte state record.
Packed metadata updates journal the whole affected 4 KiB sector, preserving
neighboring records across a torn write. No capacity-sized RAM index is needed.

Geometry is checked with wide arithmetic. The byte format can describe larger
images; that is not evidence that the current NFC or FTL supports those images.
For the initial 512-page, 16-block geometry, the layout is 2,314,240 bytes versus
the old 8,478,720-byte development image. This is layout arithmetic, not a
performance measurement or a full logical-capacity test.

## Redo transaction

Media calls are serialized. One transaction installs at most three full-sector
postimages: main, packed page metadata and packed block metadata. Erase/bad-block
changes can use only block metadata. A bank has one 4 KiB descriptor, up to three
4 KiB postimages, and one 4 KiB commit seal at offset 16 KiB.

1. Invalidate and sync the reused bank's old seal.
2. Write and sync the complete descriptor and postimages.
3. Write and sync a commit seal bound to the transaction sequence and digest.
4. Install home postimages and sync them.
5. Write and sync the alternating applied-sequence record.

Only then can the next transaction reuse older redo space. Recovery chooses
the highest valid applied record and replays its unique committed successor,
if present. Installation writes fixed postimages; it never increments an erase
counter a second time. A failed mutation quarantines the live binding, leaving
the committed redo intact for recovery. A committed effect may survive without
a caller completion; that does not manufacture an FTL mapping/Flush witness.

This requires the substrate's sync operation to preserve completed barriers;
CRC checks detect ordinary incomplete/corrupt records, not malicious forgery.
Recovery does not silently format, ignore committed redo corruption or create a
missing image. Host process interruption, simulated NAND torn effects and host
filesystem/device power loss are separate claims and tests.

## Physical erase semantics

A complete erase increments the persisted block generation and resets its
program cursor. Older-generation pages read as erased bytes without physically
rewriting every main home. A partial erase retains the generation and records
the erased prefix and TORN block state: prefix pages return erased bytes/TORN,
the unaffected suffix retains its prior state, and further program is rejected
until complete erase. Repeated partial erases accumulate the erased prefix.

Program is ascending and once per erase. Partial programming consumes the page
and installs the exact applied byte prefixes; untouched bytes remain `0xff`.
Generation/counter overflow is rejected rather than wrapped. Simulated block
metadata still supplies erase-generation truth; physical NAND portability
remains a separate unresolved contract.

## Bounded validation

The integration target exercises the real J0 Write/Flush/Read/RMW, restart and
live-page GC path, including a sealed physical data page that must remain an
FTL orphan after recovery. The direct-media target covers torn effects and five
named durable-byte redo cuts: UNSEALED, SEALED-NO-REPLY, TORN-ERASE-INSTALL,
HOME-BEFORE-APPLIED (with another interruption during replay), and REUSE.
Independent design advice is not executable source approval. No new generalized
test generator or review framework belongs to this component.

From the repository root, without root or a device:

```sh
make -C media/file-nand-v1 check
make -C frontends/headless-scale check
```

The first command uses an in-memory durable-byte substrate to isolate recovery.
The second creates a private temporary regular file and exercises the production
firmware layers; on success it removes only that test's disposable image and
directory. Failures retain the reported directory for diagnosis. Neither command
loads kernel modules, assigns a device, uses a raw disk or performs a large write.

The direct target accepts `CC`, `CFLAGS`, `LDFLAGS` and `BUILD_DIR`; the integrated
target accepts those compiler flags and `BUILD` for a separate compiler build.
The production media arena is 16,720 bytes on the tested x86-64 build and does
not grow with physical-page count. Test-fixture buffers are separate from that
arena. This is not a total FTL/process/OS memory claim.

See [scalable storage development](../../docs/scalable-storage.md) for the next
FTL/geometry/continuous-workload slices. The native worker still selects v0;
this target is a headless integration harness, not a second NVMe executor.
