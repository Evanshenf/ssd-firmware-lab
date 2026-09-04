/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_internal.h"

int fwlab_c43_policy_plan_valid(const struct fwlab_c43_policy_plan *plan)
{
    uint32_t expected_witness;

    if (plan == NULL || plan->version != FWLAB_C43_POLICY_VERSION ||
        plan->size != sizeof(*plan) || plan->reserved0 != 0 ||
        !c43_handle_valid(&plan->command) || !c43_origin_valid(&plan->origin) ||
        plan->transaction_uid == 0 ||
        plan->kind < FWLAB_C43_PLAN_IMMEDIATE ||
        plan->kind > FWLAB_C43_PLAN_BLOCK ||
        !c43_semantic_status_valid(plan->semantic_status) ||
        !c43_witness_mask_valid(plan->required_witness_mask) ||
        !c43_witness_mask_valid(plan->satisfied_witness_mask) ||
        (plan->required_witness_mask &
         FWLAB_C43_WITNESS_VALIDATED_ONLY) != 0 ||
        (plan->satisfied_witness_mask &
         ~(plan->required_witness_mask |
           FWLAB_C43_WITNESS_VALIDATED_ONLY)) != 0 ||
        plan->dnr > 1 || plan->more != 0 || plan->crd != 0 ||
        plan->effect_class != FWLAB_NVME_EFFECT_NONE ||
        plan->reserved_branch_padding != 0 ||
        !c43_bytes_zero(plan->reserved1, sizeof(plan->reserved1))) {
        return 0;
    }
    if (plan->semantic_status == FWLAB_C43_STATUS_SUCCESS && plan->dnr != 0) {
        return 0;
    }
    if (plan->semantic_status != FWLAB_C43_STATUS_SUCCESS &&
        (plan->kind != FWLAB_C43_PLAN_IMMEDIATE ||
         plan->result_dword0 != 0 || plan->actual_length != 0)) {
        return 0;
    }
    switch (plan->kind) {
    case FWLAB_C43_PLAN_IMMEDIATE:
        return plan->required_witness_mask == 0 &&
               plan->satisfied_witness_mask == 0 &&
               plan->result_dword0 == 0 && plan->actual_length == 0 &&
               c43_bytes_zero(&plan->shape, sizeof(plan->shape)) &&
               c43_bytes_zero(&plan->identify, sizeof(plan->identify)) &&
               c43_bytes_zero(&plan->block, sizeof(plan->block));
    case FWLAB_C43_PLAN_QUEUE_EFFECT:
        return plan->required_witness_mask ==
                   FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED &&
               (plan->satisfied_witness_mask &
                ~FWLAB_C43_WITNESS_QUEUE_EFFECT_COMMITTED) == 0 &&
               plan->result_dword0 == 0 && plan->actual_length == 0 &&
               c43_bytes_zero(&plan->shape, sizeof(plan->shape)) &&
               c43_bytes_zero(&plan->identify, sizeof(plan->identify)) &&
               c43_bytes_zero(&plan->block, sizeof(plan->block));
    case FWLAB_C43_PLAN_ABORT_RESOLVE:
        return plan->required_witness_mask == 0 &&
               plan->satisfied_witness_mask == 0 &&
               plan->result_dword0 <= 1 && plan->actual_length == 0 &&
               c43_bytes_zero(&plan->shape, sizeof(plan->shape)) &&
               c43_bytes_zero(&plan->identify, sizeof(plan->identify)) &&
               c43_bytes_zero(&plan->block, sizeof(plan->block));
    case FWLAB_C43_PLAN_PAYLOAD:
        return fwlab_c43_transfer_shape_valid(&plan->shape) &&
               fwlab_c43_identify_recipe_valid(&plan->identify) &&
               c43_bytes_zero(&plan->block, sizeof(plan->block)) &&
               plan->shape.direction ==
                   FWLAB_C43_TRANSFER_CONTROLLER_TO_HOST &&
               plan->shape.lba_count == 0 &&
               plan->shape.data_bytes == FWLAB_C43_IDENTIFY_BYTES &&
               plan->required_witness_mask ==
                   (FWLAB_C43_WITNESS_PAYLOAD_READY |
                    FWLAB_C43_WITNESS_DMA_OUT_COMPLETE) &&
               (plan->satisfied_witness_mask &
                ~(FWLAB_C43_WITNESS_PAYLOAD_READY |
                  FWLAB_C43_WITNESS_DMA_OUT_COMPLETE)) == 0 &&
               plan->result_dword0 == 0 &&
               plan->actual_length == FWLAB_C43_IDENTIFY_BYTES;
    case FWLAB_C43_PLAN_BLOCK:
        if (!fwlab_c43_transfer_shape_valid(&plan->shape) ||
            !c43_bytes_zero(&plan->identify, sizeof(plan->identify)) ||
            !fwlab_c43_block_intent_valid(&plan->block) ||
            !c43_handle_equal(&plan->command, &plan->block.command) ||
            !c43_origin_equal(&plan->origin, &plan->block.origin) ||
            plan->result_dword0 != 0) {
            return 0;
        }
        if (plan->block.operation == FWLAB_C43_BLOCK_READ) {
            expected_witness = FWLAB_C43_WITNESS_BLOCK_READ_READY |
                               FWLAB_C43_WITNESS_DMA_OUT_COMPLETE;
            if (plan->shape.direction !=
                FWLAB_C43_TRANSFER_CONTROLLER_TO_HOST) {
                return 0;
            }
        } else if (plan->block.operation == FWLAB_C43_BLOCK_WRITE) {
            expected_witness = FWLAB_C43_WITNESS_DMA_IN_COMPLETE |
                               FWLAB_C43_WITNESS_BLOCK_WRITE_COMPLETE;
            if (plan->block.durability ==
                FWLAB_C43_DURABILITY_REQUIRE_SELF) {
                expected_witness |= FWLAB_C43_WITNESS_DURABILITY_COMPLETE;
            }
            if (plan->shape.direction !=
                FWLAB_C43_TRANSFER_HOST_TO_CONTROLLER) {
                return 0;
            }
        } else {
            expected_witness = FWLAB_C43_WITNESS_DURABILITY_COMPLETE;
            if (plan->shape.direction != FWLAB_C43_TRANSFER_NONE) {
                return 0;
            }
        }
        return plan->required_witness_mask == expected_witness &&
               plan->shape.lba_count == plan->block.lba_count &&
               plan->shape.data_bytes == plan->block.data_bytes &&
               plan->actual_length == plan->block.data_bytes;
    default:
        return 0;
    }
}

