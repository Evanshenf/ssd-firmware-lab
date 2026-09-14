/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_PRIVATE_NFC_CHANNEL_V2_H
#define FWLAB_PRIVATE_NFC_CHANNEL_V2_H

#include "fwlab/private/nand_channel_v2.h"
#include "fwlab/private/nfc_page_v2_lab.h"
#include "fwlab/private/nfc_channel_v2_job.h"

#define FWLAB_NFC_CHANNEL_V2_CREDITS 4u

enum fwlab_nfc_channel_v2_phase {
    FWLAB_NFC_CHANNEL_V2_BUILD = 0,
    FWLAB_NFC_CHANNEL_V2_FLOOR,
    FWLAB_NFC_CHANNEL_V2_ADMIT,
    FWLAB_NFC_CHANNEL_V2_RUN,
    FWLAB_NFC_CHANNEL_V2_JOINED,
    FWLAB_NFC_CHANNEL_V2_CLOSING,
    FWLAB_NFC_CHANNEL_V2_CLOSED,
    FWLAB_NFC_CHANNEL_V2_RETIRING,
    FWLAB_NFC_CHANNEL_V2_SHUTDOWN
};
/* Private closed-batch WAVE4 evidence, not a stable observer/thread API.
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
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_init_executor(
    void *, size_t, const struct fwlab_nfc_page_v2_lab_mutation_config *,
    const struct fwlab_nand_channel_v2 *, const struct fwlab_nfc_channel_executor *,
    struct fwlab_nfc_channel_v2 **);
/* Policy selection is independent of placement; NULL executor is cooperative.
 * Existing channel constructors retain LUN-exclusive READ semantics. */
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_init_policy(
    void *, size_t, const struct fwlab_nfc_page_v2_lab_mutation_config *,
    const struct fwlab_nand_channel_v2 *, enum fwlab_nfc_page_v2_lab_read_policy,
    const struct fwlab_nfc_channel_executor *, struct fwlab_nfc_channel_v2 **);
/* Executor callbacks/lifetime belong to the construction owner. They must not
 * reenter this PAGE2 facade. NULL selects the same bounded cooperative jobs. */
struct fwlab_nfc_page_v2_provider fwlab_nfc_channel_v2_provider(struct fwlab_nfc_channel_v2 *);
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_live_idle(const struct fwlab_nfc_channel_v2 *, bool *);
/* Coordinator-only sleep eligibility, never a NAND/FTL idle certificate.
 * True only after every phase job is posted and no local action/result remains.
 * The executor must still acquire-check its replies before an external wait. */
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_external_wait(const struct fwlab_nfc_channel_v2 *, bool *);
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_snapshot(
    const struct fwlab_nfc_channel_v2 *, struct fwlab_nfc_channel_v2_stats *);
/* Coordinator-owned synchronized cache of the child's existing bounded trace.
 * Returned PPA is global; getters never access a worker-owned actor/model. */
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
 * later actor retirement ACK recycles the credit. Reset quiescence also waits
 * for executor shutdown (including actual joins in the Linux binding).
 * Step budget bounds coordinator transitions/cooperative quanta; asynchronous
 * worker CPU and NAND transitions are not charged as if they ran on this CPU. */

#endif
