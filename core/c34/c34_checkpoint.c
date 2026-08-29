/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_internal.h"

#include <string.h>

static enum c34_result take_identity(
    struct c34 *instance,
    uint32_t *record_id,
    uint32_t *commit_sequence
)
{
    if (instance->next_record_id == 0 ||
        instance->next_record_id > C34_RECORD_LIMIT ||
        instance->next_commit_sequence == 0 ||
        instance->next_commit_sequence > C34_RECORD_LIMIT) {
        return C34_COUNTER_EXHAUSTED;
    }
    *record_id = instance->next_record_id++;
    *commit_sequence = instance->next_commit_sequence++;
    return C34_OK;
}

enum c34_result c34_build_checkpoint_record(
    struct c34 *instance,
    uint32_t generation,
    struct c34_record *record
)
{
    unsigned int atom;
    enum c34_result result;
    uint16_t block;

    if (!c34_instance_valid(instance) || record == NULL || generation == 0 ||
        generation > C34_CHECKPOINT_LIMIT ||
        generation != instance->checkpoint_generation + 1u ||
        instance->next_record_id >= C34_RECORD_LIMIT) {
        return C34_INVALID_CONTRACT;
    }
    block = (uint16_t)(C34_CHECKPOINT_BLOCK0 + generation - 1u);
    memset(record, 0, sizeof(*record));
    result = take_identity(
        instance, &record->record_id, &record->commit_sequence);
    if (result != C34_OK) {
        return result;
    }
    record->type = C34_RECORD_CHECKPOINT;
    record->atom = 0xff;
    record->logical_version = 0xff;
    record->copy_sequence = 0xff;
    record->erase_generation = instance->blocks[block].erase_generation;
    record->payload_length = C34_MAIN_BYTES;
    record->checkpoint_generation = generation;
    record->covered_commit_sequence = record->commit_sequence - 1u;
    record->next_record_id = instance->next_record_id + 1u;
    record->next_logical_state_id = instance->next_logical_state_id;
    for (atom = 0; atom < C34_ATOMS; ++atom) {
        const struct c34_logical_entry *source = &instance->l2p[atom];
        struct c34_checkpoint_entry *target = &record->checkpoint[atom];

        target->atom = (uint8_t)atom;
        target->kind = source->kind;
        target->version = source->version;
        target->copy_sequence = source->copy_sequence;
        target->logical_state_id = source->logical_state_id;
        target->authority_record_id = source->authority_record_id;
        target->data_record_id = source->data_record_id;
        target->data_ppa = source->data_ppa;
        target->data_erase_generation = source->data_erase_generation;
        target->value_crc32c = source->value_crc32c;
    }
    return C34_OK;
}

enum c34_result c34_build_anchor_record(
    struct c34 *instance,
    const struct c34_record *checkpoint,
    uint8_t slot,
    uint32_t payload_crc,
    struct c34_record *record
)
{
    uint16_t block = (uint16_t)(C34_CHECKPOINT_BLOCK0 + slot);
    enum c34_result result;

    if (!c34_instance_valid(instance) || checkpoint == NULL ||
        record == NULL || checkpoint->type != C34_RECORD_CHECKPOINT ||
        slot >= 2 || checkpoint->checkpoint_generation != slot + 1u) {
        return C34_INVALID_CONTRACT;
    }
    memset(record, 0, sizeof(*record));
    result = take_identity(
        instance, &record->record_id, &record->commit_sequence);
    if (result != C34_OK) {
        return result;
    }
    record->type = C34_RECORD_ANCHOR;
    record->atom = 0xff;
    record->logical_version = 0xff;
    record->copy_sequence = 0xff;
    record->erase_generation = instance->blocks[block].erase_generation;
    record->payload_length = 32;
    record->checkpoint_generation = checkpoint->checkpoint_generation;
    record->checkpoint_slot = slot;
    record->checkpoint_ppa = c34_ppa(block, 0);
    record->checkpoint_erase_generation =
        instance->blocks[block].erase_generation;
    record->checkpoint_record_id = checkpoint->record_id;
    record->checkpoint_payload_crc32c = payload_crc;
    record->covered_commit_sequence =
        checkpoint->covered_commit_sequence;
    return C34_OK;
}
