/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_internal.h"

#include <string.h>

struct c34_decoded_page {
    uint8_t valid;
    uint8_t reserved[3];
    struct c34_record record;
    struct fwlab_nfc_ppa ppa;
};

static int block_info_equal(
    const struct fwlab_nand_block_info *left,
    const struct fwlab_nand_block_info *right
)
{
    return memcmp(left, right, sizeof(*left)) == 0;
}

enum c34_result c34_scan_raw_media(
    const struct fwlab_nand_media *media,
    struct c34_raw_image *image
)
{
    uint16_t block;

    if (media == NULL || media->ops == NULL || media->context == NULL ||
        image == NULL ||
        media->ops->version != FWLAB_NFC_CONTRACT_VERSION ||
        media->ops->size != sizeof(*media->ops) ||
        media->ops->reserved != 0 || media->ops->read_page == NULL) {
        return C34_INVALID_CONTRACT;
    }
    memset(image, 0, sizeof(*image));
    for (block = 0; block < C34_BLOCKS; ++block) {
        uint16_t page;

        for (page = 0; page < C34_PAGES_PER_BLOCK; ++page) {
            struct fwlab_nfc_ppa ppa = c34_ppa(block, page);
            struct fwlab_nand_block_info block_info;
            struct c34_raw_page *raw =
                &image->pages[(uint32_t)block * C34_PAGES_PER_BLOCK + page];

            memset(&block_info, 0, sizeof(block_info));
            if (media->ops->read_page(
                    media->context, &ppa, raw->main, C34_MAIN_BYTES,
                    raw->oob, C34_OOB_BYTES, &raw->page, &block_info) !=
                FWLAB_NFC_API_OK) {
                return C34_MEDIA_FAILURE;
            }
            if (page == 0) {
                image->blocks[block] = block_info;
            } else if (!block_info_equal(&image->blocks[block],
                                         &block_info)) {
                return C34_CORRUPT;
            }
        }
    }
    return C34_OK;
}

static enum c34_record_decode_result decode_page(
    const struct c34_raw_image *image,
    uint16_t block,
    uint16_t page,
    struct c34_record *record
)
{
    const struct c34_raw_page *raw =
        &image->pages[(uint32_t)block * C34_PAGES_PER_BLOCK + page];
    enum c34_record_decode_result result = c34_record_decode(
        raw->main, raw->oob, &raw->page, &image->blocks[block], record);

    if (result == C34_DECODE_OK && record->type == C34_RECORD_DATA) {
        record->target_ppa = c34_ppa(block, page);
        record->target_erase_generation =
            image->blocks[block].erase_generation;
    }
    return result;
}

static int data_matches(
    const struct c34_decoded_page data[C34_DATA_PAGES],
    const struct fwlab_nfc_ppa *ppa,
    uint16_t erase_generation,
    uint8_t atom,
    uint8_t version,
    uint8_t copy_sequence,
    uint32_t logical_state_id,
    uint32_t data_record_id,
    uint32_t value_crc32c,
    uint32_t before_sequence
)
{
    uint32_t index;
    const struct c34_record *record;

    if (ppa->channel != 0 || ppa->lun != 0 || ppa->plane != 0 ||
        ppa->block >= C34_DATA_BLOCKS ||
        ppa->page >= C34_PAGES_PER_BLOCK || ppa->reserved != 0) {
        return 0;
    }
    index = (uint32_t)ppa->block * C34_PAGES_PER_BLOCK + ppa->page;
    if (!data[index].valid || !c34_ppa_equal(&data[index].ppa, ppa)) {
        return 0;
    }
    record = &data[index].record;
    return record->type == C34_RECORD_DATA && record->atom == atom &&
           record->logical_version == version &&
           record->copy_sequence == copy_sequence &&
           record->logical_state_id == logical_state_id &&
           record->record_id == data_record_id &&
           record->value_crc32c == value_crc32c &&
           record->target_erase_generation == erase_generation &&
           (before_sequence == 0 ||
            record->commit_sequence < before_sequence);
}

