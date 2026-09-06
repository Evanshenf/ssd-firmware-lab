/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "physical_nand_codec.h"
#include <stdalign.h>
#include <string.h>

static bool live(const struct fwlab_file_nand_v2 *m)
{ return m && m->magic == FNV2_MAGIC && !m->closed && !m->quarantined && !m->busy; }
static enum fwlab_nfc_api_result broken(struct fwlab_file_nand_v2 *m)
{ m->quarantined = 1; m->busy = 0; return FWLAB_NFC_API_INVARIANT_FAILURE; }
static bool read_bytes(struct fwlab_file_nand_v2 *m, uint64_t at, void *out, size_t n)
{
    if (at > m->image_bytes || n > m->image_bytes - at ||
        m->io.read(m->io.context, at, out, n) != FWLAB_NFC_API_OK) { (void)broken(m); return false; }
    return true;
}
static bool write_bytes(struct fwlab_file_nand_v2 *m, uint64_t at, const void *in, size_t n)
{
    if (!n || at > m->image_bytes || n > m->image_bytes - at ||
        m->io.write(m->io.context, at, in, n) != FWLAB_NFC_API_OK) { (void)broken(m); return false; }
    return true;
}
static bool barrier(struct fwlab_file_nand_v2 *m)
{
    if (m->io.sync(m->io.context) != FWLAB_NFC_API_OK) { (void)broken(m); return false; }
    return true;
}
static uint64_t bank_at(uint64_t sequence)
{ return FNV2_BANK_BASE + (sequence & 1u) * FNV2_BANK_BYTES; }
static uint64_t block_at(const struct fwlab_file_nand_v2 *m, uint32_t id)
{ return m->block_metadata_offset + (uint64_t)id * FNV2_BLOCK_RECORD_BYTES; }
static uint64_t page_at(const struct fwlab_file_nand_v2 *m, uint32_t id)
{ return m->page_metadata_offset + (uint64_t)id * FNV2_PAGE_RECORD_BYTES; }
static bool ppa_ids(const struct fwlab_file_nand_v2 *m, const struct fwlab_nfc_ppa *p,
                    uint32_t *block, uint32_t *page)
{
    const struct fwlab_nfc_geometry *g = &m->config.geometry;
    uint64_t id;
    if (!p || p->reserved || p->channel >= g->channels || p->lun >= g->luns_per_channel ||
        p->plane >= g->planes_per_lun || p->block >= g->blocks_per_plane || p->page >= g->pages_per_block)
        return false;
    id = ((uint64_t)p->channel * g->luns_per_channel + p->lun) * g->planes_per_lun + p->plane;
    id = id * g->blocks_per_plane + p->block;
    *block = (uint32_t)id; *page = (uint32_t)(id * g->pages_per_block + p->page); return true;
}
static bool load_block(struct fwlab_file_nand_v2 *m, uint32_t id, struct fnv2_block *b)
{
    uint8_t bytes[FNV2_BLOCK_RECORD_BYTES];
    if (!read_bytes(m, block_at(m, id), bytes, sizeof(bytes)) ||
        !fnv2_block_decode(m, bytes, id, m->sequence, b)) { (void)broken(m); return false; }
    return true;
}
static bool save_block(struct fwlab_file_nand_v2 *m, uint32_t id, const struct fnv2_block *b)
{
    uint8_t bytes[FNV2_BLOCK_RECORD_BYTES]; fnv2_block_encode(bytes, id, b);
    return write_bytes(m, block_at(m, id), bytes, sizeof(bytes));
}

/* A current block generation/cursor gives zero and stale records their erased
 * meaning. A current consumed cell can never be inferred from blank payload. */
