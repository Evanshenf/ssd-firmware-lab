/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "ftl_scale_codec.h"
#include <string.h>

static void metadata_fail(struct fwlab_ftl_scale *f, uint32_t fault)
{
    f->meta.result = FWLAB_SPINE_V0_QUARANTINED;
    f->meta.phase = SF_M_IDLE;
    f->meta.pending = 0;
    sf_fail(f, fault);
}

static void metadata_done(struct fwlab_ftl_scale *f)
{
    f->meta.phase = SF_M_IDLE;
    f->meta.result = FWLAB_SPINE_V0_OK;
}

bool sf_meta_busy(const struct fwlab_ftl_scale *f)
{ return f != NULL && f->meta.phase != SF_M_IDLE; }
enum fwlab_spine_result_v0 sf_meta_result(const struct fwlab_ftl_scale *f)
{ return f == NULL ? FWLAB_SPINE_V0_INVALID : f->meta.result; }

static bool usable(const struct fwlab_ftl_scale *f)
{
    return f != NULL && f->magic == SF_MAGIC && f->initialized &&
        !f->quarantined && !f->admission_closed && !sf_meta_busy(f) && sf_io_idle(f);
}

static void reset_resident(struct fwlab_ftl_scale *f)
{
    uint32_t i;
    memset(f->map, 0, (size_t)f->config.mapping_slots * sizeof(*f->map));
    for (i = 0; i < f->config.mapping_slots; ++i) f->map[i].ppa = SF_NONE;
    memset(f->blocks, 0, (size_t)f->physical_blocks * sizeof(*f->blocks));
    memset(f->validity, 0, ((size_t)f->physical_pages + 7u) / 8u);
    for (i = 0; i < f->physical_blocks; ++i) {
        f->blocks[i].free_heap_pos = SF_HEAP_NONE;
        f->blocks[i].victim_heap_pos = SF_HEAP_NONE;
    }
    f->free_count = 0; f->victim_count = 0; f->host_head = SF_NONE;
    f->erase_intent_sequence = 0; f->erase_intent_block = SF_NONE;
}

/* Return 2 when a child was staged, 0 while waiting, 1 when consumed, -1 on
 * error. Staging performs no lower IO; the sole outer driver owns sf_io_step. */
static int read_child(struct fwlab_ftl_scale *f, uint32_t ppa, uint8_t frame,
                       uint8_t *classification)
{
    struct sf_io_result result;
    if (!f->meta.pending) {
        if (sf_io_read_start(f, ppa, frame) != FWLAB_SPINE_V0_OK) {
            metadata_fail(f, SF_FAULT_IO); return -1;
        }
        f->meta.pending = 1; return 2;
    }
    if (!sf_io_take(f, &result)) return 0;
    f->meta.pending = 0;
    if (result.kind != SF_IO_READ || result.ppa != ppa || result.frame != frame) {
        metadata_fail(f, SF_FAULT_IO); return -1;
    }
    if (result.result == FWLAB_SPINE_V0_OK && result.read_valid) {
        *classification = sf_bytes_ff(f->io.main[frame], SF_PAGE_BYTES) &&
            sf_bytes_ff(f->io.oob[frame], SF_OOB_BYTES) ? SF_READ_ERASED : SF_READ_VALID;
        return 1;
    }
    if (!result.read_valid && result.completion.reason == FWLAB_NFC_REASON_ECC_UNCORRECTABLE &&
        result.completion.ecc_status == FWLAB_NFC_ECC_UNCORRECTABLE) {
        *classification = SF_READ_ECC; return 1;
    }
    metadata_fail(f, SF_FAULT_IO); return -1;
}

static int effect_child(struct fwlab_ftl_scale *f, uint8_t kind, uint32_t target,
                         uint8_t frame, struct sf_io_result *result)
{
    enum fwlab_spine_result_v0 status;
    uint32_t ppa = kind == SF_IO_ERASE ? target * f->config.geometry.pages_per_block : target;
    if (!f->meta.pending) {
        status = kind == SF_IO_ERASE ? sf_io_erase_start(f, target) : sf_io_program_start(f, target, frame);
        if (status != FWLAB_SPINE_V0_OK) { metadata_fail(f, SF_FAULT_IO); return -1; }
        f->meta.pending = 1; return 2;
    }
    if (!sf_io_take(f, result)) return 0;
    f->meta.pending = 0;
    if (result->kind != kind || result->ppa != ppa || result->result != FWLAB_SPINE_V0_OK ||
        result->completion.terminal != FWLAB_NFC_TERMINAL_SUCCESS ||
        result->completion.physical_outcome != FWLAB_NFC_PHYS_APPLIED ||
        result->completion.integrity != FWLAB_NFC_INTEGRITY_COMPLETE ||
        result->completion.block_health != FWLAB_NFC_BLOCK_GOOD ||
        (kind == SF_IO_PROGRAM && result->frame != frame) ||
        (kind == SF_IO_ERASE && result->completion.final_erase_generation <= result->completion.base_erase_generation)) {
        metadata_fail(f, SF_FAULT_IO); return -1;
    }
    return 1;
}

