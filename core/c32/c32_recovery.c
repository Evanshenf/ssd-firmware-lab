/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c32_internal.h"

#include <string.h>

static uint8_t event_mask(enum c32_cut_kind event)
{
    switch (event) {
    case C32_CUT_CONTROLLER_RESET:
        return FWLAB_PERSIST_EVENT_CONTROLLER_RESET;
    case C32_CUT_POWER_LOSS:
        return FWLAB_PERSIST_EVENT_POWER_LOSS;
    case C32_CUT_DAEMON_CRASH:
        return FWLAB_PERSIST_EVENT_DAEMON_CRASH;
    case C32_CUT_HOST_CRASH:
        return FWLAB_PERSIST_EVENT_HOST_CRASH;
    default:
        return 0;
    }
}

static int ref_in_range(const struct c32_phys_ref *ref)
{
    return ref->valid != 0 && ref->group < C32_GROUPS &&
           ref->slot < C32_SLOTS_PER_GROUP;
}

static int materialize_media(
    struct c32_logical_image *image,
    uint8_t group,
    uint8_t slot,
    const struct c32_persistent_record *frozen
)
{
    struct c32_persistent_record record;

    if (group >= C32_GROUPS || slot >= C32_SLOTS_PER_GROUP) {
        return 0;
    }
    record = *frozen;
    record.self.group = group;
    record.self.slot = slot;
    record.self.erase_generation = image->erase_generation[group];
    record.self.valid = 1;
    record.c_applied = 1;
    image->media[group][slot] = record;
    return 1;
}

static int materialize_operation(
    struct c32_logical_image *image,
    const struct c32_physical_op *operation
)
{
    unsigned int slot;

    if (operation->target_domain == C32_TARGET_MEDIA) {
        return materialize_media(image, operation->target_group,
                                 operation->target_slot,
                                 &operation->frozen_record);
    }
    if (operation->target_domain == C32_TARGET_CHECKPOINT) {
        if (operation->target_slot >= C32_CHECKPOINTS) {
            return 0;
        }
        image->checkpoint[operation->target_slot] =
            operation->frozen_checkpoint;
        image->checkpoint[operation->target_slot].c_applied = 1;
        return 1;
    }
    if (operation->target_domain == C32_TARGET_ANCHOR) {
        if (operation->target_slot >= C32_ANCHORS) {
            return 0;
        }
        image->anchor[operation->target_slot] = operation->frozen_anchor;
        image->anchor[operation->target_slot].c_applied = 1;
        return 1;
    }
    if (operation->target_domain == C32_TARGET_ERASE_GROUP) {
        if (operation->target_group >= C32_GROUPS ||
            image->erase_generation[operation->target_group] >= 1) {
            return 0;
        }
        ++image->erase_generation[operation->target_group];
        for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
            memset(&image->media[operation->target_group][slot], 0,
                   sizeof(image->media[operation->target_group][slot]));
        }
        return 1;
    }
    return 0;
}

static int settle_operations(
    const struct c32_model_state *cut,
    struct c32_physical_result *result
)
{
    uint16_t order;

    for (order = 0; order <= 3; ++order) {
        unsigned int index;

        for (index = 0; index < C32_PHYSICAL_OPS; ++index) {
            const struct c32_physical_op *operation = &cut->inflight[index];
            int applied;

            if (operation->phase == C32_OP_FREE ||
                operation->begin_order != order) {
                continue;
            }
            applied = operation->phase == C32_OP_C_APPLIED ||
                      operation->phase == C32_OP_A_APPLIED ||
                      (operation->phase == C32_OP_B &&
                       operation->frozen_outcome == C32_PHYS_APPLIED);
            if (operation->phase != C32_OP_C_APPLIED &&
                operation->phase != C32_OP_C_NO_EFFECT) {
                ++result->settled_operations;
                if (applied && !materialize_operation(&result->image,
                                                       operation)) {
                    return 0;
                }
            }
            if (operation->outcome_delivered != 0 &&
                operation->owner_epoch != cut->current_epoch) {
                ++result->stale_deliveries_blocked;
            }
        }
    }
    return 1;
}

