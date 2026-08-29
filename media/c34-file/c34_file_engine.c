/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_file_internal.h"

#include <stdalign.h>
#include <string.h>

static int substrate_valid(const struct c34_file_substrate *substrate)
{
    return substrate != NULL && substrate->ops != NULL &&
           substrate->context != NULL &&
           substrate->ops->version == C34_FILE_FORMAT_VERSION &&
           substrate->ops->size == sizeof(*substrate->ops) &&
           substrate->ops->reserved == 0 &&
           substrate->ops->size_bytes != NULL &&
           substrate->ops->resize != NULL && substrate->ops->read != NULL &&
           substrate->ops->write != NULL &&
           substrate->ops->barrier != NULL;
}

size_t c34_file_media_arena_alignment(void)
{
    return alignof(max_align_t);
}

size_t c34_file_media_arena_size(void)
{
    size_t alignment = alignof(max_align_t);

    return (sizeof(struct c34_file_media) + alignment - 1u) &
           ~(alignment - 1u);
}

static void raw_erased(struct c34_file_media *media)
{
    unsigned int index;

    memset(media->page, 0, sizeof(media->page));
    for (index = 0; index < C34F_PAGES; ++index) {
        memset(media->page[index].main, 0xff, C34F_MAIN_BYTES);
        memset(media->page[index].oob, 0xff, C34F_OOB_BYTES);
        media->page[index].info.version = FWLAB_NFC_CONTRACT_VERSION;
        media->page[index].info.size = sizeof(media->page[index].info);
        media->page[index].info.state = FWLAB_NAND_PAGE_ERASED;
    }
    memset(media->block, 0, sizeof(media->block));
    for (index = 0; index < C34F_BLOCKS; ++index) {
        media->block[index].version = FWLAB_NFC_CONTRACT_VERSION;
        media->block[index].size = sizeof(media->block[index]);
        media->block[index].health = FWLAB_NFC_BLOCK_GOOD;
        media->block[index].erase_state = FWLAB_NAND_ERASE_CLEAN;
    }
}

static enum c34_file_result checkpoint_hash(
    struct c34_file_media *media,
    uint8_t slot,
    uint32_t *hash
)
{
    uint8_t bytes[C34F_CHECKPOINT_BYTES];
    enum c34_file_result result = c34f_read(
        media, slot == 0 ? C34F_CP0_OFFSET : C34F_CP1_OFFSET, bytes,
        sizeof(bytes));

    if (result == C34_FILE_OK) {
        *hash = c34f_crc32c(bytes, sizeof(bytes));
    }
    return result;
}

enum c34_file_result c34_file_format(
    void *arena,
    size_t arena_size,
    const struct c34_file_substrate *substrate,
    const uint8_t image_uuid[16],
    struct c34_file_media **media_out
)
{
    struct c34_file_media *media;
    uint8_t erased[4096];
    uint64_t size;
    uint32_t cp_hash;
    unsigned int offset;
    enum c34_file_result result;

    if (arena == NULL || media_out == NULL || image_uuid == NULL ||
        (uintptr_t)arena % alignof(max_align_t) != 0 ||
        arena_size < c34_file_media_arena_size() ||
        !substrate_valid(substrate) ||
        substrate->ops->size_bytes(substrate->context, &size) !=
            C34_FILE_IO_OK || size != 0 ||
        substrate->ops->resize(substrate->context,
                               C34_FILE_IMAGE_BYTES) != C34_FILE_IO_OK) {
        return C34_FILE_INVALID;
    }
    memset(arena, 0, arena_size);
    media = arena;
    media->magic = C34F_MAGIC;
    media->substrate = *substrate;
    memcpy(media->uuid, image_uuid, sizeof(media->uuid));
    memset(erased, 0xff, sizeof(erased));
    for (offset = 0; offset < C34_FILE_IMAGE_BYTES; offset += sizeof(erased)) {
        result = c34f_write(media, offset, erased, sizeof(erased));
        if (result != C34_FILE_OK) {
            return result;
        }
    }
    result = c34f_barrier(media);
    if (result != C34_FILE_OK) {
        return result;
    }
    raw_erased(media);
    media->next_lsn = 1;
    media->next_op_id = 1;
    media->next_commit_sequence = 1;
    media->checkpoint_generation = 1;
    media->super_generation = 1;
    result = c34f_write_checkpoint(media, 0, 1, 0);
    if (result != C34_FILE_OK ||
        (result = c34f_barrier(media)) != C34_FILE_OK ||
        (result = checkpoint_hash(media, 0, &cp_hash)) != C34_FILE_OK ||
        (result = c34f_write_super(
             media, 0, 1, 0, 1, cp_hash, 0, 1)) != C34_FILE_OK ||
        (result = c34f_barrier(media)) != C34_FILE_OK ||
        (result = c34f_recycle_wal(media)) != C34_FILE_OK) {
        return result;
    }
    media->active_super = 0;
    media->active_checkpoint = 0;
    *media_out = media;
    return C34_FILE_OK;
}

