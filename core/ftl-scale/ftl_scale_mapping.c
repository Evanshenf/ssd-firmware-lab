/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "ftl_scale_internal.h"
#include <string.h>

static uint32_t *heap_position(struct fwlab_ftl_scale *f, uint32_t b, bool victim)
{
    return victim ? &f->blocks[b].victim_heap_pos : &f->blocks[b].free_heap_pos;
}

static bool less(const struct fwlab_ftl_scale *f, uint32_t a, uint32_t b,
                 bool victim)
{
    if (victim && f->blocks[a].live_pages != f->blocks[b].live_pages)
        return f->blocks[a].live_pages < f->blocks[b].live_pages;
    if (f->blocks[a].wear != f->blocks[b].wear)
        return f->blocks[a].wear < f->blocks[b].wear;
    return a < b;
}

static void heap_swap(struct fwlab_ftl_scale *f, uint32_t *heap,
                      uint32_t a, uint32_t b, bool victim)
{
    uint32_t saved = heap[a];
    heap[a] = heap[b];
    heap[b] = saved;
    *heap_position(f, heap[a], victim) = a;
    *heap_position(f, heap[b], victim) = b;
}

static void heap_fix(struct fwlab_ftl_scale *f, uint32_t *heap,
                     uint32_t size, uint32_t position, bool victim)
{
    while (position && less(f, heap[position], heap[(position - 1u) / 2u], victim)) {
        uint32_t parent = (position - 1u) / 2u;
        heap_swap(f, heap, position, parent, victim);
        position = parent;
    }
    for (;;) {
        uint64_t child = (uint64_t)position * 2u + 1u;
        uint32_t selected;
        if (child >= size) break;
        selected = (uint32_t)child;
        if (selected + 1u < size && less(f, heap[selected + 1u], heap[selected], victim))
            ++selected;
        if (!less(f, heap[selected], heap[position], victim)) break;
        heap_swap(f, heap, selected, position, victim);
        position = selected;
    }
}

static void heap_update(struct fwlab_ftl_scale *f, uint32_t b, bool victim,
                        bool eligible)
{
    uint32_t *heap = victim ? f->victim_heap : f->free_heap;
    uint32_t *size = victim ? &f->victim_count : &f->free_count;
    uint32_t position = *heap_position(f, b, victim);
    bool partitioned = !victim && f->disk_format == SF_MULTIHEAD_FORMAT_VERSION;
    uint32_t previous_size;
    if (partitioned) {
        struct sf_head_domain_index *domain = &f->heads.domain[sf_head_domain(f, b)];
        heap += domain->heap_offset;
        size = &domain->free_count;
    }
    previous_size = *size;
    if (position != SF_HEAP_NONE && !eligible) {
        --*size;
        *heap_position(f, b, victim) = SF_HEAP_NONE;
        if (position < *size) {
            heap[position] = heap[*size];
            *heap_position(f, heap[position], victim) = position;
            heap_fix(f, heap, *size, position, victim);
        }
    } else if (eligible) {
        if (position == SF_HEAP_NONE) {
            position = (*size)++;
            heap[position] = b;
            *heap_position(f, b, victim) = position;
        }
        heap_fix(f, heap, *size, position, victim);
    }
    if (partitioned) {
        if (*size > previous_size) ++f->free_count;
        else if (*size < previous_size) --f->free_count;
    }
}

uint32_t sf_free_best(const struct fwlab_ftl_scale *f)
{
    uint32_t best = SF_NONE;
    if (!f || !f->free_count) return SF_NONE;
    if (f->disk_format != SF_MULTIHEAD_FORMAT_VERSION) return f->free_heap[0];
    for (uint32_t d = 0; d < f->heads.count; ++d) {
        const struct sf_head_domain_index *domain = &f->heads.domain[d];
        if (domain->free_count) {
            uint32_t candidate = f->free_heap[domain->heap_offset];
            if (best == SF_NONE || less(f, candidate, best, false)) best = candidate;
        }
    }
    return best;
}