static int drain_one_plp_atom(
    struct c32_logical_image *image,
    const struct c32_plp_envelope *envelope,
    unsigned int atom
)
{
    const struct c32_plp_atom *redo = &envelope->atom[atom];
    struct c32_persistent_record data;
    struct c32_persistent_record metadata;

    if (!ref_in_range(&redo->drain_metadata_ref)) {
        return 0;
    }
    memset(&data, 0, sizeof(data));
    if (redo->target_kind == C32_LOGICAL_VALUE) {
        if (!ref_in_range(&redo->drain_data_ref)) {
            return 0;
        }
        data.presence = C32_RECORD_VALID;
        data.kind = C32_REC_DATA;
        data.atom = redo->atom;
        data.logical_version = redo->version;
        data.value_token = redo->value_token;
        data.body_complete = 1;
        data.checksum_ok = 1;
        data.record_id = (uint16_t)(envelope->envelope_id * 8u +
                                    atom * 2u + 1u);
        data.mutation_id = envelope->mutation_id;
        data.c_sequence = (uint16_t)(envelope->persistent_order * 4u +
                                     atom * 2u + 1u);
        if (!materialize_media(image, redo->drain_data_ref.group,
                               redo->drain_data_ref.slot, &data)) {
            return 0;
        }
    }
    memset(&metadata, 0, sizeof(metadata));
    metadata.presence = C32_RECORD_VALID;
    metadata.kind = redo->target_kind == C32_LOGICAL_TOMBSTONE ?
                    C32_REC_TOMBSTONE : C32_REC_MAP;
    metadata.atom = redo->atom;
    metadata.logical_version = redo->version;
    metadata.value_token = redo->value_token;
    metadata.predecessor_version = redo->predecessor_version;
    metadata.predecessor_state_id = redo->predecessor_state_id;
    metadata.body_complete = 1;
    metadata.checksum_ok = 1;
    metadata.record_id = (uint16_t)(envelope->envelope_id * 8u +
                                    atom * 2u + 2u);
    metadata.mutation_id = envelope->mutation_id;
    metadata.c_sequence = (uint16_t)(envelope->persistent_order * 4u +
                                     atom * 2u + 2u);
    metadata.data_ref = redo->drain_data_ref;
    return materialize_media(image, redo->drain_metadata_ref.group,
                             redo->drain_metadata_ref.slot, &metadata);
}

static int drain_plp(
    const struct c32_model_state *cut,
    enum c32_cut_kind event,
    enum c32_broken_variant broken,
    struct c32_physical_result *result
)
{
    uint16_t order;
    uint8_t required_event = event_mask(event);

    for (order = 0; order <= 3; ++order) {
        unsigned int index;

        for (index = 0; index < C32_PLP_SLOTS; ++index) {
            const struct c32_plp_envelope *envelope = &cut->plp[index];
            unsigned int atom;

            if (envelope->state < C32_PLP_ADMITTED ||
                envelope->persistent_order != order) {
                continue;
            }
            if (broken == C32_BM_READY_BEFORE_PLP_DRAIN) {
                continue;
            }
            if ((envelope->flags & FWLAB_PLP_REQUIRED_FLAGS) !=
                    FWLAB_PLP_REQUIRED_FLAGS ||
                envelope->capacity_cost == 0 ||
                envelope->capacity_cost > cut->profile.plp_capacity_credits ||
                envelope->capacity_cost !=
                    (unsigned int)((envelope->atom_mask & 1u) != 0) +
                    (unsigned int)((envelope->atom_mask & 2u) != 0) ||
                envelope->drain_budget_reserved < envelope->capacity_cost ||
                (envelope->survival_event_mask & required_event) !=
                    required_event) {
                return 0;
            }
            for (atom = 0; atom < C32_ATOMS; ++atom) {
                uint8_t bit = (uint8_t)(1u << atom);

                if ((envelope->atom_mask & bit) != 0 &&
                    (envelope->drained_atom_mask & bit) == 0 &&
                    !drain_one_plp_atom(&result->image, envelope, atom)) {
                    return 0;
                }
            }
            ++result->drained_envelopes;
        }
    }
    return 1;
}

