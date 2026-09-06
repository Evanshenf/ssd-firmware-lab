/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "physical_nand_codec.h"
#include "fwlab/portable/crc32c.h"
#include <string.h>

uint16_t fnv2_get16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
uint32_t fnv2_get32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint64_t fnv2_get64(const uint8_t *p)
{ return (uint64_t)fnv2_get32(p) | ((uint64_t)fnv2_get32(p + 4) << 32); }
void fnv2_put16(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void fnv2_put32(uint8_t *p, uint32_t v)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8u * i)); }
void fnv2_put64(uint8_t *p, uint64_t v)
{ fnv2_put32(p, (uint32_t)v); fnv2_put32(p + 4, (uint32_t)(v >> 32)); }
bool fnv2_all(const uint8_t *p, size_t n, uint8_t value)
{ for (size_t i = 0; i < n; ++i) if (p[i] != value) return false; return true; }
uint32_t fnv2_crc(const uint8_t *p, size_t n) { return fwlab_crc32c(p, n); }
bool fnv2_record_crc(const uint8_t *p, size_t n)
{ return n >= 4 && fnv2_get32(p + n - 4) == fnv2_crc(p, n - 4); }
static void seal(uint8_t *p, size_t n) { fnv2_put32(p + n - 4, fnv2_crc(p, n - 4)); }

bool fnv2_layout(const struct fwlab_file_nand_v2_config *c,
                 uint32_t *blocks, uint32_t *pages, uint64_t *pm,
                 uint64_t *bm, uint64_t *bytes)
{
    const struct fwlab_nfc_geometry *g;
    uint64_t count;
    if (!c || fnv2_all(c->media_uuid, 16, 0)) return false;
    g = &c->geometry;
    if (g->version != FWLAB_NFC_CONTRACT_VERSION || g->size != sizeof(*g) ||
        !g->channels || !g->luns_per_channel || !g->planes_per_lun ||
        !g->blocks_per_plane || (g->pages_per_block != 32 && g->pages_per_block != 64) ||
        !g->plane_parallelism_per_lun || g->plane_parallelism_per_lun > g->planes_per_lun ||
        g->main_bytes_per_page != 4096 || g->oob_bytes_per_page != 128 ||
        g->max_programs_per_erase != 1 || g->program_order != FWLAB_NFC_PROGRAM_ASCENDING ||
        g->reserved0 || g->reserved1[0] || g->reserved1[1]) return false;
    count = (uint64_t)g->channels * g->luns_per_channel;
    if (count > UINT32_MAX / g->planes_per_lun) return false;
    count *= g->planes_per_lun;
    if (count > UINT32_MAX / g->blocks_per_plane) return false;
    count *= g->blocks_per_plane; *blocks = (uint32_t)count;
    if (count > UINT32_MAX / g->pages_per_block) return false;
    count *= g->pages_per_block; *pages = (uint32_t)count;
    *pm = FNV2_HOME_BASE + count * 4096u;
    *bm = *pm + count * FNV2_PAGE_RECORD_BYTES;
    *bytes = *bm + (uint64_t)*blocks * FNV2_BLOCK_RECORD_BYTES;
    return *bytes <= INT64_MAX;
}

void fnv2_super_encode(const struct fwlab_file_nand_v2 *m, uint8_t *out)
{
    const struct fwlab_nfc_geometry *g = &m->config.geometry;
    memset(out, 0, FNV2_SUPER_BYTES);
    fnv2_put32(out, FNV2_SUPER); fnv2_put16(out + 4, 2);
    fnv2_put16(out + 6, FNV2_SUPER_BYTES); memcpy(out + 8, m->config.media_uuid, 16);
    fnv2_put32(out + 24, m->blocks); fnv2_put32(out + 28, m->pages);
    fnv2_put16(out + 32, g->channels); fnv2_put16(out + 34, g->luns_per_channel);
    fnv2_put16(out + 36, g->planes_per_lun); fnv2_put16(out + 38, g->blocks_per_plane);
    fnv2_put16(out + 40, g->pages_per_block); fnv2_put16(out + 42, g->plane_parallelism_per_lun);
    fnv2_put32(out + 44, g->main_bytes_per_page); fnv2_put32(out + 48, g->oob_bytes_per_page);
    fnv2_put16(out + 52, g->max_programs_per_erase); out[54] = g->program_order;
    fnv2_put64(out + 64, FNV2_HOME_BASE); fnv2_put64(out + 72, m->page_metadata_offset);
    fnv2_put64(out + 80, m->block_metadata_offset); fnv2_put64(out + 88, m->image_bytes);
    fnv2_put64(out + 96, FNV2_BANK_BASE); fnv2_put64(out + 104, FNV2_BANK_BYTES);
    fnv2_put32(out + 112, FNV2_INTENT_BYTES); fnv2_put32(out + 116, FNV2_TERMINAL_BYTES);
    fnv2_put32(out + 120, FNV2_PAGE_RECORD_BYTES); fnv2_put32(out + 124, FNV2_BLOCK_RECORD_BYTES);
    seal(out, FNV2_SUPER_BYTES);
}
bool fnv2_super_valid(const struct fwlab_file_nand_v2 *m, const uint8_t *bytes)
{
    uint8_t expected[FNV2_SUPER_BYTES];
    fnv2_super_encode(m, expected);
    return memcmp(bytes, expected, sizeof(expected)) == 0;
}

