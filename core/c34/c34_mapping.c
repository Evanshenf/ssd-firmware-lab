/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_internal.h"

#include <stdalign.h>
#include <string.h>

int c34_instance_valid(const struct c34 *instance)
{
    return instance != NULL && instance->magic == C34_MAGIC &&
           instance->phase <= 2;
}

int c34_outer_equal(
    const struct fwlab_c31_operation_token *left,
    const struct fwlab_c31_operation_token *right
)
{
    return left->command.instance_nonce == right->command.instance_nonce &&
           left->command.command_uid == right->command.command_uid &&
           left->command.controller_epoch ==
               right->command.controller_epoch &&
           left->command.slot == right->command.slot &&
           left->command.slot_generation ==
               right->command.slot_generation &&
           left->cookie == right->cookie &&
           left->operation_generation == right->operation_generation &&
           left->reserved == right->reserved;
}

int c34_inner_equal(
    const struct fwlab_nfc_operation_token *left,
    const struct fwlab_nfc_operation_token *right
)
{
    return left->instance_nonce == right->instance_nonce &&
           left->operation_uid == right->operation_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->generation == right->generation;
}

static int config_valid(const struct c34_config *config)
{
    return config != NULL && config->version == C34_CONTRACT_VERSION &&
           config->size == sizeof(*config) && config->reserved0 == 0 &&
           config->reserved1 == 0 && config->instance_nonce != 0 &&
           config->controller_epoch == 1 &&
           config->controller_region != 0 &&
           config->controller_buffer_length >=
               C34_MAIN_BYTES + C34_OOB_BYTES &&
           (uint64_t)config->controller_buffer_offset +
                   config->controller_buffer_length <=
               UINT32_MAX &&
           config->persistence.plp_kind == FWLAB_PERSIST_PLP_NONE &&
           fwlab_persist_profile_validate(&config->persistence) ==
               FWLAB_PERSIST_OK &&
           config->inner_uid_limit >= 32 &&
           config->inner_uid_limit <= UINT32_MAX &&
           config->physical_op_limit >= 16 &&
           config->physical_sequence_limit >= 16;
}

static int bindings_valid(
    const struct fwlab_nfc_buffer_provider *buffers,
    const struct fwlab_nfc_provider *nfc,
    const struct fwlab_nand_media *raw_media,
    const struct c34_physical_txn_provider *physical
)
{
    return buffers != NULL && buffers->ops != NULL &&
           buffers->context != NULL &&
           buffers->ops->version == FWLAB_NFC_CONTRACT_VERSION &&
           buffers->ops->size == sizeof(*buffers->ops) &&
           buffers->ops->reserved == 0 && buffers->ops->read != NULL &&
           buffers->ops->write != NULL && nfc != NULL && nfc->ops != NULL &&
           nfc->context != NULL &&
           nfc->ops->version == FWLAB_NFC_CONTRACT_VERSION &&
           nfc->ops->size == sizeof(*nfc->ops) && nfc->ops->reserved == 0 &&
           nfc->ops->try_submit != NULL && nfc->ops->cancel != NULL &&
           nfc->ops->step != NULL && nfc->ops->poll != NULL &&
           nfc->ops->reset_begin != NULL && nfc->ops->quiescent != NULL &&
           raw_media != NULL && raw_media->ops != NULL &&
           raw_media->context != NULL && raw_media->ops->read_page != NULL &&
           physical != NULL && physical->ops != NULL &&
           physical->context != NULL &&
           physical->ops->version == C34_PHYSICAL_TXN_VERSION &&
           physical->ops->size == sizeof(*physical->ops) &&
           physical->ops->reserved == 0 && physical->ops->bind != NULL &&
           physical->ops->abandon != NULL &&
           physical->ops->receipt != NULL &&
           physical->ops->quiescent != NULL;
}

size_t c34_arena_alignment(void)
{
    return alignof(max_align_t);
}

size_t c34_arena_size(const struct c34_config *config)
{
    size_t alignment = alignof(max_align_t);
    size_t size = sizeof(struct c34);

    if (!config_valid(config) || size > SIZE_MAX - (alignment - 1u)) {
        return 0;
    }
    return (size + alignment - 1u) & ~(alignment - 1u);
}