int c32_physical_settle(
    const struct c32_model_state *cut,
    enum c32_cut_kind event,
    enum c32_broken_variant broken,
    struct c32_physical_result *result
)
{
    if (cut == NULL || result == NULL || event > C32_CUT_HOST_CRASH) {
        return 0;
    }
    memset(result, 0, sizeof(*result));
    memcpy(result->image.erase_generation, cut->erase_generation,
           sizeof(result->image.erase_generation));
    memcpy(result->image.media, cut->media, sizeof(result->image.media));
    memcpy(result->image.checkpoint, cut->checkpoint,
           sizeof(result->image.checkpoint));
    memcpy(result->image.anchor, cut->anchor, sizeof(result->image.anchor));
    memcpy(result->image.genesis, cut->genesis,
           sizeof(result->image.genesis));
    if (!settle_operations(cut, result) ||
        !drain_plp(cut, event, broken, result)) {
        result->status = C32_RECOVERY_FAIL_CLOSED;
        return 1;
    }
    result->status = C32_RECOVERY_OK;
    return 1;
}

static const struct c32_persistent_record *record_at(
    const struct c32_logical_image *image,
    const struct c32_phys_ref *ref
)
{
    if (!ref_in_range(ref) ||
        ref->erase_generation != image->erase_generation[ref->group]) {
        return NULL;
    }
    return &image->media[ref->group][ref->slot];
}

static int strict_record(
    const struct c32_logical_image *image,
    unsigned int group,
    unsigned int slot,
    enum c32_broken_variant broken
)
{
    const struct c32_persistent_record *record = &image->media[group][slot];

    return record->presence == C32_RECORD_VALID && record->c_applied != 0 &&
           record->body_complete != 0 &&
           (record->checksum_ok != 0 ||
            broken == C32_BM_TORN_SKIP_CHECKSUM) &&
           record->self.valid != 0 && record->self.group == group &&
           record->self.slot == slot &&
           record->self.erase_generation == image->erase_generation[group];
}

static int strict_checkpoint(
    const struct c32_checkpoint *checkpoint,
    enum c32_broken_variant broken
)
{
    return checkpoint->image_state == C32_IMAGE_VALID &&
           checkpoint->c_applied != 0 && checkpoint->checksum_ok != 0 &&
           checkpoint->provenance_ok != 0 &&
           (checkpoint->body_complete != 0 ||
            broken == C32_BM_ANCHOR_BEFORE_CKPT_COMPLETE);
}

static int strict_anchor(const struct c32_anchor *anchor)
{
    return anchor->image_state == C32_IMAGE_VALID &&
           anchor->c_applied != 0 && anchor->body_complete != 0 &&
           anchor->checksum_ok != 0;
}

static int select_checkpoint(
    const struct c32_logical_image *image,
    enum c32_broken_variant broken,
    struct c32_recovery_result *result
)
{
    int selected = -1;
    unsigned int anchor_index;

    for (anchor_index = 0; anchor_index < C32_ANCHORS; ++anchor_index) {
        const struct c32_anchor *anchor = &image->anchor[anchor_index];
        const struct c32_checkpoint *checkpoint;

        if (!strict_anchor(anchor) || anchor->target_slot >= C32_CHECKPOINTS) {
            continue;
        }
        checkpoint = &image->checkpoint[anchor->target_slot];
        if (!strict_checkpoint(checkpoint, broken) ||
            checkpoint->generation != anchor->generation ||
            checkpoint->watermark != anchor->watermark ||
            checkpoint->payload_hash != anchor->checkpoint_hash) {
            continue;
        }
        if (selected < 0 ||
            checkpoint->generation >
                image->checkpoint[(unsigned int)selected].generation) {
            selected = anchor->target_slot;
        } else if (checkpoint->generation ==
                       image->checkpoint[(unsigned int)selected].generation &&
                   (anchor->target_slot != (unsigned int)selected ||
                    checkpoint->payload_hash !=
                        image->checkpoint[(unsigned int)selected].payload_hash)) {
            return 0;
        }
    }
    if (selected < 0) {
        memcpy(result->atom, image->genesis, sizeof(result->atom));
        result->selected_checkpoint = UINT8_MAX;
        result->watermark = 0;
    } else {
        memcpy(result->atom, image->checkpoint[(unsigned int)selected].entry,
               sizeof(result->atom));
        result->selected_checkpoint = (uint8_t)selected;
        result->watermark =
            image->checkpoint[(unsigned int)selected].watermark;
    }
    return 1;
}

