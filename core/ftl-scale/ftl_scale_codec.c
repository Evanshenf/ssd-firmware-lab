/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "ftl_scale_codec.h"
#include "fwlab/portable/crc32c.h"
#include <string.h>

#define SF_ROOT_TAG UINT32_C(0x31524653)
#define SF_PAGE_TAG UINT32_C(0x31504653)
#define SF_RAIL_TAG UINT32_C(0x31484653)
#define SF_JOURNAL_TAG UINT32_C(0x314a4653)
enum { SF_PAGE_ROOT = 1, SF_PAGE_CP, SF_PAGE_RAIL, SF_PAGE_JOURNAL, SF_PAGE_DATA };

static uint16_t get16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t get64(const uint8_t *p) { return get32(p) | ((uint64_t)get32(p + 4) << 32); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { unsigned i; for (i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8u)); }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }

bool sf_bytes_zero(const void *opaque, size_t size)
{
    const uint8_t *p = opaque; size_t i;
    if (!p && size) return false;
    for (i = 0; i < size; ++i) if (p[i]) return false;
    return true;
}
bool sf_bytes_ff(const uint8_t *p, size_t size)
{
    size_t i; if (!p && size) return false;
    for (i = 0; i < size; ++i) if (p[i] != 0xff) return false;
    return true;
}
uint32_t sf_crc32c(const uint8_t *p, size_t size)
{
    return fwlab_crc32c(p, size);
}
uint64_t sf_digest(uint64_t prior, const uint8_t *p, size_t size)
{
    size_t i; for (i = 0; i < size; ++i) { prior ^= p[i]; prior *= UINT64_C(1099511628211); }
    return prior;
}
uint64_t sf_page_digest(uint64_t prior, const uint8_t *main, const uint8_t *oob)
{
    return sf_digest(sf_digest(prior, oob, SF_OOB_BYTES), main, SF_PAGE_BYTES);
}

bool sf_geometry_counts(const struct fwlab_nfc_geometry *g, uint32_t *blocks, uint32_t *pages)
{
    uint64_t n;
    if (!g || !blocks || !pages || g->version != FWLAB_NFC_CONTRACT_VERSION ||
        g->size != sizeof(*g) || !g->channels || g->channels > 4 ||
        !g->luns_per_channel || g->luns_per_channel > 4 || !g->planes_per_lun ||
        g->planes_per_lun > 4 || !g->blocks_per_plane ||
        (g->pages_per_block != 32 && g->pages_per_block != 64) ||
        !g->plane_parallelism_per_lun || g->plane_parallelism_per_lun > g->planes_per_lun ||
        g->main_bytes_per_page != SF_PAGE_BYTES || g->oob_bytes_per_page != SF_OOB_BYTES ||
        g->max_programs_per_erase != 1 || g->program_order != FWLAB_NFC_PROGRAM_ASCENDING ||
        g->reserved0 || g->reserved1[0] || g->reserved1[1]) return false;
    n = (uint64_t)g->channels * g->luns_per_channel * g->planes_per_lun * g->blocks_per_plane;
    if (n > UINT32_MAX || n > UINT32_MAX / g->pages_per_block) return false;
    *blocks = (uint32_t)n; *pages = (uint32_t)(n * g->pages_per_block);
    return true;
}

