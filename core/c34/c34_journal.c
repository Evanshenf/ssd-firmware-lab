/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_internal.h"

#include <string.h>

static enum c34_result take_record_identity(
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

enum c34_result c34_build_data_record(
    struct c34 *instance,
    struct c34_mutation *mutation,
    const struct fwlab_nfc_ppa *ppa,
    uint8_t copy_sequence,
    const uint8_t payload[C34_ATOM_BYTES],
    struct c34_record *record
)
{
    enum c34_result result;

    if (!c34_instance_valid(instance) || mutation == NULL || ppa == NULL ||
        payload == NULL || record == NULL || mutation->atom >= C34_ATOMS ||
        copy_sequence > 1 || ppa->block >= C34_DATA_BLOCKS ||
        ppa->page >= C34_PAGES_PER_BLOCK ||
        instance->blocks[ppa->block].health != FWLAB_NFC_BLOCK_GOOD) {
        return C34_INVALID_CONTRACT;
    }
    memset(record, 0, sizeof(*record));
    result = take_record_identity(
        instance, &record->record_id, &record->commit_sequence);
    if (result != C34_OK) {
        return result;
    }
    record->type = C34_RECORD_DATA;
    record->atom = mutation->atom;
    record->logical_version = mutation->target_version;
    record->copy_sequence = copy_sequence;
    record->logical_state_id = mutation->logical_state_id;
    record->predecessor_state_id = mutation->predecessor_state_id;
    record->mutation_id = mutation->mutation_id;
    record->erase_generation =
        instance->blocks[ppa->block].erase_generation;
    record->payload_length = C34_ATOM_BYTES;
    record->target_ppa = *ppa;
    record->target_erase_generation = record->erase_generation;
    memcpy(record->data, payload, C34_ATOM_BYTES);
    record->value_crc32c = c34_crc32c(payload, C34_ATOM_BYTES);
    mutation->data_record_id = record->record_id;
    mutation->data_ppa = *ppa;
    mutation->data_erase_generation = record->erase_generation;
    mutation->copy_sequence = copy_sequence;
    return C34_OK;
}

enum c34_result c34_build_mapping_record(
    struct c34 *instance,
    const struct c34_mutation *mutation,
    struct c34_record *record
)
{
    const struct c34_logical_entry *current;
    struct fwlab_nfc_ppa journal;
    enum c34_result result;

    if (!c34_instance_valid(instance) || mutation == NULL || record == NULL ||
        mutation->atom >= C34_ATOMS || mutation->data_record_id == 0 ||
        c34_allocate_journal(instance, &journal) != C34_OK) {
        return C34_INVALID_CONTRACT;
    }
    current = &instance->l2p[mutation->atom];
    memset(record, 0, sizeof(*record));
    result = take_record_identity(
        instance, &record->record_id, &record->commit_sequence);
    if (result != C34_OK) {
        return result;
    }
    record->type = C34_RECORD_MAP;
    record->atom = mutation->atom;
    record->logical_version = mutation->target_version;
    record->copy_sequence = 0;
    record->logical_state_id = mutation->logical_state_id;
    record->predecessor_state_id = current->logical_state_id;
    record->mutation_id = mutation->mutation_id;
    record->value_crc32c = c34_crc32c(
        mutation->payload, C34_ATOM_BYTES);
    record->erase_generation =
        instance->blocks[C34_JOURNAL_BLOCK].erase_generation;
    record->payload_length = 64;
    record->target_ppa = mutation->data_ppa;
    record->target_erase_generation = mutation->data_erase_generation;
    record->target_data_record_id = mutation->data_record_id;
    record->source_authority_record_id = current->authority_record_id;
    return C34_OK;
}

enum c34_result c34_build_tombstone_record(
    struct c34 *instance,
    const struct c34_mutation *mutation,
    struct c34_record *record
)
{
    const struct c34_logical_entry *current;
    struct fwlab_nfc_ppa journal;
    enum c34_result result;

    if (!c34_instance_valid(instance) || mutation == NULL || record == NULL ||
        mutation->atom >= C34_ATOMS ||
        c34_allocate_journal(instance, &journal) != C34_OK) {
        return C34_INVALID_CONTRACT;
    }
    current = &instance->l2p[mutation->atom];
    memset(record, 0, sizeof(*record));
    result = take_record_identity(
        instance, &record->record_id, &record->commit_sequence);
    if (result != C34_OK) {
        return result;
    }
    record->type = C34_RECORD_TOMBSTONE;
    record->atom = mutation->atom;
    record->logical_version = mutation->target_version;
    record->copy_sequence = 0;
    record->logical_state_id = mutation->logical_state_id;
    record->predecessor_state_id = current->logical_state_id;
    record->mutation_id = mutation->mutation_id;
    record->erase_generation =
        instance->blocks[C34_JOURNAL_BLOCK].erase_generation;
    record->payload_length = 0;
    return C34_OK;
}

enum c34_result c34_build_relocation_record(
    struct c34 *instance,
    const struct c34_mutation *mutation,
    const struct c34_logical_entry *source,
    struct c34_record *record
)
{
    struct fwlab_nfc_ppa journal;
    enum c34_result result;

    if (!c34_instance_valid(instance) || mutation == NULL || source == NULL ||
        record == NULL || source->kind != C34_LOGICAL_VALUE ||
        mutation->copy_sequence != 1 ||
        c34_allocate_journal(instance, &journal) != C34_OK) {
        return C34_INVALID_CONTRACT;
    }
    memset(record, 0, sizeof(*record));
    result = take_record_identity(
        instance, &record->record_id, &record->commit_sequence);
    if (result != C34_OK) {
        return result;
    }
    record->type = C34_RECORD_RELOCATION;
    record->atom = mutation->atom;
    record->logical_version = mutation->target_version;
    record->copy_sequence = 1;
    record->logical_state_id = source->logical_state_id;
    record->predecessor_state_id = source->logical_state_id;
    record->mutation_id = mutation->mutation_id;
    record->value_crc32c = source->value_crc32c;
    record->erase_generation =
        instance->blocks[C34_JOURNAL_BLOCK].erase_generation;
    record->payload_length = 64;
    record->target_ppa = mutation->data_ppa;
    record->target_erase_generation = mutation->data_erase_generation;
    record->source_ppa = source->data_ppa;
    record->source_erase_generation = source->data_erase_generation;
    record->target_data_record_id = mutation->data_record_id;
    record->source_authority_record_id = source->authority_record_id;
    return C34_OK;
}