static int data_dependency_valid(
    const struct c32_logical_image *image,
    const struct c32_persistent_record *metadata,
    enum c32_broken_variant broken
)
{
    const struct c32_persistent_record *data =
        record_at(image, &metadata->data_ref);

    if (broken == C32_BM_MAP_OMIT_DATA_C_GUARD) {
        return data != NULL && data->kind == C32_REC_DATA;
    }
    if (data == NULL || data->self.group >= C32_GROUPS ||
        data->self.slot >= C32_SLOTS_PER_GROUP ||
        !strict_record(image, data->self.group, data->self.slot, broken)) {
        return 0;
    }
    return data->kind == C32_REC_DATA && data->atom == metadata->atom &&
           data->logical_version == metadata->logical_version &&
           data->value_token == metadata->value_token &&
           data->c_sequence < metadata->c_sequence;
}

static int candidate_from_record(
    const struct c32_logical_image *image,
    const struct c32_persistent_record *record,
    const struct c32_logical_state *current,
    enum c32_broken_variant broken,
    struct c32_logical_state *candidate
)
{
    memset(candidate, 0, sizeof(*candidate));
    if (broken == C32_BM_RELOC_OVERRIDES_TOMBSTONE &&
        current->kind == C32_LOGICAL_TOMBSTONE &&
        record->kind == C32_REC_RELOCATION &&
        data_dependency_valid(image, record, broken)) {
        candidate->kind = C32_LOGICAL_VALUE;
        candidate->atom = record->atom;
        candidate->version = record->logical_version;
        candidate->copy_discriminator = record->copy_discriminator;
        candidate->value_token = record->value_token;
        candidate->state_id = record->record_id;
        candidate->predecessor_state_id = current->state_id;
        candidate->authority_record_id = record->record_id;
        candidate->data_ref = record->data_ref;
        return 1;
    }
    if (record->atom != current->atom ||
        record->predecessor_state_id != current->state_id) {
        return 0;
    }
    if (record->kind == C32_REC_MAP) {
        if (record->logical_version <= current->version ||
            !data_dependency_valid(image, record, broken)) {
            return 0;
        }
        candidate->kind = C32_LOGICAL_VALUE;
        candidate->version = record->logical_version;
        candidate->value_token = record->value_token;
        candidate->state_id = record->record_id;
        candidate->data_ref = record->data_ref;
    } else if (record->kind == C32_REC_TOMBSTONE) {
        if (record->logical_version <= current->version) {
            return 0;
        }
        candidate->kind = C32_LOGICAL_TOMBSTONE;
        candidate->version = record->logical_version;
        candidate->state_id = record->record_id;
    } else if (record->kind == C32_REC_RELOCATION) {
        if (current->kind == C32_LOGICAL_TOMBSTONE &&
            broken != C32_BM_RELOC_OVERRIDES_TOMBSTONE) {
            return 0;
        }
        if (record->logical_version != current->version ||
            record->value_token != current->value_token ||
            record->copy_discriminator <= current->copy_discriminator ||
            !data_dependency_valid(image, record, broken)) {
            return 0;
        }
        *candidate = *current;
        candidate->copy_discriminator = record->copy_discriminator;
        candidate->authority_record_id = record->record_id;
        candidate->data_ref = record->data_ref;
        return 1;
    } else {
        return 0;
    }
    candidate->atom = record->atom;
    candidate->predecessor_state_id = current->state_id;
    candidate->authority_record_id = record->record_id;
    return 1;
}

static int logical_equal(
    const struct c32_logical_state *left,
    const struct c32_logical_state *right
)
{
    return left->kind == right->kind && left->atom == right->atom &&
           left->version == right->version &&
           left->copy_discriminator == right->copy_discriminator &&
           left->value_token == right->value_token &&
           left->state_id == right->state_id &&
           left->authority_record_id == right->authority_record_id &&
           left->data_ref.group == right->data_ref.group &&
           left->data_ref.slot == right->data_ref.slot &&
           left->data_ref.erase_generation ==
               right->data_ref.erase_generation &&
           left->data_ref.valid == right->data_ref.valid;
}

