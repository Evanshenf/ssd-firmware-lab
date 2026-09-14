/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_PRIVATE_NFC_CHANNEL_V2_H
#define FWLAB_PRIVATE_NFC_CHANNEL_V2_H

#include "fwlab/private/nand_channel_v2.h"
#include "fwlab/private/nfc_page_v2_lab.h"

#define FWLAB_NFC_CHANNEL_V2_CREDITS 4u

enum fwlab_nfc_channel_v2_phase {
    FWLAB_NFC_CHANNEL_V2_BUILD = 0,
    FWLAB_NFC_CHANNEL_V2_FLOOR,
    FWLAB_NFC_CHANNEL_V2_ADMIT,
    FWLAB_NFC_CHANNEL_V2_RUN,
    FWLAB_NFC_CHANNEL_V2_JOINED,
    FWLAB_NFC_CHANNEL_V2_CLOSING,
    FWLAB_NFC_CHANNEL_V2_CLOSED
};
/* Private cooperative WAVE4 evidence, not a stable observer/thread API.
 * now_ns is committed JOIN time. Actor reports are cached at init/JOIN/close;
 * no snapshot getter interrogates a running actor. No global media sequence. */
struct fwlab_nfc_channel_v2_stats {
    uint64_t now_ns, accepted_requests, sealed_batches, joined_batches, retired_acks;
    uint64_t snapshot_main_bytes, snapshot_oob_bytes;
    uint32_t phase, occupied_credits, results_pending, retirement_pending;
    uint32_t actor_owned[FWLAB_NAND_CHANNEL_V2_MAX];
    uint32_t batch_requests[FWLAB_NAND_CHANNEL_V2_MAX];
    uint8_t closed, quarantined, poisoned, counters_saturated;
    struct fwlab_nfc_page_v2_lab_stats channel[FWLAB_NAND_CHANNEL_V2_MAX];
};
struct fwlab_nfc_channel_v2;
size_t fwlab_nfc_channel_v2_arena_size(void);
size_t fwlab_nfc_channel_v2_arena_alignment(void);
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_init(
    void *, size_t, const struct fwlab_nfc_page_v2_lab_mutation_config *,
    const struct fwlab_nand_channel_v2 *, struct fwlab_nfc_channel_v2 **);
struct fwlab_nfc_page_v2_provider fwlab_nfc_channel_v2_provider(struct fwlab_nfc_channel_v2 *);
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_live_idle(const struct fwlab_nfc_channel_v2 *, bool *);
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_snapshot(
    const struct fwlab_nfc_channel_v2 *, struct fwlab_nfc_channel_v2_stats *);
/* Joined/idle actors only. Returned PPA is global; index is the child's existing
 * bounded trace index. This does not create a second trace storage system. */
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_trace_at(
    const struct fwlab_nfc_channel_v2 *, uint32_t channel, uint32_t index,
    struct fwlab_nfc_page_v2_lab_trace *);

/* The PAGE2 facade has one serialized caller. It snapshots PROGRAM before
 * ACCEPTED and never queues caller-owned payload pointers. The first step
 * seals the current batch, then all child ingress precedes any child step.
 * No new batch is admitted until all results and retirement ACKs drain.
 *
 * Explicit WAVE4 policy: cancel is drain-only control for every accepted kind,
 * including READ; it is never forwarded to a child. Physical terminal results
 * remain unchanged. The upper owner suppresses cancelled Host publication.
 * This profile does not model mid-batch virtual-time cancellation. Reset
 * closes admission immediately, drains accepted work/results/ACKs, and only
 * then closes the actors. Only take_result OK consumes a caller result; a
 * later step consumes its reserved retirement ACK and recycles the credit. */

#endif
