<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# NFC page-v2 functional and resource-timed LAB models

## Functional PAGE2-R0

This is an explicit, unreleased PAGE2-R0 construction, not a silent replacement
for the existing C3 fault/timing model. It accepts READ_GROUP, PROGRAM_GROUP and
ERASE through the [typed interface](../../include/fwlab/contracts/nfc_page_v2_provider.h).
Groups contain up to 64 contiguous full physical pages in one block, each with
4096 main bytes and 128 OOB bytes. NAND addresses are not namespace LBAs.

One serialized caller/worker owns one slot. PROGRAM input is copied once before
ACCEPTED and may then be released by the caller. A repeated active operation key
with the same canonical shape cannot resnapshot or reissue media I/O. The slot
remains occupied until its terminal result is taken or explicitly discarded.
Accepted UIDs increase monotonically; a retired key cannot be admitted again.

One step performs at most one physical operation synchronously; it is a work
bound, not a time bound or an asynchronous disk-I/O promise. READ validates every
page's physical/ECC/generation/health facts before any output copy. Failed reads
leave caller output unchanged. A mutating backend error is UNKNOWN unless no
mutating callback was dispatched; an error's result array is not a guessed
successful prefix. Cancelling an already completed program does not undo it.
Reset closes new admission and drains retained results before quiescence.

The model owns a 270336-byte payload window and 4224-byte scratch, plus request,
result and physical facts (279040-byte arena on the tested 64-bit ABI). This is
not zero-copy. CPU spans are controller-owned storage, never Host DMA authority.
Its private payload window is 64-byte aligned. Allocate the arena using both
`fwlab_nfc_page_v2_arena_size()` and `fwlab_nfc_page_v2_arena_alignment()`;
ordinary `calloc` does not guarantee this alignment. The constructor initializes
the arena. Caller input/output spans have no new alignment requirement.
R0 rejects nonzero injected-fault, timing and retry settings. Physical TORN,
unknown, CRC/OOB failures and erase-generation state still participate; no C3
injected-fault/timing equivalence or hardware NAND/ECC qualification is claimed.

The private [physical descriptor](../../include/fwlab/private/nand_batch_v2.h)
combines scalar and batch operations from one media instance. The concrete
[Cprime constructor](../../media/file-nand-v2/physical_nand_batch.c) binds its
geometry, UUID and context without exposing a file descriptor or file offsets
to this portable model. The [window FTL](../ftl-scale/ftl_scale_window.c) is the
actual consumer, selected through a distinct headless factory; legacy C3
construction remains available and does not consume batches implicitly.

The finite adjacent contract test uses a controlled in-memory physical adapter:

```sh
make -C core/nfc-page-v2 check
```

Actual FTL/NFC/physical-file journeys use the independently provisioned, capped
tmpfs described in the [FTL guide](../ftl-scale/README.md):

```sh
make -C frontends/headless-scale -f ftl.mk check-parent-window-v2
make -C frontends/headless-scale -f ftl.mk check-window-v2
```

These are functional/process-recovery checks, not physical disk persistence,
Host DMA suppression, native NVMe throughput or a 10-GB/s acceptance claim.

## Explicit resource-timed LAB constructions

[LAB-READ-R1](../../docs/adr/0018-resource-scheduled-nand-read-lab.md) adds four
finite slots, per-LUN read-register/array ownership and shared-channel transfer
arbitration. PREP uses R0; the one-way timed-read phase rejects mutations.
Its explicit FTL construction supplies bounded parallel read runs.

[LAB-RW-R2](../../docs/adr/0019-timed-nand-mutations.md) uses the same event engine
and PAGE2 interfaces, adding PROGRAM/ERASE and STATUS arbitration. It is timed
from construction, including startup/recovery, and initially binds ordinary
serial format2 FTL. Accepted PROGRAM bytes are snapshotted; real effects occur
one page at a time. Once the first confirm starts, normal cancellation drains
the accepted group. Physical SUCCESS remains SUCCESS; genuine failures preserve
the known prefix and stop later effects. The media format and FTL recovery are
unchanged. None of these LAB constructions silently replaces the native R0 path.

`fwlab_nfc_page_v2_lab_mutation_config` requires explicit positive timing and
channel-rate fields. There is no vendor/default device performance profile.
The real-media/J0 examples use 4-KiB main/128-B OOB, 1-us command/confirm/status,
10-us read array, 100-us program array, 1-ms erase array, 1-B status response and
1-GB/s per channel. These are **synthetic fixture costs**, not commercial NAND
specifications. The corresponding uncontended main-payload read/program rates
are about 269.049/38.200 decimal MB/s per LUN, before other work/contention.
Core fake tests deliberately use a different small time scale.

Each LUN independently applies its array delay, while traffic shares its channel.
More LUNs can overlap array work; more channels can supply more aggregate bus
bandwidth. Increasing only capacity or package labels does not increase speed.
The four active slots can limit scaling, and independent-plane operations are
not implemented. Virtual time advances without sleep and does not throttle
wall-clock I/O. Real simulator rate must be measured separately.

Use the existing capped tmpfs and run these media tests **serially**:

```sh
make -C core/nfc-page-v2 check-mutation
make -C media/file-nand-v2 check-nfc-mutation
make -C frontends/headless-scale -f ftl.mk check-mutation-j0
```

Media tests require `FWLAB_TEST_MEDIA_DIR` (Makefile default
`/run/fwlab-test-media`), reject absent/non-tmpfs/insufficient storage and use only
fresh disposable images. They do not format existing images or raw devices.
See the [exact-source result](../../docs/results/2026-09-14-timed-nand-mutations.md)
for scope and limitations.