enum c34_file_result c34_file_restart(
    void *arena,
    size_t arena_size,
    const struct c34_file_substrate *substrate,
    const uint8_t expected_uuid[16],
    struct c34_file_media **media_out
)
{
    struct c34_file_media *media;
    uint64_t size;
    uint8_t super;
    uint8_t checkpoint;
    uint64_t super_generation;
    uint64_t checkpoint_generation;
    uint32_t checkpoint_crc;
    uint64_t covered_lsn;
    uint32_t wal_epoch;
    enum c34_file_result result;

    if (arena == NULL || media_out == NULL || expected_uuid == NULL ||
        (uintptr_t)arena % alignof(max_align_t) != 0 ||
        arena_size < c34_file_media_arena_size() ||
        !substrate_valid(substrate) ||
        substrate->ops->size_bytes(substrate->context, &size) !=
            C34_FILE_IO_OK || size != C34_FILE_IMAGE_BYTES) {
        return C34_FILE_INVALID;
    }
    memset(arena, 0, arena_size);
    media = arena;
    media->magic = C34F_MAGIC;
    media->substrate = *substrate;
    memcpy(media->uuid, expected_uuid, sizeof(media->uuid));
    result = c34f_select_super(
        media, &super, &super_generation, &checkpoint,
        &checkpoint_generation, &checkpoint_crc, &covered_lsn, &wal_epoch);
    if (result != C34_FILE_OK) {
        return result;
    }
    media->active_super = super;
    media->super_generation = super_generation;
    media->active_checkpoint = checkpoint;
    media->wal_epoch = wal_epoch;
    result = c34f_load_checkpoint(
        media, checkpoint, checkpoint_generation, checkpoint_crc);
    if (result != C34_FILE_OK || media->covered_lsn != covered_lsn) {
        return result != C34_FILE_OK ? result : C34_FILE_CORRUPT;
    }
    result = c34f_recover_wal(media);
    if (result != C34_FILE_OK) {
        return result;
    }
    *media_out = media;
    return C34_FILE_OK;
}

enum c34_file_result c34_file_physical_checkpoint(
    struct c34_file_media *media
)
{
    uint8_t checkpoint;
    uint8_t super;
    uint64_t checkpoint_generation;
    uint64_t super_generation;
    uint32_t cp_hash;
    uint32_t next_wal_epoch;
    enum c34_file_result result;

    if (media == NULL || media->magic != C34F_MAGIC || media->stopped ||
        media->receipt_used || media->next_lsn == 0 ||
        media->checkpoint_generation == UINT64_MAX ||
        media->super_generation == UINT64_MAX ||
        media->wal_epoch == UINT32_MAX) {
        return C34_FILE_BUSY;
    }
    checkpoint = (uint8_t)(media->active_checkpoint ^ 1u);
    super = (uint8_t)(media->active_super ^ 1u);
    checkpoint_generation = media->checkpoint_generation + 1u;
    super_generation = media->super_generation + 1u;
    next_wal_epoch = media->wal_epoch + 1u;
    result = c34f_write_checkpoint(
        media, checkpoint, checkpoint_generation, media->next_lsn - 1u);
    if (result != C34_FILE_OK ||
        (result = c34f_barrier(media)) != C34_FILE_OK ||
        (result = checkpoint_hash(media, checkpoint, &cp_hash)) !=
            C34_FILE_OK ||
        (result = c34f_write_super(
             media, super, super_generation, checkpoint,
             checkpoint_generation, cp_hash, media->next_lsn - 1u,
             next_wal_epoch)) != C34_FILE_OK ||
        (result = c34f_barrier(media)) != C34_FILE_OK ||
        (result = c34f_recycle_wal(media)) != C34_FILE_OK) {
        return result;
    }
    if (media->wal_epoch != next_wal_epoch) {
        return C34_FILE_CORRUPT;
    }
    media->active_checkpoint = checkpoint;
    media->checkpoint_generation = checkpoint_generation;
    media->active_super = super;
    media->super_generation = super_generation;
    media->covered_lsn = media->next_lsn - 1u;
    return C34_FILE_OK;
}

uint64_t c34_file_image_hash(struct c34_file_media *media)
{
    uint8_t bytes[4096];
    uint64_t hash = UINT64_C(1469598103934665603);
    unsigned int offset;

    if (media == NULL || media->magic != C34F_MAGIC) {
        return 0;
    }
    for (offset = 0; offset < C34_FILE_IMAGE_BYTES; offset += sizeof(bytes)) {
        if (c34f_read(media, offset, bytes, sizeof(bytes)) != C34_FILE_OK) {
            return 0;
        }
        hash = c34f_hash_bytes(hash, bytes, sizeof(bytes));
    }
    return hash;
}

uint64_t c34_file_physical_hash(const struct c34_file_media *media)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    uint8_t fields[16];
    unsigned int index;

    if (media == NULL || media->magic != C34F_MAGIC) {
        return 0;
    }
    for (index = 0; index < C34F_PAGES; ++index) {
        hash = c34f_hash_bytes(hash, media->page[index].main,
                               C34F_MAIN_BYTES);
        hash = c34f_hash_bytes(hash, media->page[index].oob,
                               C34F_OOB_BYTES);
        memset(fields, 0, sizeof(fields));
        c34f_put_u16(&fields[0],
                     media->page[index].info.erase_generation_seen);
        fields[2] = media->page[index].info.state;
        fields[3] = media->page[index].info.program_count;
        hash = c34f_hash_bytes(hash, fields, sizeof(fields));
    }
    for (index = 0; index < C34F_BLOCKS; ++index) {
        memset(fields, 0, sizeof(fields));
        c34f_put_u16(&fields[0], media->block[index].erase_generation);
        c34f_put_u16(&fields[2],
                     media->block[index].successful_erase_count);
        c34f_put_u16(&fields[4], media->block[index].erase_attempt_count);
        c34f_put_u16(&fields[6], media->block[index].next_program_page);
        fields[8] = media->block[index].health;
        fields[9] = media->block[index].erase_state;
        hash = c34f_hash_bytes(hash, fields, sizeof(fields));
    }
    return hash;
}