static int replay_tail(
    const struct c32_logical_image *image,
    enum c32_broken_variant broken,
    struct c32_recovery_result *result
)
{
    uint16_t after = result->watermark;

    if (broken == C32_BM_RECOVERY_SKIP_TAIL_AFTER_CKPT) {
        return 1;
    }
    for (;;) {
        uint16_t next = UINT16_MAX;
        unsigned int group;
        unsigned int slot;
        unsigned int atom;

        for (group = 0; group < C32_GROUPS; ++group) {
            for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
                const struct c32_persistent_record *record =
                    &image->media[group][slot];

                if (strict_record(image, group, slot, broken) &&
                    record->kind != C32_REC_DATA &&
                    record->c_sequence > after &&
                    record->c_sequence < next) {
                    next = record->c_sequence;
                }
            }
        }
        if (next == UINT16_MAX) {
            break;
        }
        for (atom = 0; atom < C32_ATOMS; ++atom) {
            struct c32_logical_state candidate;
            int candidates = 0;

            memset(&candidate, 0, sizeof(candidate));
            for (group = 0; group < C32_GROUPS; ++group) {
                for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
                    const struct c32_persistent_record *record =
                        &image->media[group][slot];
                    struct c32_logical_state one;

                    if (!strict_record(image, group, slot, broken) ||
                        record->c_sequence != next ||
                        !candidate_from_record(image, record,
                                               &result->atom[atom], broken,
                                               &one)) {
                        continue;
                    }
                    if (candidates < 0) {
                        continue;
                    }
                    if (candidates == 0) {
                        candidate = one;
                        candidates = 1;
                    } else if (!logical_equal(&candidate, &one)) {
                        if (broken == C32_BM_UNIQUE_KEEP_PREDECESSOR) {
                            candidates = -1;
                        } else {
                            return 0;
                        }
                    }
                }
            }
            if (candidates == 1) {
                result->atom[atom] = candidate;
            }
        }
        after = next;
    }
    return 1;
}

static void recover_orphan_data(
    const struct c32_logical_image *image,
    struct c32_recovery_result *result
)
{
    unsigned int atom;
    unsigned int group;
    unsigned int slot;

    for (atom = 0; atom < C32_ATOMS; ++atom) {
        for (group = 0; group < C32_GROUPS; ++group) {
            for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
                const struct c32_persistent_record *record =
                    &image->media[group][slot];

                if (strict_record(image, group, slot, C32_BROKEN_NONE) &&
                    record->kind == C32_REC_DATA && record->atom == atom &&
                    record->logical_version > result->atom[atom].version) {
                    result->atom[atom].kind = C32_LOGICAL_VALUE;
                    result->atom[atom].atom = (uint8_t)atom;
                    result->atom[atom].version = record->logical_version;
                    result->atom[atom].value_token = record->value_token;
                    result->atom[atom].state_id = record->record_id;
                    result->atom[atom].authority_record_id = record->record_id;
                    result->atom[atom].data_ref = record->self;
                }
            }
        }
    }
}

int c32_logical_recover(
    const struct c32_logical_image *image,
    enum c32_broken_variant broken,
    struct c32_recovery_result *result
)
{
    unsigned int atom;

    if (image == NULL || result == NULL) {
        return 0;
    }
    memset(result, 0, sizeof(*result));
    if (!select_checkpoint(image, broken, result) ||
        !replay_tail(image, broken, result)) {
        result->status = C32_RECOVERY_AMBIGUOUS;
        result->hash = c32_recovery_hash(result);
        return 1;
    }
    if (broken == C32_BM_RECOVER_HIGHEST_DATA_WITHOUT_MAP) {
        recover_orphan_data(image, result);
    }
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        result->atom[atom].atom = (uint8_t)atom;
    }
    result->status = C32_RECOVERY_OK;
    result->hash = c32_recovery_hash(result);
    return 1;
}
