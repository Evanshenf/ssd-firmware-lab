<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Scalable FTL: bounded first implementation

This is a second, construction-selected FTL behind the existing Block service
and ready-only volume descriptor. It replaces neither the protocol/lifecycle
nor the NFC executor. The tiny [M3P reference](../m3p/m3p.h) remains the
reader/writer for its old formats. A runtime selects exactly one FTL; failure
does not trigger fallback to another engine or implicit formatting.

The initial qualification profiles are 64 MiB and 256 MiB logical capacity,
using 80 MiB and 320 MiB of physical NAND main area. They are connected through
the headless Linux-profile path. The released native worker still uses its
original 1-MiB profile; it has not silently gained larger capacity. A real
64-GiB workload is a subsequent, separately provisioned qualification.

## Responsibilities and construction

| Code | Responsibility |
|---|---|
| `ftl_scale_runtime.c` | Arena construction, Block admission/status/drain, bounded Host RMW/read/write |
| `ftl_scale_parent.c` | One complete Block request, bounded private subgroups, committed prefix and between-group maintenance |
| `ftl_scale_mapping.c` | The single map-update implementation, validity bitmap, block summaries and indexed heaps |
| `ftl_scale_gc.c` | Space reservation, live victim copy, atomic GC mapping commit, erase and free-block handoff |
| `ftl_scale_codec.c` | Checked layout and explicit root/checkpoint/journal/DATA OOB serialization |
| `ftl_scale_recovery.c` | Streamed checkpoint, two-rail journal, recovery and safe cleanup ordering |
| `ftl_scale_nfc.c` | One actual NFC child at a time; no physical-media metadata side channel |
| `ftl_scale_window.c` / `ftl_scale_nfc_v2.c` | Explicit format-2 windows and typed page-v2 NFC operations |

Allocate the FTL arena from a resource configuration, obtain its staging-buffer
view, construct NFC with that view and the actual physical geometry, initialize
FTL with the resulting provider, then start **either** format **or** recovery.
Starting these state machines performs no I/O until stepping. The
[headless composition](../../frontends/headless-scale/scale_storage.c) provides
this construction and owns the modeled NFC trace-window retirement. Firmware
sources themselves use no Linux device/file/queue/DMA address.

`format_start(lba_count)` creates a new FTL format explicitly.
`recover_start(expected_lba_count)` reads capacity from valid roots; zero means
discover, nonzero is only an assertion. `mapping_slots` sizes RAM and limits
admission of recovered volumes; it never substitutes for their capacity.
The volume descriptor is unavailable until recovery and cleanup finish.

## Mapping, space and durability

The resident map is 16 bytes per allocated mapping slot. Each value binds a
32-bit linear physical page, 64-bit DATA identity, erase generation and sector
mask. One physical-page validity bit, 32 bytes per block and two 32-bit heap
arrays support indexed allocation/victim selection. There is no second full
visible map, whole-device reverse map, or capacity-sized recovery DATA table.

The default headless factory allocates mapping slots up to the physical-page
count. This resource ceiling is deliberately larger than logical capacity;
account for it when reporting RAM. At 64 GiB, 256 MiB is the arithmetic cost of
exactly one map slot per logical 4-KiB page, **not** this executable's measured
total RAM or the physical-ceiling allocation. Staging, runtime/NFC state and
filesystem cache are additional.

The first implementation supports Read, Write and Flush. The Linux profile
does not advertise DSM/TRIM, and this engine rejects unsupported Block TRIM;
the enum's presence is not a support claim. One namespace, 512-byte LBAs and an
8-KiB maximum transfer remain protocol-profile limits. FTL geometry is
4-KiB main plus 128-byte OOB, ascending one-program pages, 32 or 64 pages/block.
The two qualification profiles use 64 pages/block.

An explicit `fwlab_ftl_scale_extended_config` construction (version 2) can now
set a Block transfer limit up to 2048 LBAs (1 MiB). The legacy constructor still
limits requests to 16 LBAs. This does not change namespace capacity, on-media
format 1 or the current Linux protocol profile's 8-KiB limit. The larger Block
path is qualified below the protocol layer; a larger NVMe profile and native
transport are separate integration work.

The parent retains its original token, buffer lease, request and final status.
It streams existing <=8-KiB groups without embedding a 1-MiB payload in each
FTL command. The caller retains the buffer lease until retirement and does not
mutate Write input while it executes; the old lease itself is not a sealed,
immutable-span capability. Private CP/GC may run between resolved groups while
admission, public maintenance and epoch quiescence still count the parent.
Non-final mapping records retain the old Host durability frontier; only the
final successful group advances it. Known cancelled prefixes and uncertain
mapping outcomes remain different. No whole-1-MiB atomicity is promised.

The finite adjacent-buffer test uses the real FTL, scaled NFC and compact POSIX
media. GCC and Clang ASan/UBSan pass aligned/unaligned 1-MiB Write/Read, neighbor
preservation, SELF followed by reopen/readback without Flush, owned-parent CP,
eight owned-parent live GCs with 129 relocated pages verified, cancellation,
failed-Read status, uncertain mapping recovery and zero close. It does not
prove Host DMA suppression or native performance through a fake transport.

