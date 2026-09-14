<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0025: opt-in channel NAND in the existing native entry

## Decision

The existing MQ2 worker may explicitly select `--nand-profile channel-lab4k`.
Its default remains mapped single-file physical-v2, PAGE2-R0 and FTL format2.
There is no second controller, SQ consumer, CQ publisher or protocol engine.

The opt-in selects the same mutable format3 construction from
[ADR-0024](0024-mutable-format3-read-write.md): existing write waves and read
pool, IPR channel actors and real physical-v2 shards. The Linux profile,
lifecycle, data mover, FTL algorithms and NAND effects are unchanged. The
first preset is a 64-MiB namespace over four channels, one LUN per channel,
two planes per LUN and forty 64-page blocks per plane. Pages remain 4KiB
main plus128B OOB. It is not a modern vendor geometry or capacity graduation.

## Lifetime and storage

The native media owner retains the volume lock, four physical engines,
immutable descriptors, timing and factory across runtime reset and NO_OWNER.
Each drained firmware runtime may be reconstructed with fresh volatile
identities, using recovery rather than format. Final shutdown closes the
volume only after the owner server, runtime and frames have been closed.

The volume uses the existing **strict POSIX** channel assembly, not the
mapped adapter used by default native runs. Its immutable `volume.nand`
manifest binds four independent physical-v2 files and child UUIDs. Format
is new-only in an empty private directory; recovery never creates, converts
or resizes. The explicit profile checks recovered geometry before runtime
construction. Existing FTL recovery checks format/root/capacity separately.
Preflight budgets the sum of all four actual image sizes and the manifest,
with filesystem/RAM margins. Logs and owner sockets stay outside this exact
media file set.

The M4 `MEDIA_SCALED` field identifies a construction family, not the FTL disk
format number. Do not send `3` in that field. Record the selected NAND
profile/namespace together with the exact source, modules and worker in the
operator-supplied binding manifest. An old R0 run is not this option's proof.

## Why cooperative first

This first construction is retained as the control. The later optional worker
lifetime refinement is [ADR-0026](0026-native-worker-lifetime.md); it does not
broaden the original cooperative evidence or change the media format.

This binding passes a NULL external executor. Existing cooperative jobs feed
the MQ2 progress report without a main-loop change. A Linux executor is
permanently stopped by close: reusing its pointer after reset would be wrong.
Threaded native integration needs a fresh executor for each runtime, actual
joins before replacement and bounded waits during startup/drain. That work
must not change the lifetime of the persistent media owner.

LAB timing is explicit and unpaced: read array10us, program array100us,
erase array1ms, command/confirm/status1us, and1,000,000,000B/s per-channel
bus with a one-byte status response. These are synthetic parameters, not
wall-clock limits or calibrated NAND performance. Cooperative execution is
not four CPU workers; adding workers does not create more NAND resources.

## Evidence boundary and non-goals

Reuse the actual native constructor and `firmware_loop` with only the Host
ioctl boundary faked. Exact head/tail I/O, Flush, reset/recovery, retained
media and final reopen are the finite construction checks. Such execution
does not prove native Linux binding, DMA/IRQ behavior or a real M5 switch.
The existing fixture's reset occurs before first capture, not during accepted
NAND work. Native deployment remains a separate exact-profile operation.

No mapped-shard optimization, OS workers, NUMA policy, wall-clock pacing,
64-GiB campaign, raw media, vendor part, real-NAND generation persistence or
new test framework is selected by this ADR. Old constructions and evidence
remain independently identified; there is no automatic format migration.