static int page_class(const struct fwlab_file_nand_v2 *m, const struct fnv2_block *b,
                       const uint8_t *record, uint32_t id)
{
    uint32_t page = id % m->config.geometry.pages_per_block;
    if (fnv2_all(record, FNV2_PAGE_RECORD_BYTES, 0))
        return page < b->info.next_program_page ? -1 : FWLAB_NAND_PAGE_ERASED;
    if (!fnv2_page_valid(record, id, m->sequence) || fnv2_get16(record + 8) > b->info.erase_generation)
        return -1;
    if (fnv2_get16(record + 8) < b->info.erase_generation)
        return page < b->info.next_program_page ? -1 : FWLAB_NAND_PAGE_ERASED;
    return page >= b->info.next_program_page ? -1 : record[10];
}
static bool load_page(struct fwlab_file_nand_v2 *m, uint32_t id, const struct fnv2_block *b,
                       uint8_t *main, uint8_t *oob, struct fwlab_nand_page_info *info)
{
    uint8_t *record = m->work;
    int state;
    memset(info, 0, sizeof(*info)); info->version = FWLAB_NFC_CONTRACT_VERSION; info->size = sizeof(*info);
    info->erase_generation_seen = b->info.erase_generation;
    if (b->info.erase_state == FWLAB_NAND_ERASE_TORN &&
        id % m->config.geometry.pages_per_block < b->erased_prefix) {
        memset(main, 0xff, 4096); memset(oob, 0xff, 128);
        info->state = FWLAB_NAND_PAGE_TORN; return true;
    }
    if (!read_bytes(m, page_at(m, id), record, FNV2_PAGE_RECORD_BYTES)) return false;
    state = page_class(m, b, record, id);
    if (state < 0) { (void)broken(m); return false; }
    if (state == FWLAB_NAND_PAGE_ERASED || state == FNV2_UNKNOWN_PAGE) {
        memset(main, 0xff, 4096); memset(oob, 0xff, 128);
        info->state = state == FNV2_UNKNOWN_PAGE ? FWLAB_NAND_PAGE_TORN : FWLAB_NAND_PAGE_ERASED;
        info->program_count = (uint8_t)(state == FNV2_UNKNOWN_PAGE); return true;
    }
    if (!read_bytes(m, FNV2_HOME_BASE + (uint64_t)id * 4096u, main, 4096) ||
        fnv2_get32(record + 20) != fnv2_crc(main, 4096)) { (void)broken(m); return false; }
    memcpy(oob, record + 32, 128); info->state = (uint8_t)state; info->program_count = 1; return true;
}

static void result_none(struct fwlab_nand_media_result *r, const struct fnv2_block *b)
{
    memset(r, 0, sizeof(*r)); r->version = FWLAB_NFC_CONTRACT_VERSION; r->size = sizeof(*r);
    r->block_health = b->info.health;
    r->base_erase_generation = r->final_erase_generation = b->info.erase_generation;
}
static void intent_init(struct fwlab_file_nand_v2 *m, struct fnv2_intent *i,
                         const struct fnv2_block *base, uint32_t block, uint16_t kind,
                         uint16_t first, uint16_t count)
{
    memset(i, 0, sizeof(*i)); i->sequence = m->sequence + 1u; i->predecessor = m->sequence;
    i->base = *base; i->block = block; i->kind = kind; i->first = first; i->count = count;
    if (kind == FNV2_ERASE) i->maximum_prefix = count;
}
static bool begin(struct fwlab_file_nand_v2 *m, struct fnv2_intent *i)
{
    struct fnv2_intent checked;
    fnv2_intent_encode(m, i, m->intent);
    if (!fnv2_intent_decode(m, m->intent, (unsigned)(i->sequence & 1u), &checked)) {
        (void)broken(m); return false;
    }
    i->crc = checked.crc; m->busy = 1;
    return write_bytes(m, bank_at(i->sequence), m->intent, FNV2_INTENT_BYTES) && barrier(m);
}
static bool terminal_write(struct fwlab_file_nand_v2 *m, const struct fnv2_intent *i,
                            uint16_t disposition)
{
    struct fnv2_terminal t = { .sequence = i->sequence, .predecessor = i->predecessor,
        .intent_crc = i->crc, .kind = i->kind, .disposition = disposition };
    fnv2_terminal_encode(m, &t, m->terminal);
    if (!write_bytes(m, bank_at(i->sequence) + FNV2_INTENT_BYTES, m->terminal, FNV2_TERMINAL_BYTES) ||
        !barrier(m)) return false;
    m->sequence = i->sequence; m->busy = 0; return true;
}
static struct fnv2_block abort_block(const struct fnv2_intent *i)
{
    struct fnv2_block b = i->base; b.sequence = i->sequence;
    if (i->kind == FNV2_PROGRAM) b.info.next_program_page = (uint16_t)(i->first + i->count);
    else if (i->kind == FNV2_ERASE) {
        ++b.info.erase_attempt_count; b.info.erase_state = FWLAB_NAND_ERASE_TORN;
        if (b.erased_prefix < i->maximum_prefix) b.erased_prefix = i->maximum_prefix;
    } else b.info.health = FWLAB_NFC_BLOCK_RUNTIME_BAD;
    return b;
}
static bool abort_install(struct fwlab_file_nand_v2 *m, const struct fnv2_intent *i)
{
    struct fnv2_block b = abort_block(i);
    if (i->kind == FNV2_PROGRAM) {
        uint32_t first = i->block * m->config.geometry.pages_per_block + i->first;
        for (uint32_t p = 0; p < i->count; ++p)
            fnv2_page_encode(m->page_records[p], first + p, b.info.erase_generation,
                             FNV2_UNKNOWN_PAGE, i->sequence, NULL, NULL);
        if (!write_bytes(m, page_at(m, first), m->page_records,
                         (size_t)i->count * FNV2_PAGE_RECORD_BYTES)) return false;
    }
    return save_block(m, i->block, &b) && barrier(m) && terminal_write(m, i, FNV2_ABORT);
}

