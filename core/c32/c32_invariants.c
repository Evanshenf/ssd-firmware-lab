/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c32_internal.h"

#include <string.h>

enum c32_violation_reason {
    C32_VIOLATION_NONE = 0,
    C32_VIOLATION_AMBIGUOUS_AUTHORITY = 1,
    C32_VIOLATION_TORN_AUTHORITY = 2,
    C32_VIOLATION_MISSING_DATA = 3,
    C32_VIOLATION_BELOW_DURABLE_FLOOR = 4,
    C32_VIOLATION_OUTSIDE_ALLOWED_SET = 5,
    C32_VIOLATION_TOMBSTONE_BYPASS = 6,
    C32_VIOLATION_EARLY_GC_ERASE = 7,
    C32_VIOLATION_CHECKPOINT_PAIR = 8,
    C32_VIOLATION_STALE_EPOCH_DELIVERY = 9,
    C32_VIOLATION_FENCE_OBLIGATION = 10,
    C32_VIOLATION_PLP_WITNESS = 11,
    C32_VIOLATION_CONSERVATION = 12,
    C32_VIOLATION_HOST_AUTHORITY = 13
};

static void pass_result(
    enum fwlab_persist_invariant_id invariant,
    struct c32_invariant_result *result
)
{
    memset(result, 0, sizeof(*result));
    result->passed = 1;
    result->invariant_id = (uint8_t)invariant;
}

static int fail_result(
    struct c32_invariant_result *result,
    uint8_t atom,
    uint16_t record_id,
    uint16_t reason
)
{
    result->passed = 0;
    result->atom = atom;
    result->record_id = record_id;
    result->reason = reason;
    return 1;
}

static int ref_in_range(const struct c32_phys_ref *ref)
{
    return ref->valid != 0 && ref->group < C32_GROUPS &&
           ref->slot < C32_SLOTS_PER_GROUP;
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
    unsigned int slot
)
{
    const struct c32_persistent_record *record = &image->media[group][slot];

    return record->presence == C32_RECORD_VALID && record->c_applied != 0 &&
           record->body_complete != 0 && record->checksum_ok != 0 &&
           record->self.valid != 0 && record->self.group == group &&
           record->self.slot == slot &&
           record->self.erase_generation == image->erase_generation[group];
}

static const struct c32_persistent_record *find_record(
    const struct c32_logical_image *image,
    uint16_t record_id,
    unsigned int *group_out,
    unsigned int *slot_out
)
{
    unsigned int group;
    unsigned int slot;

    for (group = 0; group < C32_GROUPS; ++group) {
        for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
            if (image->media[group][slot].record_id == record_id) {
                if (group_out != NULL) {
                    *group_out = group;
                }
                if (slot_out != NULL) {
                    *slot_out = slot;
                }
                return &image->media[group][slot];
            }
        }
    }
    return NULL;
}

static int logical_equal(
    const struct c32_logical_state *left,
    const struct c32_logical_state *right
)
{
    return left->kind == right->kind && left->version == right->version &&
           left->value_token == right->value_token &&
           left->copy_discriminator == right->copy_discriminator &&
           left->state_id == right->state_id &&
           left->authority_record_id == right->authority_record_id;
}

static int check_unique(
    const struct c32_physical_result *physical,
    const struct c32_recovery_result *recovered,
    struct c32_invariant_result *result
)
{
    unsigned int group;
    unsigned int slot;
    unsigned int other_group;
    unsigned int other_slot;

    if (recovered->status == C32_RECOVERY_AMBIGUOUS) {
        return fail_result(result, 0, 0,
                           C32_VIOLATION_AMBIGUOUS_AUTHORITY);
    }
    for (group = 0; group < C32_GROUPS; ++group) {
        for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
            const struct c32_persistent_record *one =
                &physical->image.media[group][slot];

            if (!strict_record(&physical->image, group, slot) ||
                one->kind == C32_REC_DATA) {
                continue;
            }
            for (other_group = group; other_group < C32_GROUPS;
                 ++other_group) {
                for (other_slot = 0; other_slot < C32_SLOTS_PER_GROUP;
                     ++other_slot) {
                    const struct c32_persistent_record *two =
                        &physical->image.media[other_group][other_slot];

                    if (other_group == group && other_slot <= slot) {
                        continue;
                    }
                    if (strict_record(&physical->image, other_group,
                                      other_slot) &&
                        two->kind != C32_REC_DATA &&
                        one->atom == two->atom &&
                        one->c_sequence == two->c_sequence &&
                        one->predecessor_state_id ==
                            two->predecessor_state_id &&
                        (one->kind != two->kind ||
                         one->logical_version != two->logical_version ||
                         one->value_token != two->value_token)) {
                        return fail_result(
                            result, one->atom, two->record_id,
                            C32_VIOLATION_AMBIGUOUS_AUTHORITY);
                    }
                }
            }
        }
    }
    return 1;
}