static int checkpoint_pair(
    const struct c34_raw_image *image,
    unsigned int slot,
    struct c34_record *checkpoint,
    int *corrupt
)
{
    uint16_t block = (uint16_t)(C34_CHECKPOINT_BLOCK0 + slot);
    struct c34_record anchor;
    enum c34_record_decode_result image_result =
        decode_page(image, block, 0, checkpoint);
    enum c34_record_decode_result anchor_result =
        decode_page(image, block, 1, &anchor);

    *corrupt = 0;
    if (image_result == C34_DECODE_INVALID ||
        anchor_result == C34_DECODE_INVALID) {
        *corrupt = 1;
        return 0;
    }
    if (image_result != C34_DECODE_OK ||
        anchor_result != C34_DECODE_OK) {
        return 0;
    }
    if (checkpoint->type != C34_RECORD_CHECKPOINT ||
        anchor.type != C34_RECORD_ANCHOR ||
        checkpoint->checkpoint_generation != slot + 1u ||
        anchor.checkpoint_generation != checkpoint->checkpoint_generation ||
        anchor.checkpoint_slot != slot ||
        anchor.checkpoint_ppa.block != block ||
        anchor.checkpoint_ppa.page != 0 ||
        anchor.checkpoint_record_id != checkpoint->record_id ||
        anchor.checkpoint_payload_crc32c !=
            c34_crc32c(image->pages[(uint32_t)block *
                                    C34_PAGES_PER_BLOCK].main,
                       C34_MAIN_BYTES) ||
        anchor.covered_commit_sequence !=
            checkpoint->covered_commit_sequence) {
        *corrupt = 1;
        return 0;
    }
    return 1;
}

static int record_identity_unique(
    const struct c34_decoded_page decoded[C34_TOTAL_PAGES]
)
{
    unsigned int left;

    for (left = 0; left < C34_TOTAL_PAGES; ++left) {
        unsigned int right;

        if (!decoded[left].valid) {
            continue;
        }
        for (right = left + 1; right < C34_TOTAL_PAGES; ++right) {
            if (!decoded[right].valid) {
                continue;
            }
            if (decoded[left].record.record_id ==
                    decoded[right].record.record_id ||
                decoded[left].record.commit_sequence ==
                    decoded[right].record.commit_sequence) {
                return 0;
            }
        }
    }
    return 1;
}

static int apply_mapping_record(
    const struct c34_record *record,
    const struct c34_decoded_page data[C34_DATA_PAGES],
    struct c34_logical_entry l2p[C34_ATOMS]
)
{
    struct c34_logical_entry *current;

    if (record->atom >= C34_ATOMS) {
        return 0;
    }
    current = &l2p[record->atom];
    if (record->predecessor_state_id != current->logical_state_id) {
        return 0;
    }
    if (record->type == C34_RECORD_MAP) {
        if (record->logical_version <= current->version ||
            record->copy_sequence != 0 ||
            record->source_authority_record_id !=
                current->authority_record_id ||
            !data_matches(
                data, &record->target_ppa,
                record->target_erase_generation, record->atom,
                record->logical_version, record->copy_sequence,
                record->logical_state_id, record->target_data_record_id,
                record->value_crc32c, record->commit_sequence)) {
            return 0;
        }
        current->kind = C34_LOGICAL_VALUE;
        current->version = record->logical_version;
        current->copy_sequence = 0;
        current->atom = record->atom;
        current->logical_state_id = record->logical_state_id;
        current->authority_record_id = record->record_id;
        current->data_record_id = record->target_data_record_id;
        current->data_ppa = record->target_ppa;
        current->data_erase_generation =
            record->target_erase_generation;
        current->value_crc32c = record->value_crc32c;
        return 1;
    }
    if (record->type == C34_RECORD_TOMBSTONE) {
        if (record->logical_version <= current->version ||
            record->copy_sequence != 0 || record->value_crc32c != 0) {
            return 0;
        }
        memset(current, 0, sizeof(*current));
        current->kind = C34_LOGICAL_TOMBSTONE;
        current->version = record->logical_version;
        current->atom = record->atom;
        current->logical_state_id = record->logical_state_id;
        current->authority_record_id = record->record_id;
        return 1;
    }
    if (record->type == C34_RECORD_RELOCATION) {
        if (current->kind != C34_LOGICAL_VALUE ||
            record->logical_state_id != current->logical_state_id ||
            record->logical_version != current->version ||
            record->copy_sequence != current->copy_sequence + 1u ||
            record->source_authority_record_id !=
                current->authority_record_id ||
            !c34_ppa_equal(&record->source_ppa, &current->data_ppa) ||
            record->source_erase_generation !=
                current->data_erase_generation ||
            record->value_crc32c != current->value_crc32c ||
            !data_matches(
                data, &record->target_ppa,
                record->target_erase_generation, record->atom,
                record->logical_version, record->copy_sequence,
                record->logical_state_id, record->target_data_record_id,
                record->value_crc32c, record->commit_sequence)) {
            return 0;
        }
        current->copy_sequence = record->copy_sequence;
        current->authority_record_id = record->record_id;
        current->data_record_id = record->target_data_record_id;
        current->data_ppa = record->target_ppa;
        current->data_erase_generation =
            record->target_erase_generation;
        return 1;
    }
    return 0;
}

