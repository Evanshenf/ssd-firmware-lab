<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0002: Power domains and persistence happens-before

- Status: Accepted semantic baseline; on-media encoding awaits prototype
- Date: 2026-08-28

## Truth domains

1. Firmware truth: logical/physical mapping, journal, trim tombstones, checkpoints and recovery policy. Persistent copies are written by firmware through NFC into reserved NAND pages/OOB.
2. Physical NAND truth: page/OOB bits, erase generations, operation outcomes, wear and bad blocks.
3. Simulator infrastructure truth: physical-operation WAL, media superblock/generation and Host I/O completion. It recovers domain 2 after daemon/Host failure and never provides an authoritative decoded logical mapping.

## B/A/C/S ordering

ADR-0007 is the executable command-level refinement of this ordering. It names
these points B_phys/A_phys/C_phys/OUTCOME_DELIVERED and separately defines
mapping commit, checkpoint commit and external volatile/durable success. It
does not replace the physical ordering below.

```text
stage payload to inactive slot and flush
  ≺ B: durable BEGIN
  ≺ A: physical APPLIED or NO_EFFECT outcome recorded
  ≺ C: outcome published durably in commit-sequence order
  ≺ S: status visible to the originating firmware command
```

Before any physical mutation, B records operation ID, total-order commit sequence, base generation, target/type, payload slot/digest, NAND profile version, deterministic seed/input/outcome and full source-command identity.

A records `APPLIED` or `NO_EFFECT`, candidate root/delta and physical status. A operations may finish out of order; C cannot skip an earlier commit sequence. C publishes one unique generation; a no-effect outcome may still advance generation so recovery has no ambiguous hole. S is legal only while the exact originating command token remains live.

## Crash recovery

| Crash point | Required recovery |
|---|---|
| Before B | Reclaim non-authoritative inactive payload |
| B after, A before | Deterministically replay the frozen input/outcome |
| A(APPLIED) after, C before | Idempotently roll forward to one C; never guess a generic rollback |
| A(NO_EFFECT) after, C before | Keep old physical truth and commit the no-effect ledger outcome |
| C after, S before | Physical truth already changed; only the same live firmware instance holding the original cookie may query/ACK one completion |

The outcome ledger belongs to media/HIF infrastructure. A reset or new firmware instance cannot use it to recover mappings; new firmware scans only its NAND/OOB metadata. A late old operation may become an orphan page, but can never signal, DMA, publish a queue entry or mutate new firmware state.

The stable media profile holds a reset fence until every old operation reaches C or no-effect before the new controller becomes ready. Late physical effects are restricted to explicit fault-injection profiles.

## Firmware durability dependencies

Firmware completes a durable write or Flush only after its data and recoverable mapping dependencies satisfy the requested persistence policy. Host `fsync`/`fdatasync` is a substrate primitive, not a substitute for the firmware dependency graph. Relocation validates that the source/version is still current before mapping publication; trims use versioned tombstones so older copies cannot resurrect.

## Checkpoint and WAL

The media layout has two checkpoint/superblock slots, segmented physical WAL, page/OOB slots and physical health state. A checkpoint contains root/generation, covered log sequence, physical wear/bad-block/ECC state, unresolved BEGIN entries, encoding version and checksum—never a decoded authoritative logical map.

```text
write inactive checkpoint → flush
→ atomically flip anchor/superblock → flush
→ recycle completely covered WAL segments
```

Raw media recycles fixed segments rather than assuming file truncation. Losing active/uncovered WAL fails closed; covered history may be reclaimed.

## Event separation

Controller reset, function-level reset, firmware reset, simulated SSD power loss, daemon death, Host/substrate crash and an explicit power-loss-protection profile are distinct events. Tests and claims name the event actually injected. Owner transfer increments owner epoch; reset increments controller epoch; queue recreation increments its ring generation; rebuilding the instance creates a new nonce.

## Raw media boundary

Initialization is a separate CLI that verifies expected serial, exact size, whole-device identity and an operator token. Runtime opening validates magic, geometry version, UUID, serial and size. Discard is off. Partitions, filesystems, holders, mounts, snapshots, backups, replication and live migration are forbidden for the raw medium.