static int check_no_torn(
    const struct c32_physical_result *physical,
    const struct c32_recovery_result *recovered,
    struct c32_invariant_result *result
)
{
    unsigned int atom;

    for (atom = 0; atom < C32_ATOMS; ++atom) {
        unsigned int group;
        unsigned int slot;
        const struct c32_persistent_record *record;

        if (recovered->atom[atom].authority_record_id == 0) {
            continue;
        }
        if (recovered->selected_checkpoint < C32_CHECKPOINTS &&
            logical_equal(
                &recovered->atom[atom],
                &physical->image
                     .checkpoint[recovered->selected_checkpoint].entry[atom])) {
            continue;
        }
        record = find_record(&physical->image,
                             recovered->atom[atom].authority_record_id,
                             &group, &slot);
        if (record == NULL || !strict_record(&physical->image, group, slot)) {
            return fail_result(result, (uint8_t)atom,
                               recovered->atom[atom].authority_record_id,
                               C32_VIOLATION_TORN_AUTHORITY);
        }
    }
    return 1;
}

static int check_depend(
    const struct c32_physical_result *physical,
    const struct c32_recovery_result *recovered,
    struct c32_invariant_result *result
)
{
    unsigned int atom;

    for (atom = 0; atom < C32_ATOMS; ++atom) {
        const struct c32_persistent_record *metadata;
        const struct c32_persistent_record *data;
        unsigned int group;
        unsigned int slot;

        if (recovered->atom[atom].authority_record_id == 0) {
            continue;
        }
        metadata = find_record(&physical->image,
                               recovered->atom[atom].authority_record_id,
                               &group, &slot);
        if (metadata == NULL || metadata->kind == C32_REC_TOMBSTONE) {
            continue;
        }
        data = record_at(&physical->image, &metadata->data_ref);
        if (data == NULL || data->self.group >= C32_GROUPS ||
            data->self.slot >= C32_SLOTS_PER_GROUP ||
            !strict_record(&physical->image, data->self.group,
                           data->self.slot) || data->kind != C32_REC_DATA ||
            data->atom != metadata->atom ||
            data->logical_version != metadata->logical_version ||
            data->value_token != metadata->value_token ||
            data->c_sequence >= metadata->c_sequence) {
            return fail_result(result, (uint8_t)atom, metadata->record_id,
                               C32_VIOLATION_MISSING_DATA);
        }
    }
    return 1;
}

static int check_durable_floor(
    const struct c32_model_state *cut,
    const struct c32_recovery_result *recovered,
    struct c32_invariant_result *result
)
{
    unsigned int atom;

    for (atom = 0; atom < C32_ATOMS; ++atom) {
        const struct c32_logical_state *floor = &cut->durable_floor[atom];
        const struct c32_logical_state *actual = &recovered->atom[atom];

        if (actual->version < floor->version ||
            (actual->version == floor->version &&
             (actual->kind != floor->kind ||
              actual->value_token != floor->value_token))) {
            return fail_result(result, (uint8_t)atom,
                               actual->authority_record_id,
                               C32_VIOLATION_BELOW_DURABLE_FLOOR);
        }
    }
    return 1;
}

static uint16_t logical_bit(const struct c32_logical_state *state)
{
    if (state->kind == C32_LOGICAL_TOMBSTONE) {
        return UINT16_C(1) << 8;
    }
    return (uint16_t)(UINT16_C(1) << state->version);
}

