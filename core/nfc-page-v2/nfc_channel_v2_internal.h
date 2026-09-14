/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_NFC_CHANNEL_V2_INTERNAL_H
#define FWLAB_NFC_CHANNEL_V2_INTERNAL_H
#include "fwlab/private/nfc_channel_v2_job.h"

/* Only actor_job_step mutates this object after construction. The coordinator
 * owns its address, not permission to inspect these fields while dispatched. */
struct fwlab_nfc_channel_actor {
    struct fwlab_nfc_page_v2_lab *model;
    struct fwlab_nfc_page_v2_provider provider;
    struct fwlab_nfc_channel_job_entry entry[FWLAB_NFC_CHANNEL_JOB_SLOTS];
    struct fwlab_nfc_channel_job *current;
    uint64_t last_sequence, batch_uid, instance_nonce;
    uint32_t channel, epoch, cursor, stage, trace_sent;
    uint32_t fault;
    uint8_t granted, accepted, lower_owned, reports, completed;
    uint8_t batch_active, closed, quiet, quarantined;
};
enum fwlab_nfc_api_result fwlab_nfc_channel_actor_init(
    struct fwlab_nfc_channel_actor *, uint32_t channel, void *, size_t,
    const struct fwlab_nfc_page_v2_lab_mutation_config *,
    const struct fwlab_nand_batch_v2 *, struct fwlab_nfc_page_v2_lab_stats *);
#endif
