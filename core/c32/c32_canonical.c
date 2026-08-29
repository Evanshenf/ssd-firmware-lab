/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c32_internal.h"

#include <string.h>

struct encoder {
    uint8_t *bytes;
    size_t capacity;
    size_t length;
    int failed;
};

static void put_u8(struct encoder *encoder, uint8_t value)
{
    if (encoder->length >= encoder->capacity) {
        encoder->failed = 1;
        return;
    }
    encoder->bytes[encoder->length++] = value;
}

static void put_u16(struct encoder *encoder, uint16_t value)
{
    put_u8(encoder, (uint8_t)value);
    put_u8(encoder, (uint8_t)(value >> 8));
}

static void put_u32(struct encoder *encoder, uint32_t value)
{
    put_u16(encoder, (uint16_t)value);
    put_u16(encoder, (uint16_t)(value >> 16));
}

static void put_u64(struct encoder *encoder, uint64_t value)
{
    put_u32(encoder, (uint32_t)value);
    put_u32(encoder, (uint32_t)(value >> 32));
}

static void put_ref(struct encoder *encoder, const struct c32_phys_ref *ref)
{
    put_u8(encoder, ref->group);
    put_u8(encoder, ref->slot);
    put_u8(encoder, ref->erase_generation);
    put_u8(encoder, ref->valid);
}

static void put_logical(
    struct encoder *encoder,
    const struct c32_logical_state *state
)
{
    put_u8(encoder, state->kind);
    put_u8(encoder, state->atom);
    put_u8(encoder, state->version);
    put_u8(encoder, state->copy_discriminator);
    put_u8(encoder, state->value_token);
    put_u8(encoder, state->reserved0);
    put_u16(encoder, state->state_id);
    put_u16(encoder, state->predecessor_state_id);
    put_u16(encoder, state->authority_record_id);
    put_ref(encoder, &state->data_ref);
}

static void put_record(
    struct encoder *encoder,
    const struct c32_persistent_record *record
)
{
    put_u8(encoder, record->presence);
    put_u8(encoder, record->kind);
    put_u8(encoder, record->atom);
    put_u8(encoder, record->logical_version);
    put_u8(encoder, record->value_token);
    put_u8(encoder, record->predecessor_version);
    put_u8(encoder, record->copy_discriminator);
    put_u8(encoder, record->body_complete);
    put_u8(encoder, record->checksum_ok);
    put_u8(encoder, record->c_applied);
    put_u16(encoder, record->record_id);
    put_u16(encoder, record->mutation_id);
    put_u16(encoder, record->predecessor_state_id);
    put_u16(encoder, record->c_sequence);
    put_ref(encoder, &record->self);
    put_ref(encoder, &record->data_ref);
    put_ref(encoder, &record->source_ref);
}

static void put_checkpoint(
    struct encoder *encoder,
    const struct c32_checkpoint *checkpoint
)
{
    unsigned int atom;

    put_u8(encoder, checkpoint->image_state);
    put_u8(encoder, checkpoint->generation);
    put_u8(encoder, checkpoint->body_complete);
    put_u8(encoder, checkpoint->checksum_ok);
    put_u8(encoder, checkpoint->c_applied);
    put_u8(encoder, checkpoint->provenance_ok);
    put_u16(encoder, checkpoint->watermark);
    put_u16(encoder, checkpoint->c_sequence);
    put_u16(encoder, checkpoint->reserved0);
    put_u64(encoder, checkpoint->payload_hash);
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        put_logical(encoder, &checkpoint->entry[atom]);
    }
}

static void put_anchor(struct encoder *encoder, const struct c32_anchor *anchor)
{
    put_u8(encoder, anchor->image_state);
    put_u8(encoder, anchor->generation);
    put_u8(encoder, anchor->target_slot);
    put_u8(encoder, anchor->body_complete);
    put_u8(encoder, anchor->checksum_ok);
    put_u8(encoder, anchor->c_applied);
    put_u16(encoder, anchor->watermark);
    put_u16(encoder, anchor->c_sequence);
    put_u16(encoder, anchor->reserved0);
    put_u64(encoder, anchor->checkpoint_hash);
}

static void put_profile(
    struct encoder *encoder,
    const struct fwlab_persist_profile *profile
)
{
    put_u16(encoder, profile->version);
    put_u16(encoder, profile->size);
    put_u32(encoder, profile->reserved0);
    put_u8(encoder, profile->cache_enabled);
    put_u8(encoder, profile->plp_kind);
    put_u8(encoder, profile->plp_capacity_credits);
    put_u8(encoder, profile->survival_event_mask);
    put_u32(encoder, profile->reserved1);
}