/* Only the shared semantic apply/rebuild writes this index. The persistent
 * block's PPA, not a transient scheduling choice, determines its format3 head. */
static uint32_t *head_slot(struct fwlab_ftl_scale *f, uint32_t block)
{
    return f->disk_format == SF_MULTIHEAD_FORMAT_VERSION ?
        &f->heads.domain[sf_head_domain(f, block)].head : &f->host_head;
}

void sf_heap_refresh(struct fwlab_ftl_scale *f, uint32_t b)
{
    const struct sf_block *block = &f->blocks[b];
    bool good = b >= f->root.layout.data_first_block &&
                block->disk.health == FWLAB_NFC_BLOCK_GOOD && !block->reserved_pages;
    heap_update(f, b, false, good && block->disk.role == SF_FREE);
    heap_update(f, b, true, good && block->disk.role == SF_CLOSED &&
                block->live_pages < f->config.geometry.pages_per_block);
}

bool sf_validity_get(const struct fwlab_ftl_scale *f, uint32_t ppa)
{
    return ppa < f->physical_pages &&
           (f->validity[ppa / 8u] & (uint8_t)(1u << (ppa % 8u))) != 0;
}

static void validity_set(struct fwlab_ftl_scale *f, uint32_t ppa, bool value)
{
    uint8_t mask = (uint8_t)(1u << (ppa % 8u));
    if (value) f->validity[ppa / 8u] |= mask;
    else f->validity[ppa / 8u] &= (uint8_t)~mask;
}

static bool empty_entry(const struct sf_map_entry *e)
{
    return (e->state == SF_UNMAPPED || e->state == SF_TOMBSTONE) &&
           e->ppa == SF_NONE && !e->erase_generation && !e->valid_mask && !e->data_uid;
}

static bool value_entry(const struct fwlab_ftl_scale *f, const struct sf_map_entry *e)
{
    uint32_t ppb = f->config.geometry.pages_per_block, b;
    uint64_t uid;
    if (e->state != SF_VALUE || !e->valid_mask || e->ppa >= f->physical_pages)
        return false;
    b = e->ppa / ppb;
    if (b < f->root.layout.data_first_block || f->blocks[b].disk.role == SF_FREE ||
        f->blocks[b].disk.role == SF_META || f->blocks[b].disk.role == SF_BAD ||
        !f->blocks[b].disk.block_uid ||
        f->blocks[b].disk.block_uid > (UINT64_MAX - (ppb - 1u)) / ppb ||
        e->erase_generation != f->blocks[b].disk.erase_generation)
        return false;
    uid = f->blocks[b].disk.block_uid * ppb + e->ppa % ppb;
    return e->data_uid == uid;
}

