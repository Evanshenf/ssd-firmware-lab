/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "compact_nand_internal.h"

#include <stdalign.h>
#include <string.h>

#define FNV1_MAGIC UINT64_C(0x464e414e44303031)
#define FNV1_SUPER UINT32_C(0x31534e46)
#define FNV1_FENCE UINT32_C(0x31464e46)
#define FNV1_REDO UINT32_C(0x31524e46)
#define FNV1_SEAL UINT32_C(0x31434e46)
#define FNV1_BLOCK UINT32_C(0x31424e46)
#define FNV1_PAGE UINT32_C(0x31504e46)
#define FNV1_RECORD_BYTES 256u
#define FNV1_BLOCK_BYTES 64u

struct fnv1_block {
    struct fwlab_nand_block_info info;
    uint16_t erased_prefix;
};

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get64(const uint8_t *p)
{
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    unsigned i;
    for (i = 0; i < 4; ++i)
        p[i] = (uint8_t)(v >> (8u * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static int zero(const uint8_t *p, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i)
        if (p[i] != 0)
            return 0;
    return 1;
}

static uint32_t crc(const uint8_t *p, size_t length, size_t omitted)
{
    uint32_t value = UINT32_MAX;
    size_t i;
    unsigned bit;
    for (i = 0; i < length; ++i) {
        value ^= (i >= omitted && i - omitted < 4u) ? 0u : p[i];
        for (bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^
                (UINT32_C(0x82f63b78) & (0u - (value & 1u)));
    }
    return ~value;
}

static uint64_t hash_bytes(uint64_t hash, const uint8_t *p, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i) {
        hash ^= p[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int config_layout(const struct fwlab_file_nand_v1_config *config,
                         uint32_t *blocks, uint32_t *pages,
                         uint64_t *page_meta, uint64_t *block_meta,
                         uint64_t *image_bytes)
{
    const struct fwlab_nfc_geometry *g;
    uint64_t count;
    if (config == NULL || zero(config->media_uuid, 16))
        return 0;
    g = &config->geometry;
    if (g->version != FWLAB_NFC_CONTRACT_VERSION || g->size != sizeof(*g) ||
        !g->channels || !g->luns_per_channel || !g->planes_per_lun ||
        !g->blocks_per_plane || !g->pages_per_block ||
        !g->plane_parallelism_per_lun ||
        g->plane_parallelism_per_lun > g->planes_per_lun ||
        g->main_bytes_per_page != 4096 || g->oob_bytes_per_page != 128 ||
        g->max_programs_per_erase != 1 ||
        g->program_order != FWLAB_NFC_PROGRAM_ASCENDING ||
        g->reserved0 || g->reserved1[0] || g->reserved1[1])
        return 0;
    /* Each multiplier is u16; reject at each step before another product. */
    count = (uint64_t)g->channels * g->luns_per_channel;
    if (count > UINT32_MAX / g->planes_per_lun)
        return 0;
    count *= g->planes_per_lun;
    if (count > UINT32_MAX / g->blocks_per_plane)
        return 0;
    count *= g->blocks_per_plane;
    *blocks = (uint32_t)count;
    if (count > UINT32_MAX / g->pages_per_block)
        return 0;
    count *= g->pages_per_block;
    *pages = (uint32_t)count;
    /* Counts are <=u32, so these u64 byte calculations cannot overflow. */
    *page_meta = FNV1_HOME_BASE + count * FNV1_SECTOR;
    *block_meta = *page_meta + ((count + 15u) / 16u) * FNV1_SECTOR;
    *image_bytes = *block_meta +
        (((uint64_t)*blocks + 63u) / 64u) * FNV1_SECTOR;
    return *image_bytes <= INT64_MAX;
}

size_t fwlab_file_nand_v1_arena_alignment(void)
{
    return alignof(struct fwlab_file_nand_v1);
}

size_t fwlab_file_nand_v1_arena_size(void)
{
    return sizeof(struct fwlab_file_nand_v1);
}

uint64_t fwlab_file_nand_v1_image_bytes(
    const struct fwlab_file_nand_v1_config *config)
{
    uint32_t blocks, pages;
    uint64_t pm, bm, bytes;
    return config_layout(config, &blocks, &pages, &pm, &bm, &bytes) ? bytes : 0;
}

static int live(const struct fwlab_file_nand_v1 *m)
{
    return m != NULL && m->magic == FNV1_MAGIC && !m->closed &&
           !m->quarantined;
}

static enum fwlab_nfc_api_result broken(struct fwlab_file_nand_v1 *m)
{
    m->quarantined = 1;
    return FWLAB_NFC_API_INVARIANT_FAILURE;
}

static int read_bytes(struct fwlab_file_nand_v1 *m, uint64_t offset,
                      void *buffer, size_t bytes)
{
    if (offset > m->image_bytes || bytes > m->image_bytes - offset ||
        m->io.read(m->io.context, offset, buffer, bytes) != FWLAB_NFC_API_OK) {
        m->quarantined = 1;
        return 0;
    }
    return 1;
}

static int write_sector(struct fwlab_file_nand_v1 *m, uint64_t offset,
                        const uint8_t *bytes)
{
    if (offset % FNV1_SECTOR || offset > m->image_bytes ||
        FNV1_SECTOR > m->image_bytes - offset ||
        m->io.write(m->io.context, offset, bytes, FNV1_SECTOR) !=
            FWLAB_NFC_API_OK) {
        m->quarantined = 1;
        return 0;
    }
    return 1;
}

static int barrier(struct fwlab_file_nand_v1 *m)
{
    if (m->io.sync(m->io.context) != FWLAB_NFC_API_OK) {
        m->quarantined = 1;
        return 0;
    }
    return 1;
}

static void make_super(struct fwlab_file_nand_v1 *m, uint8_t *bytes)
{
    const struct fwlab_nfc_geometry *g = &m->config.geometry;
    memset(bytes, 0, FNV1_SECTOR);
    put32(bytes, FNV1_SUPER);
    put32(bytes + 4, 1);
    memcpy(bytes + 8, m->config.media_uuid, 16);
    put16(bytes + 24, g->channels);
    put16(bytes + 26, g->luns_per_channel);
    put16(bytes + 28, g->planes_per_lun);
    put16(bytes + 30, g->blocks_per_plane);
    put16(bytes + 32, g->pages_per_block);
    put16(bytes + 34, g->plane_parallelism_per_lun);
    put32(bytes + 36, g->main_bytes_per_page);
    put32(bytes + 40, g->oob_bytes_per_page);
    put64(bytes + 48, m->page_metadata_offset);
    put64(bytes + 56, m->block_metadata_offset);
    put64(bytes + 64, m->image_bytes);
    put32(bytes + 4092, crc(bytes, FNV1_SECTOR, 4092));
}

static int valid_record(const uint8_t *bytes, uint32_t magic)
{
    return get32(bytes) == magic && get32(bytes + 4) == 1 &&
           get32(bytes + 4092) == crc(bytes, FNV1_SECTOR, 4092);
}

static void sequence_record(struct fwlab_file_nand_v1 *m, uint32_t magic,
                            uint64_t sequence, uint64_t digest)
{
    memset(m->work, 0, FNV1_SECTOR);
    put32(m->work, magic);
    put32(m->work + 4, 1);
    put64(m->work + 8, sequence);
    put64(m->work + 16, digest);
    memcpy(m->work + 24, m->config.media_uuid, 16);
    put32(m->work + 4092, crc(m->work, FNV1_SECTOR, 4092));
}

static int sequence_valid(const struct fwlab_file_nand_v1 *m,
                          const uint8_t *bytes, uint32_t magic)
{
    return valid_record(bytes, magic) &&
           memcmp(bytes + 24, m->config.media_uuid, 16) == 0 &&
           zero(bytes + 40, 4052);
}

static void block_encode(uint8_t *bytes, uint32_t id,
                         const struct fnv1_block *block, uint64_t sequence)
{
    const struct fwlab_nand_block_info *b = &block->info;
    memset(bytes, 0, FNV1_BLOCK_BYTES);
    put32(bytes, FNV1_BLOCK);
    put32(bytes + 4, id);
    put16(bytes + 8, b->erase_generation);
    put16(bytes + 10, b->successful_erase_count);
    put16(bytes + 12, b->erase_attempt_count);
    put16(bytes + 14, b->next_program_page);
    bytes[16] = b->health;
    bytes[17] = b->erase_state;
    put16(bytes + 18, block->erased_prefix);
    put64(bytes + 24, sequence);
    put32(bytes + 60, crc(bytes, FNV1_BLOCK_BYTES, 60));
}

static int block_decode(struct fwlab_file_nand_v1 *m, const uint8_t *bytes,
                         uint32_t id, struct fnv1_block *block)
{
    struct fwlab_nand_block_info *b = &block->info;
    if (get32(bytes) != FNV1_BLOCK || get32(bytes + 4) != id ||
        get32(bytes + 60) != crc(bytes, FNV1_BLOCK_BYTES, 60) ||
        !zero(bytes + 20, 4) || !zero(bytes + 32, 28) ||
        get64(bytes + 24) > m->sequence)
        return 0;
    memset(block, 0, sizeof(*block));
    b->version = FWLAB_NFC_CONTRACT_VERSION;
    b->size = (uint16_t)sizeof(*b);
    b->erase_generation = get16(bytes + 8);
    b->successful_erase_count = get16(bytes + 10);
    b->erase_attempt_count = get16(bytes + 12);
    b->next_program_page = get16(bytes + 14);
    b->health = bytes[16];
    b->erase_state = bytes[17];
    block->erased_prefix = get16(bytes + 18);
    return b->health <= FWLAB_NFC_BLOCK_RUNTIME_BAD &&
           b->erase_state <= FWLAB_NAND_ERASE_TORN &&
           b->next_program_page <= m->config.geometry.pages_per_block &&
           block->erased_prefix <= m->config.geometry.pages_per_block &&
           (b->erase_state != FWLAB_NAND_ERASE_CLEAN ||
            block->erased_prefix == 0);
}

static int ppa_ids(const struct fwlab_file_nand_v1 *m,
                   const struct fwlab_nfc_ppa *ppa,
                   uint32_t *block, uint32_t *page)
{
    const struct fwlab_nfc_geometry *g = &m->config.geometry;
    uint64_t id;
    if (ppa == NULL || ppa->reserved || ppa->channel >= g->channels ||
        ppa->lun >= g->luns_per_channel || ppa->plane >= g->planes_per_lun ||
        ppa->block >= g->blocks_per_plane || ppa->page >= g->pages_per_block)
        return 0;
    id = ((uint64_t)ppa->channel * g->luns_per_channel + ppa->lun) *
        g->planes_per_lun + ppa->plane;
    id = id * g->blocks_per_plane + ppa->block;
    *block = (uint32_t)id;
    *page = (uint32_t)(id * g->pages_per_block + ppa->page);
    return 1;
}

static uint64_t block_sector(const struct fwlab_file_nand_v1 *m, uint32_t id)
{
    return m->block_metadata_offset + (uint64_t)(id / 64u) * FNV1_SECTOR;
}

static uint64_t page_sector(const struct fwlab_file_nand_v1 *m, uint32_t id)
{
    return m->page_metadata_offset + (uint64_t)(id / 16u) * FNV1_SECTOR;
}

static int block_read(struct fwlab_file_nand_v1 *m, uint32_t id,
                      struct fnv1_block *block)
{
    return read_bytes(m, block_sector(m, id), m->work, FNV1_SECTOR) &&
           block_decode(m, m->work + (id % 64u) * FNV1_BLOCK_BYTES, id, block);
}

static int install_redo(struct fwlab_file_nand_v1 *m, uint64_t sequence)
{
    uint32_t i;
    for (i = 0; i < m->redo_count; ++i)
        if (!write_sector(m, m->redo_target[i], m->redo[i]))
            return 0;
    if (!barrier(m))
        return 0;
    sequence_record(m, FNV1_FENCE, sequence, 0);
    if (!write_sector(m, UINT64_C(8192) +
                      (sequence & 1u) * FNV1_SECTOR, m->work) || !barrier(m))
        return 0;
    m->sequence = sequence;
    return 1;
}

static enum fwlab_nfc_api_result commit_redo(struct fwlab_file_nand_v1 *m)
{
    uint64_t sequence, bank, digest;
    uint32_t i;
    if (m->sequence == UINT64_MAX)
        return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    sequence = m->sequence + 1u;
    bank = FNV1_BANK_BASE + (sequence & 1u) * FNV1_BANK_BYTES;
    if (!m->redo_count || m->redo_count > FNV1_MAX_POSTIMAGES)
        return broken(m);
    /* A previous seal must be durably invalid before overwriting its body. */
    memset(m->work, 0, FNV1_SECTOR);
    if (!write_sector(m, bank + 4u * FNV1_SECTOR, m->work) || !barrier(m))
        return broken(m);
    put32(m->work, FNV1_REDO);
    put32(m->work + 4, 1);
    put64(m->work + 8, sequence);
    put32(m->work + 16, m->redo_count);
    memcpy(m->work + 24, m->config.media_uuid, 16);
    for (i = 0; i < m->redo_count; ++i) {
        put64(m->work + 40u + i * 16u, m->redo_target[i]);
        put32(m->work + 48u + i * 16u,
              crc(m->redo[i], FNV1_SECTOR, SIZE_MAX));
    }
    put32(m->work + 4092, crc(m->work, FNV1_SECTOR, 4092));
    digest = hash_bytes(UINT64_C(1469598103934665603), m->work, FNV1_SECTOR);
    if (!write_sector(m, bank, m->work))
        return broken(m);
    for (i = 0; i < m->redo_count; ++i) {
        digest = hash_bytes(digest, m->redo[i], FNV1_SECTOR);
        if (!write_sector(m, bank + (uint64_t)(i + 1u) * FNV1_SECTOR,
                          m->redo[i]))
            return broken(m);
    }
    if (!barrier(m))
        return broken(m);
    sequence_record(m, FNV1_SEAL, sequence, digest);
    if (!write_sector(m, bank + 4u * FNV1_SECTOR, m->work) || !barrier(m) ||
        !install_redo(m, sequence))
        return broken(m);
    return FWLAB_NFC_API_OK;
}

static int recover_redo(struct fwlab_file_nand_v1 *m)
{
    uint64_t fence[2] = {0, 0}, seq[2] = {0, 0}, seal_hash[2] = {0, 0};
    int valid[2] = {0, 0};
    uint32_t i, j, count;
    uint64_t bank, digest;
    uint32_t expected_crc[FNV1_MAX_POSTIMAGES];
    for (i = 0; i < 2; ++i) {
        if (!read_bytes(m, UINT64_C(8192) + (uint64_t)i * FNV1_SECTOR,
                        m->work, FNV1_SECTOR))
            return 0;
        valid[i] = sequence_valid(m, m->work, FNV1_FENCE) &&
                   get64(m->work + 16) == 0;
        if (valid[i])
            fence[i] = get64(m->work + 8);
    }
    if (!valid[0] && !valid[1])
        return 0;
    m->sequence = fence[0] > fence[1] ? fence[0] : fence[1];
    for (i = 0; i < 2; ++i) {
        bank = FNV1_BANK_BASE + (uint64_t)i * FNV1_BANK_BYTES;
        if (!read_bytes(m, bank + 4u * FNV1_SECTOR, m->work, FNV1_SECTOR))
            return 0;
        if (sequence_valid(m, m->work, FNV1_SEAL)) {
            seq[i] = get64(m->work + 8);
            seal_hash[i] = get64(m->work + 16);
            if (!seq[i] || (seq[i] & 1u) != i)
                return 0;
        }
    }
    for (i = 0; i < 2; ++i) {
        if (seq[i] <= m->sequence)
            continue;
        if (m->sequence == UINT64_MAX || seq[i] != m->sequence + 1u)
            return 0;
        bank = FNV1_BANK_BASE + (uint64_t)i * FNV1_BANK_BYTES;
        if (!read_bytes(m, bank, m->work, FNV1_SECTOR) ||
            !valid_record(m->work, FNV1_REDO) ||
            get64(m->work + 8) != seq[i] ||
            memcmp(m->work + 24, m->config.media_uuid, 16) != 0)
            return 0;
        count = get32(m->work + 16);
        if (!count || count > FNV1_MAX_POSTIMAGES ||
            !zero(m->work + 20, 4) ||
            !zero(m->work + 40u + count * 16u, 4052u - count * 16u))
            return 0;
        m->redo_count = count;
        for (j = 0; j < count; ++j) {
            uint64_t target = get64(m->work + 40u + j * 16u);
            if (target < FNV1_HOME_BASE || target % FNV1_SECTOR ||
                target > m->image_bytes - FNV1_SECTOR ||
                (j && target <= m->redo_target[j - 1u]) ||
                get32(m->work + 52u + j * 16u) != 0)
                return 0;
            m->redo_target[j] = target;
            expected_crc[j] = get32(m->work + 48u + j * 16u);
        }
        digest = hash_bytes(UINT64_C(1469598103934665603), m->work,
                            FNV1_SECTOR);
        for (j = 0; j < count; ++j) {
            if (!read_bytes(m, bank + (uint64_t)(j + 1u) * FNV1_SECTOR,
                            m->redo[j], FNV1_SECTOR) ||
                crc(m->redo[j], FNV1_SECTOR, SIZE_MAX) != expected_crc[j])
                return 0;
            digest = hash_bytes(digest, m->redo[j], FNV1_SECTOR);
        }
        if (digest != seal_hash[i] || !install_redo(m, seq[i]))
            return 0;
    }
    return 1;
}

enum fwlab_nfc_api_result fnv1_engine_open(
    void *arena, size_t arena_size,
    const struct fwlab_file_nand_v1_config *config,
    const struct fnv1_io *io, int format,
    struct fwlab_file_nand_v1 **out)
{
    struct fwlab_file_nand_v1 *m = arena;
    uint32_t blocks, pages, id, i;
    uint64_t pm, bm, bytes, actual_size;
    struct fnv1_block block;
    uint8_t expected[FNV1_SECTOR];
    int supers = 0;
    if (out == NULL)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    *out = NULL;
    if (!arena || arena_size < sizeof(*m) ||
        (uintptr_t)arena % alignof(struct fwlab_file_nand_v1) ||
        !config_layout(config, &blocks, &pages, &pm, &bm, &bytes) ||
        !io || !io->context || !io->read || !io->write || !io->sync ||
        !io->size || !io->resize || !io->close ||
        (format != 0 && format != 1) ||
        io->size(io->context, &actual_size) != FWLAB_NFC_API_OK ||
        actual_size != (format ? 0 : bytes))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    memset(m, 0, sizeof(*m));
    m->magic = FNV1_MAGIC;
    m->config = *config;
    m->io = *io;
    m->pages = pages;
    m->blocks = blocks;
    m->page_metadata_offset = pm;
    m->block_metadata_offset = bm;
    m->image_bytes = bytes;
    if (format) {
        if (m->io.resize(m->io.context, bytes) != FWLAB_NFC_API_OK)
            return broken(m);
        /* New file extension is zero-filled by the substrate. Initialize all
         * health records; a missing/corrupt record is never an erased block. */
        memset(&block, 0, sizeof(block));
        for (id = 0; id < blocks;) {
            uint64_t offset = block_sector(m, id);
            memset(m->work, 0, FNV1_SECTOR);
            for (i = 0; i < 64 && id < blocks; ++i, ++id)
                block_encode(m->work + i * FNV1_BLOCK_BYTES, id, &block, 0);
            if (!write_sector(m, offset, m->work))
                return broken(m);
        }
        sequence_record(m, FNV1_FENCE, 0, 0);
        if (!write_sector(m, 8192, m->work) ||
            !write_sector(m, 12288, m->work) || !barrier(m))
            return broken(m);
        make_super(m, m->work);
        if (!write_sector(m, 0, m->work) || !barrier(m) ||
            !write_sector(m, 4096, m->work) || !barrier(m))
            return broken(m);
    } else {
        make_super(m, expected);
        for (i = 0; i < 2; ++i) {
            if (!read_bytes(m, (uint64_t)i * FNV1_SECTOR,
                            m->work, FNV1_SECTOR))
                return broken(m);
            if (valid_record(m->work, FNV1_SUPER)) {
                if (memcmp(m->work, expected, FNV1_SECTOR) != 0)
                    return broken(m);
                ++supers;
            }
        }
        if (!supers || !recover_redo(m))
            return broken(m);
    }
    *out = m;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result media_read(
    void *opaque, const struct fwlab_nfc_ppa *ppa,
    uint8_t *main, uint32_t main_length, uint8_t *oob, uint32_t oob_length,
    struct fwlab_nand_page_info *page, struct fwlab_nand_block_info *block)
{
    struct fwlab_file_nand_v1 *m = opaque;
    struct fnv1_block b;
    uint32_t bid, pid, main_crc;
    const uint8_t *record;
    int erased = 0;
    if (!live(m) || !main || !oob || !page || !block ||
        main_length != 4096 || oob_length != 128 ||
        !ppa_ids(m, ppa, &bid, &pid))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!block_read(m, bid, &b))
        return broken(m);
    *block = b.info;
    memset(page, 0, sizeof(*page));
    page->version = FWLAB_NFC_CONTRACT_VERSION;
    page->size = (uint16_t)sizeof(*page);
    page->erase_generation_seen = b.info.erase_generation;
    if (b.info.erase_state == FWLAB_NAND_ERASE_TORN &&
        ppa->page < b.erased_prefix) {
        memset(main, 0xff, main_length);
        memset(oob, 0xff, oob_length);
        page->state = FWLAB_NAND_PAGE_TORN;
        return FWLAB_NFC_API_OK;
    }
    if (!read_bytes(m, page_sector(m, pid), m->work, FNV1_SECTOR))
        return broken(m);
    record = m->work + (pid % 16u) * FNV1_RECORD_BYTES;
    if (zero(record, FNV1_RECORD_BYTES)) {
        if (ppa->page < b.info.next_program_page)
            return broken(m);
        erased = 1;
    } else {
        if (get32(record) != FNV1_PAGE || get32(record + 4) != pid ||
            get32(record + 28) != crc(record, FNV1_RECORD_BYTES, 28) ||
            !zero(record + 160, 96) || record[11] != 1 ||
            (record[10] != FWLAB_NAND_PAGE_VALID &&
             record[10] != FWLAB_NAND_PAGE_TORN) ||
            get16(record + 8) > b.info.erase_generation ||
            !get64(record + 12) || get64(record + 12) > m->sequence)
            return broken(m);
        if (get16(record + 8) < b.info.erase_generation) {
            if (ppa->page < b.info.next_program_page)
                return broken(m);
            erased = 1;
        } else if (ppa->page >= b.info.next_program_page) {
            return broken(m);
        }
    }
    if (erased) {
        memset(main, 0xff, main_length);
        memset(oob, 0xff, oob_length);
        page->state = FWLAB_NAND_PAGE_ERASED;
        return FWLAB_NFC_API_OK;
    }
    page->state = record[10];
    page->program_count = record[11];
    main_crc = get32(record + 20);
    memcpy(oob, record + 32, oob_length);
    if (get32(record + 24) != crc(oob, oob_length, SIZE_MAX) ||
        !read_bytes(m, FNV1_HOME_BASE + (uint64_t)pid * FNV1_SECTOR,
                    main, main_length) ||
        main_crc != crc(main, main_length, SIZE_MAX))
        return broken(m);
    return FWLAB_NFC_API_OK;
}

static void no_effect(struct fwlab_nand_media_result *result,
                       const struct fwlab_nand_block_info *block)
{
    memset(result, 0, sizeof(*result));
    result->version = FWLAB_NFC_CONTRACT_VERSION;
    result->size = (uint16_t)sizeof(*result);
    result->block_health = block->health;
    result->base_erase_generation = block->erase_generation;
    result->final_erase_generation = block->erase_generation;
}

static enum fwlab_nfc_api_result media_program(
    void *opaque, const struct fwlab_nfc_ppa *ppa,
    const uint8_t *main, uint32_t main_length,
    const uint8_t *oob, uint32_t oob_length,
    uint32_t applied_main, uint32_t applied_oob, uint8_t integrity,
    struct fwlab_nand_media_result *result)
{
    struct fwlab_file_nand_v1 *m = opaque;
    struct fnv1_block b;
    struct fwlab_nand_page_info page;
    uint32_t bid, pid, i;
    uint8_t old_oob[128];
    uint8_t *record;
    enum fwlab_nfc_api_result status;
    if (!live(m) || !main || !oob || !result || main_length != 4096 ||
        oob_length != 128 || applied_main > main_length ||
        applied_oob > oob_length ||
        (integrity != FWLAB_NFC_INTEGRITY_COMPLETE &&
         integrity != FWLAB_NFC_INTEGRITY_TORN) ||
        !ppa_ids(m, ppa, &bid, &pid))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    status = media_read(m, ppa, m->redo[0], main_length, old_oob,
                        oob_length, &page, &b.info);
    if (status != FWLAB_NFC_API_OK)
        return status;
    no_effect(result, &b.info);
    if (b.info.health != FWLAB_NFC_BLOCK_GOOD) {
        result->reason = FWLAB_NFC_REASON_BAD_BLOCK;
        return FWLAB_NFC_API_OK;
    }
    if (b.info.erase_state != FWLAB_NAND_ERASE_CLEAN ||
        page.state != FWLAB_NAND_PAGE_ERASED || page.program_count) {
        result->reason = FWLAB_NFC_REASON_NOT_ERASED;
        return FWLAB_NFC_API_OK;
    }
    if (ppa->page != b.info.next_program_page) {
        result->reason = FWLAB_NFC_REASON_PROGRAM_ORDER;
        return FWLAB_NFC_API_OK;
    }
    if (!applied_main && !applied_oob)
        return FWLAB_NFC_API_OK;
    if (m->sequence == UINT64_MAX)
        return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    for (i = 0; i < applied_main; ++i)
        m->redo[0][i] &= main[i];
    for (i = 0; i < applied_oob; ++i)
        old_oob[i] &= oob[i];
    m->redo_target[0] = FNV1_HOME_BASE + (uint64_t)pid * FNV1_SECTOR;
    m->redo_target[1] = page_sector(m, pid);
    m->redo_target[2] = block_sector(m, bid);
    if (!read_bytes(m, m->redo_target[1], m->redo[1], FNV1_SECTOR) ||
        !read_bytes(m, m->redo_target[2], m->redo[2], FNV1_SECTOR))
        return broken(m);
    record = m->redo[1] + (pid % 16u) * FNV1_RECORD_BYTES;
    memset(record, 0, FNV1_RECORD_BYTES);
    put32(record, FNV1_PAGE);
    put32(record + 4, pid);
    put16(record + 8, b.info.erase_generation);
    record[10] = integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
                 applied_main == main_length && applied_oob == oob_length ?
                     FWLAB_NAND_PAGE_VALID : FWLAB_NAND_PAGE_TORN;
    record[11] = 1;
    put64(record + 12, m->sequence + 1u);
    put32(record + 20, crc(m->redo[0], FNV1_SECTOR, SIZE_MAX));
    put32(record + 24, crc(old_oob, sizeof(old_oob), SIZE_MAX));
    memcpy(record + 32, old_oob, sizeof(old_oob));
    put32(record + 28, crc(record, FNV1_RECORD_BYTES, 28));
    ++b.info.next_program_page;
    b.erased_prefix = 0;
    block_encode(m->redo[2] + (bid % 64u) * FNV1_BLOCK_BYTES,
                 bid, &b, m->sequence + 1u);
    m->redo_count = 3;
    status = commit_redo(m);
    if (status == FWLAB_NFC_API_OK) {
        result->physical_outcome = FWLAB_NFC_PHYS_APPLIED;
        result->integrity = integrity;
        result->applied_main_bytes = applied_main;
        result->applied_oob_bytes = applied_oob;
        result->applied_region_mask =
            (applied_main ? FWLAB_NFC_REGION_MAIN : 0u) |
            (applied_oob ? FWLAB_NFC_REGION_OOB : 0u);
    }
    return status;
}

static enum fwlab_nfc_api_result block_commit(
    struct fwlab_file_nand_v1 *m, uint32_t bid, const struct fnv1_block *b)
{
    if (m->sequence == UINT64_MAX)
        return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    m->redo_count = 1;
    m->redo_target[0] = block_sector(m, bid);
    if (!read_bytes(m, m->redo_target[0], m->redo[0], FNV1_SECTOR))
        return broken(m);
    block_encode(m->redo[0] + (bid % 64u) * FNV1_BLOCK_BYTES,
                 bid, b, m->sequence + 1u);
    return commit_redo(m);
}

static enum fwlab_nfc_api_result media_erase(
    void *opaque, const struct fwlab_nfc_ppa *ppa, uint32_t applied_pages,
    uint8_t integrity, struct fwlab_nand_media_result *result)
{
    struct fwlab_file_nand_v1 *m = opaque;
    struct fnv1_block b;
    uint32_t bid, pid;
    enum fwlab_nfc_api_result status;
    if (!live(m) || !result || !ppa_ids(m, ppa, &bid, &pid) || ppa->page ||
        applied_pages > m->config.geometry.pages_per_block ||
        (integrity != FWLAB_NFC_INTEGRITY_COMPLETE &&
         integrity != FWLAB_NFC_INTEGRITY_TORN))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!block_read(m, bid, &b))
        return broken(m);
    no_effect(result, &b.info);
    if (b.info.health != FWLAB_NFC_BLOCK_GOOD) {
        result->reason = FWLAB_NFC_REASON_BAD_BLOCK;
        return FWLAB_NFC_API_OK;
    }
    if (!applied_pages)
        return FWLAB_NFC_API_OK;
    if (b.info.erase_attempt_count == UINT16_MAX || m->sequence == UINT64_MAX)
        return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    ++b.info.erase_attempt_count;
    if (integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
        applied_pages == m->config.geometry.pages_per_block) {
        if (b.info.erase_generation == UINT16_MAX ||
            b.info.successful_erase_count == UINT16_MAX)
            return FWLAB_NFC_API_COUNTER_EXHAUSTED;
        ++b.info.erase_generation;
        ++b.info.successful_erase_count;
        b.info.next_program_page = 0;
        b.info.erase_state = FWLAB_NAND_ERASE_CLEAN;
        b.erased_prefix = 0;
    } else {
        b.info.erase_state = FWLAB_NAND_ERASE_TORN;
        if (applied_pages > b.erased_prefix)
            b.erased_prefix = (uint16_t)applied_pages;
    }
    status = block_commit(m, bid, &b);
    if (status == FWLAB_NFC_API_OK) {
        result->physical_outcome = FWLAB_NFC_PHYS_APPLIED;
        result->integrity = integrity;
        result->applied_pages = applied_pages;
        result->final_erase_generation = b.info.erase_generation;
    }
    return status;
}

static enum fwlab_nfc_api_result media_bad(
    void *opaque, const struct fwlab_nfc_ppa *ppa)
{
    struct fwlab_file_nand_v1 *m = opaque;
    struct fnv1_block b;
    uint32_t bid, pid;
    if (!live(m) || !ppa_ids(m, ppa, &bid, &pid))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!block_read(m, bid, &b))
        return broken(m);
    if (b.info.health == FWLAB_NFC_BLOCK_RUNTIME_BAD)
        return FWLAB_NFC_API_OK;
    b.info.health = FWLAB_NFC_BLOCK_RUNTIME_BAD;
    return block_commit(m, bid, &b);
}

/* Expensive diagnostic only: never called by normal scheduling or I/O. */
static uint64_t media_hash(void *opaque)
{
    struct fwlab_file_nand_v1 *m = opaque;
    const struct fwlab_nfc_geometry *g;
    uint64_t hash = UINT64_C(1469598103934665603);
    uint32_t bid, p, v;
    struct fwlab_nfc_ppa ppa;
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
    uint8_t oob[128];
    if (!live(m))
        return 0;
    g = &m->config.geometry;
    for (bid = 0; bid < m->blocks; ++bid) {
        memset(&ppa, 0, sizeof(ppa));
        v = bid;
        ppa.block = (uint16_t)(v % g->blocks_per_plane);
        v /= g->blocks_per_plane;
        ppa.plane = (uint16_t)(v % g->planes_per_lun);
        v /= g->planes_per_lun;
        ppa.lun = (uint16_t)(v % g->luns_per_channel);
        ppa.channel = (uint16_t)(v / g->luns_per_channel);
        for (p = 0; p < g->pages_per_block; ++p) {
            ppa.page = (uint16_t)p;
            if (media_read(m, &ppa, m->redo[0], FNV1_SECTOR, oob,
                           sizeof(oob), &page, &block) != FWLAB_NFC_API_OK)
                return 0;
            hash = hash_bytes(hash, m->redo[0], FNV1_SECTOR);
            hash = hash_bytes(hash, oob, sizeof(oob));
            memset(m->work, 0, 16);
            put16(m->work, page.erase_generation_seen);
            m->work[2] = page.state;
            m->work[3] = page.program_count;
            put16(m->work + 4, block.erase_generation);
            put16(m->work + 6, block.successful_erase_count);
            put16(m->work + 8, block.erase_attempt_count);
            put16(m->work + 10, block.next_program_page);
            m->work[12] = block.health;
            m->work[13] = block.erase_state;
            hash = hash_bytes(hash, m->work, 16);
        }
    }
    return hash;
}

static const struct fwlab_nand_media_ops media_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_nand_media_ops),
    .read_page = media_read,
    .program = media_program,
    .erase = media_erase,
    .mark_runtime_bad = media_bad,
    .hash = media_hash,
};

struct fwlab_nand_media fwlab_file_nand_v1_media(struct fwlab_file_nand_v1 *m)
{
    struct fwlab_nand_media media = {NULL, NULL};
    if (live(m)) {
        media.ops = &media_ops;
        media.context = m;
    }
    return media;
}

uint64_t fwlab_file_nand_v1_sequence(const struct fwlab_file_nand_v1 *m)
{
    return m != NULL && m->magic == FNV1_MAGIC ? m->sequence : 0;
}

enum fwlab_nfc_api_result fwlab_file_nand_v1_close(struct fwlab_file_nand_v1 *m)
{
    enum fwlab_nfc_api_result result;
    if (m == NULL || m->magic != FNV1_MAGIC || m->closed)
        return FWLAB_NFC_API_WRONG_STATE;
    result = m->io.close(m->io.context);
    m->closed = 1;
    return result;
}