size_t c32_logical_image_encode(
    const struct c32_logical_image *image,
    uint8_t *bytes,
    size_t capacity
)
{
    struct encoder encoder = {bytes, capacity, 0, 0};
    unsigned int group;
    unsigned int slot;
    unsigned int index;

    if (image == NULL || bytes == NULL) {
        return 0;
    }
    put_u8(&encoder, (uint8_t)'C');
    put_u8(&encoder, (uint8_t)'3');
    put_u8(&encoder, (uint8_t)'2');
    put_u8(&encoder, (uint8_t)'I');
    for (group = 0; group < C32_GROUPS; ++group) {
        put_u8(&encoder, image->erase_generation[group]);
    }
    put_u8(&encoder, image->reserved0);
    for (group = 0; group < C32_GROUPS; ++group) {
        for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
            put_record(&encoder, &image->media[group][slot]);
        }
    }
    for (index = 0; index < C32_CHECKPOINTS; ++index) {
        put_checkpoint(&encoder, &image->checkpoint[index]);
    }
    for (index = 0; index < C32_ANCHORS; ++index) {
        put_anchor(&encoder, &image->anchor[index]);
    }
    for (index = 0; index < C32_ATOMS; ++index) {
        put_logical(&encoder, &image->genesis[index]);
    }
    return encoder.failed ? 0 : encoder.length;
}

static void put_operation(
    struct encoder *encoder,
    const struct c32_physical_op *operation
)
{
    put_u8(encoder, operation->phase);
    put_u8(encoder, operation->purpose);
    put_u8(encoder, operation->frozen_outcome);
    put_u8(encoder, operation->target_domain);
    put_u8(encoder, operation->target_group);
    put_u8(encoder, operation->target_slot);
    put_u16(encoder, operation->op_id);
    put_u16(encoder, operation->owner_epoch);
    put_u16(encoder, operation->begin_order);
    put_u16(encoder, operation->commit_sequence);
    put_u8(encoder, operation->outcome_delivered);
    put_u8(encoder, operation->reserved0);
    put_record(encoder, &operation->frozen_record);
    put_checkpoint(encoder, &operation->frozen_checkpoint);
    put_anchor(encoder, &operation->frozen_anchor);
}

static void put_plp(struct encoder *encoder, const struct c32_plp_envelope *plp)
{
    unsigned int atom;

    put_u8(encoder, plp->state);
    put_u8(encoder, plp->atom_mask);
    put_u8(encoder, plp->capacity_cost);
    put_u8(encoder, plp->flags);
    put_u8(encoder, plp->survival_event_mask);
    put_u8(encoder, plp->drained_atom_mask);
    put_u8(encoder, plp->drain_budget_reserved);
    put_u8(encoder, plp->reserved0);
    put_u16(encoder, plp->envelope_id);
    put_u16(encoder, plp->mutation_id);
    put_u16(encoder, plp->owner_epoch);
    put_u16(encoder, plp->accept_sequence);
    put_u16(encoder, plp->persistent_order);
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        put_u8(encoder, plp->atom[atom].atom);
        put_u8(encoder, plp->atom[atom].version);
        put_u8(encoder, plp->atom[atom].target_kind);
        put_u8(encoder, plp->atom[atom].predecessor_version);
        put_u8(encoder, plp->atom[atom].value_token);
        put_u8(encoder, plp->atom[atom].reserved0[0]);
        put_u8(encoder, plp->atom[atom].reserved0[1]);
        put_u8(encoder, plp->atom[atom].reserved0[2]);
        put_u16(encoder, plp->atom[atom].predecessor_state_id);
        put_u16(encoder, plp->atom[atom].reserved1);
        put_ref(encoder, &plp->atom[atom].drain_data_ref);
        put_ref(encoder, &plp->atom[atom].drain_metadata_ref);
    }
}

static void put_mutation(
    struct encoder *encoder,
    const struct c32_mutation *mutation
)
{
    unsigned int atom;

    put_u8(encoder, mutation->used);
    put_u8(encoder, mutation->atom_mask);
    put_u8(encoder, mutation->request_kind);
    put_u8(encoder, mutation->publication);
    put_u16(encoder, mutation->mutation_id);
    put_u16(encoder, mutation->command_id);
    put_u16(encoder, mutation->owner_epoch);
    put_u16(encoder, mutation->accept_sequence);
    put_u16(encoder, mutation->scope);
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        put_u8(encoder, mutation->target_kind[atom]);
        put_u8(encoder, mutation->target_version[atom]);
        put_u8(encoder, mutation->predecessor_version[atom]);
        put_u8(encoder, mutation->value_token[atom]);
        put_u8(encoder, mutation->closure[atom]);
    }
}