bool sf_rebuild_indexes(struct fwlab_ftl_scale *f)
{
    uint32_t b, lpn, ppb = f->config.geometry.pages_per_block;
    f->free_count = f->victim_count = 0;
    f->host_head = SF_NONE;
    if (!sf_heads_init(f)) return false;
    memset(f->validity, 0, ((size_t)f->physical_pages + 7u) / 8u);
    for (b = 0; b < f->physical_blocks; ++b) {
        struct sf_block *block = &f->blocks[b];
        block->free_heap_pos = block->victim_heap_pos = SF_HEAP_NONE;
        block->live_pages = block->reserved_pages = 0;
        block->wear = block->disk.erase_generation;
        if (block->disk.flags || block->disk.allocation_end > ppb ||
            block->disk.health > FWLAB_NFC_BLOCK_RUNTIME_BAD ||
            block->disk.role < SF_META || block->disk.role > SF_BAD ||
            ((b < f->root.layout.data_first_block) != (block->disk.role == SF_META)))
            return false;
        if (block->disk.role == SF_FREE &&
            (block->disk.block_uid || block->disk.allocation_end || block->disk.health))
            return false;
        if (block->disk.role != SF_META && block->disk.role != SF_FREE &&
            (!block->disk.block_uid || block->disk.block_uid >= f->next_block_uid))
            return false;
        if (block->disk.role == SF_HOST_OPEN) {
            uint32_t *head = head_slot(f, b);
            if (*head != SF_NONE) return false;
            *head = b;
        }
        if (f->disk_format == SF_MULTIHEAD_FORMAT_VERSION && block->disk.role == SF_GC_DEST) {
            if (f->heads.gc_destination != SF_NONE || block->disk.allocation_end) return false;
            f->heads.gc_destination = b;
        }
    }
    if (f->disk_format == SF_MULTIHEAD_FORMAT_VERSION &&
        f->heads.gc_destination != SF_NONE && !sf_heads_empty(f)) return false;
    for (lpn = 0; lpn < f->root.layout.lpn_count; ++lpn) {
        const struct sf_map_entry *entry = &f->map[lpn];
        if (entry->state != SF_VALUE) {
            if (!empty_entry(entry)) return false;
            continue;
        }
        if (!value_entry(f, entry) || sf_validity_get(f, entry->ppa)) return false;
        b = entry->ppa / ppb;
        if (entry->ppa % ppb >= f->blocks[b].disk.allocation_end) return false;
        validity_set(f, entry->ppa, true);
        ++f->blocks[b].live_pages;
    }
    for (b = f->root.layout.data_first_block; b < f->physical_blocks; ++b) {
        if (f->blocks[b].disk.role == SF_RECLAIM_PENDING && f->blocks[b].live_pages)
            return false;
        sf_heap_refresh(f, b);
    }
    return true;
}

bool sf_seal_recovered_heads(struct fwlab_ftl_scale *f)
{
    uint32_t b;
    for (b = f->root.layout.data_first_block; b < f->physical_blocks; ++b) {
        struct sf_block *block = &f->blocks[b];
        if (block->disk.role == SF_HOST_OPEN || block->disk.role == SF_GC_DEST) {
            block->disk.role = SF_CLOSED;
            block->disk.allocation_end = f->config.geometry.pages_per_block;
        }
        if (block->disk.role == SF_CLOSED && !block->live_pages)
            block->disk.role = SF_RECLAIM_PENDING;
        sf_heap_refresh(f, b);
    }
    f->host_head = SF_NONE;
    for (b = 0; b < f->heads.count; ++b) f->heads.domain[b].head = SF_NONE;
    f->heads.gc_destination = SF_NONE;
    return true;
}

bool sf_next_reclaim_pending(struct fwlab_ftl_scale *f, uint32_t *block)
{
    uint32_t b;
    if (block == NULL) return false;
    for (b = f->root.layout.data_first_block; b < f->physical_blocks; ++b)
        if (f->blocks[b].disk.role == SF_RECLAIM_PENDING && !f->blocks[b].live_pages) {
            *block = b;
            return true;
        }
    return false;
}