```sh
make -C frontends/headless-scale -f ftl.mk check-parent
```

This entry uses the same explicitly provisioned capped tmpfs convention below,
not an existing image or a raw device. Parent streaming is the prerequisite
for physical batching; its unchanged v1 subgroups do not remove the measured
20x normal Write amplification or establish the throughput objective.

Disk-backed qualification completed full writes, interleaved half-volume
overwrite and full readback after restart for both profiles. The 64-MiB run
applied 174 GC_COMMITs and 55 checkpoints before restart; the 256-MiB run
applied 666 and 54 respectively. Ten fixed process-cut/active-close/capacity
expectation cases passed with Clang ASan/UBSan as an ordinary user. These are
bounded software-model results, not physical host-power-loss or throughput
claims. Explicitly labeled tmpfs functional runs are separate evidence.

One Host group owns at most three page deltas and never straddles allocation
blocks. OPEN durably allocates a block identity before its first DATA program;
DATA identities derive from that identity and the page offset. An unreadable
orphan's OOB is not needed to reconstruct an issuance high-water mark.

The initial policy is write-through: all new DATA and the entire MAP_GROUP are
durable before successful Write completion. The existing ABI still controls
the returned witness: a VOLATILE_ALLOWED caller receives VOLATILE with a zero
frontier, SELF receives SELF_DURABLE, and Flush receives FRONTIER_DURABLE.
Linux-profile-v1 advertises no volatile write cache and requests SELF for all
Writes, including those without FUA. The C43 reference exercises the weaker
caller contract without changing the FTL or the ABI.

Before accepting work, reserve its pages, records and identities. Data and
metadata areas are separate. After subtracting both checkpoint banks, four
journal rails and roots, the conservative logical quota is:

```text
LPN_count <= (DATA_blocks - 2) * (pages_per_block - 3)
```

Greedy GC copies at most 61 live pages into one reserved block, then publishes
the whole victim in one private mapping transaction. It does not introduce
NAND actions into the command graph. The destination becomes a Host head with
its actual remaining programmable tail. Only after the victim has no live map
references does durable ERASE_INTENT → physical erase → ERASE_DONE return it to
the free heap. The GC diagnostic counts applied GC_COMMIT records, including
recovery replay; it is not an independent physical erase/wear counter.

## Persistent authority

### Explicit format-2 batch construction

`fwlab_ftl_scale_init_window_v2` selects a separate on-media FTL format and the
[NFC PAGE2-R0 model](../nfc-page-v2/README.md). Old constructors retain format 1
and C3. Recovery rejects a constructor/format mismatch before FTL cleanup or
formatting; there is no mixed-format root, auto-conversion or fallback.

One additional 270336-byte FTL window stages up to 64 contiguous physical pages
inside a retained parent, rather than putting a 1-MiB array in every command.
Writes split at head/tail RMW and physical block boundaries. DATA is committed
before journal A, then B, then the atomic MAP_WINDOW update. Full heads close
implicitly through that record; OPEN remains durable before the first DATA.
GC still guarantees at most three available pages at a time: a live-copy victim
can produce a short window, never a hidden 64-page allocation requirement.
Reads group only consecutive mapped LPN/PPA runs with matching generation;
holes are zeroed by FTL, and all mapped page/OOB facts are checked before the
group is copied to the controller buffer. Internal storage is not Host DMA
authority, and accepted DATA still drains its A/B transaction after cancellation.

Use the explicit headless factory or these existing test entries:

```sh
make -C frontends/headless-scale -f ftl.mk check-parent-window-v2
make -C frontends/headless-scale -f ftl.mk check-window-v2
make -C frontends/headless-scale -f ftl.mk FWLAB_CRC_NATIVE=1 check-window-v2-cost
```

The real Block parent entry covers 1 MiB; the existing Linux-profile/lifecycle
entry still uses at most 8 KiB. Neither changes the native deployment or implies
Host transport, multi-queue, physical power-loss or 10-GB/s qualification.

### Root/checkpoint/journal persistence

The FTL uses its own magic and format version, separate from both M3P's formats
and [compact physical file-NAND](../../media/file-nand-v1/README.md). The latter
persists physical pages/OOB/erase state and physical redo; it does not interpret
or logically commit mappings. The file nevertheless contains FTL roots,
checkpoints and mapping journals, encoded by FTL into ordinary NAND pages.

Two fixed root blocks select two streamed checkpoint banks and two journal
epochs. Each epoch has independent, block-aligned A and B rails. Journal main
bytes are identical across rails; OOB also binds the actual rail/address.
Append A durably, append B durably, then permit the dependent operation or
acknowledgement. Active rails are never erased or reused.

Checkpoint pauses mapping changes, erases the inactive root first, prepares its
body and journal headers, and programs the candidate root last. It then
durably retires the old root **before** new-epoch writes or reclamation. Old
root authority cannot survive after DATA it might reference has been reclaimed.
Full checkpoints have linear map-size I/O cost and pause normal admission;
this is a simple initial policy, not a throughput/endurance optimization.