bool sf_layout_make(const struct fwlab_nfc_geometry *g, uint64_t lbas, struct sf_layout *l)
{
    uint32_t blocks, pages, bank, rail; uint64_t next, cp, slots;
    if (!l || !sf_geometry_counts(g, &blocks, &pages) || !lbas || lbas % 8 ||
        lbas / 8 > UINT32_MAX) return false;
    memset(l, 0, sizeof(*l)); l->geometry = *g; l->lba_count = lbas;
    l->lpn_count = (uint32_t)(lbas / 8); l->physical_pages = pages; l->physical_blocks = blocks;
    l->cp_map_pages = (uint32_t)(((uint64_t)l->lpn_count + 255u) / 256u);
    l->cp_block_pages = (uint32_t)(((uint64_t)blocks + 255u) / 256u);
    cp = (uint64_t)l->cp_map_pages + l->cp_block_pages;
    l->cp_blocks = (uint32_t)((cp + g->pages_per_block - 1u) / g->pages_per_block);
    slots = l->lpn_count / 64u; if (slots > 65536) slots = 65536;
    if (slots < 2u * g->pages_per_block) slots = 2u * g->pages_per_block;
    l->journal_slots = (uint32_t)slots;
    l->journal_blocks = (uint32_t)((slots + g->pages_per_block - 1u) / g->pages_per_block);
    next = 2;
    for (bank = 0; bank < 2; ++bank) { l->cp_base[bank] = (uint32_t)next; next += l->cp_blocks; }
    for (bank = 0; bank < 2; ++bank) for (rail = 0; rail < 2; ++rail) {
        l->journal_base[bank][rail] = (uint32_t)next; next += l->journal_blocks;
    }
    /* Keep two reserve/head blocks and enough invalid space to select a
     * victim leaving a full three-page Host group, not merely one free page. */
    if (next + 2u >= blocks || (uint64_t)l->lpn_count >
        ((uint64_t)blocks - next - 2u) * (g->pages_per_block - 3u)) return false;
    l->data_first_block = (uint32_t)next;
    return true;
}

uint32_t sf_cp_ppa(const struct sf_root *r, uint32_t ordinal)
{ return r->layout.cp_base[r->bank] * r->layout.geometry.pages_per_block + ordinal; }
uint32_t sf_journal_ppa(const struct sf_root *r, uint32_t rail, uint32_t ordinal)
{ return r->layout.journal_base[r->bank][rail] * r->layout.geometry.pages_per_block + ordinal; }

static void page_oob(const uint8_t uuid[16], uint16_t kind, uint64_t epoch,
                      uint32_t ppa, uint32_t ordinal, uint32_t part, uint32_t count,
                      uint64_t identity, uint64_t auxiliary, uint32_t lpn,
                      uint16_t generation, uint8_t mask, uint8_t state,
                      const uint8_t *main, uint8_t *oob)
{
    memset(oob, 0, SF_OOB_BYTES); put32(oob, SF_PAGE_TAG); put16(oob + 4, SF_FORMAT_VERSION);
    put16(oob + 6, kind); memcpy(oob + 8, uuid, 16); put64(oob + 24, epoch);
    put32(oob + 32, ppa); put32(oob + 36, ordinal); put32(oob + 40, part);
    put32(oob + 44, count); put64(oob + 48, identity); put64(oob + 56, auxiliary);
    put32(oob + 64, lpn); put16(oob + 68, generation); oob[70] = mask; oob[71] = state;
    put32(oob + 72, sf_crc32c(main, SF_PAGE_BYTES));
    put32(oob + 124, sf_crc32c(oob, 124));
}

static bool oob_basic(const uint8_t *oob)
{
    return get32(oob) == SF_PAGE_TAG && get16(oob + 4) == SF_FORMAT_VERSION &&
        sf_bytes_zero(oob + 76, 48) && get32(oob + 124) == sf_crc32c(oob, 124);
}

static bool meta_oob_valid(const struct sf_root *r, uint16_t kind, uint32_t ppa,
                           uint32_t ordinal, uint32_t part, uint32_t count,
                           uint64_t identity, const uint8_t *main, const uint8_t *oob)
{
    uint8_t expected[SF_OOB_BYTES];
    page_oob(r->media_uuid, kind, r->generation, ppa, ordinal, part, count,
             identity, 0, 0, 0, 0, 0, main, expected);
    return memcmp(expected, oob, SF_OOB_BYTES) == 0;
}