static bool begin_checkpoint(struct fwlab_ftl_scale *f, bool already_erased)
{
    struct sf_meta *m = &f->meta;
    if (f->root.generation == UINT64_MAX || f->erase_intent_sequence != 0 ||
        f->record_sequence > f->config.record_sequence_limit ||
        !f->next_block_uid || f->next_block_uid > UINT64_MAX / f->config.geometry.pages_per_block) {
        metadata_fail(f, SF_FAULT_STATE); return false;
    }
    m->candidate = f->root;
    m->candidate.generation = f->root.generation + 1u;
    m->candidate.bank = 1u - f->root.bank;
    m->candidate.covered_record_seq = f->record_sequence;
    m->candidate.covered_map_seq = f->map_sequence;
    m->candidate.durable_frontier = f->durable_frontier;
    m->candidate.next_block_uid = f->next_block_uid;
    m->candidate.cp_digest = 0;
    m->candidate.rail_header_digest[0] = 0; m->candidate.rail_header_digest[1] = 0;
    m->digest = SF_DIGEST_SEED; m->ordinal = 0; m->erase_index = 0; m->pending = 0;
    m->phase = already_erased ? SF_M_CP_BODY : SF_M_CP_ERASE;
    return true;
}

enum fwlab_spine_result_v0 sf_format_start(struct fwlab_ftl_scale *f, uint64_t lbas)
{
    struct sf_layout layout; uint32_t block;
    if (!usable(f) || f->ready || sf_work_busy(f)) return FWLAB_SPINE_V0_WRONG_STATE;
    if (!sf_layout_make(&f->config.geometry, lbas, &layout) || layout.lpn_count > f->config.mapping_slots)
        return FWLAB_SPINE_V0_INVALID;
    memset(&f->meta, 0, sizeof(f->meta)); f->meta.result = FWLAB_SPINE_V0_IN_PROGRESS;
    f->meta.mode = SF_M_FORMAT; f->meta.phase = SF_M_FORMAT_ERASE;
    reset_resident(f); memset(&f->root, 0, sizeof(f->root));
    f->root.layout = layout; f->root.bank = 1; memcpy(f->root.media_uuid, f->config.media_uuid, 16);
    f->record_sequence = 0; f->map_sequence = 0; f->durable_frontier = 0; f->next_block_uid = 1;
    f->expected_lba_count = 0; f->journal_next = 1;
    for (block = 0; block < f->physical_blocks; ++block) {
        f->blocks[block].disk.role = block < layout.data_first_block ? SF_META : SF_FREE;
        f->blocks[block].disk.health = FWLAB_NFC_BLOCK_GOOD;
    }
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 sf_recover_start(struct fwlab_ftl_scale *f, uint64_t expected_lbas)
{
    if (!usable(f) || f->ready || sf_work_busy(f)) return FWLAB_SPINE_V0_WRONG_STATE;
    if (expected_lbas && (expected_lbas % 8 || expected_lbas / 8 > f->config.mapping_slots)) return FWLAB_SPINE_V0_INVALID;
    memset(&f->meta, 0, sizeof(f->meta)); f->meta.result = FWLAB_SPINE_V0_IN_PROGRESS;
    f->meta.mode = SF_M_RECOVER; f->meta.phase = SF_M_READ_ROOT_A;
    f->expected_lba_count = expected_lbas; reset_resident(f);
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 sf_checkpoint_start(struct fwlab_ftl_scale *f)
{
    if (!usable(f) || !f->ready || sf_work_busy(f) || !f->root.generation || f->erase_intent_sequence)
        return FWLAB_SPINE_V0_WRONG_STATE;
    memset(&f->meta, 0, sizeof(f->meta)); f->meta.result = FWLAB_SPINE_V0_IN_PROGRESS;
    return begin_checkpoint(f, false) ? FWLAB_SPINE_V0_OK : f->meta.result;
}

static enum fwlab_spine_result_v0 begin_journal(
    struct fwlab_ftl_scale *f, const struct sf_record *record, uint32_t return_phase)
{
    struct sf_meta *m = &f->meta; bool mapping;
    if (!record || !f->root.generation || f->journal_next >= f->root.layout.journal_slots)
        return FWLAB_SPINE_V0_NO_CAPACITY;
    mapping = record->kind == SF_MAP_GROUP || record->kind == SF_GC_COMMIT;
    if (f->record_sequence == UINT64_MAX || f->record_sequence >= f->config.record_sequence_limit ||
        (mapping && f->map_sequence == UINT64_MAX)) return FWLAB_SPINE_V0_COUNTER_EXHAUSTED;
    m->record = *record; m->record.epoch = f->root.generation;
    m->record.sequence = f->record_sequence + 1u; m->record.predecessor = f->record_sequence;
    m->record.before_map_seq = f->map_sequence; m->record.after_map_seq = f->map_sequence + (mapping ? 1u : 0u);
    if (!mapping) m->record.durable_frontier = f->durable_frontier;
    if (m->record.kind == SF_ERASE_INTENT) m->record.intent_sequence = m->record.sequence;
    if (m->record.durable_frontier < f->durable_frontier || m->record.durable_frontier > f->config.host_sequence_limit ||
        !sf_record_encode(&f->root, &m->record, f->journal_next, 0, f->io.main[0], f->io.oob[0]))
        return FWLAB_SPINE_V0_INVALID;
    m->return_phase = return_phase; m->pending = 0; m->phase = SF_M_JOURNAL_A;
    m->result = FWLAB_SPINE_V0_IN_PROGRESS;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 sf_journal_start(struct fwlab_ftl_scale *f, const struct sf_record *record)
{
    /* Closing Host admission does not revoke an already owned internal
     * transaction's ability to commit/drain before the NFC close starts. */
    if (!f || f->magic != SF_MAGIC || !f->initialized || f->quarantined ||
        !f->ready || sf_meta_busy(f) || !sf_io_idle(f) || f->nfc_close_started ||
        (f->admission_closed && !sf_work_busy(f))) return FWLAB_SPINE_V0_WRONG_STATE;
    return begin_journal(f, record, SF_M_IDLE);
}

static bool select_root(struct fwlab_ftl_scale *f)
{
    struct sf_meta *m = &f->meta; unsigned selected;
    if (m->root_class[0] != SF_READ_VALID && m->root_class[1] != SF_READ_VALID) return false;
    if (m->root_class[0] == SF_READ_VALID && m->root_class[1] == SF_READ_VALID) {
        uint64_t a = m->roots[0].generation, b = m->roots[1].generation;
        if (m->roots[0].layout.lba_count != m->roots[1].layout.lba_count || a == b ||
            (a > b ? a - b : b - a) != 1) return false;
    }
    selected = m->root_class[0] != SF_READ_VALID ||
        (m->root_class[1] == SF_READ_VALID && m->roots[1].generation > m->roots[0].generation) ? 1u : 0u;
    if (f->expected_lba_count && f->expected_lba_count != m->roots[selected].layout.lba_count) return false;
    f->root = m->roots[selected]; f->record_sequence = f->root.covered_record_seq;
    f->map_sequence = f->root.covered_map_seq; f->durable_frontier = f->root.durable_frontier;
    f->next_block_uid = f->root.next_block_uid; f->journal_next = 1;
    m->ordinal = 0; m->digest = SF_DIGEST_SEED; m->phase = SF_M_READ_CP;
    return true;
}

static uint32_t checkpoint_erase_block(const struct sf_meta *m)
{
    const struct sf_root *r = &m->candidate; const struct sf_layout *l = &r->layout;
    uint32_t index = m->erase_index;
    if (index == 0) return r->bank;
    --index;
    if (index < l->cp_blocks) return l->cp_base[r->bank] + index;
    index -= l->cp_blocks;
    return l->journal_base[r->bank][index / l->journal_blocks] + index % l->journal_blocks;
}

static bool checkpoint_complete(struct fwlab_ftl_scale *f)
{
    f->root = f->meta.candidate; f->journal_next = 1; ++f->checkpoints;
    if (f->meta.mode == SF_M_RECOVER) { f->meta.phase = SF_M_CLEAN_SELECT; return true; }
    if (f->meta.mode == SF_M_FORMAT) {
        if (!sf_rebuild_indexes(f)) return false;
        f->ready = 1;
    }
    metadata_done(f); return true;
}

static bool replay_pair(struct fwlab_ftl_scale *f)
{
    struct sf_meta *m = &f->meta;
    bool a = m->rail_class[0] == SF_READ_VALID, b = m->rail_class[1] == SF_READ_VALID;
    if (a || b) {
        if (m->tail || (a && b && memcmp(&m->record, &m->comparison, sizeof(m->record)))) return false;
        if (!a) m->record = m->comparison;
        if (m->record.sequence > f->config.record_sequence_limit ||
            m->record.durable_frontier > f->config.host_sequence_limit ||
            !sf_record_validate_apply(f, &m->record)) return false;
        f->journal_next = m->ordinal + 1u;
        if (!a || !b) m->degraded = 1;
    } else {
        if (m->rail_class[0] == SF_READ_ECC && m->rail_class[1] == SF_READ_ECC) return false;
        m->tail = 1;
        if (m->rail_class[0] == SF_READ_ECC || m->rail_class[1] == SF_READ_ECC) m->degraded = 1;
    }
    ++m->ordinal;
    m->phase = m->ordinal == f->root.layout.journal_slots ? SF_M_RECOVER_NORMALIZE : SF_M_READ_JOURNAL_A;
    return true;
}

bool sf_meta_step(struct fwlab_ftl_scale *f)
{
    struct sf_meta *m; struct sf_io_result io; uint32_t block, rail, total; int status;
    enum fwlab_spine_result_v0 started;
    if (!f || !sf_meta_busy(f) || f->quarantined) return false;
    m = &f->meta;
    /* FORMAT/RECOVER start only prepares volatile state. A close before its
     * first child must not turn that pending startup into media mutation. */
    if (f->admission_closed && !f->ready && !f->nfc_children && !m->pending &&
        sf_io_idle(f) && (m->phase == SF_M_FORMAT_ERASE || m->phase == SF_M_READ_ROOT_A)) {
        metadata_done(f); return true;
    }
    switch (m->phase) {
    case SF_M_FORMAT_ERASE:
        block = m->erase_index;
        status = effect_child(f, SF_IO_ERASE, block, 0, &io);
        if (status != 1) return status != 0;
        f->blocks[block].disk.erase_generation = io.completion.final_erase_generation;
        f->blocks[block].wear = io.completion.final_erase_generation;
        if (++m->erase_index == f->physical_blocks) (void)begin_checkpoint(f, true);
        return true;
    case SF_M_READ_ROOT_A:
    case SF_M_READ_ROOT_B:
        rail = m->phase == SF_M_READ_ROOT_A ? 0u : 1u;
        status = read_child(f, rail * f->config.geometry.pages_per_block, (uint8_t)rail, &m->root_class[rail]);
        if (status != 1) return status != 0;
        if (m->root_class[rail] == SF_READ_VALID && !sf_root_decode(f, rail, f->io.main[rail], f->io.oob[rail], &m->roots[rail])) {
            metadata_fail(f, SF_FAULT_METADATA); return true;
        }
        m->phase = rail ? SF_M_SELECT_ROOT : SF_M_READ_ROOT_B; return true;
    case SF_M_SELECT_ROOT:
        if (!select_root(f)) metadata_fail(f, SF_FAULT_METADATA);
        return true;
    case SF_M_READ_CP:
        status = read_child(f, sf_cp_ppa(&f->root, m->ordinal), 0, &m->rail_class[0]);
        if (status != 1) return status != 0;
        if (m->rail_class[0] != SF_READ_VALID || !sf_cp_decode(f, m->ordinal, f->io.main[0], f->io.oob[0])) {
            metadata_fail(f, SF_FAULT_METADATA); return true;
        }
        m->digest = sf_page_digest(m->digest, f->io.main[0], f->io.oob[0]);
        if (++m->ordinal == f->root.layout.cp_map_pages + f->root.layout.cp_block_pages) {
            if (m->digest != f->root.cp_digest || !sf_rebuild_indexes(f)) { metadata_fail(f, SF_FAULT_METADATA); return true; }
            m->phase = SF_M_READ_HEADER_A;
        }
        return true;
    case SF_M_READ_HEADER_A:
    case SF_M_READ_HEADER_B:
        rail = m->phase == SF_M_READ_HEADER_A ? 0u : 1u;
        status = read_child(f, sf_journal_ppa(&f->root, rail, 0), (uint8_t)rail, &m->rail_class[rail]);
        if (status != 1) return status != 0;
        if (m->rail_class[rail] == SF_READ_ERASED ||
            (m->rail_class[rail] == SF_READ_VALID && !sf_rail_header_valid(&f->root, rail, f->io.main[rail], f->io.oob[rail]))) {
            metadata_fail(f, SF_FAULT_METADATA); return true;
        }
        if (!rail) m->phase = SF_M_READ_HEADER_B;
        else {
            if (m->rail_class[0] != SF_READ_VALID && m->rail_class[1] != SF_READ_VALID) { metadata_fail(f, SF_FAULT_METADATA); return true; }
            m->degraded = (uint8_t)(m->rail_class[0] != SF_READ_VALID || m->rail_class[1] != SF_READ_VALID);
            m->ordinal = 1; m->phase = SF_M_READ_JOURNAL_A;
        }
        return true;
    case SF_M_READ_JOURNAL_A:
    case SF_M_READ_JOURNAL_B:
        rail = m->phase == SF_M_READ_JOURNAL_A ? 0u : 1u;
        status = read_child(f, sf_journal_ppa(&f->root, rail, m->ordinal), (uint8_t)rail, &m->rail_class[rail]);
        if (status != 1) return status != 0;
        if (m->rail_class[rail] == SF_READ_VALID && !sf_record_decode(&f->root, m->ordinal, rail,
            f->io.main[rail], f->io.oob[rail], rail ? &m->comparison : &m->record)) {
            metadata_fail(f, SF_FAULT_METADATA); return true;
        }
        m->phase = rail ? SF_M_REPLAY_PAIR : SF_M_READ_JOURNAL_B; return true;
    case SF_M_REPLAY_PAIR:
        if (!replay_pair(f)) metadata_fail(f, SF_FAULT_METADATA);
        return true;
    case SF_M_RECOVER_NORMALIZE:
        if (!sf_rebuild_indexes(f) || !sf_seal_recovered_heads(f)) { metadata_fail(f, SF_FAULT_METADATA); return true; }
        f->erase_intent_sequence = 0; f->erase_intent_block = SF_NONE;
        (void)begin_checkpoint(f, false); return true;
    case SF_M_CP_ERASE:
        block = checkpoint_erase_block(m);
        status = effect_child(f, SF_IO_ERASE, block, 0, &io);
        if (status != 1) return status != 0;
        f->blocks[block].disk.erase_generation = io.completion.final_erase_generation;
        f->blocks[block].wear = io.completion.final_erase_generation;
        total = 1u + m->candidate.layout.cp_blocks + 2u * m->candidate.layout.journal_blocks;
        if (++m->erase_index == total) { m->ordinal = 0; m->phase = SF_M_CP_BODY; }
        return true;
    case SF_M_CP_BODY:
        if (!m->pending && !sf_cp_encode(f, &m->candidate, m->ordinal, f->io.main[0], f->io.oob[0])) {
            metadata_fail(f, SF_FAULT_METADATA); return true;
        }
        status = effect_child(f, SF_IO_PROGRAM, sf_cp_ppa(&m->candidate, m->ordinal), 0, &io);
        if (status != 1) return status != 0;
        m->digest = sf_page_digest(m->digest, f->io.main[0], f->io.oob[0]);
        if (++m->ordinal == m->candidate.layout.cp_map_pages + m->candidate.layout.cp_block_pages) {
            m->candidate.cp_digest = m->digest; m->phase = SF_M_CP_HEADER_A;
        }
        return true;
    case SF_M_CP_HEADER_A:
    case SF_M_CP_HEADER_B:
        rail = m->phase == SF_M_CP_HEADER_A ? 0u : 1u;
        if (!m->pending) sf_rail_header_encode(&m->candidate, rail, f->io.main[0], f->io.oob[0]);
        status = effect_child(f, SF_IO_PROGRAM, sf_journal_ppa(&m->candidate, rail, 0), 0, &io);
        if (status != 1) return status != 0;
        m->candidate.rail_header_digest[rail] = sf_page_digest(SF_DIGEST_SEED, f->io.main[0], f->io.oob[0]);
        m->phase = rail ? SF_M_CP_ROOT : SF_M_CP_HEADER_B; return true;
    case SF_M_CP_ROOT:
        if (!m->pending && !sf_root_encode(&m->candidate, f->io.main[0], f->io.oob[0])) { metadata_fail(f, SF_FAULT_METADATA); return true; }
        status = effect_child(f, SF_IO_PROGRAM, m->candidate.bank * f->config.geometry.pages_per_block, 0, &io);
        if (status != 1) return status != 0;
        if (f->root.generation) m->phase = SF_M_CP_RETIRE;
        else if (!checkpoint_complete(f)) metadata_fail(f, SF_FAULT_METADATA);
        return true;
    case SF_M_CP_RETIRE:
        block = f->root.bank;
        status = effect_child(f, SF_IO_ERASE, block, 0, &io);
        if (status != 1) return status != 0;
        f->blocks[block].disk.erase_generation = io.completion.final_erase_generation;
        f->blocks[block].wear = io.completion.final_erase_generation;
        if (!checkpoint_complete(f)) metadata_fail(f, SF_FAULT_METADATA);
        return true;
    case SF_M_JOURNAL_A:
    case SF_M_JOURNAL_B:
        rail = m->phase == SF_M_JOURNAL_A ? 0u : 1u;
        if (!m->pending && !sf_record_encode(&f->root, &m->record, f->journal_next, rail, f->io.main[0], f->io.oob[0])) {
            metadata_fail(f, SF_FAULT_METADATA); return true;
        }
        status = effect_child(f, SF_IO_PROGRAM, sf_journal_ppa(&f->root, rail, f->journal_next), 0, &io);
        if (status != 1) return status != 0;
        m->phase = rail ? SF_M_JOURNAL_APPLY : SF_M_JOURNAL_B; return true;
    case SF_M_JOURNAL_APPLY:
        if (!sf_record_validate_apply(f, &m->record)) { metadata_fail(f, SF_FAULT_METADATA); return true; }
        ++f->journal_next; m->phase = m->return_phase;
        if (m->phase == SF_M_IDLE) m->result = FWLAB_SPINE_V0_OK;
        return true;
    case SF_M_CLEAN_SELECT:
        if (!sf_next_reclaim_pending(f, &m->cleanup_block)) {
            if (!sf_rebuild_indexes(f)) metadata_fail(f, SF_FAULT_METADATA);
            else { f->ready = 1; metadata_done(f); }
            return true;
        }
        if (f->journal_next > f->root.layout.journal_slots - 2u) {
            (void)begin_checkpoint(f, false); return true;
        }
        block = m->cleanup_block; memset(&m->record, 0, sizeof(m->record));
        m->record.kind = SF_ERASE_INTENT; m->record.block = block;
        m->record.block_uid = f->blocks[block].disk.block_uid;
        m->record.erase_generation = f->blocks[block].disk.erase_generation;
        started = begin_journal(f, &m->record, SF_M_CLEAN_ERASE);
        if (started != FWLAB_SPINE_V0_OK) metadata_fail(f, SF_FAULT_STATE);
        return true;
    case SF_M_CLEAN_ERASE:
        status = effect_child(f, SF_IO_ERASE, m->cleanup_block, 0, &io);
        if (status != 1) return status != 0;
        m->cleanup_generation = io.completion.final_erase_generation;
        m->cleanup_health = io.completion.block_health; m->phase = SF_M_CLEAN_DONE;
        return true;
    case SF_M_CLEAN_DONE:
        block = m->cleanup_block; memset(&m->record, 0, sizeof(m->record));
        m->record.kind = SF_ERASE_DONE; m->record.block = block;
        m->record.block_uid = f->blocks[block].disk.block_uid;
        m->record.erase_generation = f->blocks[block].disk.erase_generation;
        m->record.final_erase_generation = m->cleanup_generation; m->record.health = m->cleanup_health;
        m->record.intent_sequence = f->erase_intent_sequence;
        started = begin_journal(f, &m->record, SF_M_CLEAN_SELECT);
        if (started != FWLAB_SPINE_V0_OK) metadata_fail(f, SF_FAULT_STATE);
        return true;
    default:
        metadata_fail(f, SF_FAULT_STATE); return true;
    }
}
