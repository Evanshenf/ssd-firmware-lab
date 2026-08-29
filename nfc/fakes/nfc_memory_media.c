/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "nfc_memory_media.h"

#include "../nfc_internal.h"

#include <stdalign.h>
#include <string.h>

static int add_size(size_t left, size_t right, size_t *result)
{
    if (left > SIZE_MAX - right) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static int mul_size(size_t left, size_t right, size_t *result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return 0;
    }
    *result = left * right;
    return 1;
}

static size_t align_up(size_t value, size_t alignment)
{
    size_t mask = alignment - 1u;

    if ((alignment & mask) != 0 || value > SIZE_MAX - mask) {
        return 0;
    }
    return (value + mask) & ~mask;
}

static int geometry_counts(
    const struct fwlab_nfc_geometry *geometry,
    size_t *blocks,
    size_t *pages
)
{
    size_t value;

    if (geometry == NULL || geometry->version != FWLAB_NFC_CONTRACT_VERSION ||
        geometry->size != sizeof(*geometry) || geometry->channels == 0 ||
        geometry->luns_per_channel == 0 || geometry->planes_per_lun == 0 ||
        geometry->blocks_per_plane == 0 || geometry->pages_per_block == 0 ||
        geometry->main_bytes_per_page == 0 ||
        geometry->oob_bytes_per_page == 0 ||
        !mul_size(geometry->channels, geometry->luns_per_channel, &value) ||
        !mul_size(value, geometry->planes_per_lun, &value) ||
        !mul_size(value, geometry->blocks_per_plane, blocks) ||
        !mul_size(*blocks, geometry->pages_per_block, pages)) {
        return 0;
    }
    return 1;
}

size_t c33_memory_media_arena_size(
    const struct fwlab_nfc_geometry *geometry
)
{
    size_t blocks;
    size_t pages;
    size_t bytes;
    size_t part;

    if (!geometry_counts(geometry, &blocks, &pages)) {
        return 0;
    }
    bytes = align_up(sizeof(struct c33_memory_media), alignof(max_align_t));
    if (bytes == 0 ||
        !mul_size(pages, geometry->main_bytes_per_page, &part) ||
        !add_size(bytes, part, &bytes) ||
        !mul_size(pages, geometry->oob_bytes_per_page, &part) ||
        !add_size(bytes, part, &bytes)) {
        return 0;
    }
    bytes = align_up(bytes, alignof(struct c33_memory_page));
    if (bytes == 0 ||
        !mul_size(pages, sizeof(struct c33_memory_page), &part) ||
        !add_size(bytes, part, &bytes)) {
        return 0;
    }
    bytes = align_up(bytes, alignof(struct c33_memory_block));
    if (bytes == 0 ||
        !mul_size(blocks, sizeof(struct c33_memory_block), &part) ||
        !add_size(bytes, part, &bytes)) {
        return 0;
    }
    return align_up(bytes, alignof(max_align_t));
}

static int ppa_valid(
    const struct c33_memory_media *media,
    const struct fwlab_nfc_ppa *ppa
)
{
    return ppa != NULL && ppa->reserved == 0 &&
           ppa->channel < media->geometry.channels &&
           ppa->lun < media->geometry.luns_per_channel &&
           ppa->plane < media->geometry.planes_per_lun &&
           ppa->block < media->geometry.blocks_per_plane &&
           ppa->page < media->geometry.pages_per_block;
}

static uint32_t block_index(
    const struct c33_memory_media *media,
    const struct fwlab_nfc_ppa *ppa
)
{
    uint32_t index = ppa->channel;

    index = index * media->geometry.luns_per_channel + ppa->lun;
    index = index * media->geometry.planes_per_lun + ppa->plane;
    return index * media->geometry.blocks_per_plane + ppa->block;
}

static uint32_t page_index(
    const struct c33_memory_media *media,
    const struct fwlab_nfc_ppa *ppa
)
{
    return block_index(media, ppa) * media->geometry.pages_per_block +
           ppa->page;
}

static void page_info(
    const struct c33_memory_media *media,
    uint32_t index,
    struct fwlab_nand_page_info *page
)
{
    memset(page, 0, sizeof(*page));
    page->version = FWLAB_NFC_CONTRACT_VERSION;
    page->size = (uint16_t)sizeof(*page);
    page->erase_generation_seen = media->page[index].erase_generation_seen;
    page->state = media->page[index].state;
    page->program_count = media->page[index].program_count;
}

