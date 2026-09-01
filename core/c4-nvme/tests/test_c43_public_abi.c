/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fwlab/portable/c4_command_graph.h"
#include "fwlab/portable/nvme_codec.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ABI_SIZE(type, expected)                                                \
    _Static_assert(sizeof(type) == (expected), #type " size changed")
#define ABI_FIELD(type, member, expected)                                       \
    _Static_assert(offsetof(type, member) == (expected),                        \
                   #type "." #member " offset changed")

_Static_assert(FWLAB_C43_CREDIT_POLICY_SCRATCH == (1u << 0),
               "policy credit bit changed");
_Static_assert(FWLAB_C43_CREDIT_INTENT == (1u << 1),
               "intent credit bit changed");
_Static_assert(FWLAB_C43_CREDIT_READY == (1u << 2),
               "ready credit bit changed");
_Static_assert(FWLAB_C43_CREDIT_LEASE == (1u << 3),
               "lease credit bit changed");
_Static_assert(FWLAB_C43_CREDIT_CONSUME == (1u << 4),
               "consume credit bit changed");
_Static_assert(FWLAB_C43_CREDIT_FINALIZER == (1u << 5),
               "finalizer credit bit changed");
_Static_assert(FWLAB_C43_CREDIT_ABORT == (1u << 6),
               "abort credit bit changed");
_Static_assert(FWLAB_C43_CREDIT_TARGET == (1u << 7),
               "target credit bit changed");
_Static_assert(FWLAB_C43_CREDIT_QUEUE_TRANSACTION == (1u << 8),
               "queue-transaction credit bit changed");
_Static_assert(FWLAB_C43_CREDIT_BLOCK_INTENT == (1u << 9),
               "block-intent credit bit changed");
_Static_assert(FWLAB_C43_CREDIT_ALL == 0x03ffu,
               "credit mask changed");

ABI_SIZE(struct fwlab_c43_policy_request, 128);
ABI_FIELD(struct fwlab_c43_policy_request, version, 0);
ABI_FIELD(struct fwlab_c43_policy_request, size, 2);
ABI_FIELD(struct fwlab_c43_policy_request, reserved0, 4);
ABI_FIELD(struct fwlab_c43_policy_request, handle, 8);
ABI_FIELD(struct fwlab_c43_policy_request, origin, 32);
ABI_FIELD(struct fwlab_c43_policy_request, transaction_uid, 48);
ABI_FIELD(struct fwlab_c43_policy_request, slba, 56);
ABI_FIELD(struct fwlab_c43_policy_request, namespace_id, 64);
ABI_FIELD(struct fwlab_c43_policy_request, zero_based_nlb, 68);
ABI_FIELD(struct fwlab_c43_policy_request, feature_selector, 72);
ABI_FIELD(struct fwlab_c43_policy_request, requested_cq_count, 76);
ABI_FIELD(struct fwlab_c43_policy_request, requested_sq_count, 80);
ABI_FIELD(struct fwlab_c43_policy_request, queue_entries, 84);
ABI_FIELD(struct fwlab_c43_policy_request, transport_fault, 88);
ABI_FIELD(struct fwlab_c43_policy_request, kind, 92);
ABI_FIELD(struct fwlab_c43_policy_request, queue_class, 93);
ABI_FIELD(struct fwlab_c43_policy_request, fuse, 94);
ABI_FIELD(struct fwlab_c43_policy_request, pointer_format, 95);
ABI_FIELD(struct fwlab_c43_policy_request, data_present, 96);
ABI_FIELD(struct fwlab_c43_policy_request, metadata_present, 97);
ABI_FIELD(struct fwlab_c43_policy_request, fua, 98);
ABI_FIELD(struct fwlab_c43_policy_request, save, 99);
ABI_FIELD(struct fwlab_c43_policy_request, reserved_command_flags, 100);
ABI_FIELD(struct fwlab_c43_policy_request, reserved1, 104);
ABI_FIELD(struct fwlab_c43_policy_request, reserved2, 124);

ABI_SIZE(struct fwlab_c43_transfer_shape, 48);
ABI_FIELD(struct fwlab_c43_transfer_shape, direction, 8);
ABI_FIELD(struct fwlab_c43_transfer_shape, data_bytes, 12);
ABI_FIELD(struct fwlab_c43_transfer_shape, metadata_bytes, 16);
ABI_FIELD(struct fwlab_c43_transfer_shape, lba_bytes, 20);
ABI_FIELD(struct fwlab_c43_transfer_shape, lba_count, 24);
ABI_FIELD(struct fwlab_c43_transfer_shape, data_pointer_required, 28);
ABI_FIELD(struct fwlab_c43_transfer_shape, metadata_pointer_required, 29);
ABI_FIELD(struct fwlab_c43_transfer_shape, reserved1, 30);
ABI_FIELD(struct fwlab_c43_transfer_shape, reserved2, 32);

ABI_SIZE(struct fwlab_c43_identify_recipe, 48);
ABI_FIELD(struct fwlab_c43_identify_recipe, kind, 8);
ABI_FIELD(struct fwlab_c43_identify_recipe, namespace_id, 12);
ABI_FIELD(struct fwlab_c43_identify_recipe, payload_bytes, 16);
ABI_FIELD(struct fwlab_c43_identify_recipe, identity_version, 20);
ABI_FIELD(struct fwlab_c43_identify_recipe, reserved1, 24);

ABI_SIZE(struct fwlab_c43_block_intent, 104);
ABI_FIELD(struct fwlab_c43_block_intent, command, 8);
ABI_FIELD(struct fwlab_c43_block_intent, origin, 32);
ABI_FIELD(struct fwlab_c43_block_intent, frontier, 48);
ABI_FIELD(struct fwlab_c43_block_intent, slba, 64);
ABI_FIELD(struct fwlab_c43_block_intent, namespace_id, 72);
ABI_FIELD(struct fwlab_c43_block_intent, operation, 76);
ABI_FIELD(struct fwlab_c43_block_intent, durability, 80);
ABI_FIELD(struct fwlab_c43_block_intent, lba_count, 84);
ABI_FIELD(struct fwlab_c43_block_intent, data_bytes, 88);
ABI_FIELD(struct fwlab_c43_block_intent, reserved1, 92);

ABI_SIZE(struct fwlab_c43_policy_plan, 304);
ABI_FIELD(struct fwlab_c43_policy_plan, command, 8);
ABI_FIELD(struct fwlab_c43_policy_plan, origin, 32);
ABI_FIELD(struct fwlab_c43_policy_plan, transaction_uid, 48);
ABI_FIELD(struct fwlab_c43_policy_plan, kind, 56);
ABI_FIELD(struct fwlab_c43_policy_plan, semantic_status, 60);
ABI_FIELD(struct fwlab_c43_policy_plan, result_dword0, 64);
ABI_FIELD(struct fwlab_c43_policy_plan, actual_length, 68);
ABI_FIELD(struct fwlab_c43_policy_plan, required_witness_mask, 72);
ABI_FIELD(struct fwlab_c43_policy_plan, satisfied_witness_mask, 76);
ABI_FIELD(struct fwlab_c43_policy_plan, dnr, 80);
ABI_FIELD(struct fwlab_c43_policy_plan, more, 81);
ABI_FIELD(struct fwlab_c43_policy_plan, crd, 82);
ABI_FIELD(struct fwlab_c43_policy_plan, effect_class, 83);
ABI_FIELD(struct fwlab_c43_policy_plan, shape, 84);
ABI_FIELD(struct fwlab_c43_policy_plan, identify, 132);
ABI_FIELD(struct fwlab_c43_policy_plan, reserved_branch_padding, 180);
ABI_FIELD(struct fwlab_c43_policy_plan, block, 184);
ABI_FIELD(struct fwlab_c43_policy_plan, reserved1, 288);

ABI_SIZE(struct fwlab_c43_completion_witness, 104);
ABI_FIELD(struct fwlab_c43_completion_witness, command, 8);
ABI_FIELD(struct fwlab_c43_completion_witness, origin, 32);
ABI_FIELD(struct fwlab_c43_completion_witness, predecessor, 48);
ABI_FIELD(struct fwlab_c43_completion_witness, provider_generation, 64);
ABI_FIELD(struct fwlab_c43_completion_witness, witness_mask, 72);
ABI_FIELD(struct fwlab_c43_completion_witness, units_completed, 76);
ABI_FIELD(struct fwlab_c43_completion_witness, effect_class, 80);
ABI_FIELD(struct fwlab_c43_completion_witness, terminal_kind, 81);
ABI_FIELD(struct fwlab_c43_completion_witness, reserved1, 82);
ABI_FIELD(struct fwlab_c43_completion_witness, reserved2, 84);
ABI_FIELD(struct fwlab_c43_completion_witness, reserved3, 100);

ABI_SIZE(struct fwlab_c43_graph_config, 232);
ABI_FIELD(struct fwlab_c43_graph_config, version, 0);
ABI_FIELD(struct fwlab_c43_graph_config, size, 2);
ABI_FIELD(struct fwlab_c43_graph_config, reserved_header, 4);
ABI_FIELD(struct fwlab_c43_graph_config, profile, 8);
ABI_FIELD(struct fwlab_c43_graph_config, command_capacity, 72);
ABI_FIELD(struct fwlab_c43_graph_config, actions_per_command, 74);
ABI_FIELD(struct fwlab_c43_graph_config, queue_mailbox_capacity, 76);
ABI_FIELD(struct fwlab_c43_graph_config, target_mailbox_capacity, 78);
ABI_FIELD(struct fwlab_c43_graph_config, block_mailbox_capacity, 80);
ABI_FIELD(struct fwlab_c43_graph_config, dma_mailbox_capacity, 82);
ABI_FIELD(struct fwlab_c43_graph_config, service_gap_maximum, 84);
ABI_FIELD(struct fwlab_c43_graph_config, ordinary_progress_maximum, 88);
ABI_FIELD(struct fwlab_c43_graph_config, control_progress_maximum, 92);
ABI_FIELD(struct fwlab_c43_graph_config, safety_generation, 96);
ABI_FIELD(struct fwlab_c43_graph_config, reserved_alignment, 100);
ABI_FIELD(struct fwlab_c43_graph_config, instance_nonce, 104);
ABI_FIELD(struct fwlab_c43_graph_config, controller_epoch, 112);
ABI_FIELD(struct fwlab_c43_graph_config, reserved0, 116);
ABI_FIELD(struct fwlab_c43_graph_config, command_uid, 120);
ABI_FIELD(struct fwlab_c43_graph_config, action_uid, 136);
ABI_FIELD(struct fwlab_c43_graph_config, transaction_uid, 152);
ABI_FIELD(struct fwlab_c43_graph_config, lease_uid, 168);
ABI_FIELD(struct fwlab_c43_graph_config, consume_uid, 184);
ABI_FIELD(struct fwlab_c43_graph_config, finalizer_uid, 200);
ABI_FIELD(struct fwlab_c43_graph_config, reserved1, 216);

ABI_SIZE(struct fwlab_c43_graph_providers, 112);
ABI_FIELD(struct fwlab_c43_graph_providers, version, 0);
ABI_FIELD(struct fwlab_c43_graph_providers, size, 2);
ABI_FIELD(struct fwlab_c43_graph_providers, reserved0, 4);
ABI_FIELD(struct fwlab_c43_graph_providers, queue, 8);
ABI_FIELD(struct fwlab_c43_graph_providers, target, 32);
ABI_FIELD(struct fwlab_c43_graph_providers, block, 56);
ABI_FIELD(struct fwlab_c43_graph_providers, dma_generation, 88);
ABI_FIELD(struct fwlab_c43_graph_providers, dma_bound, 96);
ABI_FIELD(struct fwlab_c43_graph_providers, reserved, 100);

ABI_SIZE(struct fwlab_c43_command_observer, 96);
ABI_FIELD(struct fwlab_c43_command_observer, handle, 0);
ABI_FIELD(struct fwlab_c43_command_observer, origin, 24);
ABI_FIELD(struct fwlab_c43_command_observer, transaction_uid, 40);
ABI_FIELD(struct fwlab_c43_command_observer, phase, 48);
ABI_FIELD(struct fwlab_c43_command_observer, terminal_winner, 52);
ABI_FIELD(struct fwlab_c43_command_observer, publication, 56);
ABI_FIELD(struct fwlab_c43_command_observer, required_witness_mask, 60);
ABI_FIELD(struct fwlab_c43_command_observer, satisfied_witness_mask, 64);
ABI_FIELD(struct fwlab_c43_command_observer, action_count, 68);
ABI_FIELD(struct fwlab_c43_command_observer, in_use, 70);
ABI_FIELD(struct fwlab_c43_command_observer, success_eligible, 71);
ABI_FIELD(struct fwlab_c43_command_observer, provider_generation_current, 72);
ABI_FIELD(struct fwlab_c43_command_observer, reserved0, 73);
ABI_FIELD(struct fwlab_c43_command_observer, reservation_credit_mask, 76);
ABI_FIELD(struct fwlab_c43_command_observer, first_action_uid, 80);
ABI_FIELD(struct fwlab_c43_command_observer, action_generation, 88);
ABI_FIELD(struct fwlab_c43_command_observer, reserved2, 92);

ABI_SIZE(struct fwlab_c43_graph_observer, 488);
ABI_FIELD(struct fwlab_c43_graph_observer, controller_epoch, 4);
ABI_FIELD(struct fwlab_c43_graph_observer, instance_nonce, 8);
ABI_FIELD(struct fwlab_c43_graph_observer, active_commands, 16);
ABI_FIELD(struct fwlab_c43_graph_observer, active_actions, 20);
ABI_FIELD(struct fwlab_c43_graph_observer, ready_count, 24);
ABI_FIELD(struct fwlab_c43_graph_observer, cleanup_count, 28);
ABI_FIELD(struct fwlab_c43_graph_observer, provider_generation, 32);
ABI_FIELD(struct fwlab_c43_graph_observer, admission_closed, 64);
ABI_FIELD(struct fwlab_c43_graph_observer, resetting, 65);
ABI_FIELD(struct fwlab_c43_graph_observer, tearing_down, 66);
ABI_FIELD(struct fwlab_c43_graph_observer, dead, 67);
ABI_FIELD(struct fwlab_c43_graph_observer, reserved_intent_credits, 68);
ABI_FIELD(struct fwlab_c43_graph_observer, commands, 72);
ABI_FIELD(struct fwlab_c43_graph_observer, reserved_ready_credits, 456);
ABI_FIELD(struct fwlab_c43_graph_observer, reserved_lease_credits, 460);
ABI_FIELD(struct fwlab_c43_graph_observer, reserved_consume_credits, 464);
ABI_FIELD(struct fwlab_c43_graph_observer, reserved_finalizer_credits, 468);
ABI_FIELD(struct fwlab_c43_graph_observer, reserved_abort_credits, 472);
ABI_FIELD(struct fwlab_c43_graph_observer, reserved_target_credits, 476);
ABI_FIELD(struct fwlab_c43_graph_observer,
          reserved_queue_transaction_credits, 480);
ABI_FIELD(struct fwlab_c43_graph_observer, reserved_block_intent_credits, 484);

ABI_SIZE(struct fwlab_c43_step_result, 40);
ABI_FIELD(struct fwlab_c43_step_result, requested_budget, 8);
ABI_FIELD(struct fwlab_c43_step_result, units_executed, 12);
ABI_FIELD(struct fwlab_c43_step_result, transitions, 16);
ABI_FIELD(struct fwlab_c43_step_result, ready_events, 20);
ABI_FIELD(struct fwlab_c43_step_result, service_gap_maximum, 24);
ABI_FIELD(struct fwlab_c43_step_result, reserved, 28);

ABI_SIZE(struct fwlab_c43_queue_facts, 96);
ABI_FIELD(struct fwlab_c43_queue_facts, transaction, 8);
ABI_FIELD(struct fwlab_c43_queue_facts, queue, 24);
ABI_FIELD(struct fwlab_c43_queue_facts, associated_cq, 40);
ABI_FIELD(struct fwlab_c43_queue_facts, operation, 56);
ABI_FIELD(struct fwlab_c43_queue_facts, role, 60);
ABI_FIELD(struct fwlab_c43_queue_facts, queue_entries, 64);
ABI_FIELD(struct fwlab_c43_queue_facts, address_present, 68);
ABI_FIELD(struct fwlab_c43_queue_facts, current_relation, 76);
ABI_FIELD(struct fwlab_c43_queue_facts, reserved1, 77);
ABI_FIELD(struct fwlab_c43_queue_facts, reserved2, 80);

ABI_SIZE(struct fwlab_c43_queue_effect_request, 128);
ABI_FIELD(struct fwlab_c43_queue_effect_request, version, 0);
ABI_FIELD(struct fwlab_c43_queue_effect_request, size, 2);
ABI_FIELD(struct fwlab_c43_queue_effect_request, reserved0, 4);
ABI_FIELD(struct fwlab_c43_queue_effect_request, common, 8);
ABI_FIELD(struct fwlab_c43_queue_effect_request, operation, 104);
ABI_FIELD(struct fwlab_c43_queue_effect_request, role, 108);
ABI_FIELD(struct fwlab_c43_queue_effect_request, reserved, 112);
ABI_SIZE(struct fwlab_c43_queue_finish_request, 104);
ABI_FIELD(struct fwlab_c43_queue_finish_request, version, 0);
ABI_FIELD(struct fwlab_c43_queue_finish_request, size, 2);
ABI_FIELD(struct fwlab_c43_queue_finish_request, reserved0, 4);
ABI_FIELD(struct fwlab_c43_queue_finish_request, token, 8);
ABI_FIELD(struct fwlab_c43_queue_finish_request, transaction, 64);
ABI_FIELD(struct fwlab_c43_queue_finish_request, decision, 80);
ABI_FIELD(struct fwlab_c43_queue_finish_request, reserved1, 84);
ABI_SIZE(struct fwlab_c43_queue_effect_terminal, 224);
ABI_FIELD(struct fwlab_c43_queue_effect_terminal, version, 0);
ABI_FIELD(struct fwlab_c43_queue_effect_terminal, size, 2);
ABI_FIELD(struct fwlab_c43_queue_effect_terminal, reserved0, 4);
ABI_FIELD(struct fwlab_c43_queue_effect_terminal, common, 8);
ABI_FIELD(struct fwlab_c43_queue_effect_terminal, facts, 104);
ABI_FIELD(struct fwlab_c43_queue_effect_terminal, state, 200);
ABI_FIELD(struct fwlab_c43_queue_effect_terminal, decision, 204);
ABI_FIELD(struct fwlab_c43_queue_effect_terminal, reserved, 208);
ABI_SIZE(struct fwlab_c43_queue_effect_port_ops, 88);
ABI_FIELD(struct fwlab_c43_queue_effect_port_ops, prepare_start, 8);
ABI_FIELD(struct fwlab_c43_queue_effect_port_ops, prepare_query, 16);
ABI_FIELD(struct fwlab_c43_queue_effect_port_ops, finish_start, 24);
ABI_FIELD(struct fwlab_c43_queue_effect_port_ops, finish_query, 32);
ABI_FIELD(struct fwlab_c43_queue_effect_port_ops, cancel, 40);
ABI_FIELD(struct fwlab_c43_queue_effect_port_ops, retire, 48);
ABI_FIELD(struct fwlab_c43_queue_effect_port_ops, reset_begin, 56);
ABI_FIELD(struct fwlab_c43_queue_effect_port_ops, quiescent, 64);
ABI_FIELD(struct fwlab_c43_queue_effect_port_ops, reserved1, 72);
ABI_SIZE(struct fwlab_c43_queue_effect_port, 24);
ABI_FIELD(struct fwlab_c43_queue_effect_port, ops, 0);
ABI_FIELD(struct fwlab_c43_queue_effect_port, context, 8);
ABI_FIELD(struct fwlab_c43_queue_effect_port, generation, 16);

ABI_SIZE(struct fwlab_c43_target_request, 176);
ABI_FIELD(struct fwlab_c43_target_request, version, 0);
ABI_FIELD(struct fwlab_c43_target_request, size, 2);
ABI_FIELD(struct fwlab_c43_target_request, reserved0, 4);
ABI_FIELD(struct fwlab_c43_target_request, common, 8);
ABI_FIELD(struct fwlab_c43_target_request, abort_command, 104);
ABI_FIELD(struct fwlab_c43_target_request, operation, 160);
ABI_FIELD(struct fwlab_c43_target_request, reserved, 164);
ABI_SIZE(struct fwlab_c43_target_terminal, 208);
ABI_FIELD(struct fwlab_c43_target_terminal, version, 0);
ABI_FIELD(struct fwlab_c43_target_terminal, size, 2);
ABI_FIELD(struct fwlab_c43_target_terminal, reserved0, 4);
ABI_FIELD(struct fwlab_c43_target_terminal, common, 8);
ABI_FIELD(struct fwlab_c43_target_terminal, target, 104);
ABI_FIELD(struct fwlab_c43_target_terminal, reference, 160);
ABI_FIELD(struct fwlab_c43_target_terminal, outcome, 176);
ABI_FIELD(struct fwlab_c43_target_terminal, reserved, 180);
ABI_SIZE(struct fwlab_c43_target_resolver_port_ops, 80);
ABI_FIELD(struct fwlab_c43_target_resolver_port_ops, submit, 8);
ABI_FIELD(struct fwlab_c43_target_resolver_port_ops, query, 16);
ABI_FIELD(struct fwlab_c43_target_resolver_port_ops, cancel, 24);
ABI_FIELD(struct fwlab_c43_target_resolver_port_ops, release, 32);
ABI_FIELD(struct fwlab_c43_target_resolver_port_ops, release_query, 40);
ABI_FIELD(struct fwlab_c43_target_resolver_port_ops, reset_begin, 48);
ABI_FIELD(struct fwlab_c43_target_resolver_port_ops, quiescent, 56);
ABI_FIELD(struct fwlab_c43_target_resolver_port_ops, reserved1, 64);
ABI_SIZE(struct fwlab_c43_target_resolver_port, 24);
ABI_FIELD(struct fwlab_c43_target_resolver_port, ops, 0);
ABI_FIELD(struct fwlab_c43_target_resolver_port, context, 8);
ABI_FIELD(struct fwlab_c43_target_resolver_port, generation, 16);

ABI_SIZE(struct fwlab_c43_block_action_request, 248);
ABI_FIELD(struct fwlab_c43_block_action_request, version, 0);
ABI_FIELD(struct fwlab_c43_block_action_request, size, 2);
ABI_FIELD(struct fwlab_c43_block_action_request, reserved0, 4);
ABI_FIELD(struct fwlab_c43_block_action_request, common, 8);
ABI_FIELD(struct fwlab_c43_block_action_request, intent, 104);
ABI_FIELD(struct fwlab_c43_block_action_request, predecessor, 208);
ABI_FIELD(struct fwlab_c43_block_action_request, requested_witness_mask, 224);
ABI_FIELD(struct fwlab_c43_block_action_request, reserved, 228);
ABI_SIZE(struct fwlab_c43_block_action_terminal, 232);
ABI_FIELD(struct fwlab_c43_block_action_terminal, version, 0);
ABI_FIELD(struct fwlab_c43_block_action_terminal, size, 2);
ABI_FIELD(struct fwlab_c43_block_action_terminal, reserved0, 4);
ABI_FIELD(struct fwlab_c43_block_action_terminal, common, 8);
ABI_FIELD(struct fwlab_c43_block_action_terminal, witness, 104);
ABI_FIELD(struct fwlab_c43_block_action_terminal, block_terminal_kind, 208);
ABI_FIELD(struct fwlab_c43_block_action_terminal, reserved, 212);
ABI_SIZE(struct fwlab_c43_block_action_port_ops, 72);
ABI_FIELD(struct fwlab_c43_block_action_port_ops, submit, 8);
ABI_FIELD(struct fwlab_c43_block_action_port_ops, query, 16);
ABI_FIELD(struct fwlab_c43_block_action_port_ops, cancel, 24);
ABI_FIELD(struct fwlab_c43_block_action_port_ops, retire, 32);
ABI_FIELD(struct fwlab_c43_block_action_port_ops, reset_begin, 40);
ABI_FIELD(struct fwlab_c43_block_action_port_ops, quiescent, 48);
ABI_FIELD(struct fwlab_c43_block_action_port_ops, reserved1, 56);
ABI_SIZE(struct fwlab_c43_block_action_port, 32);
ABI_FIELD(struct fwlab_c43_block_action_port, ops, 0);
ABI_FIELD(struct fwlab_c43_block_action_port, context, 8);
ABI_FIELD(struct fwlab_c43_block_action_port, generation, 16);
ABI_FIELD(struct fwlab_c43_block_action_port, capability_bits, 24);
ABI_FIELD(struct fwlab_c43_block_action_port, reserved, 28);

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "C4.3 ABI check failed at line %d\n", __LINE__);   \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static struct fwlab_nvme_command_handle handle(uint64_t uid)
{
    const struct fwlab_nvme_command_handle value = {
        UINT64_C(0xc430000000000001), uid, 1, 1
    };

    return value;
}

static struct fwlab_nvme_origin_token origin(uint64_t uid)
{
    const struct fwlab_nvme_origin_token value = {
        {UINT64_C(0x1111222233330000) + uid,
         UINT64_C(0x5555666677770000) + uid}
    };

    return value;
}

static struct fwlab_hif_action_token action_token(uint16_t kind)
{
    struct fwlab_hif_action_token token = {0};

    token.command = handle(1);
    token.origin = origin(1);
    token.action_uid = 1;
    token.generation = 1;
    token.kind = kind;
    return token;
}

static struct fwlab_hif_action_envelope envelope(uint16_t kind)
{
    struct fwlab_hif_action_envelope value = {0};

    value.version = FWLAB_HIF_ACTION_VERSION;
    value.size = sizeof(value);
    value.token = action_token(kind);
    value.cookie = 1;
    value.requested_units = 1;
    return value;
}

static struct fwlab_hif_action_terminal action_terminal(
    const struct fwlab_hif_action_envelope *request)
{
    struct fwlab_hif_action_terminal terminal = {0};

    terminal.version = FWLAB_HIF_ACTION_VERSION;
    terminal.size = sizeof(terminal);
    terminal.token = request->token;
    terminal.cookie = request->cookie;
    terminal.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
    return terminal;
}

static struct fwlab_hif_command_ticket ticket(uint64_t uid)
{
    struct fwlab_hif_command_ticket value = {0};

    value.handle = handle(uid);
    value.origin = origin(uid);
    value.ticket_uid = uid;
    value.generation = 1;
    return value;
}

static struct fwlab_c43_graph_config fixed_config(void)
{
    struct fwlab_c43_graph_config config = {0};
    const struct fwlab_c43_counter_seed seed = {1, 100};

    config.version = FWLAB_C43_GRAPH_VERSION;
    config.size = sizeof(config);
    fwlab_nvme_profile_fixed(&config.profile);
    config.command_capacity = FWLAB_C43_MAX_COMMANDS;
    config.actions_per_command = FWLAB_C43_ACTIONS_PER_COMMAND;
    config.queue_mailbox_capacity = 8;
    config.target_mailbox_capacity = 4;
    config.block_mailbox_capacity = 20;
    config.service_gap_maximum = FWLAB_C43_SERVICE_GAP_MAXIMUM;
    config.ordinary_progress_maximum = FWLAB_C43_PROGRESS_MAXIMUM;
    config.control_progress_maximum = FWLAB_C43_CONTROL_PROGRESS_MAXIMUM;
    config.safety_generation = 1;
    config.instance_nonce = handle(1).instance_nonce;
    config.controller_epoch = 1;
    config.command_uid = seed;
    config.action_uid = seed;
    config.transaction_uid = seed;
    config.lease_uid = seed;
    config.consume_uid = seed;
    config.finalizer_uid = seed;
    return config;
}

static struct fwlab_c43_block_intent read_intent(void)
{
    struct fwlab_c43_block_intent intent = {0};

    intent.version = FWLAB_C43_POLICY_VERSION;
    intent.size = sizeof(intent);
    intent.command = handle(1);
    intent.origin = origin(1);
    intent.namespace_id = 1;
    intent.operation = FWLAB_C43_BLOCK_READ;
    intent.durability = FWLAB_C43_DURABILITY_DEFAULT;
    intent.lba_count = 1;
    intent.data_bytes = 512;
    return intent;
}

static struct fwlab_c43_transfer_shape read_shape(void)
{
    struct fwlab_c43_transfer_shape shape = {0};

    shape.version = FWLAB_C43_POLICY_VERSION;
    shape.size = sizeof(shape);
    shape.direction = FWLAB_C43_TRANSFER_CONTROLLER_TO_HOST;
    shape.data_bytes = 512;
    shape.lba_bytes = 512;
    shape.lba_count = 1;
    shape.data_pointer_required = 1;
    return shape;
}

static int check_policy_and_layout(void)
{
    struct fwlab_c43_policy_request request = {0};
    struct fwlab_c43_transfer_shape shape = read_shape();
    struct fwlab_c43_identify_recipe recipe = {0};
    struct fwlab_c43_block_intent intent = read_intent();
    struct fwlab_c43_policy_plan plan = {0};
    struct fwlab_c43_completion_witness witness = {0};

    request.version = FWLAB_C43_POLICY_VERSION;
    request.size = sizeof(request);
    request.handle = handle(1);
    request.origin = origin(1);
    request.transaction_uid = 1;
    request.namespace_id = 1;
    request.kind = FWLAB_C43_REQUEST_READ;
    request.queue_class = FWLAB_NVME_QUEUE_IO;
    request.pointer_format = FWLAB_NVME_DATA_POINTER_PRP;
    request.data_present = 1;
    CHECK(fwlab_c43_policy_request_valid(&request));
    request.feature_selector = 1;
    CHECK(!fwlab_c43_policy_request_valid(&request));
    request.feature_selector = 0;
    request.requested_cq_count = 1;
    CHECK(!fwlab_c43_policy_request_valid(&request));
    request.requested_cq_count = 0;
    request.reserved_command_flags = 1;
    CHECK(!fwlab_c43_policy_request_valid(&request));
    request.reserved_command_flags = 0;
    request.reserved2 = 1;
    CHECK(!fwlab_c43_policy_request_valid(&request));

    CHECK(fwlab_c43_transfer_shape_valid(&shape));
    shape.metadata_pointer_required = 1;
    CHECK(!fwlab_c43_transfer_shape_valid(&shape));
    shape.metadata_pointer_required = 0;
    CHECK(fwlab_c43_block_intent_valid(&intent));
    intent.namespace_id = 2;
    CHECK(!fwlab_c43_block_intent_valid(&intent));
    intent.namespace_id = 1;

    recipe.version = FWLAB_C43_POLICY_VERSION;
    recipe.size = sizeof(recipe);
    recipe.kind = FWLAB_C43_IDENTIFY_CONTROLLER;
    recipe.payload_bytes = FWLAB_C43_IDENTIFY_BYTES;
    recipe.identity_version = 1;
    CHECK(fwlab_c43_identify_recipe_valid(&recipe));
    recipe.namespace_id = 1;
    CHECK(!fwlab_c43_identify_recipe_valid(&recipe));

    plan.version = FWLAB_C43_POLICY_VERSION;
    plan.size = sizeof(plan);
    plan.command = handle(1);
    plan.origin = origin(1);
    plan.transaction_uid = 1;
    plan.kind = FWLAB_C43_PLAN_BLOCK;
    plan.semantic_status = FWLAB_C43_STATUS_SUCCESS;
    plan.actual_length = 512;
    plan.required_witness_mask = FWLAB_C43_WITNESS_BLOCK_READ_READY |
                                 FWLAB_C43_WITNESS_DMA_OUT_COMPLETE;
    plan.satisfied_witness_mask = FWLAB_C43_WITNESS_VALIDATED_ONLY;
    plan.shape = read_shape();
    plan.block = read_intent();
    CHECK(fwlab_c43_policy_plan_valid(&plan));
    plan.block.command = handle(2);
    CHECK(!fwlab_c43_policy_plan_valid(&plan));
    plan.block.command = handle(1);
    plan.kind = FWLAB_C43_PLAN_IMMEDIATE;
    memset(&plan.shape, 0, sizeof(plan.shape));
    memset(&plan.block, 0, sizeof(plan.block));
    plan.actual_length = 0;
    plan.required_witness_mask = FWLAB_C43_WITNESS_VALIDATED_ONLY;
    plan.satisfied_witness_mask = FWLAB_C43_WITNESS_VALIDATED_ONLY;
    CHECK(!fwlab_c43_policy_plan_valid(&plan));

    witness.version = FWLAB_C43_POLICY_VERSION;
    witness.size = sizeof(witness);
    witness.command = handle(1);
    witness.origin = origin(1);
    witness.provider_generation = 1;
    witness.witness_mask = FWLAB_C43_WITNESS_VALIDATED_ONLY;
    witness.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
    CHECK(fwlab_c43_completion_witness_valid(&witness));
    witness.witness_mask = FWLAB_C43_WITNESS_BLOCK_READ_READY;
    CHECK(!fwlab_c43_completion_witness_valid(&witness));
    witness.predecessor.word[0] = 1;
    witness.effect_class = FWLAB_NVME_EFFECT_FULL;
    witness.units_completed = 1;
    CHECK(fwlab_c43_completion_witness_valid(&witness));
    witness.reserved3 = 1;
    CHECK(!fwlab_c43_completion_witness_valid(&witness));
    return 0;
}

static int check_queue_target_block(void)
{
    struct fwlab_c43_queue_effect_request queue_request = {0};
    struct fwlab_c43_queue_effect_terminal queue_terminal = {0};
    struct fwlab_c43_queue_finish_request finish = {0};
    struct fwlab_c43_target_request target_request = {0};
    struct fwlab_c43_target_terminal target_terminal = {0};
    struct fwlab_c43_block_action_request block_request = {0};
    struct fwlab_c43_block_action_terminal block_terminal = {0};

    queue_request.version = FWLAB_C43_QUEUE_EFFECT_PORT_VERSION;
    queue_request.size = sizeof(queue_request);
    queue_request.common = envelope(FWLAB_HIF_ACTION_QUEUE_EFFECT);
    queue_request.operation = FWLAB_C43_QUEUE_CREATE_CQ;
    queue_request.role = FWLAB_C43_QUEUE_ROLE_IO_CQ;
    CHECK(fwlab_c43_queue_effect_request_valid(&queue_request));

    queue_terminal.version = FWLAB_C43_QUEUE_EFFECT_PORT_VERSION;
    queue_terminal.size = sizeof(queue_terminal);
    queue_terminal.common = action_terminal(&queue_request.common);
    queue_terminal.common.effect_class = FWLAB_NVME_EFFECT_FULL;
    queue_terminal.common.units_completed = 1;
    queue_terminal.facts.version = FWLAB_C43_QUEUE_EFFECT_PORT_VERSION;
    queue_terminal.facts.size = sizeof(queue_terminal.facts);
    queue_terminal.facts.transaction.word[0] = 1;
    queue_terminal.facts.queue.word[0] = 1;
    queue_terminal.facts.operation = queue_request.operation;
    queue_terminal.facts.role = queue_request.role;
    queue_terminal.facts.queue_entries = 4;
    queue_terminal.facts.address_present = 1;
    queue_terminal.facts.queue_exists = 1;
    queue_terminal.state = FWLAB_C43_QUEUE_EFFECT_COMMITTED;
    queue_terminal.decision = FWLAB_C43_QUEUE_FINISH_COMMIT;
    CHECK(fwlab_c43_queue_effect_terminal_matches_request(
        &queue_request, &queue_terminal));
    queue_terminal.common.units_completed = 0;
    CHECK(!fwlab_c43_queue_effect_terminal_valid(&queue_terminal));
    queue_terminal.common.units_completed = 1;
    queue_terminal.decision = FWLAB_C43_QUEUE_FINISH_ABORT;
    CHECK(!fwlab_c43_queue_effect_terminal_valid(&queue_terminal));
    queue_terminal.decision = FWLAB_C43_QUEUE_FINISH_COMMIT;
    queue_terminal.facts.queue.word[0] = 0;
    CHECK(!fwlab_c43_queue_effect_terminal_valid(&queue_terminal));
    queue_terminal.facts.queue.word[0] = 1;

    finish.version = FWLAB_C43_QUEUE_EFFECT_PORT_VERSION;
    finish.size = sizeof(finish);
    finish.token = queue_request.common.token;
    finish.transaction = queue_terminal.facts.transaction;
    finish.decision = FWLAB_C43_QUEUE_FINISH_COMMIT;
    CHECK(fwlab_c43_queue_finish_terminal_matches_request(
        &finish, &queue_terminal));

    target_request.version = FWLAB_C43_TARGET_RESOLVER_PORT_VERSION;
    target_request.size = sizeof(target_request);
    target_request.common = envelope(FWLAB_HIF_ACTION_QUEUE_EFFECT);
    target_request.abort_command = ticket(1);
    target_request.operation = FWLAB_C43_TARGET_RESOLVE_ABORT;
    CHECK(fwlab_c43_target_request_valid(&target_request));
    target_terminal.version = FWLAB_C43_TARGET_RESOLVER_PORT_VERSION;
    target_terminal.size = sizeof(target_terminal);
    target_terminal.common = action_terminal(&target_request.common);
    target_terminal.target = ticket(2);
    target_terminal.reference.word[0] = 1;
    target_terminal.outcome = FWLAB_C43_TARGET_FOUND;
    CHECK(fwlab_c43_target_terminal_matches_request(
        &target_request, &target_terminal));
    ++target_terminal.target.handle.instance_nonce;
    CHECK(!fwlab_c43_target_terminal_valid(&target_terminal));

    block_request.version = FWLAB_C43_BLOCK_ACTION_PORT_VERSION;
    block_request.size = sizeof(block_request);
    block_request.common = envelope(FWLAB_HIF_ACTION_BLOCK);
    block_request.intent = read_intent();
    block_request.requested_witness_mask =
        FWLAB_C43_WITNESS_VALIDATED_ONLY;
    CHECK(fwlab_c43_block_action_request_valid(&block_request));
    block_terminal.version = FWLAB_C43_BLOCK_ACTION_PORT_VERSION;
    block_terminal.size = sizeof(block_terminal);
    block_terminal.common = action_terminal(&block_request.common);
    block_terminal.witness.version = FWLAB_C43_POLICY_VERSION;
    block_terminal.witness.size = sizeof(block_terminal.witness);
    block_terminal.witness.command = handle(1);
    block_terminal.witness.origin = origin(1);
    block_terminal.witness.provider_generation = 1;
    block_terminal.witness.witness_mask =
        FWLAB_C43_WITNESS_VALIDATED_ONLY;
    block_terminal.witness.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
    block_terminal.block_terminal_kind = FWLAB_C43_BLOCK_VALIDATED_ONLY;
    CHECK(fwlab_c43_block_action_terminal_valid(&block_terminal));
    block_terminal.common.units_completed = 1;
    CHECK(!fwlab_c43_block_action_terminal_valid(&block_terminal));
    return 0;
}

static int check_graph_records(void)
{
    struct fwlab_c43_graph_config config = fixed_config();
    struct fwlab_c43_graph_observer observer = {0};

    CHECK(fwlab_c43_graph_config_valid(&config));
    ++config.profile.maximum_transfer_bytes;
    CHECK(!fwlab_c43_graph_config_valid(&config));
    config = fixed_config();
    config.reserved_alignment = 1;
    CHECK(!fwlab_c43_graph_config_valid(&config));

    observer.version = FWLAB_C43_GRAPH_VERSION;
    observer.size = sizeof(observer);
    observer.controller_epoch = 1;
    observer.instance_nonce = handle(1).instance_nonce;
    observer.provider_generation[0] = 1;
    observer.provider_generation[1] = 1;
    observer.provider_generation[2] = 1;
    observer.provider_generation[3] = 1;
    CHECK(fwlab_c43_graph_observer_valid(&observer));
    observer.active_commands = 1;
    CHECK(!fwlab_c43_graph_observer_valid(&observer));
    observer.active_commands = 1;
    observer.active_actions = FWLAB_C43_ACTIONS_PER_COMMAND;
    observer.commands[0].handle = handle(1);
    observer.commands[0].origin = origin(1);
    observer.commands[0].transaction_uid = 1;
    observer.commands[0].phase = FWLAB_C43_PHASE_ADMITTED_POLICY;
    observer.commands[0].action_count = FWLAB_C43_ACTIONS_PER_COMMAND;
    observer.commands[0].in_use = 1;
    observer.commands[0].reservation_credit_mask = FWLAB_C43_CREDIT_ALL;
    observer.commands[0].first_action_uid = 1;
    observer.commands[0].action_generation = 1;
    observer.reserved_intent_credits = 1;
    observer.reserved_ready_credits = 1;
    observer.reserved_lease_credits = 1;
    observer.reserved_consume_credits = 1;
    observer.reserved_finalizer_credits = 1;
    observer.reserved_abort_credits = 1;
    observer.reserved_target_credits = 1;
    observer.reserved_queue_transaction_credits = 1;
    observer.reserved_block_intent_credits = 1;
    observer.commands[0].satisfied_witness_mask =
        FWLAB_C43_WITNESS_VALIDATED_ONLY;
    observer.commands[0].success_eligible = 1;
    CHECK(!fwlab_c43_graph_observer_valid(&observer));
    observer.commands[0].success_eligible = 0;
    CHECK(fwlab_c43_graph_observer_valid(&observer));
    observer.commands[0].satisfied_witness_mask =
        FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED;
    observer.commands[0].required_witness_mask =
        FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED;
    CHECK(!fwlab_c43_graph_observer_valid(&observer));
    observer.commands[0].success_eligible = 1;
    CHECK(fwlab_c43_graph_observer_valid(&observer));
    ++observer.commands[0].handle.instance_nonce;
    CHECK(!fwlab_c43_graph_observer_valid(&observer));
    return 0;
}

int main(void)
{
    CHECK(FWLAB_C43_PHASE_FREE == 0 &&
          FWLAB_C43_PHASE_RETIRED_TOMBSTONE == 13);
    CHECK(FWLAB_C43_MAX_COMMANDS == 4 &&
          FWLAB_C43_ACTIONS_PER_COMMAND == 8 &&
          FWLAB_C43_MAX_ACTIONS == 32);
    CHECK(FWLAB_C43_WITNESS_ALL == 0xffu);
    CHECK(check_policy_and_layout() == 0);
    CHECK(check_queue_target_block() == 0);
    CHECK(check_graph_records() == 0);

    printf("C4.3 phase1 sizes: request=%zu plan=%zu witness=%zu "
           "graph_config=%zu providers=%zu observer=%zu\n",
           sizeof(struct fwlab_c43_policy_request),
           sizeof(struct fwlab_c43_policy_plan),
           sizeof(struct fwlab_c43_completion_witness),
           sizeof(struct fwlab_c43_graph_config),
           sizeof(struct fwlab_c43_graph_providers),
           sizeof(struct fwlab_c43_graph_observer));
    puts("C4.3 phase1 public ABI: PASS");
    return 0;
}