void fnv2_block_encode(uint8_t *out, uint32_t id, const struct fnv2_block *block)
{
    const struct fwlab_nand_block_info *b = &block->info;
    memset(out, 0, FNV2_BLOCK_RECORD_BYTES); fnv2_put32(out, FNV2_BLOCK); fnv2_put32(out + 4, id);
    fnv2_put16(out + 8, b->erase_generation); fnv2_put16(out + 10, b->successful_erase_count);
    fnv2_put16(out + 12, b->erase_attempt_count); fnv2_put16(out + 14, b->next_program_page);
    out[16] = b->health; out[17] = b->erase_state; fnv2_put16(out + 18, block->erased_prefix);
    fnv2_put64(out + 24, block->sequence); seal(out, FNV2_BLOCK_RECORD_BYTES);
}
bool fnv2_block_decode(const struct fwlab_file_nand_v2 *m, const uint8_t *p,
                       uint32_t id, uint64_t limit, struct fnv2_block *block)
{
    struct fwlab_nand_block_info *b = &block->info;
    if (id >= m->blocks || fnv2_get32(p) != FNV2_BLOCK || fnv2_get32(p + 4) != id ||
        !fnv2_record_crc(p, FNV2_BLOCK_RECORD_BYTES) || !fnv2_all(p + 20, 4, 0) ||
        !fnv2_all(p + 32, 28, 0) || fnv2_get64(p + 24) > limit) return false;
    memset(block, 0, sizeof(*block)); b->version = FWLAB_NFC_CONTRACT_VERSION; b->size = sizeof(*b);
    b->erase_generation = fnv2_get16(p + 8); b->successful_erase_count = fnv2_get16(p + 10);
    b->erase_attempt_count = fnv2_get16(p + 12); b->next_program_page = fnv2_get16(p + 14);
    b->health = p[16]; b->erase_state = p[17]; block->erased_prefix = fnv2_get16(p + 18);
    block->sequence = fnv2_get64(p + 24);
    return b->erase_generation == b->successful_erase_count &&
        b->successful_erase_count <= b->erase_attempt_count && b->health <= FWLAB_NFC_BLOCK_RUNTIME_BAD &&
        b->next_program_page <= m->config.geometry.pages_per_block &&
        b->erase_state <= FWLAB_NAND_ERASE_TORN && block->erased_prefix <= m->config.geometry.pages_per_block &&
        ((b->erase_state == FWLAB_NAND_ERASE_CLEAN) == (block->erased_prefix == 0)) &&
        (block->sequence || (!b->erase_generation && !b->erase_attempt_count && !b->next_program_page &&
                            b->health == FWLAB_NFC_BLOCK_GOOD && !block->erased_prefix));
}

void fnv2_page_encode(uint8_t *out, uint32_t id, uint16_t generation,
                      uint8_t state, uint64_t sequence, const uint8_t *main, const uint8_t *oob)
{
    memset(out, 0, FNV2_PAGE_RECORD_BYTES); fnv2_put32(out, FNV2_PAGE); fnv2_put32(out + 4, id);
    fnv2_put16(out + 8, generation); out[10] = state; out[11] = 1; fnv2_put64(out + 12, sequence);
    if (state == FNV2_UNKNOWN_PAGE) memset(out + 32, 0xff, 128);
    else {
        fnv2_put32(out + 20, fnv2_crc(main, 4096)); fnv2_put32(out + 24, fnv2_crc(oob, 128));
        memcpy(out + 32, oob, 128);
    }
    seal(out, FNV2_PAGE_RECORD_BYTES);
}
bool fnv2_page_valid(const uint8_t *p, uint32_t id, uint64_t limit)
{
    if (fnv2_get32(p) != FNV2_PAGE || fnv2_get32(p + 4) != id ||
        !fnv2_record_crc(p, FNV2_PAGE_RECORD_BYTES) || !fnv2_all(p + 28, 4, 0) ||
        !fnv2_all(p + 160, 92, 0) || p[11] != 1 || p[10] < FWLAB_NAND_PAGE_VALID ||
        p[10] > FNV2_UNKNOWN_PAGE || !fnv2_get64(p + 12) || fnv2_get64(p + 12) > limit) return false;
    return p[10] == FNV2_UNKNOWN_PAGE ?
        fnv2_get32(p + 20) == 0 && fnv2_get32(p + 24) == 0 && fnv2_all(p + 32, 128, 0xff) :
        fnv2_get32(p + 24) == fnv2_crc(p + 32, 128);
}