bool sf_record_validate_apply(struct fwlab_ftl_scale *f, const struct sf_record *r)
{
    uint32_t ppb = f->config.geometry.pages_per_block, i, j;
    struct sf_block *block;
    uint32_t *head;
    bool multi = f->disk_format == SF_MULTIHEAD_FORMAT_VERSION;
    bool window = r->kind == SF_MAP_WINDOW;
    bool mapping = r->kind == SF_MAP_GROUP || r->kind == SF_GC_COMMIT || window;
    if (f->disk_format != f->root.disk_format ||
        (f->disk_format != SF_FORMAT_VERSION && !sf_format_windowed(f->disk_format)) ||
        (window && !sf_format_windowed(f->disk_format)) ||
        r->epoch != f->root.generation || r->predecessor != f->record_sequence ||
        f->record_sequence == UINT64_MAX || r->sequence != f->record_sequence + 1u ||
        r->before_map_seq != f->map_sequence ||
        (mapping && f->map_sequence == UINT64_MAX) ||
        r->after_map_seq != f->map_sequence + (mapping ? 1u : 0u) ||
        r->durable_frontier < f->durable_frontier ||
        r->block < f->root.layout.data_first_block || r->block >= f->physical_blocks)
        return false;
    block = &f->blocks[r->block];
    head = head_slot(f, r->block);
    if (!mapping && r->durable_frontier != f->durable_frontier) return false;
    switch (r->kind) {
    case SF_OPEN_HOST:
    case SF_OPEN_GC_DEST:
        if (r->count || block->disk.role != SF_FREE || block->live_pages ||
            r->erase_generation != block->disk.erase_generation ||
            r->block_uid != f->next_block_uid || !r->block_uid ||
            r->block_uid >= UINT64_MAX / ppb ||
            (r->kind == SF_OPEN_HOST && *head != SF_NONE) ||
            (multi && (f->heads.gc_destination != SF_NONE ||
                (r->kind == SF_OPEN_GC_DEST && !sf_heads_empty(f))))) return false;
        break;
    case SF_CLOSE:
        if (r->count || block->disk.role != SF_HOST_OPEN ||
            r->block_uid != block->disk.block_uid || block->reserved_pages ||
            (multi && (*head != r->block || r->erase_generation != block->disk.erase_generation))) return false;
        break;
    case SF_MAP_GROUP:
    case SF_MAP_WINDOW:
    case SF_GC_COMMIT: {
        uint32_t destination = r->kind == SF_GC_COMMIT ? r->other_block : r->block;
        uint32_t max_count = window ? SF_MAX_DELTAS :
            (r->kind == SF_GC_COMMIT ? 61u : SF_MAX_HOST_DELTAS);
        struct sf_block *dest;
        uint32_t start_page;
        if (!r->count || r->count > max_count ||
            destination < f->root.layout.data_first_block || destination >= f->physical_blocks)
            return false;
        dest = &f->blocks[destination];
        start_page = dest->disk.allocation_end;
        if (r->kind == SF_GC_COMMIT) {
            if (destination == r->block || block->disk.role != SF_CLOSED ||
                r->block_uid != block->disk.block_uid || block->live_pages != r->count ||
                dest->disk.role != SF_GC_DEST || r->other_block_uid != dest->disk.block_uid ||
                start_page != 0 || !sf_heads_empty(f) ||
                (multi && (f->heads.gc_destination != destination ||
                    *head_slot(f, destination) != SF_NONE ||
                    r->erase_generation != block->disk.erase_generation))) return false;
        } else if (dest->disk.role != SF_HOST_OPEN ||
                   r->block_uid != dest->disk.block_uid || *head_slot(f, destination) != destination ||
                   (multi && (r->erase_generation != dest->disk.erase_generation ||
                       (dest->reserved_pages && dest->reserved_pages != r->count))))
            return false;
        if (start_page > ppb || r->count > ppb - start_page) return false;
        for (i = 0; i < r->count; ++i) {
            const struct sf_delta *d = &r->delta[i];
            if (d->reserved || d->lpn >= f->root.layout.lpn_count ||
                memcmp(&d->before, &f->map[d->lpn], sizeof(d->before)) != 0 ||
                !value_entry(f, &d->after) ||
                d->after.ppa != destination * ppb + start_page + i ||
                sf_validity_get(f, d->after.ppa)) return false;
            if (d->before.state == SF_VALUE) {
                if (!value_entry(f, &d->before) || !sf_validity_get(f, d->before.ppa))
                    return false;
            } else if (!empty_entry(&d->before)) return false;
            if (r->kind == SF_GC_COMMIT &&
                (d->before.state != SF_VALUE || d->before.ppa / ppb != r->block ||
                 d->after.valid_mask != d->before.valid_mask)) return false;
            if (window) {
                if ((uint64_t)d->lpn != (uint64_t)r->delta[0].lpn + i) return false;
            } else for (j = 0; j < i; ++j)
                if (r->delta[j].lpn == d->lpn) return false;
        }
        break;
    }
    case SF_ERASE_INTENT:
        if (r->count || block->live_pages || block->reserved_pages ||
            (block->disk.role != SF_CLOSED && block->disk.role != SF_RECLAIM_PENDING) ||
            r->block_uid != block->disk.block_uid ||
            r->erase_generation != block->disk.erase_generation ||
            r->intent_sequence != r->sequence ||
            (f->erase_intent_block != SF_NONE && f->erase_intent_block != r->block))
            return false;
        break;
    case SF_ERASE_DONE:
        if (r->count || block->live_pages || block->disk.role != SF_RECLAIM_PENDING ||
            r->block_uid != block->disk.block_uid || f->erase_intent_block != r->block ||
            !r->intent_sequence || r->intent_sequence != f->erase_intent_sequence ||
            r->erase_generation != block->disk.erase_generation ||
            r->final_erase_generation <= r->erase_generation || r->health != FWLAB_NFC_BLOCK_GOOD)
            return false;
        break;
    default: return false;
    }
    switch (r->kind) {
    case SF_OPEN_HOST:
    case SF_OPEN_GC_DEST:
        block->disk.block_uid = r->block_uid;
        block->disk.role = r->kind == SF_OPEN_HOST ? SF_HOST_OPEN : SF_GC_DEST;
        block->disk.allocation_end = 0;
        ++f->next_block_uid;
        if (r->kind == SF_OPEN_HOST) *head = r->block;
        else if (multi) f->heads.gc_destination = r->block;
        break;
    case SF_CLOSE:
        block->disk.role = SF_CLOSED;
        block->disk.allocation_end = (uint16_t)ppb;
        *head = SF_NONE;
        break;
    case SF_MAP_GROUP:
    case SF_MAP_WINDOW:
    case SF_GC_COMMIT:
        for (i = 0; i < r->count; ++i) {
            const struct sf_delta *d = &r->delta[i];
            uint32_t destination = d->after.ppa / ppb;
            if (d->before.state == SF_VALUE) {
                uint32_t old = d->before.ppa / ppb;
                validity_set(f, d->before.ppa, false);
                --f->blocks[old].live_pages;
                sf_heap_refresh(f, old);
            }
            f->map[d->lpn] = d->after;
            validity_set(f, d->after.ppa, true);
            ++f->blocks[destination].live_pages;
            f->blocks[destination].disk.allocation_end =
                (uint16_t)(d->after.ppa % ppb + 1u);
        }
        if (r->kind == SF_GC_COMMIT) {
            block->disk.role = SF_RECLAIM_PENDING;
            f->blocks[r->other_block].disk.role = SF_HOST_OPEN;
            *head_slot(f, r->other_block) = r->other_block;
            if (multi) f->heads.gc_destination = SF_NONE;
            sf_heap_refresh(f, r->other_block);
            ++f->garbage_collections;
        } else {
            block->reserved_pages = 0;
            /* A window-format full run closes its head in the same atomic
             * mapping transaction. Partial heads still use explicit CLOSE. */
            if (window && block->disk.allocation_end == ppb) {
                block->disk.role = SF_CLOSED;
                *head = SF_NONE;
            }
        }
        break;
    case SF_ERASE_INTENT:
        block->disk.role = SF_RECLAIM_PENDING;
        f->erase_intent_block = r->block;
        f->erase_intent_sequence = r->sequence;
        break;
    case SF_ERASE_DONE:
        block->disk.role = SF_FREE;
        block->disk.block_uid = 0;
        block->disk.allocation_end = 0;
        block->disk.erase_generation = r->final_erase_generation;
        block->disk.health = r->health;
        block->wear = r->final_erase_generation;
        f->erase_intent_block = SF_NONE;
        f->erase_intent_sequence = 0;
        break;
    default: break;
    }
    sf_heap_refresh(f, r->block);
    f->record_sequence = r->sequence;
    f->map_sequence = r->after_map_seq;
    f->durable_frontier = r->durable_frontier;
    return true;
}
