<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0012: Versioned physical NAND media and explicit substrate profiles

- Status: Recorded implemented decision; integration/release validation is separate
- Date: 2026-09-10
- Implementation baseline: `a6ee009bbca5932857d51c3a5f265e0b60183a76`
- Partially supersedes: ADR-0002's inactive-payload/redo layout and corresponding physical replay prescription, for physical format v2 only
- Refines: ADR-0007's physical-versus-logical persistence boundary
- Preserves: firmware-owned mapping, explicit durability witnesses, separate failure domains and no runtime autoformat

## Context and decision

Physical persistence must not be a hidden logical-block store, but it also need
not duplicate every main-page payload into an infrastructure redo slot. Keep
the existing physical formats and introduce an explicitly selected v2 engine
with reservation-first, single-home payloads. This is a physical-format change,
not a change to NVMe LBA addressing or an optimization that skips NAND work.

The [physical engine](../../media/file-nand-v2/physical_nand.c) accepts physical
addresses through `ppa_ids`, validates block health, program order and page
generation, and reads/writes main bytes plus OOB/page records. It never receives
an NVMe LBA or a decoded FTL map. The image contains both these physical facts
and FTL metadata that firmware has programmed into NAND pages; executable FTL
and NFC code is not stored as a backend service inside the image.

### Reservation, homes and terminal

A successful `fwlab_file_nand_v2_program_pages` transaction covers at most 64
contiguous full pages in one block. The same instance also exposes scalar
read/program/erase/bad-block operations. The ordering is:

```text
validated base + INTENT -> sync
-> main/page/block homes -> sync
-> matching COMMIT -> sync
```

`begin`, `terminal_write`, `recover` and `abort_install` implement the boundary.
Two alternating transaction banks bind sequence, predecessor, operation kind
and intent CRC. The resolved predecessor remains available while the next
reservation is outstanding. A stale terminal cannot resolve a new reservation.

Recovery does **not** reconstruct an interrupted payload from redo. It first
validates the unresolved reservation and its base facts, installs an absolute
abort outcome, synchronizes that outcome, then publishes and synchronizes
ABORT. An interrupted program reservation is consumed and marked UNKNOWN,
reported as TORN to the NAND interface; placeholder FF bytes do not make it
readable erased space. Interrupted erase is likewise not inferred to have
completed or incremented a generation. Absolute abort facts make interruption
during recovery repeatable without double-counting the same abort.

Complete erase uses the modeled block-generation/cursor transition rather than
rewriting the whole main area with FF. Page classification makes previous
generations stale. This is the simulator's physical model, not a measured
electrical NAND erase or a claim of erased-byte write bandwidth.

The logical contract remains separate: a synced physical COMMIT is not by
itself an FTL mapping commit or a Host completion. FTL must still publish valid
mapping dependencies before acknowledging the requested durability.

### Byte substrate and validation are explicit choices

The [POSIX adapter](../../media/file-nand-v2/physical_nand_posix.c) is below the
same physical engine. `fwlab_file_nand_v2_posix_format` creates a new private
regular file with `O_CREAT | O_EXCL`; restart opens an existing file and
validates format, UUID, geometry, exact extent and holder identity. Restart
does not create, resize, convert or silently choose another engine. OFD locking
is retained for cooperating owners, and all required `fdatasync` calls remain.

There are three independent selection axes, not a single "fast mode":

| Axis | Choice and boundary |
|---|---|
| Byte access | Ordinary complete POSIX I/O, or explicit fixed mapped copies |
| Validation descriptor | Strict per-byte-callback checks, or `fwlab_file_nand_v2_posix_operation_batch` checks before/after a whole physical operation |
| Mapped ownership build | Default `FWLAB_MEDIA_EXCLUSIVE=0`; explicit `=1` moves mapped operation-descriptor file-property checks to admission/recovery and close |