static int check_volatile_bound(
    const struct c32_model_state *cut,
    const struct c32_physical_result *physical,
    const struct c32_recovery_result *recovered,
    struct c32_invariant_result *result
)
{
    uint16_t allowed[C32_ATOMS];
    unsigned int atom;
    uint16_t sequence;

    for (atom = 0; atom < C32_ATOMS; ++atom) {
        allowed[atom] = logical_bit(&cut->durable_floor[atom]);
    }
    for (sequence = 0; sequence <= 1; ++sequence) {
        unsigned int mutation_index;

        for (mutation_index = 0; mutation_index < C32_MUTATIONS;
             ++mutation_index) {
            const struct c32_mutation *mutation =
                &cut->mutation[mutation_index];

            if (!mutation->used || mutation->accept_sequence != sequence) {
                continue;
            }
            for (atom = 0; atom < C32_ATOMS; ++atom) {
                uint8_t bit = (uint8_t)(1u << atom);
                uint16_t target_bit;

                if ((mutation->atom_mask & bit) == 0) {
                    continue;
                }
                target_bit = mutation->target_kind[atom] ==
                                     C32_LOGICAL_TOMBSTONE ?
                             (UINT16_C(1) << 8) :
                             (uint16_t)(UINT16_C(1) <<
                                        mutation->target_version[atom]);
                if (mutation->publication == C32_PUBLISH_DURABLE ||
                    (mutation->publication == C32_PUBLISH_NONE &&
                     (mutation->closure[atom] ==
                          FWLAB_PERSIST_CLOSE_C_MAP ||
                      mutation->closure[atom] ==
                          FWLAB_PERSIST_CLOSE_PLP))) {
                    allowed[atom] = target_bit;
                } else if (mutation->publication == C32_PUBLISH_VOLATILE ||
                           mutation->publication ==
                               C32_PUBLISH_FAILED_INDETERMINATE) {
                    allowed[atom] |= target_bit;
                }
            }
        }
    }
    for (sequence = 0; sequence < C32_PHYSICAL_OPS; ++sequence) {
        const struct c32_physical_op *operation = &cut->inflight[sequence];
        const struct c32_persistent_record *record =
            &operation->frozen_record;
        unsigned int mutation_index;
        uint16_t target_bit;

        if ((operation->purpose != C32_PURPOSE_MAP &&
             operation->purpose != C32_PURPOSE_TOMB) ||
            operation->frozen_outcome != C32_PHYS_APPLIED ||
            operation->phase == C32_OP_FREE ||
            operation->phase == C32_OP_A_NO_EFFECT ||
            operation->phase == C32_OP_C_NO_EFFECT ||
            record->mutation_id == 0 ||
            record->mutation_id > C32_MUTATIONS) {
            continue;
        }
        mutation_index = record->mutation_id - 1u;
        target_bit = record->kind == C32_REC_TOMBSTONE ?
                     (UINT16_C(1) << 8) :
                     (uint16_t)(UINT16_C(1) << record->logical_version);
        if (cut->mutation[mutation_index].publication == C32_PUBLISH_NONE ||
            cut->mutation[mutation_index].publication ==
                C32_PUBLISH_VOLATILE ||
            cut->mutation[mutation_index].publication ==
                C32_PUBLISH_FAILED_INDETERMINATE) {
            allowed[record->atom] |= target_bit;
        }
    }
    (void)physical;
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        if ((allowed[atom] & logical_bit(&recovered->atom[atom])) == 0) {
            return fail_result(result, (uint8_t)atom,
                               recovered->atom[atom].authority_record_id,
                               C32_VIOLATION_OUTSIDE_ALLOWED_SET);
        }
    }
    return 1;
}

static int check_trim(
    const struct c32_model_state *cut,
    const struct c32_recovery_result *recovered,
    struct c32_invariant_result *result
)
{
    unsigned int atom;
    unsigned int mutation_index;

    for (atom = 0; atom < C32_ATOMS; ++atom) {
        uint8_t tombstone_version = 0;

        for (mutation_index = 0; mutation_index < C32_MUTATIONS;
             ++mutation_index) {
            const struct c32_mutation *mutation =
                &cut->mutation[mutation_index];

            if (mutation->used && mutation->publication ==
                                      C32_PUBLISH_DURABLE &&
                mutation->target_kind[atom] == C32_LOGICAL_TOMBSTONE &&
                mutation->target_version[atom] > tombstone_version) {
                tombstone_version = mutation->target_version[atom];
            }
        }
        if (tombstone_version != 0 &&
            recovered->atom[atom].kind != C32_LOGICAL_TOMBSTONE &&
            recovered->atom[atom].version <= tombstone_version) {
            return fail_result(result, (uint8_t)atom,
                               recovered->atom[atom].authority_record_id,
                               C32_VIOLATION_TOMBSTONE_BYPASS);
        }
    }
    return 1;
}