static void block_info(
    const struct c33_memory_media *media,
    uint32_t index,
    struct fwlab_nand_block_info *block
)
{
    memset(block, 0, sizeof(*block));
    block->version = FWLAB_NFC_CONTRACT_VERSION;
    block->size = (uint16_t)sizeof(*block);
    block->erase_generation = media->block[index].erase_generation;
    block->successful_erase_count =
        media->block[index].successful_erase_count;
    block->erase_attempt_count = media->block[index].erase_attempt_count;
    block->next_program_page = media->block[index].next_program_page;
    block->health = media->block[index].health;
    block->erase_state = media->block[index].erase_state;
}

static enum fwlab_nfc_api_result memory_read(
    void *opaque,
    const struct fwlab_nfc_ppa *ppa,
    uint8_t *main,
    uint32_t main_length,
    uint8_t *oob,
    uint32_t oob_length,
    struct fwlab_nand_page_info *page,
    struct fwlab_nand_block_info *block
)
{
    struct c33_memory_media *media = opaque;
    uint32_t page_id;
    uint32_t block_id;

    if (media == NULL || !ppa_valid(media, ppa) || main == NULL ||
        oob == NULL || page == NULL || block == NULL ||
        main_length != media->geometry.main_bytes_per_page ||
        oob_length != media->geometry.oob_bytes_per_page) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    page_id = page_index(media, ppa);
    block_id = block_index(media, ppa);
    memcpy(main, &media->main[(size_t)page_id * main_length], main_length);
    memcpy(oob, &media->oob[(size_t)page_id * oob_length], oob_length);
    page_info(media, page_id, page);
    block_info(media, block_id, block);
    return FWLAB_NFC_API_OK;
}

static void result_init(
    struct fwlab_nand_media_result *result,
    const struct c33_memory_block *block
)
{
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_NFC_CONTRACT_VERSION;
    result->size = (uint16_t)sizeof(*result);
    result->physical_outcome = FWLAB_NFC_PHYS_NO_EFFECT;
    result->integrity = FWLAB_NFC_INTEGRITY_NOT_APPLICABLE;
    result->block_health = block->health;
    result->base_erase_generation = block->erase_generation;
    result->final_erase_generation = block->erase_generation;
}