void fnv2_intent_encode(const struct fwlab_file_nand_v2 *m,
                        const struct fnv2_intent *i, uint8_t *out)
{
    memset(out, 0, FNV2_INTENT_BYTES); fnv2_put32(out, FNV2_INTENT); fnv2_put16(out + 4, 2);
    fnv2_put16(out + 6, i->kind); fnv2_put64(out + 8, i->sequence); fnv2_put64(out + 16, i->predecessor);
    memcpy(out + 24, m->config.media_uuid, 16); fnv2_put32(out + 40, i->block);
    fnv2_put16(out + 44, i->first); fnv2_put16(out + 46, i->count); fnv2_put16(out + 48, i->maximum_prefix);
    fnv2_block_encode(out + 64, i->block, &i->base); seal(out, FNV2_INTENT_BYTES);
}
bool fnv2_intent_decode(const struct fwlab_file_nand_v2 *m, const uint8_t *p,
                        unsigned bank, struct fnv2_intent *i)
{
    uint32_t ppb = m->config.geometry.pages_per_block;
    if (fnv2_get32(p) != FNV2_INTENT || fnv2_get16(p + 4) != 2 ||
        !fnv2_record_crc(p, FNV2_INTENT_BYTES) || memcmp(p + 24, m->config.media_uuid, 16) ||
        !fnv2_all(p + 50, 14, 0) || !fnv2_all(p + 128, 892, 0)) return false;
    memset(i, 0, sizeof(*i)); i->kind = fnv2_get16(p + 6); i->sequence = fnv2_get64(p + 8);
    i->predecessor = fnv2_get64(p + 16); i->block = fnv2_get32(p + 40);
    i->first = fnv2_get16(p + 44); i->count = fnv2_get16(p + 46); i->maximum_prefix = fnv2_get16(p + 48);
    i->crc = fnv2_get32(p + 1020);
    if (!i->sequence || i->predecessor != i->sequence - 1u || (i->sequence & 1u) != bank ||
        !fnv2_block_decode(m, p + 64, i->block, i->predecessor, &i->base)) return false;
    if (i->kind == FNV2_PROGRAM)
        return i->base.info.health == FWLAB_NFC_BLOCK_GOOD && i->base.info.erase_state == FWLAB_NAND_ERASE_CLEAN &&
            i->count && i->count <= FWLAB_FILE_NAND_V2_MAX_BATCH_PAGES && i->first == i->base.info.next_program_page &&
            i->first < ppb && i->count <= ppb - i->first && !i->maximum_prefix;
    if (i->kind == FNV2_ERASE)
        return i->base.info.health == FWLAB_NFC_BLOCK_GOOD && i->base.info.erase_attempt_count < UINT16_MAX &&
            !i->first && i->count && i->count <= ppb && i->maximum_prefix == i->count;
    return i->kind == FNV2_MARK_BAD && !i->first && !i->count && !i->maximum_prefix &&
        i->base.info.health != FWLAB_NFC_BLOCK_RUNTIME_BAD;
}

void fnv2_terminal_encode(const struct fwlab_file_nand_v2 *m,
                          const struct fnv2_terminal *t, uint8_t *out)
{
    memset(out, 0, FNV2_TERMINAL_BYTES); fnv2_put32(out, FNV2_TERMINAL); fnv2_put16(out + 4, 2);
    fnv2_put16(out + 6, t->disposition); fnv2_put64(out + 8, t->sequence); fnv2_put64(out + 16, t->predecessor);
    memcpy(out + 24, m->config.media_uuid, 16); fnv2_put32(out + 40, t->intent_crc);
    fnv2_put32(out + 44, t->kind); seal(out, FNV2_TERMINAL_BYTES);
}
bool fnv2_terminal_decode(const struct fwlab_file_nand_v2 *m, const uint8_t *p,
                          unsigned bank, struct fnv2_terminal *t)
{
    if (fnv2_get32(p) != FNV2_TERMINAL || fnv2_get16(p + 4) != 2 ||
        !fnv2_record_crc(p, FNV2_TERMINAL_BYTES) || memcmp(p + 24, m->config.media_uuid, 16) ||
        !fnv2_all(p + 48, 460, 0)) return false;
    memset(t, 0, sizeof(*t)); t->sequence = fnv2_get64(p + 8); t->predecessor = fnv2_get64(p + 16);
    t->intent_crc = fnv2_get32(p + 40); t->kind = fnv2_get32(p + 44); t->disposition = fnv2_get16(p + 6);
    if (t->disposition == FNV2_BOOTSTRAP)
        return !t->sequence && !t->predecessor && !t->intent_crc && !t->kind;
    return (t->disposition == FNV2_COMMIT || t->disposition == FNV2_ABORT) &&
        t->sequence && t->predecessor == t->sequence - 1u && (t->sequence & 1u) == bank &&
        t->kind >= FNV2_PROGRAM && t->kind <= FNV2_MARK_BAD;
}
bool fnv2_terminal_matches(const struct fnv2_terminal *t, const struct fnv2_intent *i)
{
    return t->disposition != FNV2_BOOTSTRAP && t->sequence == i->sequence &&
        t->predecessor == i->predecessor && t->intent_crc == i->crc && t->kind == i->kind;
}