static void map_encode(uint8_t *p, const struct sf_map_entry *m)
{
    put32(p, m->ppa); put16(p + 4, m->erase_generation); p[6] = m->valid_mask;
    p[7] = m->state; put64(p + 8, m->data_uid);
}
static bool map_decode(const uint8_t *p, struct sf_map_entry *m)
{
    m->ppa = get32(p); m->erase_generation = get16(p + 4); m->valid_mask = p[6];
    m->state = p[7]; m->data_uid = get64(p + 8);
    if (m->state == SF_VALUE) return m->valid_mask && m->ppa != SF_NONE && m->data_uid;
    return m->state <= SF_TOMBSTONE && m->ppa == SF_NONE &&
        !m->valid_mask && !m->erase_generation && !m->data_uid;
}
static void block_encode(uint8_t *p, const struct sf_block_disk *b)
{
    put64(p, b->block_uid); put16(p + 8, b->erase_generation);
    put16(p + 10, b->allocation_end); p[12] = b->role; p[13] = b->health; put16(p + 14, b->flags);
}

bool sf_root_encode(const struct sf_root *r, uint8_t *main, uint8_t *oob)
{
    const struct sf_layout *l = &r->layout; const struct fwlab_nfc_geometry *g = &l->geometry;
    uint32_t i, j;
    if (r->bank > 1 || !r->generation || !r->next_block_uid) return false;
    memset(main, 0, SF_PAGE_BYTES); put32(main, SF_ROOT_TAG); put16(main + 4, SF_FORMAT_VERSION);
    put16(main + 6, 256); memcpy(main + 8, r->media_uuid, 16);
    put64(main + 24, r->generation); put64(main + 32, r->covered_record_seq);
    put64(main + 40, r->covered_map_seq); put64(main + 48, r->durable_frontier);
    put64(main + 56, r->next_block_uid); put64(main + 64, r->cp_digest);
    put64(main + 72, r->rail_header_digest[0]); put64(main + 80, r->rail_header_digest[1]);
    put32(main + 88, r->bank); put64(main + 96, l->lba_count);
    put32(main + 104, l->lpn_count); put32(main + 108, l->physical_pages);
    put32(main + 112, l->physical_blocks); put32(main + 116, l->cp_map_pages);
    put32(main + 120, l->cp_block_pages); put32(main + 124, l->cp_blocks);
    put32(main + 128, l->journal_slots); put32(main + 132, l->journal_blocks);
    put32(main + 136, l->data_first_block); put32(main + 140, SF_PAGE_BYTES);
    put32(main + 144, SF_OOB_BYTES); put32(main + 148, FWLAB_FTL_SCALE_LBA_BYTES);
    put16(main + 152, g->channels); put16(main + 154, g->luns_per_channel);
    put16(main + 156, g->planes_per_lun); put16(main + 158, g->blocks_per_plane);
    put16(main + 160, g->pages_per_block); put16(main + 162, g->plane_parallelism_per_lun);
    main[164] = g->max_programs_per_erase; main[165] = g->program_order;
    for (i = 0; i < 2; ++i) {
        put32(main + 168 + i * 4u, l->cp_base[i]);
        for (j = 0; j < 2; ++j) put32(main + 176 + (i * 2u + j) * 4u, l->journal_base[i][j]);
    }
    put32(main + 4092, sf_crc32c(main, 4092));
    page_oob(r->media_uuid, SF_PAGE_ROOT, r->generation, r->bank * g->pages_per_block,
        0, r->bank, 1, r->generation, 0, 0, 0, 0, 0, main, oob);
    return true;
}