static enum fwlab_nfc_api_result memory_program(
    void *opaque,
    const struct fwlab_nfc_ppa *ppa,
    const uint8_t *main,
    uint32_t main_length,
    const uint8_t *oob,
    uint32_t oob_length,
    uint32_t applied_main_bytes,
    uint32_t applied_oob_bytes,
    uint8_t integrity,
    struct fwlab_nand_media_result *result
)
{
    struct c33_memory_media *media = opaque;
    uint32_t page_id;
    uint32_t block_id;
    struct c33_memory_page *page;
    struct c33_memory_block *block;
    size_t index;

    if (media == NULL || !ppa_valid(media, ppa) || main == NULL ||
        oob == NULL || result == NULL ||
        main_length != media->geometry.main_bytes_per_page ||
        oob_length != media->geometry.oob_bytes_per_page ||
        applied_main_bytes > main_length || applied_oob_bytes > oob_length ||
        (integrity != FWLAB_NFC_INTEGRITY_COMPLETE &&
         integrity != FWLAB_NFC_INTEGRITY_TORN)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    page_id = page_index(media, ppa);
    block_id = block_index(media, ppa);
    page = &media->page[page_id];
    block = &media->block[block_id];
    result_init(result, block);
    if (block->health != FWLAB_NFC_BLOCK_GOOD) {
        result->reason = FWLAB_NFC_REASON_BAD_BLOCK;
        return FWLAB_NFC_API_OK;
    }
    if (block->erase_state != FWLAB_NAND_ERASE_CLEAN ||
        page->state != FWLAB_NAND_PAGE_ERASED || page->program_count != 0 ||
        (media->geometry.program_order == FWLAB_NFC_PROGRAM_ASCENDING &&
         ppa->page != block->next_program_page)) {
        result->reason = page->state != FWLAB_NAND_PAGE_ERASED ||
                                 page->program_count != 0 ?
                         FWLAB_NFC_REASON_NOT_ERASED :
                         FWLAB_NFC_REASON_PROGRAM_ORDER;
        return FWLAB_NFC_API_OK;
    }
    for (index = 0; index < applied_main_bytes; ++index) {
        size_t offset = (size_t)page_id * main_length + index;

        media->main[offset] &= main[index];
    }
    for (index = 0; index < applied_oob_bytes; ++index) {
        size_t offset = (size_t)page_id * oob_length + index;

        media->oob[offset] &= oob[index];
    }
    if (applied_main_bytes != 0 || applied_oob_bytes != 0) {
        page->program_count = 1;
        page->erase_generation_seen = block->erase_generation;
        page->state = integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
                              applied_main_bytes == main_length &&
                              applied_oob_bytes == oob_length ?
                      FWLAB_NAND_PAGE_VALID : FWLAB_NAND_PAGE_TORN;
        if (media->geometry.program_order == FWLAB_NFC_PROGRAM_ASCENDING &&
            block->next_program_page < media->geometry.pages_per_block) {
            ++block->next_program_page;
        }
        result->physical_outcome = FWLAB_NFC_PHYS_APPLIED;
        result->integrity = page->state == FWLAB_NAND_PAGE_VALID ?
                            FWLAB_NFC_INTEGRITY_COMPLETE :
                            FWLAB_NFC_INTEGRITY_TORN;
        result->applied_main_bytes = applied_main_bytes;
        result->applied_oob_bytes = applied_oob_bytes;
        result->applied_region_mask =
            (uint8_t)((applied_main_bytes != 0 ? FWLAB_NFC_REGION_MAIN : 0) |
                      (applied_oob_bytes != 0 ? FWLAB_NFC_REGION_OOB : 0));
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result memory_erase(
    void *opaque,
    const struct fwlab_nfc_ppa *ppa,
    uint32_t applied_pages,
    uint8_t integrity,
    struct fwlab_nand_media_result *result
)
{
    struct c33_memory_media *media = opaque;
    struct c33_memory_block *block;
    uint32_t block_id;
    uint32_t page;

    if (media == NULL || !ppa_valid(media, ppa) || ppa->page != 0 ||
        result == NULL || applied_pages > media->geometry.pages_per_block ||
        (integrity != FWLAB_NFC_INTEGRITY_COMPLETE &&
         integrity != FWLAB_NFC_INTEGRITY_TORN)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    block_id = block_index(media, ppa);
    block = &media->block[block_id];
    result_init(result, block);
    if (block->health != FWLAB_NFC_BLOCK_GOOD) {
        result->reason = FWLAB_NFC_REASON_BAD_BLOCK;
        return FWLAB_NFC_API_OK;
    }
    if (block->erase_attempt_count == UINT16_MAX) {
        return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    }
    if (integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
        applied_pages == media->geometry.pages_per_block &&
        (block->erase_generation == UINT16_MAX ||
         block->successful_erase_count == UINT16_MAX)) {
        return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    }
    ++block->erase_attempt_count;
    for (page = 0; page < applied_pages; ++page) {
        uint32_t page_id = block_id * media->geometry.pages_per_block + page;

        memset(&media->main[(size_t)page_id *
                            media->geometry.main_bytes_per_page],
               0xff, media->geometry.main_bytes_per_page);
        memset(&media->oob[(size_t)page_id *
                           media->geometry.oob_bytes_per_page],
               0xff, media->geometry.oob_bytes_per_page);
        media->page[page_id].state =
            integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
                    applied_pages == media->geometry.pages_per_block ?
            FWLAB_NAND_PAGE_ERASED : FWLAB_NAND_PAGE_TORN;
        media->page[page_id].program_count = 0;
    }
    if (applied_pages != 0) {
        result->physical_outcome = FWLAB_NFC_PHYS_APPLIED;
        result->integrity = integrity;
        result->applied_pages = applied_pages;
        result->applied_region_mask = FWLAB_NFC_REGION_MASK;
    }
    if (integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
        applied_pages == media->geometry.pages_per_block) {
        ++block->erase_generation;
        ++block->successful_erase_count;
        block->erase_state = FWLAB_NAND_ERASE_CLEAN;
        block->next_program_page = 0;
        for (page = 0; page < media->geometry.pages_per_block; ++page) {
            uint32_t page_id = block_id *
                               media->geometry.pages_per_block + page;

            media->page[page_id].erase_generation_seen =
                block->erase_generation;
        }
    } else if (applied_pages != 0) {
        block->erase_state = FWLAB_NAND_ERASE_TORN;
    }
    result->final_erase_generation = block->erase_generation;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result memory_mark_bad(
    void *opaque,
    const struct fwlab_nfc_ppa *ppa
)
{
    struct c33_memory_media *media = opaque;
    struct c33_memory_block *block;

    if (media == NULL || !ppa_valid(media, ppa)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    block = &media->block[block_index(media, ppa)];
    if (block->health == FWLAB_NFC_BLOCK_GOOD) {
        block->health = FWLAB_NFC_BLOCK_RUNTIME_BAD;
    }
    return FWLAB_NFC_API_OK;
}

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int byte;

    for (byte = 0; byte < 8; ++byte) {
        hash ^= (uint8_t)(value >> (byte * 8u));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t memory_hash(void *opaque)
{
    struct c33_memory_media *media = opaque;
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t index;
    size_t bytes;

    if (media == NULL) {
        return 0;
    }
    hash = hash_u64(hash, media->geometry.channels);
    hash = hash_u64(hash, media->geometry.luns_per_channel);
    hash = hash_u64(hash, media->geometry.planes_per_lun);
    hash = hash_u64(hash, media->geometry.blocks_per_plane);
    hash = hash_u64(hash, media->geometry.pages_per_block);
    hash = hash_u64(hash, media->geometry.main_bytes_per_page);
    hash = hash_u64(hash, media->geometry.oob_bytes_per_page);
    bytes = (size_t)media->page_count *
            media->geometry.main_bytes_per_page;
    for (index = 0; index < bytes; ++index) {
        hash = hash_u64(hash, media->main[index]);
    }
    bytes = (size_t)media->page_count * media->geometry.oob_bytes_per_page;
    for (index = 0; index < bytes; ++index) {
        hash = hash_u64(hash, media->oob[index]);
    }
    for (index = 0; index < media->page_count; ++index) {
        hash = hash_u64(hash, media->page[index].state);
        hash = hash_u64(hash, media->page[index].program_count);
        hash = hash_u64(hash,
                        media->page[index].erase_generation_seen);
    }
    for (index = 0; index < media->block_count; ++index) {
        hash = hash_u64(hash, media->block[index].health);
        hash = hash_u64(hash, media->block[index].erase_state);
        hash = hash_u64(hash, media->block[index].erase_generation);
        hash = hash_u64(hash,
                        media->block[index].successful_erase_count);
        hash = hash_u64(hash, media->block[index].erase_attempt_count);
        hash = hash_u64(hash, media->block[index].next_program_page);
    }
    return hash;
}

static const struct fwlab_nand_media_ops memory_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_nand_media_ops),
    .reserved = 0,
    .read_page = memory_read,
    .program = memory_program,
    .erase = memory_erase,
    .mark_runtime_bad = memory_mark_bad,
    .hash = memory_hash,
};

enum fwlab_nfc_api_result c33_memory_media_init(
    void *arena,
    size_t arena_size,
    const struct fwlab_nfc_geometry *geometry,
    const struct fwlab_nfc_factory_bad *factory_bad,
    size_t factory_bad_count,
    struct c33_memory_media **media_out
)
{
    size_t required = c33_memory_media_arena_size(geometry);
    size_t blocks;
    size_t pages;
    size_t offset;
    struct c33_memory_media *media;
    size_t index;

    if (arena == NULL || media_out == NULL || required == 0 ||
        arena_size < required ||
        ((uintptr_t)arena % alignof(max_align_t)) != 0 ||
        (factory_bad_count != 0 && factory_bad == NULL) ||
        !geometry_counts(geometry, &blocks, &pages)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(arena, 0, required);
    media = arena;
    media->geometry = *geometry;
    media->page_count = (uint32_t)pages;
    media->block_count = (uint32_t)blocks;
    offset = align_up(sizeof(*media), alignof(max_align_t));
    media->main = (uint8_t *)arena + offset;
    offset += pages * geometry->main_bytes_per_page;
    media->oob = (uint8_t *)arena + offset;
    offset += pages * geometry->oob_bytes_per_page;
    offset = align_up(offset, alignof(struct c33_memory_page));
    media->page = (struct c33_memory_page *)((uint8_t *)arena + offset);
    offset += pages * sizeof(*media->page);
    offset = align_up(offset, alignof(struct c33_memory_block));
    media->block = (struct c33_memory_block *)((uint8_t *)arena + offset);
    memset(media->main, 0xff,
           pages * geometry->main_bytes_per_page);
    memset(media->oob, 0xff, pages * geometry->oob_bytes_per_page);
    for (index = 0; index < pages; ++index) {
        media->page[index].state = FWLAB_NAND_PAGE_ERASED;
    }
    for (index = 0; index < factory_bad_count; ++index) {
        const struct fwlab_nfc_ppa *ppa = &factory_bad[index].block;

        if (factory_bad[index].version != FWLAB_NFC_CONTRACT_VERSION ||
            factory_bad[index].size != sizeof(factory_bad[index]) ||
            factory_bad[index].reserved[0] != 0 ||
            factory_bad[index].reserved[1] != 0 ||
            ppa->page != 0 || !ppa_valid(media, ppa)) {
            return FWLAB_NFC_API_INVALID_CONTRACT;
        }
        media->block[block_index(media, ppa)].health =
            FWLAB_NFC_BLOCK_FACTORY_BAD;
    }
    *media_out = media;
    return FWLAB_NFC_API_OK;
}

struct fwlab_nand_media c33_memory_media_provider(
    struct c33_memory_media *media
)
{
    struct fwlab_nand_media provider;

    provider.ops = &memory_ops;
    provider.context = media;
    return provider;
}