Mapped constructors require tmpfs on the actual opened FD, cap the complete
image at 600 MiB, perform strict ordinary-I/O format/recovery first, preallocate
the extent and prefault one fixed shared mapping. Preparation failure has no
automatic ordinary-I/O fallback. Range checks, CRC/OOB, physical-state checks,
the lock and synchronization are not removed. No mapped pointer escapes to NFC
or FTL, and mapped copies are not zero-copy.

Construction follow-up: [ADR-0015](0015-capacity-presets-and-mapped-budgets.md)
retains600MiB as the default and permits an explicit bounded per-open budget.
It does not change this baseline's on-media format or broaden its old evidence.

The operation descriptor assumes exclusive backend control for the whole
operation; it does not promise per-callback detection of an external truncate
and regrowth. The optional exclusive build additionally requires stable
credentials and no independent mutation, resize, unlink, attribute change or
FD reuse for the admitted lifetime. It does not prevent those actions. Direct
strict callbacks, cold format/recovery, diagnostic hash and ordinary non-mapped
I/O remain strict. Mapped-access faults are process-fatal; restart decides the
surviving image state. These narrower assumptions must be named with results.

## Compatibility and consequences

- [File NAND v0](../../media/file-nand-v0/file_nand_engine.c) and
  [compact v1](../../media/file-nand-v1/README.md) remain separate readers and
  writers for their existing images. This ADR neither upgrades those images
  nor rewrites their historical crash evidence.
- Physical v2, FTL format, NFC interface/model revision and Host profile are
  independently selected identities. Their version numbers are not equivalent.
  [ADR-0013](0013-scalable-ftl-and-page-windows.md) describes the actual FTL/NFC
  consumer; [ADR-0014](0014-native-profile-and-serial-mq2.md) fixes native
  construction.
- File or tmpfs selection changes substrate properties, not the data path.
  Tmpfs proves only named functional/process-restart behavior while the
  filesystem survives, not disk persistence, whole-machine power loss or SSD
  throughput. Ordinary disk barriers also do not establish every possible
  sector-collateral failure model.
- Raw block-device admission is not implemented by this choice. It needs its
  own size/alignment/ownership/initialization adapter and deployment validation;
  neither these constructors nor this ADR authorize writes to an existing disk.

## Open real-NAND contract

The physical engine persists erase generation and health in independent block
records. `result_none`, `load_page` and `media_erase` return that truth to NFC;
FTL subsequently uses it to validate DATA/OOB and recover reclamation. Real
NAND does not automatically supply this simulator metadata.

The existing P01 boundary remains open: a physical implementation must assign
durable ownership and recovery of generation/health, including blank blocks and
interrupted erase, before replacing the simulated medium. No such hardware
persistence implementation is established by this ADR. This is a silicon-port
contract gap, not evidence of current simulator corruption, and it forbids the
claim "replace only the backend for lossless real-NAND migration." See the
[published preview limitations](../results/2026-09-05-vertical-spine-preview.md#review-and-remaining-limits).

## Source and verification anchors

- [physical_nand.c](../../media/file-nand-v2/physical_nand.c): `begin`,
  `abort_install`, `recover`, `fnv2_engine_open`,
  `fwlab_file_nand_v2_program_pages`, `media_erase`.
- [physical_nand_codec.c](../../media/file-nand-v2/physical_nand_codec.c):
  versioned superblock, block/page and intent/terminal encoding and validation.
- [physical_nand_posix.c](../../media/file-nand-v2/physical_nand_posix.c):
  `mapped_prepare`, `operation_begin`, `operation_end` and explicit constructors.
- [physical_nand_batch.c](../../media/file-nand-v2/physical_nand_batch.c):
  `fwlab_file_nand_v2_batch` binds scalar/batch callbacks, UUID and geometry to
  one actual media instance.
- Existing adjacent tests are in
  [test_physical_nand.c](../../media/file-nand-v2/tests/test_physical_nand.c).
  Available test entries and their failure-model limits are documented in the
  [media guide](../../media/file-nand-v2/README.md); an available target is not
  evidence that a release candidate ran it.