static int check_gc(
    const struct c32_model_state *cut,
    const struct c32_physical_result *physical,
    struct c32_invariant_result *result
)
{
    const struct c32_gc_plan *gc = &cut->gc;
    const struct c32_persistent_record *relocation = NULL;
    unsigned int group;
    unsigned int slot;

    if (!gc->used || gc->stage < 4) {
        return 1;
    }
    for (group = 0; group < C32_GROUPS; ++group) {
        for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
            const struct c32_persistent_record *record =
                &physical->image.media[group][slot];

            if (strict_record(&physical->image, group, slot) &&
                record->kind == C32_REC_RELOCATION &&
                record->atom == gc->atom &&
                record->logical_version == gc->version) {
                relocation = record;
            }
        }
    }
    if (relocation == NULL || gc->lease_held != 0) {
        return fail_result(result, gc->atom,
                           relocation == NULL ? 0 : relocation->record_id,
                           C32_VIOLATION_EARLY_GC_ERASE);
    }
    return 1;
}

static int check_checkpoint(
    const struct c32_physical_result *physical,
    const struct c32_recovery_result *recovered,
    struct c32_invariant_result *result
)
{
    const struct c32_checkpoint *checkpoint;
    unsigned int anchor_index;
    int pair = 0;

    if (recovered->status == C32_RECOVERY_AMBIGUOUS) {
        return fail_result(result, 0, 0, C32_VIOLATION_CHECKPOINT_PAIR);
    }
    if (recovered->selected_checkpoint == UINT8_MAX) {
        return 1;
    }
    if (recovered->selected_checkpoint >= C32_CHECKPOINTS) {
        return fail_result(result, 0, 0, C32_VIOLATION_CHECKPOINT_PAIR);
    }
    checkpoint = &physical->image.checkpoint[recovered->selected_checkpoint];
    if (checkpoint->image_state != C32_IMAGE_VALID ||
        checkpoint->c_applied == 0 || checkpoint->body_complete == 0 ||
        checkpoint->checksum_ok == 0 || checkpoint->provenance_ok == 0) {
        return fail_result(result, 0, 0, C32_VIOLATION_CHECKPOINT_PAIR);
    }
    for (anchor_index = 0; anchor_index < C32_ANCHORS; ++anchor_index) {
        const struct c32_anchor *anchor =
            &physical->image.anchor[anchor_index];

        if (anchor->image_state == C32_IMAGE_VALID &&
            anchor->c_applied != 0 && anchor->body_complete != 0 &&
            anchor->checksum_ok != 0 &&
            anchor->target_slot == recovered->selected_checkpoint &&
            anchor->generation == checkpoint->generation &&
            anchor->watermark == checkpoint->watermark &&
            anchor->checkpoint_hash == checkpoint->payload_hash) {
            pair = 1;
        }
    }
    return pair ? 1 : fail_result(result, 0, 0,
                                  C32_VIOLATION_CHECKPOINT_PAIR);
}

static int check_epoch(
    const struct c32_model_state *cut,
    struct c32_invariant_result *result
)
{
    unsigned int index;

    for (index = 0; index < C32_PHYSICAL_OPS; ++index) {
        const struct c32_physical_op *operation = &cut->inflight[index];

        if (operation->phase != C32_OP_FREE &&
            operation->owner_epoch != cut->current_epoch &&
            operation->outcome_delivered != 0) {
            return fail_result(result, 0, operation->op_id,
                               C32_VIOLATION_STALE_EPOCH_DELIVERY);
        }
    }
    for (index = 0; index < C32_MUTATIONS; ++index) {
        const struct c32_mutation *mutation = &cut->mutation[index];

        if (mutation->used && mutation->owner_epoch != cut->current_epoch &&
            mutation->publication != C32_PUBLISH_NONE) {
            return fail_result(result, 0, mutation->mutation_id,
                               C32_VIOLATION_STALE_EPOCH_DELIVERY);
        }
    }
    return 1;
}

static int mutation_closed_for_fence(const struct c32_mutation *mutation)
{
    unsigned int atom;

    for (atom = 0; atom < C32_ATOMS; ++atom) {
        uint8_t bit = (uint8_t)(1u << atom);

        if ((mutation->atom_mask & bit) == 0) {
            continue;
        }
        if (mutation->closure[atom] != FWLAB_PERSIST_CLOSE_C_MAP &&
            mutation->closure[atom] != FWLAB_PERSIST_CLOSE_PLP &&
            mutation->closure[atom] != FWLAB_PERSIST_CLOSE_NO_COMMIT) {
            return 0;
        }
    }
    return 1;
}

