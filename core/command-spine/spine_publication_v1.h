/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_SPINE_PUBLICATION_V1_H
#define FWLAB_SPINE_PUBLICATION_V1_H

#include "spine_internal.h"

enum fwlab_spine_publication_decision_v1 {
    FWLAB_SPINE_PUBLICATION_V1_COMMITTED = 1,
    FWLAB_SPINE_PUBLICATION_V1_DISCARDED = 2
};

/* Private native-stream extension. Callers serialize with lifecycle step.
 * NORMAL commands without Abort relations only. An unknown publication must
 * retain its lease; DISCARDED requires a closed epoch and HIF effect drain.
 * HIF owns CQ occupancy and notification after COMMITTED. It supplies fresh
 * command/origin identities for each new command, even when a queue slot is
 * reused. The last command_capacity finish receipts support exact retries;
 * an older, acknowledged receipt is STALE. */
enum fwlab_spine_result_v0 fwlab_spine_publication_v1_acquire(
    void *arena,
    const struct fwlab_spine_command_ticket_v0 *ticket,
    struct fwlab_completion_lease_v0 *lease,
    struct fwlab_nvme_completion_intent *intent);

enum fwlab_spine_result_v0 fwlab_spine_publication_v1_finish(
    void *arena,
    const struct fwlab_spine_command_ticket_v0 *ticket,
    const struct fwlab_completion_lease_v0 *lease,
    uint32_t decision);

#endif /* FWLAB_SPINE_PUBLICATION_V1_H */