bool sf_root_decode(const struct fwlab_ftl_scale *f, uint32_t bank,
                     const uint8_t *main, const uint8_t *oob, struct sf_root *r)
{
    uint8_t expected[SF_PAGE_BYTES], expected_oob[SF_OOB_BYTES];
    if (!f || !r || bank > 1 || get32(main) != SF_ROOT_TAG ||
        get16(main + 4) != SF_FORMAT_VERSION || get16(main + 6) != 256 ||
        get32(main + 4092) != sf_crc32c(main, 4092) ||
        memcmp(main + 8, f->config.media_uuid, 16) != 0 || get32(main + 88) != bank) return false;
    memset(r, 0, sizeof(*r)); memcpy(r->media_uuid, main + 8, 16);
    if (!sf_layout_make(&f->config.geometry, get64(main + 96), &r->layout) ||
        r->layout.lpn_count > f->config.mapping_slots) return false;
    r->generation = get64(main + 24); r->covered_record_seq = get64(main + 32);
    r->covered_map_seq = get64(main + 40); r->durable_frontier = get64(main + 48);
    r->next_block_uid = get64(main + 56); r->cp_digest = get64(main + 64);
    r->rail_header_digest[0] = get64(main + 72); r->rail_header_digest[1] = get64(main + 80); r->bank = bank;
    if (!r->generation || (uint32_t)((r->generation - 1u) & 1u) != bank ||
        !r->next_block_uid || r->next_block_uid > UINT64_MAX / r->layout.geometry.pages_per_block ||
        r->covered_map_seq > r->covered_record_seq ||
        r->covered_record_seq > f->config.record_sequence_limit ||
        r->durable_frontier > f->config.host_sequence_limit ||
        !sf_root_encode(r, expected, expected_oob)) return false;
    return memcmp(main, expected, SF_PAGE_BYTES) == 0 && memcmp(oob, expected_oob, SF_OOB_BYTES) == 0;
}

bool sf_cp_encode(const struct fwlab_ftl_scale *f, const struct sf_root *r,
                   uint32_t ordinal, uint8_t *main, uint8_t *oob)
{
    uint32_t start, count, i, part; bool maps = ordinal < r->layout.cp_map_pages;
    if (ordinal >= r->layout.cp_map_pages + r->layout.cp_block_pages) return false;
    part = maps ? 1u : 2u;
    start = (maps ? ordinal : ordinal - r->layout.cp_map_pages) * 256u;
    count = (maps ? r->layout.lpn_count : r->layout.physical_blocks) - start;
    if (count > 256) count = 256;
    memset(main, 0, SF_PAGE_BYTES);
    for (i = 0; i < count; ++i) {
        if (maps) map_encode(main + i * 16u, &f->map[start + i]);
        else block_encode(main + i * 16u, &f->blocks[start + i].disk);
    }
    page_oob(r->media_uuid, SF_PAGE_CP, r->generation, sf_cp_ppa(r, ordinal), ordinal,
        part, count, start, 0, 0, 0, 0, 0, main, oob);
    return true;
}

bool sf_cp_decode(struct fwlab_ftl_scale *f, uint32_t ordinal,
                   const uint8_t *main, const uint8_t *oob)
{
    const struct sf_root *r = &f->root; uint32_t start, count, i; bool maps;
    if (ordinal >= r->layout.cp_map_pages + r->layout.cp_block_pages) return false;
    maps = ordinal < r->layout.cp_map_pages;
    start = (maps ? ordinal : ordinal - r->layout.cp_map_pages) * 256u;
    count = (maps ? r->layout.lpn_count : r->layout.physical_blocks) - start;
    if (count > 256) count = 256;
    if (!meta_oob_valid(r, SF_PAGE_CP, sf_cp_ppa(r, ordinal), ordinal,
        maps ? 1u : 2u, count, start, main, oob) || !sf_bytes_zero(main + count * 16u, SF_PAGE_BYTES - count * 16u)) return false;
    for (i = 0; i < count; ++i) {
        const uint8_t *p = main + i * 16u;
        if (maps) { if (!map_decode(p, &f->map[start + i])) return false; }
        else {
            struct sf_block_disk *b = &f->blocks[start + i].disk;
            b->block_uid = get64(p); b->erase_generation = get16(p + 8);
            b->allocation_end = get16(p + 10); b->role = p[12]; b->health = p[13]; b->flags = get16(p + 14);
            if (b->role < SF_META || b->role > SF_BAD || b->flags ||
                b->health > FWLAB_NFC_BLOCK_RUNTIME_BAD || b->allocation_end > r->layout.geometry.pages_per_block ||
                b->block_uid >= r->next_block_uid || b->role == SF_GC_DEST ||
                ((start + i < r->layout.data_first_block) != (b->role == SF_META))) return false;
        }
    }
    return true;
}