enum c34_result c34_init(
    void *arena,
    size_t arena_size,
    const struct c34_config *config,
    const struct fwlab_nfc_buffer_provider *buffers,
    const struct fwlab_nfc_provider *nfc,
    const struct fwlab_nand_media *raw_media,
    const struct c34_physical_txn_provider *physical,
    struct c34 **instance_out
)
{
    struct c34 *instance;
    enum c34_result result;

    if (arena == NULL || instance_out == NULL ||
        (uintptr_t)arena % alignof(max_align_t) != 0 ||
        arena_size < c34_arena_size(config) || !config_valid(config) ||
        !bindings_valid(buffers, nfc, raw_media, physical)) {
        return C34_INVALID_CONTRACT;
    }
    memset(arena, 0, arena_size);
    instance = arena;
    instance->magic = C34_MAGIC;
    instance->config = *config;
    instance->buffers = *buffers;
    instance->nfc = *nfc;
    instance->raw_media = *raw_media;
    instance->physical = *physical;
    instance->current_epoch = config->controller_epoch;
    instance->next_inner_uid = 1;
    instance->next_physical_op_id = 1;
    instance->next_physical_sequence = 1;
    instance->next_record_id = 1;
    instance->next_logical_state_id = 1;
    instance->next_commit_sequence = 1;
    result = c34_refresh(instance, true);
    if (result != C34_OK) {
        memset(instance, 0, sizeof(*instance));
        return result;
    }
    *instance_out = instance;
    return C34_OK;
}

enum c34_result c34_logical_state(
    const struct c34 *instance,
    uint8_t atom,
    struct c34_logical_entry *entry
)
{
    if (!c34_instance_valid(instance) || atom >= C34_ATOMS || entry == NULL) {
        return C34_INVALID_CONTRACT;
    }
    *entry = instance->l2p[atom];
    return C34_OK;
}

enum c34_result c34_allocate_data(
    struct c34 *instance,
    struct fwlab_nfc_ppa *ppa
)
{
    unsigned int index;

    if (!c34_instance_valid(instance) || ppa == NULL) {
        return C34_INVALID_CONTRACT;
    }
    for (index = 0; index < C34_DATA_PAGES; ++index) {
        if (instance->p2l[index].kind != C34_P2L_FREE) {
            continue;
        }
        *ppa = c34_ppa((uint16_t)(index / C34_PAGES_PER_BLOCK),
                       (uint16_t)(index % C34_PAGES_PER_BLOCK));
        instance->p2l[index].kind = C34_P2L_RESERVED;
        return C34_OK;
    }
    return C34_NO_CAPACITY;
}

enum c34_result c34_allocate_journal(
    const struct c34 *instance,
    struct fwlab_nfc_ppa *ppa
)
{
    uint16_t page;

    if (!c34_instance_valid(instance) || ppa == NULL) {
        return C34_INVALID_CONTRACT;
    }
    page = instance->blocks[C34_JOURNAL_BLOCK].next_program_page;
    if (instance->blocks[C34_JOURNAL_BLOCK].health !=
            FWLAB_NFC_BLOCK_GOOD ||
        instance->blocks[C34_JOURNAL_BLOCK].erase_state !=
            FWLAB_NAND_ERASE_CLEAN ||
        page >= C34_PAGES_PER_BLOCK) {
        return C34_NO_CAPACITY;
    }
    *ppa = c34_ppa(C34_JOURNAL_BLOCK, page);
    return C34_OK;
}