/* Both possible ERASE COMMIT outcomes are explicit physical transitions. A
 * partial/full-torn erase is not inferred to have advanced the generation. */
static bool block_after(const struct fwlab_file_nand_v2 *m, const struct fnv2_intent *i,
                         const struct fnv2_terminal *t, const struct fnv2_block *actual)
{
    struct fnv2_block expected = abort_block(i);
    uint8_t a[FNV2_BLOCK_RECORD_BYTES], e[FNV2_BLOCK_RECORD_BYTES];
    fnv2_block_encode(a, i->block, actual); fnv2_block_encode(e, i->block, &expected);
    if (memcmp(a, e, sizeof(a)) == 0) return true;
    if (t->disposition != FNV2_COMMIT || i->kind != FNV2_ERASE ||
        i->count != m->config.geometry.pages_per_block ||
        i->base.info.erase_generation == UINT16_MAX || i->base.info.successful_erase_count == UINT16_MAX)
        return false;
    expected = i->base; expected.sequence = i->sequence;
    ++expected.info.erase_attempt_count; ++expected.info.erase_generation; ++expected.info.successful_erase_count;
    expected.info.next_program_page = 0; expected.info.erase_state = FWLAB_NAND_ERASE_CLEAN; expected.erased_prefix = 0;
    fnv2_block_encode(e, i->block, &expected); return memcmp(a, e, sizeof(a)) == 0;
}
static bool validate_transaction(struct fwlab_file_nand_v2 *m, const struct fnv2_intent *i,
                                  const struct fnv2_terminal *t, bool latest_block)
{
    struct fnv2_block b;
    if (!load_block(m, i->block, &b) || (latest_block && !block_after(m, i, t, &b))) return false;
    if (i->kind == FNV2_PROGRAM) {
        uint32_t first = i->block * m->config.geometry.pages_per_block + i->first;
        for (uint32_t p = 0; p < i->count; ++p) {
            const uint8_t *record = m->work;
            if (b.info.erase_generation > i->base.info.erase_generation ||
                (b.info.erase_state == FWLAB_NAND_ERASE_TORN && i->first + p < b.erased_prefix)) continue;
            if (!read_bytes(m, page_at(m, first + p), m->work, FNV2_PAGE_RECORD_BYTES) ||
                !fnv2_page_valid(record, first + p, m->sequence) || fnv2_get64(record + 12) != i->sequence ||
                fnv2_get16(record + 8) != i->base.info.erase_generation ||
                (t->disposition == FNV2_ABORT ? record[10] != FNV2_UNKNOWN_PAGE :
                    record[10] == FNV2_UNKNOWN_PAGE || (i->count > 1 && record[10] != FWLAB_NAND_PAGE_VALID))) return false;
            if (record[10] != FNV2_UNKNOWN_PAGE &&
                (!read_bytes(m, FNV2_HOME_BASE + (uint64_t)(first + p) * 4096u, m->scratch_main, 4096) ||
                 fnv2_get32(record + 20) != fnv2_crc(m->scratch_main, 4096))) return false;
        }
    }
    return true;
}
static bool validate_block_cells(struct fwlab_file_nand_v2 *m, uint32_t id)
{
    struct fnv2_block b; struct fwlab_nand_page_info p;
    if (!load_block(m, id, &b)) return false;
    for (uint32_t n = 0; n < m->config.geometry.pages_per_block; ++n)
        if (!load_page(m, id * m->config.geometry.pages_per_block + n, &b,
                       m->scratch_main, m->scratch_oob, &p)) return false;
    return true;
}
static bool recover(struct fwlab_file_nand_v2 *m)
{
    struct fnv2_terminal terminal[2]; struct fnv2_intent intent[2];
    bool tv[2], iv[2]; unsigned selected, prospective;
    for (unsigned b = 0; b < 2; ++b) {
        if (!read_bytes(m, (uint64_t)b * FNV2_SUPER_BYTES, m->work, FNV2_SUPER_BYTES) ||
            !fnv2_super_valid(m, m->work)) return false;
    }
    for (unsigned b = 0; b < 2; ++b) {
        if (!read_bytes(m, FNV2_BANK_BASE + b * FNV2_BANK_BYTES, m->work, FNV2_SUPER_BYTES) ||
            !fnv2_all(m->work + FNV2_INTENT_BYTES + FNV2_TERMINAL_BYTES,
                       FNV2_SUPER_BYTES - FNV2_INTENT_BYTES - FNV2_TERMINAL_BYTES, 0)) return false;
        tv[b] = fnv2_terminal_decode(m, m->work + FNV2_INTENT_BYTES, b, &terminal[b]);
        iv[b] = fnv2_intent_decode(m, m->work, b, &intent[b]);
        if ((!tv[b] && fnv2_record_crc(m->work + FNV2_INTENT_BYTES, FNV2_TERMINAL_BYTES)) ||
            (!iv[b] && fnv2_record_crc(m->work, FNV2_INTENT_BYTES))) return false;
    }
    if (!tv[0] && !tv[1]) return false;
    selected = tv[1] && (!tv[0] || terminal[1].sequence > terminal[0].sequence) ? 1u : 0u;
    if (tv[0] && tv[1]) {
        uint64_t high = terminal[selected].sequence, low = terminal[1u - selected].sequence;
        if (high - low > 1u || (high == low && high != 0)) return false;
    }
    m->sequence = terminal[selected].sequence;
    if (m->sequence && (!iv[selected] || !fnv2_terminal_matches(&terminal[selected], &intent[selected]))) return false;
    if (!m->sequence && !tv[0]) return false;
    prospective = (unsigned)((m->sequence & 1u) ^ 1u);
    if (iv[prospective] && intent[prospective].sequence > m->sequence) {
        struct fnv2_terminal aborted;
        if (m->sequence == UINT64_MAX || intent[prospective].sequence != m->sequence + 1u ||
            intent[prospective].predecessor != m->sequence) return false;
        /* CONTROL/BASE checks precede all superseded home checks (Cprime-R1).
         * On the same block, BASE itself must be a possible predecessor result. */
        if (m->sequence && intent[selected].block == intent[prospective].block &&
            !block_after(m, &intent[selected], &terminal[selected], &intent[prospective].base)) return false;
        if (!abort_install(m, &intent[prospective])) return false;
        aborted = (struct fnv2_terminal){ .sequence = intent[prospective].sequence,
            .predecessor = intent[prospective].predecessor, .intent_crc = intent[prospective].crc,
            .kind = intent[prospective].kind, .disposition = FNV2_ABORT };
        if (!validate_transaction(m, &intent[prospective], &aborted, true) ||
            !validate_block_cells(m, intent[prospective].block)) return false;
        if (terminal[selected].sequence &&
            (!validate_transaction(m, &intent[selected], &terminal[selected],
                intent[selected].block != intent[prospective].block) ||
             (intent[selected].block != intent[prospective].block &&
              !validate_block_cells(m, intent[selected].block)))) return false;
    } else {
        if (iv[prospective] && (!m->sequence || intent[prospective].sequence != m->sequence - 1u)) return false;
        if (m->sequence && (!validate_transaction(m, &intent[selected], &terminal[selected], true) ||
                            !validate_block_cells(m, intent[selected].block))) return false;
        if (!m->sequence) {
            struct fnv2_block b;
            for (uint32_t id = 0; id < m->blocks; ++id) if (!load_block(m, id, &b)) return false;
        }
    }
    return true;
}