static int checkpoint_to_l2p(
    const struct c34_record *checkpoint,
    struct c34_logical_entry l2p[C34_ATOMS]
)
{
    unsigned int atom;

    for (atom = 0; atom < C34_ATOMS; ++atom) {
        const struct c34_checkpoint_entry *source =
            &checkpoint->checkpoint[atom];
        struct c34_logical_entry *target = &l2p[atom];

        memset(target, 0, sizeof(*target));
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
        if (target->kind == C34_LOGICAL_TOMBSTONE &&
            (target->version == 0 || target->logical_state_id == 0 ||
             target->authority_record_id == 0)) {
            return 0;
        }
        if (target->kind == C34_LOGICAL_NONE &&
            (target->version != 0 || target->logical_state_id != 0 ||
             target->authority_record_id != 0)) {
            return 0;
        }
    }
    return 1;
}

static void classify_p2l(
    const struct c34_decoded_page data[C34_DATA_PAGES],
    const struct c34_logical_entry l2p[C34_ATOMS],
    const struct c34_raw_image *image,
    struct c34_p2l_entry p2l[C34_DATA_PAGES]
)
{
    int reserve = -1;
    unsigned int block;

    memset(p2l, 0, sizeof(struct c34_p2l_entry) * C34_DATA_PAGES);
    for (block = 0; block < C34_DATA_BLOCKS; ++block) {
        unsigned int page;
        int all_free = image->blocks[block].health == FWLAB_NFC_BLOCK_GOOD &&
                       image->blocks[block].erase_state ==
                           FWLAB_NAND_ERASE_CLEAN;

        for (page = 0; page < C34_PAGES_PER_BLOCK; ++page) {
            uint32_t index = block * C34_PAGES_PER_BLOCK + page;

            if (image->pages[index].page.state != FWLAB_NAND_PAGE_ERASED) {
                all_free = 0;
            }
        }
        if (all_free) {
            reserve = (int)block;
        }
    }
    for (block = 0; block < C34_DATA_PAGES; ++block) {
        const struct c34_raw_page *raw = &image->pages[block];
        struct c34_p2l_entry *entry = &p2l[block];

        if (raw->page.state == FWLAB_NAND_PAGE_ERASED) {
            entry->kind = block / C34_PAGES_PER_BLOCK ==
                                  (unsigned int)reserve ?
                              C34_P2L_RESERVED : C34_P2L_FREE;
            continue;
        }
        if (raw->page.state == FWLAB_NAND_PAGE_TORN || !data[block].valid) {
            entry->kind = C34_P2L_TORN;
            continue;
        }
        entry->atom = data[block].record.atom;
        entry->version = data[block].record.logical_version;
        entry->copy_sequence = data[block].record.copy_sequence;
        entry->data_record_id = data[block].record.record_id;
        entry->logical_state_id = data[block].record.logical_state_id;
        entry->value_crc32c = data[block].record.value_crc32c;
        if (entry->atom < C34_ATOMS &&
            l2p[entry->atom].kind == C34_LOGICAL_VALUE &&
            l2p[entry->atom].data_record_id == entry->data_record_id &&
            c34_page_index(&l2p[entry->atom].data_ppa) == block) {
            entry->kind = C34_P2L_LIVE;
        } else if (entry->atom < C34_ATOMS &&
                   l2p[entry->atom].kind != C34_LOGICAL_NONE &&
                   entry->version <= l2p[entry->atom].version) {
            entry->kind = C34_P2L_STALE;
        } else {
            entry->kind = C34_P2L_ORPHAN;
        }
    }
}