static int check_fence(
    const struct c32_model_state *cut,
    struct c32_invariant_result *result
)
{
    unsigned int index;

    if (!cut->fence.used || !cut->fence.success) {
        return 1;
    }
    for (index = 0; index < C32_MUTATIONS; ++index) {
        const struct c32_mutation *mutation = &cut->mutation[index];

        if (mutation->used && mutation->owner_epoch == cut->fence.owner_epoch &&
            mutation->scope == cut->fence.scope &&
            mutation->accept_sequence <= cut->fence.frontier &&
            !mutation_closed_for_fence(mutation)) {
            return fail_result(result, 0, mutation->mutation_id,
                               C32_VIOLATION_FENCE_OBLIGATION);
        }
    }
    return 1;
}

static const struct c32_plp_envelope *plp_for_mutation(
    const struct c32_model_state *cut,
    uint16_t mutation_id
)
{
    unsigned int index;

    for (index = 0; index < C32_PLP_SLOTS; ++index) {
        if (cut->plp[index].state >= C32_PLP_ADMITTED &&
            cut->plp[index].mutation_id == mutation_id) {
            return &cut->plp[index];
        }
    }
    return NULL;
}

static int valid_plp(
    const struct c32_model_state *cut,
    const struct c32_plp_envelope *plp
)
{
    unsigned int credits =
        (unsigned int)((plp->atom_mask & 1u) != 0) +
        (unsigned int)((plp->atom_mask & 2u) != 0);

    return plp->capacity_cost == credits && credits != 0 &&
           credits <= cut->profile.plp_capacity_credits &&
           (plp->flags & FWLAB_PLP_REQUIRED_FLAGS) ==
               FWLAB_PLP_REQUIRED_FLAGS &&
           plp->drain_budget_reserved >= credits;
}

static int check_plp(
    const struct c32_model_state *cut,
    enum c32_cut_kind event,
    const struct c32_physical_result *physical,
    const struct c32_recovery_result *recovered,
    struct c32_invariant_result *result
)
{
    unsigned int index;

    for (index = 0; index < C32_MUTATIONS; ++index) {
        const struct c32_mutation *mutation = &cut->mutation[index];
        unsigned int atom;
        int needs_plp = 0;

        if (!mutation->used ||
            mutation->publication != C32_PUBLISH_DURABLE) {
            continue;
        }
        for (atom = 0; atom < C32_ATOMS; ++atom) {
            if (mutation->closure[atom] == FWLAB_PERSIST_CLOSE_PLP) {
                needs_plp = 1;
            }
        }
        if (needs_plp) {
            const struct c32_plp_envelope *plp =
                plp_for_mutation(cut, mutation->mutation_id);
            uint8_t required_event = event == C32_CUT_CONTROLLER_RESET ?
                FWLAB_PERSIST_EVENT_CONTROLLER_RESET :
                event == C32_CUT_POWER_LOSS ?
                FWLAB_PERSIST_EVENT_POWER_LOSS :
                event == C32_CUT_DAEMON_CRASH ?
                FWLAB_PERSIST_EVENT_DAEMON_CRASH :
                FWLAB_PERSIST_EVENT_HOST_CRASH;

            if (plp == NULL || !valid_plp(cut, plp) ||
                (plp->survival_event_mask & required_event) == 0 ||
                (plp->drained_atom_mask != plp->atom_mask &&
                 physical->drained_envelopes == 0)) {
                return fail_result(result, 0, mutation->mutation_id,
                                   C32_VIOLATION_PLP_WITNESS);
            }
            for (atom = 0; atom < C32_ATOMS; ++atom) {
                uint8_t bit = (uint8_t)(1u << atom);

                if ((mutation->atom_mask & bit) != 0 &&
                    (recovered->atom[atom].version <
                         mutation->target_version[atom] ||
                     (recovered->atom[atom].version ==
                          mutation->target_version[atom] &&
                      (recovered->atom[atom].kind !=
                           mutation->target_kind[atom] ||
                       recovered->atom[atom].value_token !=
                           mutation->value_token[atom])))) {
                    return fail_result(result, (uint8_t)atom,
                                       mutation->mutation_id,
                                       C32_VIOLATION_PLP_WITNESS);
                }
            }
        }
    }
    return 1;
}