size_t fwlab_file_nand_v2_arena_alignment(void) { return alignof(struct fwlab_file_nand_v2); }
size_t fwlab_file_nand_v2_arena_size(void) { return sizeof(struct fwlab_file_nand_v2); }
uint64_t fwlab_file_nand_v2_image_bytes(const struct fwlab_file_nand_v2_config *c)
{
    uint32_t b, p; uint64_t pm, bm, bytes;
    return fnv2_layout(c, &b, &p, &pm, &bm, &bytes) ? bytes : 0;
}
enum fwlab_nfc_api_result fnv2_engine_open(void *arena, size_t size,
    const struct fwlab_file_nand_v2_config *c, const struct fnv2_io *io, int format,
    struct fwlab_file_nand_v2 **out)
{
    struct fwlab_file_nand_v2 *m = arena; uint32_t blocks, pages;
    uint64_t pm, bm, bytes, actual;
    if (!out) return FWLAB_NFC_API_INVALID_CONTRACT;
    *out = NULL;
    if (!arena || size < sizeof(*m) || (uintptr_t)arena % alignof(struct fwlab_file_nand_v2) ||
        !fnv2_layout(c, &blocks, &pages, &pm, &bm, &bytes) || !io || !io->context ||
        !io->read || !io->write || !io->sync || !io->resize || !io->size || !io->close ||
        (format != 0 && format != 1) || io->size(io->context, &actual) != FWLAB_NFC_API_OK ||
        actual != (format ? 0 : bytes)) return FWLAB_NFC_API_INVALID_CONTRACT;
    memset(m, 0, sizeof(*m)); m->magic = FNV2_MAGIC; m->config = *c; m->io = *io;
    m->blocks = blocks; m->pages = pages; m->page_metadata_offset = pm;
    m->block_metadata_offset = bm; m->image_bytes = bytes; m->busy = 1;
    if (format) {
        struct fnv2_block b = {0}; struct fnv2_terminal t = { .disposition = FNV2_BOOTSTRAP };
        if (m->io.resize(m->io.context, bytes) != FWLAB_NFC_API_OK) return broken(m);
        b.info.version = FWLAB_NFC_CONTRACT_VERSION; b.info.size = sizeof(b.info);
        for (uint32_t id = 0; id < blocks; ++id) if (!save_block(m, id, &b)) return broken(m);
        fnv2_terminal_encode(m, &t, m->terminal);
        for (unsigned bank = 0; bank < 2; ++bank)
            if (!write_bytes(m, FNV2_BANK_BASE + bank * FNV2_BANK_BYTES + FNV2_INTENT_BYTES,
                             m->terminal, FNV2_TERMINAL_BYTES)) return broken(m);
        if (!barrier(m)) return broken(m);
        fnv2_super_encode(m, m->work);
        if (!write_bytes(m, 0, m->work, FNV2_SUPER_BYTES) ||
            !write_bytes(m, FNV2_SUPER_BYTES, m->work, FNV2_SUPER_BYTES) || !barrier(m)) return broken(m);
    } else if (!recover(m)) return broken(m);
    m->busy = 0; *out = m; return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result media_read(void *context, const struct fwlab_nfc_ppa *ppa,
    uint8_t *main, uint32_t main_bytes, uint8_t *oob, uint32_t oob_bytes,
    struct fwlab_nand_page_info *page, struct fwlab_nand_block_info *block)
{
    struct fwlab_file_nand_v2 *m = context; struct fnv2_block b; uint32_t bid, pid;
    if (!live(m) || !main || !oob || !page || !block || main_bytes != 4096 || oob_bytes != 128 ||
        !ppa_ids(m, ppa, &bid, &pid)) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!load_block(m, bid, &b) || !load_page(m, pid, &b, main, oob, page)) return broken(m);
    *block = b.info; return FWLAB_NFC_API_OK;
}
static uint8_t program_reason(const struct fnv2_block *b, uint32_t page)
{
    if (b->info.health != FWLAB_NFC_BLOCK_GOOD) return FWLAB_NFC_REASON_BAD_BLOCK;
    if (b->info.erase_state != FWLAB_NAND_ERASE_CLEAN || page < b->info.next_program_page)
        return FWLAB_NFC_REASON_NOT_ERASED;
    return page != b->info.next_program_page ? FWLAB_NFC_REASON_PROGRAM_ORDER : 0;
}