void sf_rail_header_encode(const struct sf_root *r, uint32_t rail, uint8_t *main, uint8_t *oob)
{
    memset(main, 0, SF_PAGE_BYTES); put32(main, SF_RAIL_TAG); put32(main + 4, SF_FORMAT_VERSION);
    memcpy(main + 8, r->media_uuid, 16); put64(main + 24, r->generation);
    put64(main + 32, r->covered_record_seq); put64(main + 40, r->covered_map_seq);
    put64(main + 48, r->durable_frontier); put64(main + 56, r->cp_digest);
    put32(main + 64, r->bank); put32(main + 68, rail); put32(main + 72, r->layout.journal_slots);
    put64(main + 80, r->layout.lba_count);
    page_oob(r->media_uuid, SF_PAGE_RAIL, r->generation, sf_journal_ppa(r, rail, 0),
        0, rail, 1, r->generation, 0, 0, 0, 0, 0, main, oob);
}
bool sf_rail_header_valid(const struct sf_root *r, uint32_t rail, const uint8_t *main, const uint8_t *oob)
{
    uint8_t expected[SF_PAGE_BYTES], expected_oob[SF_OOB_BYTES];
    sf_rail_header_encode(r, rail, expected, expected_oob);
    return memcmp(main, expected, SF_PAGE_BYTES) == 0 && memcmp(oob, expected_oob, SF_OOB_BYTES) == 0 &&
        sf_page_digest(SF_DIGEST_SEED, main, oob) == r->rail_header_digest[rail];
}