static int check_conserve(
    const struct c32_model_state *cut,
    struct c32_invariant_result *result
)
{
    uint16_t expected_reserved = 0;
    uint16_t expected_free = 0;
    unsigned int index;
    unsigned int group;
    unsigned int slot;

    for (index = 0; index < C32_PHYSICAL_OPS; ++index) {
        const struct c32_physical_op *operation = &cut->inflight[index];

        if (operation->phase != C32_OP_FREE &&
            operation->target_domain == C32_TARGET_MEDIA &&
            operation->target_group < C32_GROUPS &&
            operation->target_slot < C32_SLOTS_PER_GROUP &&
            operation->phase != C32_OP_C_APPLIED &&
            operation->phase != C32_OP_C_NO_EFFECT) {
            expected_reserved |= (uint16_t)(UINT16_C(1) <<
                (operation->target_group * C32_SLOTS_PER_GROUP +
                 operation->target_slot));
        }
    }
    for (group = 0; group < C32_GROUPS; ++group) {
        for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
            uint16_t bit = (uint16_t)(UINT16_C(1) <<
                (group * C32_SLOTS_PER_GROUP + slot));

            if (cut->media[group][slot].presence == C32_RECORD_EMPTY &&
                (expected_reserved & bit) == 0) {
                expected_free |= bit;
            }
        }
    }
    if (cut->free_bitmap != expected_free ||
        cut->reserved_bitmap != expected_reserved) {
        return fail_result(result, 0, 0, C32_VIOLATION_CONSERVATION);
    }
    return 1;
}

static int check_no_host(
    const struct c32_model_state *cut,
    const struct c32_physical_result *physical,
    const struct c32_recovery_result *recovered,
    struct c32_invariant_result *result
)
{
    struct c32_recovery_result strict;
    unsigned int atom;
    int adversarial_host = 0;

    for (atom = 0; atom < C32_ATOMS; ++atom) {
        if ((cut->host_adversarial_mask & (uint8_t)(1u << atom)) != 0) {
            adversarial_host = 1;
        }
    }
    if (!adversarial_host) {
        return 1;
    }

    if (!c32_logical_recover(&physical->image, C32_BROKEN_NONE, &strict) ||
        strict.status != recovered->status) {
        return fail_result(result, 0, 0, C32_VIOLATION_HOST_AUTHORITY);
    }
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        if ((cut->host_adversarial_mask & (uint8_t)(1u << atom)) != 0 &&
            logical_equal(&cut->host_cache[atom], &recovered->atom[atom]) &&
            !logical_equal(&cut->host_cache[atom], &strict.atom[atom])) {
            return fail_result(result, (uint8_t)atom,
                               recovered->atom[atom].authority_record_id,
                               C32_VIOLATION_HOST_AUTHORITY);
        }
    }
    return 1;
}

int c32_check_invariant(
    enum fwlab_persist_invariant_id invariant,
    const struct c32_model_state *cut,
    enum c32_cut_kind event,
    const struct c32_physical_result *physical,
    const struct c32_recovery_result *recovered,
    struct c32_invariant_result *result
)
{
    if (cut == NULL || physical == NULL || recovered == NULL ||
        result == NULL || invariant < FWLAB_P_UNIQUE ||
        invariant > FWLAB_P_NO_HOST_AUTHORITY) {
        return 0;
    }
    pass_result(invariant, result);
    if (physical->status == C32_RECOVERY_FAIL_CLOSED) {
        return 1;
    }
    switch (invariant) {
    case FWLAB_P_UNIQUE:
        return check_unique(physical, recovered, result);
    case FWLAB_P_NO_TORN:
        return check_no_torn(physical, recovered, result);
    case FWLAB_P_DEPEND:
        return check_depend(physical, recovered, result);
    case FWLAB_P_DURABLE_FLOOR:
        return check_durable_floor(cut, recovered, result);
    case FWLAB_P_VOLATILE_BOUND:
        return check_volatile_bound(cut, physical, recovered, result);
    case FWLAB_P_TRIM:
        return check_trim(cut, recovered, result);
    case FWLAB_P_GC:
        return check_gc(cut, physical, result);
    case FWLAB_P_CHECKPOINT:
        return check_checkpoint(physical, recovered, result);
    case FWLAB_P_EPOCH:
        return check_epoch(cut, result);
    case FWLAB_P_FENCE:
        return check_fence(cut, result);
    case FWLAB_P_PLP:
        return check_plp(cut, event, physical, recovered, result);
    case FWLAB_P_CONSERVE:
        return check_conserve(cut, result);
    case FWLAB_P_NO_HOST_AUTHORITY:
        return check_no_host(cut, physical, recovered, result);
    default:
        return 0;
    }
}