int fwlab_c43_completion_witness_valid(
    const struct fwlab_c43_completion_witness *witness)
{
    const uint32_t strong = FWLAB_C43_WITNESS_DMA_IN_COMPLETE |
                            FWLAB_C43_WITNESS_BLOCK_READ_READY |
                            FWLAB_C43_WITNESS_BLOCK_WRITE_COMPLETE |
                            FWLAB_C43_WITNESS_DMA_OUT_COMPLETE |
                            FWLAB_C43_WITNESS_DURABILITY_COMPLETE;

    if (witness == NULL || witness->version != FWLAB_C43_POLICY_VERSION ||
        witness->size != sizeof(*witness) || witness->reserved0 != 0 ||
        !c43_handle_valid(&witness->command) ||
        !c43_origin_valid(&witness->origin) ||
        witness->provider_generation == 0 ||
        !c43_witness_mask_valid(witness->witness_mask) ||
        witness->effect_class > FWLAB_NVME_EFFECT_UNKNOWN_PREFIX ||
        witness->terminal_kind > FWLAB_HIF_ACTION_FAILED ||
        !c43_bytes_zero(witness->reserved1, sizeof(witness->reserved1)) ||
        !c43_bytes_zero(witness->reserved2, sizeof(witness->reserved2)) ||
        witness->reserved3 != 0) {
        return 0;
    }
    if (witness->terminal_kind != FWLAB_HIF_ACTION_SUCCESS) {
        return witness->witness_mask == 0 &&
               c43_ref_zero(&witness->predecessor) &&
               ((witness->effect_class == FWLAB_NVME_EFFECT_NONE &&
                 witness->units_completed == 0) ||
                witness->effect_class == FWLAB_NVME_EFFECT_UNKNOWN_PREFIX ||
                ((witness->effect_class == FWLAB_NVME_EFFECT_FULL ||
                  witness->effect_class == FWLAB_NVME_EFFECT_EXACT_PREFIX) &&
                 witness->units_completed != 0));
    }
    if (witness->witness_mask == 0) {
        return 0;
    }
    if (witness->witness_mask == FWLAB_C43_WITNESS_VALIDATED_ONLY) {
        return c43_ref_zero(&witness->predecessor) &&
               witness->effect_class == FWLAB_NVME_EFFECT_NONE &&
               witness->units_completed == 0;
    }
    if ((witness->witness_mask & FWLAB_C43_WITNESS_VALIDATED_ONLY) != 0) {
        return 0;
    }
    if ((witness->witness_mask & strong) != 0 &&
        c43_ref_zero(&witness->predecessor)) {
        return 0;
    }
    if ((witness->witness_mask & strong) == 0 &&
        !c43_ref_zero(&witness->predecessor)) {
        return 0;
    }
    return (witness->effect_class == FWLAB_NVME_EFFECT_NONE &&
            witness->units_completed == 0) ||
           witness->effect_class == FWLAB_NVME_EFFECT_UNKNOWN_PREFIX ||
           ((witness->effect_class == FWLAB_NVME_EFFECT_FULL ||
             witness->effect_class == FWLAB_NVME_EFFECT_EXACT_PREFIX) &&
            witness->units_completed != 0);
}