enum c34_result c34_recover_image(
    const struct c34_raw_image *image,
    struct c34_logical_entry l2p[C34_ATOMS],
    struct c34_p2l_entry p2l[C34_DATA_PAGES],
    uint32_t *checkpoint_generation,
    uint32_t *checkpoint_watermark,
    uint32_t *next_record_id,
    uint32_t *next_logical_state_id,
    uint32_t *next_commit_sequence
)
{
    struct c34_decoded_page decoded[C34_TOTAL_PAGES];
    struct c34_decoded_page data[C34_DATA_PAGES];
    struct c34_record checkpoint[2];
    struct c34_decoded_page journal[C34_PAGES_PER_BLOCK];
    int pair_valid[2] = {0, 0};
    uint32_t max_record = 0;
    uint32_t max_state = 0;
    uint32_t max_sequence = 0;
    unsigned int index;
    int selected = -1;

    if (image == NULL || l2p == NULL || p2l == NULL ||
        checkpoint_generation == NULL || checkpoint_watermark == NULL ||
        next_record_id == NULL || next_logical_state_id == NULL ||
        next_commit_sequence == NULL) {
        return C34_INVALID_CONTRACT;
    }
    memset(decoded, 0, sizeof(decoded));
    memset(data, 0, sizeof(data));
    memset(journal, 0, sizeof(journal));
    for (index = 0; index < C34_TOTAL_PAGES; ++index) {
        uint16_t block = (uint16_t)(index / C34_PAGES_PER_BLOCK);
        uint16_t page = (uint16_t)(index % C34_PAGES_PER_BLOCK);
        struct c34_record record;
        enum c34_record_decode_result result =
            decode_page(image, block, page, &record);

        decoded[index].ppa = c34_ppa(block, page);
        if (result == C34_DECODE_OK) {
            decoded[index].valid = 1;
            decoded[index].record = record;
            if (record.record_id > max_record) {
                max_record = record.record_id;
            }
            if (record.logical_state_id > max_state) {
                max_state = record.logical_state_id;
            }
            if (record.commit_sequence > max_sequence) {
                max_sequence = record.commit_sequence;
            }
        }
        if (block < C34_DATA_BLOCKS) {
            if (result == C34_DECODE_OK && record.type != C34_RECORD_DATA) {
                return C34_CORRUPT;
            }
            if (result == C34_DECODE_OK) {
                data[index] = decoded[index];
            }
        } else if (block == C34_JOURNAL_BLOCK) {
            if (result == C34_DECODE_INVALID ||
                (result == C34_DECODE_OK &&
                 record.type != C34_RECORD_MAP &&
                 record.type != C34_RECORD_TOMBSTONE &&
                 record.type != C34_RECORD_RELOCATION)) {
                return C34_CORRUPT;
            }
            if (result == C34_DECODE_OK) {
                journal[page] = decoded[index];
            }
        } else if (page >= 2 && result == C34_DECODE_OK) {
            return C34_CORRUPT;
        }
    }
    if (!record_identity_unique(decoded)) {
        return C34_CORRUPT;
    }
    for (index = 0; index < 2; ++index) {
        int corrupt;

        pair_valid[index] = checkpoint_pair(
            image, index, &checkpoint[index], &corrupt);
        if (corrupt) {
            return C34_CORRUPT;
        }
        if (pair_valid[index]) {
            selected = (int)index;
        }
    }
    memset(l2p, 0, sizeof(struct c34_logical_entry) * C34_ATOMS);
    for (index = 0; index < C34_ATOMS; ++index) {
        l2p[index].atom = (uint8_t)index;
    }
    *checkpoint_generation = 0;
    *checkpoint_watermark = 0;
    if (selected >= 0) {
        const struct c34_record *chosen = &checkpoint[selected];

        if (!checkpoint_to_l2p(chosen, l2p)) {
            return C34_CORRUPT;
        }
        *checkpoint_generation = chosen->checkpoint_generation;
        *checkpoint_watermark = chosen->covered_commit_sequence;
        if (chosen->next_record_id > max_record + 1u) {
            max_record = chosen->next_record_id - 1u;
        }
        if (chosen->next_logical_state_id > max_state + 1u) {
            max_state = chosen->next_logical_state_id - 1u;
        }
    }
    for (;;) {
        int chosen = -1;
        uint32_t chosen_sequence = UINT32_MAX;

        for (index = 0; index < C34_PAGES_PER_BLOCK; ++index) {
            if (!journal[index].valid ||
                journal[index].record.commit_sequence <=
                    *checkpoint_watermark ||
                journal[index].record.commit_sequence >= chosen_sequence) {
                continue;
            }
            chosen = (int)index;
            chosen_sequence = journal[index].record.commit_sequence;
        }
        if (chosen < 0) {
            break;
        }
        if (!apply_mapping_record(&journal[chosen].record, data, l2p)) {
            return C34_CORRUPT;
        }
        journal[chosen].valid = 0;
    }
    for (index = 0; index < C34_ATOMS; ++index) {
        const struct c34_logical_entry *entry = &l2p[index];

        if (entry->kind == C34_LOGICAL_VALUE &&
            !data_matches(
                data, &entry->data_ppa, entry->data_erase_generation,
                (uint8_t)index, entry->version, entry->copy_sequence,
                entry->logical_state_id, entry->data_record_id,
                entry->value_crc32c, 0)) {
            return C34_CORRUPT;
        }
    }
    classify_p2l(data, l2p, image, p2l);
    *next_record_id = max_record + 1u;
    *next_logical_state_id = max_state + 1u;
    *next_commit_sequence = max_sequence + 1u;
    return C34_OK;
}