enum c34_result c34_choose_relocation(
    const struct c34 *instance,
    uint8_t *atom,
    struct fwlab_nfc_ppa *source,
    struct fwlab_nfc_ppa *destination
)
{
    int reserve = -1;
    unsigned int block;

    if (!c34_instance_valid(instance) || atom == NULL || source == NULL ||
        destination == NULL) {
        return C34_INVALID_CONTRACT;
    }
    for (block = 0; block < C34_DATA_BLOCKS; ++block) {
        unsigned int page;
        int all_reserved = 1;

        for (page = 0; page < C34_PAGES_PER_BLOCK; ++page) {
            if (instance->p2l[block * C34_PAGES_PER_BLOCK + page].kind !=
                C34_P2L_RESERVED) {
                all_reserved = 0;
            }
        }
        if (all_reserved) {
            reserve = (int)block;
        }
    }
    if (reserve < 0) {
        return C34_NO_CAPACITY;
    }
    for (block = 0; block < C34_DATA_BLOCKS; ++block) {
        unsigned int page;
        unsigned int live_count = 0;
        unsigned int live_page = 0;

        if ((int)block == reserve) {
            continue;
        }
        for (page = 0; page < C34_PAGES_PER_BLOCK; ++page) {
            if (instance->p2l[block * C34_PAGES_PER_BLOCK + page].kind ==
                C34_P2L_LIVE) {
                ++live_count;
                live_page = page;
            }
        }
        if (live_count == 1) {
            const struct c34_p2l_entry *entry =
                &instance->p2l[block * C34_PAGES_PER_BLOCK + live_page];

            if (entry->copy_sequence != 0 || entry->atom >= C34_ATOMS) {
                continue;
            }
            *atom = entry->atom;
            *source = c34_ppa((uint16_t)block, (uint16_t)live_page);
            *destination = c34_ppa((uint16_t)reserve, 0);
            return C34_OK;
        }
    }
    return C34_NOT_FOUND;
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
    uint8_t bytes[4];

    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
    return c34_hash_bytes(hash, bytes, sizeof(bytes));
}

uint64_t c34_state_hash(const struct c34 *instance)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    unsigned int atom;
    unsigned int page;

    if (!c34_instance_valid(instance)) {
        return 0;
    }
    hash = hash_u32(hash, instance->current_epoch);
    hash = hash_u32(hash, instance->checkpoint_generation);
    hash = hash_u32(hash, instance->checkpoint_watermark);
    hash = hash_u32(hash, instance->next_record_id);
    hash = hash_u32(hash, instance->next_logical_state_id);
    hash = hash_u32(hash, instance->next_commit_sequence);
    hash = hash_u32(hash, instance->last_sequence);
    for (atom = 0; atom < C34_ATOMS; ++atom) {
        const struct c34_logical_entry *entry = &instance->l2p[atom];

        hash = hash_u32(hash, entry->kind);
        hash = hash_u32(hash, entry->version);
        hash = hash_u32(hash, entry->copy_sequence);
        hash = hash_u32(hash, entry->logical_state_id);
        hash = hash_u32(hash, entry->authority_record_id);
        hash = hash_u32(hash, entry->data_record_id);
        hash = hash_u32(hash, c34_page_index(&entry->data_ppa));
        hash = hash_u32(hash, entry->data_erase_generation);
        hash = hash_u32(hash, entry->value_crc32c);
        hash = hash_u32(hash, instance->overlay_valid[atom]);
        hash = hash_u32(hash, instance->overlay_kind[atom]);
        hash = c34_hash_bytes(hash, instance->overlay_payload[atom],
                              C34_ATOM_BYTES);
    }
    for (page = 0; page < C34_DATA_PAGES; ++page) {
        hash = hash_u32(hash, instance->p2l[page].kind);
        hash = hash_u32(hash, instance->p2l[page].data_record_id);
    }
    hash = hash_u32(hash, instance->graph.kind);
    hash = hash_u32(hash, instance->graph.state);
    return hash;
}

enum c34_result c34_maintenance_quiescent(
    const struct c34 *instance,
    bool *quiescent
)
{
    bool physical_quiescent;

    if (!c34_instance_valid(instance) || quiescent == NULL ||
        instance->physical.ops->quiescent(
            instance->physical.context, &physical_quiescent) !=
            C34_PHYSICAL_TXN_OK) {
        return C34_INVALID_CONTRACT;
    }
    *quiescent = instance->graph.kind == C34_GRAPH_NONE &&
                 physical_quiescent;
    return C34_OK;
}
