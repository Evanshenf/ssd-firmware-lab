/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_SPINE_FAKE_ADJACENT_H
#define FWLAB_SPINE_FAKE_ADJACENT_H

#include <stddef.h>
#include <stdint.h>

#include "spine_internal.h"

#define FWLAB_SPINE_FAKE_V0_MAX_ACTIONS 256u

struct fwlab_spine_fake_behavior_v0 {
    uint32_t backpressure_count;
    uint32_t terminal_delay;
    uint32_t retire_start_delay;
    uint32_t retire_delay;
    uint32_t cancel_delay;
    uint32_t terminal_kind;
    uint32_t effect;
    uint32_t units_completed;
    uint32_t fault_domain;
    uint32_t fault_code;
    uint32_t normalized_outcome;
    uint32_t result_dword0;
    uint8_t lose_submit_response;
    uint8_t wait_for_epoch_close;
    uint8_t abort_candidate_present;
    uint8_t reserved0[5];
    uint64_t abort_uid;
    uint32_t abort_report;
    uint8_t target_present;
    uint8_t reserved1[3];
    struct fwlab_spine_command_ticket_v0 target;
};

struct fwlab_spine_fake_v0;

struct fwlab_spine_fake_lane_v0 {
    struct fwlab_spine_fake_v0 *owner;
    uint16_t kind;
    uint16_t reserved0;
    uint32_t close_in_progress;
    uint32_t close_calls;
    uint32_t quiescent_calls;
    uint8_t close_acked;
    uint8_t reserved1[7];
};

struct fwlab_spine_fake_action_v0 {
    struct fwlab_host_action_token_v0 token;
    struct fwlab_host_action_argument_ref_v0 argument;
    struct fwlab_spine_fake_behavior_v0 behavior;
    struct fwlab_spine_profile_argument_v0 argument_value;
    uint8_t payload[4096];
    uint32_t payload_bytes;
    uint32_t query_count;
    uint32_t retire_query_count;
    uint32_t retire_start_calls;
    uint32_t cancel_calls;
    uint8_t occupied;
    uint8_t cancelled;
    uint8_t retire_started;
    uint8_t drained;
    uint8_t result_latched;
    uint8_t reserved0[3];
};

struct fwlab_spine_fake_expected_v0 {
    struct fwlab_nvme_command_handle command;
    struct fwlab_nvme_origin_token origin;
    struct fwlab_spine_profile_binding_v0 binding;
    struct fwlab_host_action_program_v0 program;
    struct fwlab_host_action_argument_ref_v0
        argument[FWLAB_HOST_ACTION_V0_MAX_ACTIONS];
    struct fwlab_spine_profile_argument_v0
        argument_value[FWLAB_HOST_ACTION_V0_MAX_ACTIONS];
    uint16_t action_count;
    uint8_t occupied;
    uint8_t reserved0;
};

struct fwlab_spine_fake_mailbox_v0 {
    struct fwlab_spine_abort_candidate_v0 candidate;
    uint8_t occupied;
    uint8_t poisoned;
    uint8_t reserved0[6];
};

struct fwlab_spine_fake_v0 {
    struct fwlab_spine_fake_lane_v0
        lane[FWLAB_HOST_ACTION_V0_KIND_COUNT];
    struct fwlab_spine_fake_action_v0
        action[FWLAB_SPINE_FAKE_V0_MAX_ACTIONS];
    struct fwlab_spine_fake_behavior_v0
        next[FWLAB_HOST_ACTION_V0_KIND_COUNT];
    struct fwlab_host_action_token_v0
        observed[FWLAB_SPINE_FAKE_V0_MAX_ACTIONS];
    struct fwlab_spine_fake_expected_v0
        expected[FWLAB_SPINE_FAKE_V0_MAX_ACTIONS];
    struct fwlab_spine_fake_mailbox_v0
        mailbox[FWLAB_SPINE_LIFECYCLE_V0_MAX_ABORTS];
    uint32_t observed_count;
    uint32_t expected_count;
    uint32_t active_actions;
};

