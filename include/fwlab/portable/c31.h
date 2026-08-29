/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PORTABLE_C31_H
#define FWLAB_PORTABLE_C31_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/contracts/c31_provider.h"
#include "fwlab/portable/c31_types.h"

struct fwlab_c31;

/* The caller owns one stable, max_align_t-aligned arena until teardown ACK. */
size_t fwlab_c31_arena_alignment(void);

size_t fwlab_c31_arena_size(const struct fwlab_c31_capacity *capacity);

enum fwlab_c31_api_result fwlab_c31_init(
    void *arena,
    size_t arena_size,
    const struct fwlab_c31_capacity *capacity,
    uint64_t instance_nonce,
    const struct fwlab_c31_provider_set *providers,
    struct fwlab_c31 **instance
);

/* All calls for one instance are serialized by the caller. */
enum fwlab_c31_instance_phase fwlab_c31_phase(
    const struct fwlab_c31 *instance
);

/* Validation and capacity failure leave descriptor ownership with the caller. */
enum fwlab_c31_api_result fwlab_c31_submit(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_command_descriptor *descriptor,
    struct fwlab_c31_command_handle *command
);

/* One unit performs at most one bounded provider poll and one core transition. */
enum fwlab_c31_api_result fwlab_c31_step(
    struct fwlab_c31 *instance,
    uint32_t budget,
    struct fwlab_c31_step_result *result
);

enum fwlab_c31_api_result fwlab_c31_command_state(
    const struct fwlab_c31 *instance,
    const struct fwlab_c31_command_handle *command,
    enum fwlab_c31_lifecycle_state *state
);

enum fwlab_c31_api_result fwlab_c31_completion_acquire(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_command_handle *command,
    struct fwlab_c31_completion_lease *lease,
    struct fwlab_c31_completion_intent *intent
);

/* A released lease is stale; reacquisition creates a new generation. */
enum fwlab_c31_api_result fwlab_c31_completion_release(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_completion_lease *lease
);

/* Consume means HIF has committed its one physical publication decision. */
enum fwlab_c31_api_result fwlab_c31_completion_consume(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_completion_lease *lease
);

/* Abort is a control ticket, not a protocol command or rollback promise. */
enum fwlab_c31_api_result fwlab_c31_abort_request(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_command_handle *command,
    struct fwlab_c31_abort_ticket *ticket,
    enum fwlab_c31_abort_outcome *outcome
);

enum fwlab_c31_api_result fwlab_c31_abort_query(
    const struct fwlab_c31 *instance,
    const struct fwlab_c31_abort_ticket *ticket,
    enum fwlab_c31_abort_outcome *outcome
);

enum fwlab_c31_api_result fwlab_c31_abort_ack(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_abort_ticket *ticket
);

enum fwlab_c31_api_result fwlab_c31_reset_begin(
    struct fwlab_c31 *instance
);

/* ACK is unavailable until old operations, commands, leases and tickets drain. */
enum fwlab_c31_api_result fwlab_c31_reset_ack(
    struct fwlab_c31 *instance
);

enum fwlab_c31_api_result fwlab_c31_teardown_begin(
    struct fwlab_c31 *instance
);

enum fwlab_c31_api_result fwlab_c31_teardown_ack(
    struct fwlab_c31 *instance
);

uint32_t fwlab_c31_trace_count(const struct fwlab_c31 *instance);

/* Ordinal zero is the oldest retained entry in the fixed trace ring. */
enum fwlab_c31_api_result fwlab_c31_trace_read(
    const struct fwlab_c31 *instance,
    uint32_t ordinal,
    struct fwlab_c31_trace_entry *entry
);

#endif