bool sf_record_encode(const struct sf_root *r, const struct sf_record *j,
                       uint32_t ordinal, uint32_t rail, uint8_t *main, uint8_t *oob)
{
    uint32_t i; uint8_t *p;
    if (!ordinal || ordinal >= r->layout.journal_slots || rail > 1 || j->epoch != r->generation ||
        !j->sequence || j->count > SF_MAX_DELTAS || j->kind < SF_OPEN_HOST || j->kind > SF_ERASE_DONE) return false;
    memset(main, 0, SF_PAGE_BYTES); put32(main, SF_JOURNAL_TAG); put16(main + 4, SF_FORMAT_VERSION); put16(main + 6, 128);
    put64(main + 8, j->epoch); put64(main + 16, j->sequence); put64(main + 24, j->predecessor);
    put64(main + 32, j->before_map_seq); put64(main + 40, j->after_map_seq);
    put64(main + 48, j->durable_frontier); put64(main + 56, j->block_uid);
    put64(main + 64, j->other_block_uid); put64(main + 72, j->intent_sequence);
    put32(main + 80, j->block); put32(main + 84, j->other_block);
    put16(main + 88, j->erase_generation); put16(main + 90, j->final_erase_generation);
    put16(main + 92, j->count); main[94] = j->kind; main[95] = j->health;
    memcpy(main + 96, r->media_uuid, 16); put32(main + 112, ordinal);
    for (i = 0; i < j->count; ++i) {
        p = main + 128u + i * 40u; put32(p, j->delta[i].lpn); put32(p + 4, j->delta[i].reserved);
        map_encode(p + 8, &j->delta[i].before); map_encode(p + 24, &j->delta[i].after);
    }
    page_oob(r->media_uuid, SF_PAGE_JOURNAL, r->generation, sf_journal_ppa(r, rail, ordinal),
        ordinal, rail, j->count, j->sequence, 0, 0, 0, 0, 0, main, oob);
    return true;
}
bool sf_record_decode(const struct sf_root *r, uint32_t ordinal, uint32_t rail,
                       const uint8_t *main, const uint8_t *oob, struct sf_record *j)
{
    uint32_t i; const uint8_t *p;
    if (get32(main) != SF_JOURNAL_TAG || get16(main + 4) != SF_FORMAT_VERSION || get16(main + 6) != 128 ||
        get64(main + 8) != r->generation || memcmp(main + 96, r->media_uuid, 16) ||
        get32(main + 112) != ordinal || !sf_bytes_zero(main + 116, 12)) return false;
    memset(j, 0, sizeof(*j)); j->epoch = get64(main + 8); j->sequence = get64(main + 16);
    j->predecessor = get64(main + 24); j->before_map_seq = get64(main + 32); j->after_map_seq = get64(main + 40);
    j->durable_frontier = get64(main + 48); j->block_uid = get64(main + 56); j->other_block_uid = get64(main + 64);
    j->intent_sequence = get64(main + 72); j->block = get32(main + 80); j->other_block = get32(main + 84);
    j->erase_generation = get16(main + 88); j->final_erase_generation = get16(main + 90);
    j->count = get16(main + 92); j->kind = main[94]; j->health = main[95];
    if (!j->sequence || j->predecessor == UINT64_MAX || j->sequence != j->predecessor + 1u ||
        j->count > SF_MAX_DELTAS || j->kind < SF_OPEN_HOST || j->kind > SF_ERASE_DONE ||
        !meta_oob_valid(r, SF_PAGE_JOURNAL, sf_journal_ppa(r, rail, ordinal), ordinal, rail, j->count,
            j->sequence, main, oob) || !sf_bytes_zero(main + 128u + j->count * 40u, SF_PAGE_BYTES - 128u - j->count * 40u)) return false;
    for (i = 0; i < j->count; ++i) {
        p = main + 128u + i * 40u; j->delta[i].lpn = get32(p); j->delta[i].reserved = get32(p + 4);
        if (j->delta[i].reserved || !map_decode(p + 8, &j->delta[i].before) || !map_decode(p + 24, &j->delta[i].after)) return false;
    }
    return true;
}

void sf_data_oob_encode(const struct fwlab_ftl_scale *f, uint32_t lpn, const struct sf_map_entry *entry,
                        uint64_t block_uid, const uint8_t main[SF_PAGE_BYTES], uint8_t oob[SF_OOB_BYTES])
{
    /* DATA survives checkpoint epochs; its immutable identity is block UID and PPA. */
    page_oob(f->config.media_uuid, SF_PAGE_DATA, 0, entry->ppa,
        entry->ppa % f->config.geometry.pages_per_block, 0, 1, entry->data_uid, block_uid,
        lpn, entry->erase_generation, entry->valid_mask, entry->state, main, oob);
}
bool sf_data_oob_validate(const struct fwlab_ftl_scale *f, uint32_t lpn, const struct sf_map_entry *entry,
                          uint64_t block_uid, const uint8_t main[SF_PAGE_BYTES], const uint8_t oob[SF_OOB_BYTES])
{
    uint8_t expected[SF_OOB_BYTES]; uint64_t page = entry->ppa % f->config.geometry.pages_per_block;
    if (entry->state != SF_VALUE || !entry->valid_mask || !block_uid ||
        block_uid > (UINT64_MAX - page) / f->config.geometry.pages_per_block ||
        entry->data_uid != block_uid * f->config.geometry.pages_per_block + page) return false;
    sf_data_oob_encode(f, lpn, entry, block_uid, main, expected);
    return memcmp(expected, oob, SF_OOB_BYTES) == 0;
}
bool sf_data_oob_lpn(const uint8_t oob[SF_OOB_BYTES], uint32_t *lpn)
{
    if (!oob || !lpn || !oob_basic(oob) || get16(oob + 6) != SF_PAGE_DATA) return false;
    *lpn = get32(oob + 64); return true;
}
