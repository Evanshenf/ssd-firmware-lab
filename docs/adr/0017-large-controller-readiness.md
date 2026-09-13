<!-- SPDX-FileCopyrightText: 2026 Evanshenf -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# ADR-0017: Separate drained shutdown from successor readiness

- Status: Implemented candidate; native 64-GiB reset/rebind confirmed, full-volume qualification pending
- Date: 2026-09-12
- Refines: [ADR-0014](0014-native-profile-and-serial-mq2.md), [ADR-0015](0015-capacity-presets-and-mapped-budgets.md)

## Observed problem

The first native 64-GiB volume completed Identify and shaped reads/writes, but
reset exceeded the original CAP.TO=1 deadline. Recovery reconstructed the FTL in
about 6.4 seconds. Extending only the test harness wait cannot change Linux's
controller deadline. A separate five-second shutdown wait also expired because
the original worker acknowledged shutdown only after reconstructing the next
runtime, even though old work had already drained in about two milliseconds.

These were observed transport/readiness failures, not evidence of lost NAND
data or a reason to change FTL, NFC, CRC, Flush/FUA or the media format.

## Fixed construction deadline

LARGE and MQ2 advertise CAP.TO=120, a fixed 60-second readiness budget, with CAP
`0x000000207801001f`. SMALL keeps its original value. This is a controller
construction property, not a capacity field imported into HIF. It does not
change the BAR size, namespace authority or the private attachment layout.
Qualification must still measure recovery against this bound, including after
full-volume overwrite. No worst-case timing guarantee follows from one sample.

CAP.TO does not extend Linux's separate shutdown-complete timeout. Keep Identify
RTD3E zero: its location is reserved under the advertised NVMe VS 1.0, even if
a later Linux driver would read it. No driver timeout override is needed by the
corrected reset/rebind result.

## Drain completion is not readiness

Append private native operation `DRAIN_ACK=16` for matching LARGE/PUMP builds;
message size and existing operation values remain unchanged. Matching worker
and kernel revisions are required. Existing SMALL/BAR and initial/owner-grant
construction keep their previous paths.

After the old runtime reports complete drain, DRAIN_ACK checks the current
function/epoch/owner, pending reset, closed effects and non-ready firmware. It
uses the same retained-request, DMA, frame and queue-reference cleanup as the
old final acknowledgement. Only successful cleanup establishes `reset_drained`.

For shutdown with CC.EN still set, this may publish SHST=complete while keeping
RDY set. An observed CC.EN=0 clears shutdown/fault state and publishes RDY=0.
That transition executes before the pending-recovery gate. A duplicate drain
acknowledgement cannot replay obsolete register state. No SQ capture, new DMA,
new command admission or firmware-ready permission is granted by DRAIN_ACK.

The worker pumps control at iteration zero and every 1024 recovery iterations,
only after accepted drain completion. This services the disable/re-enable
handshake while the successor remains non-ready; it is not another data thread
or a guaranteed wall-clock polling period. Another FLR during the already
drained interval resets the registers and shutdown state under the closed gate.
Its observation is not silently consumed. Only after the new runtime is actually
ready does the existing RESET_ACK permit activation. The transient recovery
pump flag clears on both success and failure and is not used for owner-grant
construction. Owner transitions clear the kernel's drain marker.

## Confirmation and stop

The existing actual-worker fixture confirms drain-before-recovery, control pump
while admission is closed and final acknowledgement after ready. This is not
kernel timing evidence. The real ARM native 64-GiB retained-media episode then
passed reset (6.44 s), unbind (0.06 s), rebind and exact shaped readback. Read-only
BAR samples recorded shutdown completion about 2.2 ms after observing SHN,
CC.EN=0/RDY=0, and new readiness only after recovery. Register samples are not
an atomic hardware snapshot. A single FLR during drained/unbound recovery also
completed with successful fresh native enable/readback and no new kernel warning.

The first FLR harness attempt ended before issuing FLR because it sampled the
recovery-progress line too early. That guard failure is retained, not counted
as an executed FLR or a product defect. No media was reformatted.

Stop this readiness repair at those source and runtime confirmations. The
candidate subsequently passed the full-volume fill/striped-overwrite/cold-read
data group. A later cut campaign exposed the separate PCI/MSI lock inversion
described in [ADR-0016](0016-arm64-native-platform.md); its correction now has
native runtime confirmation and does not reopen the drain handshake
as a general redesign. Comprehensive integrity acceptance, concurrent publication
N01, performance, ARM L2/KVM and physical power-loss behavior retain their own
evidence boundaries.