enum fwlab_nfc_api_result fwlab_file_nand_v2_read_pages(struct fwlab_file_nand_v2 *m,
    const struct fwlab_nfc_ppa *first, uint32_t count, uint8_t *main, size_t main_bytes,
    uint8_t *oob, size_t oob_bytes, struct fwlab_nand_page_info *pages,
    size_t page_capacity, struct fwlab_nand_block_info *block)
{
    struct fnv2_block b;
    uint32_t bid, pid;
    uint8_t states[FWLAB_FILE_NAND_V2_MAX_BATCH_PAGES];
    bool needs_main = false;
    if (!live(m) || !count || count > FWLAB_FILE_NAND_V2_MAX_BATCH_PAGES ||
        !main || !oob || !pages || !block || page_capacity < count ||
        main_bytes != (size_t)count * 4096u || oob_bytes != (size_t)count * 128u ||
        !ppa_ids(m, first, &bid, &pid) ||
        count > (uint32_t)m->config.geometry.pages_per_block - first->page)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    m->busy = 1;
    if (!load_block(m, bid, &b) ||
        !read_bytes(m, page_at(m, pid), m->page_records,
                    (size_t)count * FNV2_PAGE_RECORD_BYTES)) return broken(m);
    for (uint32_t p = 0; p < count; ++p) {
        struct fwlab_nand_page_info *info = &pages[p];
        int state;
        memset(info, 0, sizeof(*info));
        info->version = FWLAB_NFC_CONTRACT_VERSION; info->size = sizeof(*info);
        info->erase_generation_seen = b.info.erase_generation;
        if (b.info.erase_state == FWLAB_NAND_ERASE_TORN &&
            (uint32_t)first->page + p < b.erased_prefix) {
            /* This physical prefix has unknown erased contents, not valid FF. */
            states[p] = FNV2_UNKNOWN_PAGE;
            info->state = FWLAB_NAND_PAGE_TORN;
            continue;
        }
        state = page_class(m, &b, m->page_records[p], pid + p);
        if (state < 0) return broken(m);
        states[p] = (uint8_t)state;
        info->state = state == FNV2_UNKNOWN_PAGE ? FWLAB_NAND_PAGE_TORN : (uint8_t)state;
        info->program_count = (uint8_t)(state != FWLAB_NAND_PAGE_ERASED);
        needs_main |= state == FWLAB_NAND_PAGE_VALID || state == FWLAB_NAND_PAGE_TORN;
    }
    /* One physical range read; erased/unknown bytes are replaced below before
     * successful return. They are never treated as current data by this read. */
    if (needs_main && !read_bytes(m, FNV2_HOME_BASE + (uint64_t)pid * 4096u,
                                  main, main_bytes)) return broken(m);
    for (uint32_t p = 0; p < count; ++p) {
        uint8_t *page_main = main + (size_t)p * 4096u;
        uint8_t *page_oob = oob + (size_t)p * 128u;
        const uint8_t *record = m->page_records[p];
        if (states[p] == FWLAB_NAND_PAGE_ERASED || states[p] == FNV2_UNKNOWN_PAGE) {
            memset(page_main, 0xff, 4096); memset(page_oob, 0xff, 128);
        } else {
            if (fnv2_get32(record + 20) != fnv2_crc(page_main, 4096)) return broken(m);
            memcpy(page_oob, record + 32, 128);
        }
    }
    *block = b.info;
    m->busy = 0;
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_file_nand_v2_program_pages(struct fwlab_file_nand_v2 *m,
    const struct fwlab_nfc_ppa *first, uint32_t count, const uint8_t *main, size_t main_bytes,
    const uint8_t *oob, size_t oob_bytes, struct fwlab_nand_media_result *results, size_t result_count)
{
    struct fnv2_block b, after; struct fnv2_intent i; uint32_t bid, pid; uint8_t reason;
    if (!live(m) || !count || count > FWLAB_FILE_NAND_V2_MAX_BATCH_PAGES || !main || !oob ||
        !results || result_count < count || main_bytes != (size_t)count * 4096u || oob_bytes != (size_t)count * 128u ||
        !ppa_ids(m, first, &bid, &pid) || count > (uint32_t)m->config.geometry.pages_per_block - first->page)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!load_block(m, bid, &b)) return broken(m);
    reason = program_reason(&b, first->page);
    if (reason) {
        for (uint32_t p = 0; p < count; ++p) { result_none(&results[p], &b); results[p].reason = reason; }
        return FWLAB_NFC_API_OK;
    }
    if (m->sequence == UINT64_MAX) return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    if (!read_bytes(m, page_at(m, pid), m->page_records, (size_t)count * FNV2_PAGE_RECORD_BYTES)) return broken(m);
    for (uint32_t p = 0; p < count; ++p)
        if (page_class(m, &b, m->page_records[p], pid + p) != FWLAB_NAND_PAGE_ERASED) return broken(m);
    intent_init(m, &i, &b, bid, FNV2_PROGRAM, first->page, (uint16_t)count);
    for (uint32_t p = 0; p < count; ++p)
        fnv2_page_encode(m->page_records[p], pid + p, b.info.erase_generation, FWLAB_NAND_PAGE_VALID,
                         i.sequence, main + (size_t)p * 4096u, oob + (size_t)p * 128u);
    after = b; after.sequence = i.sequence; after.info.next_program_page = (uint16_t)(first->page + count);
    if (!begin(m, &i) || !write_bytes(m, FNV2_HOME_BASE + (uint64_t)pid * 4096u, main, main_bytes) ||
        !write_bytes(m, page_at(m, pid), m->page_records, (size_t)count * FNV2_PAGE_RECORD_BYTES) ||
        !save_block(m, bid, &after) || !barrier(m) || !terminal_write(m, &i, FNV2_COMMIT)) return broken(m);
    for (uint32_t p = 0; p < count; ++p) {
        result_none(&results[p], &b); results[p].physical_outcome = FWLAB_NFC_PHYS_APPLIED;
        results[p].integrity = FWLAB_NFC_INTEGRITY_COMPLETE; results[p].applied_region_mask = FWLAB_NFC_REGION_MASK;
        results[p].applied_main_bytes = 4096; results[p].applied_oob_bytes = 128;
    }
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result media_program(void *context, const struct fwlab_nfc_ppa *ppa,
    const uint8_t *main, uint32_t main_bytes, const uint8_t *oob, uint32_t oob_bytes,
    uint32_t applied_main, uint32_t applied_oob, uint8_t integrity, struct fwlab_nand_media_result *out)
{
    struct fwlab_file_nand_v2 *m = context; struct fnv2_block b, after; struct fnv2_intent i;
    struct fwlab_nand_page_info page; struct fwlab_nand_media_result result; uint32_t bid, pid; uint8_t reason;
    if (!live(m) || !main || !oob || !out || main_bytes != 4096 || oob_bytes != 128 ||
        applied_main > main_bytes || applied_oob > oob_bytes ||
        (integrity != FWLAB_NFC_INTEGRITY_COMPLETE && integrity != FWLAB_NFC_INTEGRITY_TORN) ||
        !ppa_ids(m, ppa, &bid, &pid)) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (applied_main == 4096 && applied_oob == 128 && integrity == FWLAB_NFC_INTEGRITY_COMPLETE)
        return fwlab_file_nand_v2_program_pages(m, ppa, 1, main, main_bytes, oob, oob_bytes, out, 1);
    if (!load_block(m, bid, &b) || !load_page(m, pid, &b, m->scratch_main, m->scratch_oob, &page)) return broken(m);
    result_none(&result, &b); reason = program_reason(&b, ppa->page);
    if (reason || (!applied_main && !applied_oob)) { result.reason = reason; *out = result; return FWLAB_NFC_API_OK; }
    if (page.state != FWLAB_NAND_PAGE_ERASED || page.program_count) return broken(m);
    if (m->sequence == UINT64_MAX) return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    memcpy(m->scratch_main, main, applied_main); memcpy(m->scratch_oob, oob, applied_oob);
    intent_init(m, &i, &b, bid, FNV2_PROGRAM, ppa->page, 1);
    fnv2_page_encode(m->page_records[0], pid, b.info.erase_generation, FWLAB_NAND_PAGE_TORN,
                     i.sequence, m->scratch_main, m->scratch_oob);
    after = b; after.sequence = i.sequence; ++after.info.next_program_page;
    if (!begin(m, &i) || !write_bytes(m, FNV2_HOME_BASE + (uint64_t)pid * 4096u, m->scratch_main, 4096) ||
        !write_bytes(m, page_at(m, pid), m->page_records[0], FNV2_PAGE_RECORD_BYTES) ||
        !save_block(m, bid, &after) || !barrier(m) || !terminal_write(m, &i, FNV2_COMMIT)) return broken(m);
    result.physical_outcome = FWLAB_NFC_PHYS_APPLIED; result.integrity = integrity;
    result.applied_main_bytes = applied_main; result.applied_oob_bytes = applied_oob;
    result.applied_region_mask = (uint8_t)((applied_main ? FWLAB_NFC_REGION_MAIN : 0u) |
                                          (applied_oob ? FWLAB_NFC_REGION_OOB : 0u));
    *out = result; return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result media_erase(void *context, const struct fwlab_nfc_ppa *ppa,
    uint32_t applied, uint8_t integrity, struct fwlab_nand_media_result *out)
{
    struct fwlab_file_nand_v2 *m = context; struct fnv2_block b, after; struct fnv2_intent i;
    struct fwlab_nand_media_result result; uint32_t bid, pid;
    if (!live(m) || !out || !ppa_ids(m, ppa, &bid, &pid) || ppa->page ||
        applied > m->config.geometry.pages_per_block ||
        (integrity != FWLAB_NFC_INTEGRITY_COMPLETE && integrity != FWLAB_NFC_INTEGRITY_TORN)) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!load_block(m, bid, &b)) return broken(m);
    result_none(&result, &b);
    if (b.info.health != FWLAB_NFC_BLOCK_GOOD || !applied) {
        if (b.info.health != FWLAB_NFC_BLOCK_GOOD) result.reason = FWLAB_NFC_REASON_BAD_BLOCK;
        *out = result; return FWLAB_NFC_API_OK;
    }
    if (m->sequence == UINT64_MAX || b.info.erase_attempt_count == UINT16_MAX ||
        (integrity == FWLAB_NFC_INTEGRITY_COMPLETE && applied == m->config.geometry.pages_per_block &&
         (b.info.erase_generation == UINT16_MAX || b.info.successful_erase_count == UINT16_MAX))) return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    intent_init(m, &i, &b, bid, FNV2_ERASE, 0, (uint16_t)applied); after = abort_block(&i);
    if (integrity == FWLAB_NFC_INTEGRITY_COMPLETE && applied == m->config.geometry.pages_per_block) {
        ++after.info.erase_generation; ++after.info.successful_erase_count;
        after.info.next_program_page = 0; after.info.erase_state = FWLAB_NAND_ERASE_CLEAN; after.erased_prefix = 0;
    }
    if (!begin(m, &i) || !save_block(m, bid, &after) || !barrier(m) || !terminal_write(m, &i, FNV2_COMMIT)) return broken(m);
    result.physical_outcome = FWLAB_NFC_PHYS_APPLIED; result.integrity = integrity; result.applied_pages = applied;
    result.final_erase_generation = after.info.erase_generation; *out = result; return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result media_bad(void *context, const struct fwlab_nfc_ppa *ppa)
{
    struct fwlab_file_nand_v2 *m = context; struct fnv2_block b, after; struct fnv2_intent i; uint32_t bid, pid;
    if (!live(m) || !ppa_ids(m, ppa, &bid, &pid)) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!load_block(m, bid, &b)) return broken(m);
    if (b.info.health == FWLAB_NFC_BLOCK_RUNTIME_BAD) return FWLAB_NFC_API_OK;
    if (m->sequence == UINT64_MAX) return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    intent_init(m, &i, &b, bid, FNV2_MARK_BAD, 0, 0); after = abort_block(&i);
    return begin(m, &i) && save_block(m, bid, &after) && barrier(m) && terminal_write(m, &i, FNV2_COMMIT) ?
        FWLAB_NFC_API_OK : broken(m);
}

static uint64_t hash_bytes(uint64_t h, const uint8_t *p, size_t n)
{ for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= UINT64_C(1099511628211); } return h; }
/* Diagnostic-only compatibility tuple; no normal operation calls this scan. */
static uint64_t media_hash(void *context)
{
    struct fwlab_file_nand_v2 *m = context; uint64_t hash = UINT64_C(1469598103934665603);
    struct fnv2_block b; struct fwlab_nand_page_info p;
    if (!live(m)) return 0;
    for (uint32_t id = 0; id < m->blocks; ++id) {
        if (!load_block(m, id, &b)) return 0;
        for (uint32_t n = 0; n < m->config.geometry.pages_per_block; ++n) {
            if (!load_page(m, id * m->config.geometry.pages_per_block + n, &b, m->scratch_main, m->scratch_oob, &p)) return 0;
            hash = hash_bytes(hash, m->scratch_main, 4096); hash = hash_bytes(hash, m->scratch_oob, 128);
            memset(m->work, 0, 16); fnv2_put16(m->work, p.erase_generation_seen); m->work[2] = p.state; m->work[3] = p.program_count;
            fnv2_put16(m->work + 4, b.info.erase_generation); fnv2_put16(m->work + 6, b.info.successful_erase_count);
            fnv2_put16(m->work + 8, b.info.erase_attempt_count); fnv2_put16(m->work + 10, b.info.next_program_page);
            m->work[12] = b.info.health; m->work[13] = b.info.erase_state; hash = hash_bytes(hash, m->work, 16);
        }
    }
    return hash;
}
static const struct fwlab_nand_media_ops media_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(media_ops),
    .read_page = media_read, .program = media_program, .erase = media_erase,
    .mark_runtime_bad = media_bad, .hash = media_hash
};
struct fwlab_nand_media fwlab_file_nand_v2_media(struct fwlab_file_nand_v2 *m)
{ struct fwlab_nand_media out = {0}; if (live(m)) { out.ops = &media_ops; out.context = m; } return out; }
uint64_t fwlab_file_nand_v2_sequence(const struct fwlab_file_nand_v2 *m)
{ return m && m->magic == FNV2_MAGIC ? m->sequence : 0; }
enum fwlab_nfc_api_result fwlab_file_nand_v2_close(struct fwlab_file_nand_v2 *m)
{
    enum fwlab_nfc_api_result result;
    if (!m || m->magic != FNV2_MAGIC || m->closed || m->busy) return FWLAB_NFC_API_WRONG_STATE;
    result = m->io.close(m->io.context); m->closed = 1; return result;
}