enum c34_result c34_refresh(struct c34 *instance, bool clear_overlay)
{
    struct c34_raw_image image;
    enum c34_result result;
    uint32_t checkpoint_generation;
    uint32_t checkpoint_watermark;
    uint32_t next_record_id;
    uint32_t next_logical_state_id;
    uint32_t next_commit_sequence;

    if (instance == NULL || instance->magic != C34_MAGIC) {
        return C34_INVALID_CONTRACT;
    }
    result = c34_scan_raw_media(&instance->raw_media, &image);
    if (result != C34_OK) {
        return result;
    }
    result = c34_recover_image(
        &image, instance->l2p, instance->p2l,
        &checkpoint_generation, &checkpoint_watermark, &next_record_id,
        &next_logical_state_id, &next_commit_sequence);
    if (result != C34_OK) {
        return result;
    }
    instance->checkpoint_generation = checkpoint_generation;
    instance->checkpoint_watermark = checkpoint_watermark;
    if (instance->next_record_id < next_record_id) {
        instance->next_record_id = next_record_id;
    }
    if (instance->next_logical_state_id < next_logical_state_id) {
        instance->next_logical_state_id = next_logical_state_id;
    }
    if (instance->next_commit_sequence < next_commit_sequence) {
        instance->next_commit_sequence = next_commit_sequence;
    }
    memcpy(instance->blocks, image.blocks, sizeof(instance->blocks));
    if (clear_overlay) {
        memset(instance->overlay_valid, 0,
               sizeof(instance->overlay_valid));
        memset(instance->overlay_kind, 0,
               sizeof(instance->overlay_kind));
        memset(instance->overlay_payload, 0,
               sizeof(instance->overlay_payload));
    }
    return C34_OK;
}

enum c34_result c34_recover(struct c34 *instance)
{
    return c34_refresh(instance, true);
}
