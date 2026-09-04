/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C43_FAKE_SERVICES_H
#define FWLAB_C43_FAKE_SERVICES_H

#include "fwlab/portable/c4_command_graph.h"

#define C43_FAKE_EVENT_CAPACITY 32u

enum c43_fake_event {
    C43_FAKE_QUEUE_PREPARE_START = 1,
    C43_FAKE_QUEUE_PREPARE_QUERY = 2,
    C43_FAKE_QUEUE_FINISH_START = 3,
    C43_FAKE_QUEUE_FINISH_QUERY = 4,
    C43_FAKE_QUEUE_CANCEL = 5,
    C43_FAKE_QUEUE_RETIRE = 6,
    C43_FAKE_QUEUE_RESET_BEGIN = 7,
    C43_FAKE_QUEUE_QUIESCENT = 8,
    C43_FAKE_TARGET_SUBMIT = 9,
    C43_FAKE_TARGET_QUERY = 10,
    C43_FAKE_TARGET_CANCEL = 11,
    C43_FAKE_TARGET_RELEASE = 12,
    C43_FAKE_TARGET_RELEASE_QUERY = 13,
    C43_FAKE_TARGET_RESET_BEGIN = 14,
    C43_FAKE_TARGET_QUIESCENT = 15,
    C43_FAKE_BLOCK_SUBMIT = 16,
    C43_FAKE_BLOCK_QUERY = 17,
    C43_FAKE_BLOCK_CANCEL = 18,
    C43_FAKE_BLOCK_RETIRE = 19,
    C43_FAKE_BLOCK_RESET_BEGIN = 20,
    C43_FAKE_BLOCK_QUIESCENT = 21
};

union c43_fake_event_input {
    struct fwlab_c43_queue_effect_request queue_prepare;
    struct fwlab_c43_queue_finish_request queue_finish;
    struct fwlab_c43_target_request target;
    struct fwlab_c43_block_action_request block;
    struct fwlab_hif_action_token token;
    uint32_t epoch;
};

union c43_fake_event_output {
    struct fwlab_hif_action_submit_result submit;
    struct fwlab_c43_queue_effect_terminal queue_terminal;
    struct fwlab_c43_target_terminal target_terminal;
    struct fwlab_c43_block_action_terminal block_terminal;
};

struct c43_fake_event_record {
    uint32_t sequence;
    uint32_t kind;
    uint32_t returned;
    uint8_t ready_written;
    uint8_t ready_value;
    uint8_t output_written;
    uint8_t reserved0;
    union c43_fake_event_input input;
    union c43_fake_event_output output;
};

struct c43_fake_queue_script {
    uint8_t enabled;
    uint8_t fault;
    uint8_t prepare_accepted;
    uint8_t finish_accepted;
    uint8_t corrupt_prepare_submit_token;
    uint8_t corrupt_finish_submit_token;
    uint8_t reserved_flags[2];
    uint32_t prepare_backpressure_remaining;
    uint32_t prepare_not_ready_remaining;
    uint32_t finish_backpressure_remaining;
    uint32_t finish_not_ready_remaining;
    uint32_t retire_in_progress_remaining;
    uint32_t finish_state;
    uint32_t prepare_start_calls;
    uint32_t prepare_query_calls;
    uint32_t finish_start_calls;
    uint32_t finish_query_calls;
    uint32_t retire_calls;
    struct fwlab_c43_queue_facts facts;
    struct fwlab_c43_queue_effect_request first_prepare_request;
    struct fwlab_c43_queue_effect_request last_prepare_request;
    struct fwlab_c43_queue_finish_request first_finish_request;
    struct fwlab_c43_queue_finish_request last_finish_request;
    struct fwlab_c43_queue_effect_terminal prepared_terminal;
    struct fwlab_c43_queue_effect_terminal finish_terminal;
};

struct c43_fake_services {
    uint32_t event_count;
    uint8_t overflow;
    uint8_t reserved[3];
    struct c43_fake_event_record events[C43_FAKE_EVENT_CAPACITY];
    struct c43_fake_queue_script queue_script;
};

void c43_fake_services_init(struct c43_fake_services *services);
void c43_fake_services_providers(
    struct c43_fake_services *services,
    struct fwlab_c43_graph_providers *providers
);
void c43_fake_queue_script_configure(
    struct c43_fake_services *services,
    const struct fwlab_c43_queue_facts *facts,
    uint32_t finish_state,
    uint32_t prepare_backpressure,
    uint32_t prepare_not_ready,
    uint32_t finish_backpressure,
    uint32_t finish_not_ready
);

#endif /* FWLAB_C43_FAKE_SERVICES_H */