size_t c32_state_encode(
    const struct c32_model_state *state,
    uint8_t *bytes,
    size_t capacity
)
{
    struct encoder encoder = {bytes, capacity, 0, 0};
    unsigned int group;
    unsigned int slot;
    unsigned int index;

    if (state == NULL || bytes == NULL) {
        return 0;
    }
    put_u8(&encoder, (uint8_t)'C');
    put_u8(&encoder, (uint8_t)'3');
    put_u8(&encoder, (uint8_t)'2');
    put_u8(&encoder, (uint8_t)'S');
    put_profile(&encoder, &state->profile);
    put_u8(&encoder, state->phase);
    put_u8(&encoder, state->current_epoch);
    put_u8(&encoder, state->next_accept_sequence);
    put_u8(&encoder, state->next_begin_order);
    put_u8(&encoder, state->next_commit_sequence);
    put_u8(&encoder, state->cut_used);
    for (group = 0; group < C32_GROUPS; ++group) {
        put_u8(&encoder, state->erase_generation[group]);
    }
    put_u8(&encoder, state->reserved0);
    put_u16(&encoder, state->free_bitmap);
    put_u16(&encoder, state->reserved_bitmap);
    put_u8(&encoder, state->scenario_family);
    put_u8(&encoder, state->scenario_profile);
    put_u8(&encoder, state->scenario_variant);
    put_u8(&encoder, state->scenario_request);
    put_u32(&encoder, state->grammar_progress);
    put_u32(&encoder, state->grammar_choice);
    put_u8(&encoder, state->host_adversarial_mask);
    put_u8(&encoder, state->reserved1[0]);
    put_u8(&encoder, state->reserved1[1]);
    put_u8(&encoder, state->reserved1[2]);
    for (group = 0; group < C32_GROUPS; ++group) {
        for (slot = 0; slot < C32_SLOTS_PER_GROUP; ++slot) {
            put_record(&encoder, &state->media[group][slot]);
        }
    }
    for (index = 0; index < C32_PHYSICAL_OPS; ++index) {
        put_operation(&encoder, &state->inflight[index]);
    }
    for (index = 0; index < C32_CHECKPOINTS; ++index) {
        put_checkpoint(&encoder, &state->checkpoint[index]);
    }
    for (index = 0; index < C32_ANCHORS; ++index) {
        put_anchor(&encoder, &state->anchor[index]);
    }
    for (index = 0; index < C32_PLP_SLOTS; ++index) {
        put_plp(&encoder, &state->plp[index]);
    }
    for (index = 0; index < C32_MUTATIONS; ++index) {
        put_mutation(&encoder, &state->mutation[index]);
    }
    put_u8(&encoder, state->fence.used);
    put_u8(&encoder, state->fence.success);
    put_u16(&encoder, state->fence.fence_id);
    put_u16(&encoder, state->fence.owner_epoch);
    put_u16(&encoder, state->fence.scope);
    put_u16(&encoder, state->fence.frontier);
    put_u8(&encoder, state->fence.covered_mutation_mask);
    put_u8(&encoder, state->fence.reserved0);
    put_u8(&encoder, state->gc.used);
    put_u8(&encoder, state->gc.atom);
    put_u8(&encoder, state->gc.version);
    put_u8(&encoder, state->gc.stage);
    put_u8(&encoder, state->gc.lease_held);
    put_u8(&encoder, state->gc.reserved0[0]);
    put_u8(&encoder, state->gc.reserved0[1]);
    put_u8(&encoder, state->gc.reserved0[2]);
    put_ref(&encoder, &state->gc.source);
    put_ref(&encoder, &state->gc.destination);
    for (index = 0; index < C32_ATOMS; ++index) {
        put_logical(&encoder, &state->firmware_ram[index]);
        put_logical(&encoder, &state->host_cache[index]);
        put_logical(&encoder, &state->durable_floor[index]);
        put_logical(&encoder, &state->genesis[index]);
    }
    return encoder.failed ? 0 : encoder.length;
}

uint64_t c32_hash_bytes(const uint8_t *bytes, size_t length)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    for (index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

uint64_t c32_state_hash(const struct c32_model_state *state)
{
    uint8_t bytes[C32_CANONICAL_BYTES];
    size_t length = c32_state_encode(state, bytes, sizeof(bytes));

    return length == 0 ? 0 : c32_hash_bytes(bytes, length);
}

uint64_t c32_logical_image_hash(const struct c32_logical_image *image)
{
    uint8_t bytes[C32_CANONICAL_BYTES];
    size_t length = c32_logical_image_encode(image, bytes, sizeof(bytes));

    return length == 0 ? 0 : c32_hash_bytes(bytes, length);
}

uint64_t c32_recovery_hash(const struct c32_recovery_result *result)
{
    struct encoder encoder;
    uint8_t bytes[128];
    unsigned int atom;

    if (result == NULL) {
        return 0;
    }
    memset(&encoder, 0, sizeof(encoder));
    encoder.bytes = bytes;
    encoder.capacity = sizeof(bytes);
    put_u8(&encoder, result->status);
    put_u8(&encoder, result->selected_checkpoint);
    put_u16(&encoder, result->watermark);
    for (atom = 0; atom < C32_ATOMS; ++atom) {
        put_logical(&encoder, &result->atom[atom]);
    }
    return encoder.failed ? 0 : c32_hash_bytes(bytes, encoder.length);
}