NFC does not expose a reliable VALID/TORN classification for a failed read.
Recovery therefore uses both journal rails: one valid copy permits replay,
two valid copies must agree, and degraded old copies do not truncate later
acknowledged records. An ECC result plus a genuinely successful all-FF peer
read can identify an unconfirmed tail; later nonempty records invalidate that
tail. Both unreadable, readable-invalid identity/CRC, and internal errors fail
closed. Never treat prefilled or stale buffers as evidence of an erased page.

Recovery replays the ordered journal, rebuilds map indexes, seals open heads
without reusing their tails, and persists that complete state in a fresh
checkpoint. Only after old-root retirement may it clean up zero-live blocks.
Thus a second interruption cannot discard a formerly single-copy GC decision
after its source has already been erased. Atomic whole-victim GC recovers
all-old or all-new mappings; it does not strand a partially published victim.

An unresolved erase can be performed again as a **new physical erase**. Record
its actual final generation; do not invent an idempotent wear increment.
Simulated NFC still supplies this generation truth. The real-NAND persistence
contract is not implemented, so replacing a backend alone is not a lossless
silicon-port guarantee. Unrecoverable DATA/checkpoint corruption fails closed;
all possible physical power failures or multiple media errors are not claimed.

## Reproduce without a raw device

From the repository root, as an ordinary user:

```sh
make -C frontends/headless-scale -f ftl.mk check
frontends/headless-scale/build/scale-ftl/test_scale_ftl --cuts
make -C frontends/headless-scale -f ftl.mk check-full
```

The default journey covers both capacities, real page/OOB I/O, edge/range/RMW,
caller-specific witnesses, live GC, journal reuse, recovery and early close.
`--cuts` adds the ten fixed process-cut/active-close/expectation cases.
`check-full` writes every logical block, overwrites half the namespace to force
reclamation, reopens and verifies the whole volume using deterministic data.
It is deliberately separate from the quick iteration target and may take a
substantial time when the backing filesystem has slow synchronous writes.

For faster **functional-only** regression, the same test accepts
`FWLAB_TEST_MEDIA_DIR` as an absolute parent directory. The Make entry defaults
to `/run/fwlab-test-media`; direct invocation requires the variable explicitly.
Missing, non-tmpfs or insufficient-capacity media is an error, never a fallback
to the system disk. This functional entry does not launch disk qualification.
Only the disposable NAND image directory changes. FTL, NFC, physical redo and
all synchronization calls remain identical. For the 64/256-MiB profiles, use
an isolated tmpfs with a 1-GiB size limit, run one invocation at a time, and
keep executables and logs outside that mount:

```sh
FWLAB_TEST_MEDIA_DIR=/path/to/isolated-tmpfs \
  /usr/bin/time -v frontends/headless-scale/build/scale-ftl/test_scale_ftl --full
```

The program still runs 64 MiB and then 256 MiB serially. Output identifies the
filesystem and labels tmpfs results. Before removing each image it reports the
dedicated tmpfs's allocated bytes; compact-NAND allocation is monotone within
each serial image, so this captures its filesystem-memory high-water mark.
Report that separately from the process maximum RSS printed by `time`.
Do not move an active image, silently use tmpfs for disk qualification, or treat
tmpfs functional/process-restart results as disk persistence, host power-loss
or storage-performance evidence. The mount limit is not a whole-process RAM
limit, and this permission does not extend to a 64-GiB image.

### Explicit 64-GiB functional profile

An isolated validation branch also provides `--plan-64g` and `--full-64g`.
The first checks sizing/resources without NAND I/O; the second runs a64-GiB
namespace over80GiB modeled main area on an explicitly selected large tmpfs.
It requires separately approved memory provisioning. The small regression's
1-GiB mount is not enlarged or reused automatically. Large preflight checks the
actual image size, free filesystem space and MemAvailable, with a90-GiB maximum
mount size and headroom for runtime/OS memory.

The large entry preserves the production firmware, NFC, media format, locks
and synchronization calls. It skips the tiny1100-write journal-rollover setup
because the large journal has64K slots; full fill plus interleaved half-volume
overwrite must actually cause GC/checkpoint reuse before restart/readback can
pass. It is not a sparse high-address-only qualification.

Stage transitions and30-second progress report completed Host bytes, elapsed
time, GC/checkpoint counts, NFC starts and private maintenance cursors. Increased
iteration allowance applies only to the test's large initialization/recovery;
no firmware transition or resource limit is relaxed. A wall-time overrun calls
for read-only CPU/I/O/memory inspection and an explicit report, not automatic
whole-run retries. A started large run is not a pass, and tmpfs never proves
real-disk persistence or SSD performance.

These commands create private regular files in uniquely named temporary
directories. Successful journeys remove their own test image; failed journeys
retain it and print its location. They never load a module, write a raw device,
mount a filesystem, modify a VM, or operate the release worker. A command being
available is not proof that it has passed; qualification records bind actual
source/executable identities and observed results.