void fwlab_spine_fake_v0_init(
    struct fwlab_spine_fake_v0 *fake,
    struct fwlab_host_action_driver_table_v0 *drivers
);
void fwlab_spine_fake_v0_set_next(
    struct fwlab_spine_fake_v0 *fake,
    uint16_t kind,
    const struct fwlab_spine_fake_behavior_v0 *behavior
);
void fwlab_spine_fake_v0_set_close_delay(
    struct fwlab_spine_fake_v0 *fake,
    uint16_t kind,
    uint32_t attempts
);
enum fwlab_spine_result_v0 fwlab_spine_fake_v0_expect_program(
    struct fwlab_spine_fake_v0 *fake,
    const struct fwlab_spine_profile_binding_v0 *binding,
    const struct fwlab_host_action_program_v0 *program
);
enum fwlab_spine_result_v0 fwlab_spine_fake_v0_attach_relation_source(
    struct fwlab_spine_fake_v0 *fake,
    struct fwlab_spine_profile_binding_v0 *binding
);
enum fwlab_spine_result_v0 fwlab_spine_fake_v0_abort_candidate_append(
    struct fwlab_spine_fake_v0 *fake,
    const struct fwlab_spine_abort_candidate_v0 *candidate
);
uint32_t fwlab_spine_fake_v0_observed_count(
    const struct fwlab_spine_fake_v0 *fake
);
const struct fwlab_host_action_token_v0 *fwlab_spine_fake_v0_observed(
    const struct fwlab_spine_fake_v0 *fake,
    uint32_t index
);

size_t fwlab_c43_p1_adapter_v0_arena_size(void);
size_t fwlab_c43_p1_adapter_v0_arena_alignment(void);
enum fwlab_spine_result_v0 fwlab_c43_p1_adapter_v0_init(
    void *arena,
    size_t arena_size,
    uint64_t instance_nonce,
    uint32_t generation,
    struct fwlab_host_profile_adapter_v0 *adapter
);
enum fwlab_spine_result_v0 fwlab_c43_p1_binding_v0(
    const struct fwlab_host_profile_adapter_v0 *adapter,
    uint32_t role,
    struct fwlab_spine_profile_binding_v0 *binding
);
void fwlab_c43_p1_retire_delay_v0(
    const struct fwlab_host_profile_adapter_v0 *adapter,
    uint32_t attempts
);

size_t fwlab_linux_profile_v1_adapter_arena_size(void);
size_t fwlab_linux_profile_v1_adapter_arena_alignment(void);
enum fwlab_spine_result_v0 fwlab_linux_profile_v1_adapter_init(
    void *arena,
    size_t arena_size,
    uint64_t instance_nonce,
    uint32_t generation,
    struct fwlab_host_profile_adapter_v0 *adapter
);
enum fwlab_spine_result_v0 fwlab_linux_profile_v1_binding_v0(
    const struct fwlab_host_profile_adapter_v0 *adapter,
    uint32_t role,
    struct fwlab_spine_profile_binding_v0 *binding
);
void fwlab_linux_profile_v1_retire_delay_v0(
    const struct fwlab_host_profile_adapter_v0 *adapter,
    uint32_t attempts
);

size_t fwlab_tiny_profile_v0_arena_size(void);
size_t fwlab_tiny_profile_v0_arena_alignment(void);
enum fwlab_spine_result_v0 fwlab_tiny_profile_v0_init(
    void *arena,
    size_t arena_size,
    uint64_t instance_nonce,
    uint32_t generation,
    struct fwlab_host_profile_adapter_v0 *adapter
);
enum fwlab_spine_result_v0 fwlab_tiny_profile_v0_binding(
    const struct fwlab_host_profile_adapter_v0 *adapter,
    struct fwlab_spine_profile_binding_v0 *binding
);

#endif /* FWLAB_SPINE_FAKE_ADJACENT_H */
