/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "fwlab/private/nfc_page_v2_lab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
#define MAIN FWLAB_NFC_PAGE_V2_MAIN_BYTES
#define OOB FWLAB_NFC_PAGE_V2_OOB_BYTES
#define PAGES FWLAB_NFC_PAGE_V2_MAX_PAGES

struct cells {
    uint8_t main[PAGES * MAIN], oob[PAGES * OOB];
    uint16_t generation, next;
};
struct fake {
    struct cells cell[4][2];
    uint32_t reads, programs, erases, last_count;
    uint8_t error; /* first LUN/page: 1=IO, 2=invalid facts, 3=TORN */
};
struct fixture {
    struct fake fake;
    struct fwlab_nfc_page_v2_lab_config config;
    struct fwlab_nand_batch_v2 media;
    struct fwlab_nfc_page_v2_lab *model;
    struct fwlab_nfc_page_v2_provider provider;
    void *arena;
    uint8_t main[PAGES * MAIN + 1], oob[PAGES * OOB + 1];
};
static struct cells *cells(struct fake *f, const struct fwlab_nfc_ppa *p)
{
    CHECK(p->channel < 2 && p->lun < 2 && p->plane < 2 && !p->block && p->page < PAGES);
    return &f->cell[p->channel * 2u + p->lun][p->plane];
}
static enum fwlab_nfc_api_result read_pages(void *opaque, const struct fwlab_nfc_ppa *p,
    uint32_t n, uint8_t *main, size_t mn, uint8_t *oob, size_t on,
    struct fwlab_nand_page_info *out, size_t capacity, struct fwlab_nand_block_info *block)
{
    struct fake *f = opaque;
    struct cells *c = cells(f, p);
    bool error = !p->channel && !p->lun && !p->plane && !p->page;
    CHECK(n && n <= PAGES - p->page && capacity >= n && mn == n * MAIN && on == n * OOB);
    CHECK((uintptr_t)main % 64u == 0 && (uintptr_t)oob % 64u == 0);
    ++f->reads; f->last_count = n;
    if (error && f->error == 1) { memset(main, 0x91, mn); return FWLAB_NFC_API_INVARIANT_FAILURE; }
    memset(block, 0, sizeof(*block));
    block->version = FWLAB_NFC_CONTRACT_VERSION; block->size = sizeof(*block);
    block->next_program_page = c->next; block->erase_generation = c->generation;
    memcpy(main, c->main + p->page * MAIN, mn); memcpy(oob, c->oob + p->page * OOB, on);
    for (uint32_t i = 0; i < n; ++i) {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].version = FWLAB_NFC_CONTRACT_VERSION; out[i].size = sizeof(out[i]);
        out[i].erase_generation_seen = c->generation;
        out[i].program_count = (uint8_t)(p->page + i < c->next);
        out[i].state = out[i].program_count ? FWLAB_NAND_PAGE_VALID : FWLAB_NAND_PAGE_ERASED;
        if (error && !i && f->error == 2) out[i].program_count = 2;
        if (error && !i && f->error == 3) out[i].state = FWLAB_NAND_PAGE_TORN;
    }
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result scalar_read(void *f, const struct fwlab_nfc_ppa *p,
    uint8_t *m, uint32_t mn, uint8_t *o, uint32_t on,
    struct fwlab_nand_page_info *pi, struct fwlab_nand_block_info *bi)
{ return read_pages(f, p, 1, m, mn, o, on, pi, 1, bi); }
static void effect(struct fwlab_nand_media_result *r, const struct cells *c)
{
    memset(r, 0, sizeof(*r)); r->version = FWLAB_NFC_CONTRACT_VERSION; r->size = sizeof(*r);
    r->base_erase_generation = r->final_erase_generation = c->generation;
    r->physical_outcome = FWLAB_NFC_PHYS_APPLIED; r->integrity = FWLAB_NFC_INTEGRITY_COMPLETE;
}
static enum fwlab_nfc_api_result program_pages(void *opaque, const struct fwlab_nfc_ppa *p,
    uint32_t n, const uint8_t *main, size_t mn, const uint8_t *oob, size_t on,
    struct fwlab_nand_media_result *out, size_t capacity)
{
    struct fake *f = opaque; struct cells *c = cells(f, p);
    CHECK(n && p->page == c->next && n <= PAGES - p->page && mn == n * MAIN && on == n * OOB && capacity >= n);
    ++f->programs;
    memcpy(c->main + p->page * MAIN, main, mn); memcpy(c->oob + p->page * OOB, oob, on);
    for (uint32_t i = 0; i < n; ++i) {
        effect(&out[i], c); out[i].applied_main_bytes = MAIN; out[i].applied_oob_bytes = OOB;
        out[i].applied_region_mask = FWLAB_NFC_REGION_MASK;
    }
    c->next = (uint16_t)(p->page + n);
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result scalar_program(void *f, const struct fwlab_nfc_ppa *p,
    const uint8_t *m, uint32_t mn, const uint8_t *o, uint32_t on,
    uint32_t am, uint32_t ao, uint8_t integrity, struct fwlab_nand_media_result *out)
{
    CHECK(am == mn && ao == on && integrity == FWLAB_NFC_INTEGRITY_COMPLETE);
    return program_pages(f, p, 1, m, mn, o, on, out, 1);
}
static enum fwlab_nfc_api_result erase(void *opaque, const struct fwlab_nfc_ppa *p,
    uint32_t n, uint8_t integrity, struct fwlab_nand_media_result *out)
{
    struct fake *f = opaque; struct cells *c = cells(f, p);
    CHECK(!p->page && n == PAGES && integrity == FWLAB_NFC_INTEGRITY_COMPLETE);
    ++f->erases; effect(out, c); out->applied_pages = n;
    out->final_erase_generation = ++c->generation; c->next = 0;
    memset(c->main, 0xff, sizeof(c->main)); memset(c->oob, 0xff, sizeof(c->oob));
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result bad(void *f, const struct fwlab_nfc_ppa *p)
{ (void)f; (void)p; CHECK(0); return FWLAB_NFC_API_INVARIANT_FAILURE; }
static uint64_t hash(void *f) { (void)f; CHECK(0); return 0; }
static const struct fwlab_nand_media_ops scalar_ops = {
    FWLAB_NFC_CONTRACT_VERSION, sizeof(struct fwlab_nand_media_ops), 0,
    scalar_read, scalar_program, erase, bad, hash
};
static const struct fwlab_nand_batch_v2_ops batch_ops = {
    FWLAB_NAND_BATCH_V2_VERSION, sizeof(struct fwlab_nand_batch_v2_ops), 0, read_pages, program_pages
};
static struct fixture *create(void)
{
    struct fixture *f = calloc(1, sizeof(*f));
    struct fwlab_nfc_page_v2_config *c;
    CHECK(f); c = &f->config.base;
    c->version = FWLAB_NFC_PAGE_V2_VERSION; c->size = sizeof(*c); c->profile = FWLAB_NFC_PAGE_V2_PROFILE_R0;
    c->instance_nonce = 71; c->operation_uid_limit = UINT64_MAX; c->controller_epoch = 3; c->generation = 5;
    c->media_uuid[0] = 9;
    c->geometry = (struct fwlab_nfc_geometry){
        .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(struct fwlab_nfc_geometry),
        .channels = 2, .luns_per_channel = 2, .planes_per_lun = 2, .blocks_per_plane = 1,
        .pages_per_block = PAGES, .plane_parallelism_per_lun = 2,
        .main_bytes_per_page = MAIN, .oob_bytes_per_page = OOB,
        .max_programs_per_erase = 1, .program_order = FWLAB_NFC_PROGRAM_ASCENDING
    };
    f->config.version = FWLAB_NFC_PAGE_V2_LAB_VERSION; f->config.size = sizeof(f->config);
    f->config.command_ns = 10; f->config.array_read_ns = 100;
    f->config.channel_bytes_per_second = UINT64_C(211200000000); /* LAB 4224 B /20 ns */
    f->config.virtual_ns_limit = UINT64_C(1000000);
    for (unsigned l = 0; l < 4; ++l) {
        f->config.lun[l].package = (uint16_t)(l / 2); f->config.lun[l].die = (uint16_t)l;
        f->config.lun[l].target_lun = (uint16_t)(l % 2);
        for (unsigned p = 0; p < 2; ++p) {
            struct cells *b = &f->fake.cell[l][p]; b->next = PAGES;
            for (unsigned i = 0; i < PAGES * MAIN; ++i) b->main[i] = (uint8_t)(i + (i >> 12) * 7 + l * 31 + p * 13);
            for (unsigned i = 0; i < PAGES * OOB; ++i) b->oob[i] = (uint8_t)(i * 11 + l * 47 + p * 17);
        }
    }
    f->media.version = FWLAB_NAND_BATCH_V2_VERSION; f->media.size = sizeof(f->media); f->media.ops = &batch_ops;
    f->media.scalar = (struct fwlab_nand_media){&scalar_ops, &f->fake}; f->media.geometry = c->geometry;
    memcpy(f->media.media_uuid, c->media_uuid, 16);
    CHECK(fwlab_nfc_page_v2_lab_arena_size() % fwlab_nfc_page_v2_lab_arena_alignment() == 0);
    f->arena = aligned_alloc(fwlab_nfc_page_v2_lab_arena_alignment(), fwlab_nfc_page_v2_lab_arena_size());
    CHECK(f->arena);
    CHECK(fwlab_nfc_page_v2_lab_init(f->arena, fwlab_nfc_page_v2_lab_arena_size(), &f->config,
                                    &f->media, &f->model) == FWLAB_NFC_API_OK);
    f->provider = fwlab_nfc_page_v2_lab_provider(f->model); CHECK(f->provider.ops);
    return f;
}
static struct fwlab_nfc_page_v2_request request(uint64_t uid, unsigned channel, unsigned lun, unsigned count)
{
    struct fwlab_nfc_page_v2_request r = {0};
    r.version = FWLAB_NFC_PAGE_V2_VERSION; r.size = sizeof(r); r.kind = FWLAB_NFC_PAGE_V2_READ_GROUP;
    r.operation = (struct fwlab_nfc_operation_token){71, uid, 3, 5};
    r.first.channel = (uint16_t)channel; r.first.lun = (uint16_t)lun; r.page_count = count;
    return r;
}
static void submit(struct fixture *f, const struct fwlab_nfc_page_v2_request *r)
{ CHECK(f->provider.ops->try_submit(f->model, r).disposition == FWLAB_NFC_ACCEPTED); }
static void begin(struct fixture *f)
{ CHECK(fwlab_nfc_page_v2_lab_begin_timed_read(f->model) == FWLAB_NFC_API_OK); }
static struct fwlab_nfc_page_v2_lab_stats stats(struct fixture *f)
{
    struct fwlab_nfc_page_v2_lab_stats s;
    CHECK(fwlab_nfc_page_v2_lab_snapshot(f->model, &s) == FWLAB_NFC_API_OK); return s;
}
static void drive(struct fixture *f)
{
    struct fwlab_nfc_page_v2_step_result s;
    unsigned guard = 0;
    do {
        CHECK(++guard <= 2000);
        CHECK(f->provider.ops->step(f->model, 1, &s) == FWLAB_NFC_API_OK && s.units_used <= 1);
    } while (s.units_used);
}
static struct fwlab_nfc_page_v2_result take(struct fixture *f, const struct fwlab_nfc_page_v2_request *r,
                                          bool expect_data)
{
    struct fwlab_nfc_page_v2_result result;
    struct fwlab_nfc_page_v2_output out = {f->main + 1, r->page_count * MAIN, f->oob + 1, r->page_count * OOB};
    memset(f->main, 0xa7, sizeof(f->main)); memset(f->oob, 0xb8, sizeof(f->oob));
    CHECK(f->provider.ops->take_result(f->model, &r->operation, &result, &out) == FWLAB_NFC_API_OK);
    CHECK(result.operation.operation_uid == r->operation.operation_uid && result.page_count == r->page_count);
    if (expect_data) {
        struct cells *c = cells(&f->fake, &r->first);
        CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS && result.read_valid && result.delivered_pages == r->page_count);
        CHECK(!memcmp(out.main, c->main + r->first.page * MAIN, out.main_bytes));
        CHECK(!memcmp(out.oob, c->oob + r->first.page * OOB, out.oob_bytes));
    } else {
        CHECK(!result.read_valid && !result.delivered_pages);
        for (size_t i = 0; i < sizeof(f->main); ++i) CHECK(f->main[i] == 0xa7);
        for (size_t i = 0; i < sizeof(f->oob); ++i) CHECK(f->oob[i] == 0xb8);
    }
    CHECK(f->main[0] == 0xa7 && f->oob[0] == 0xb8);
    return result;
}
static uint64_t event_time(struct fixture *f, uint64_t uid, unsigned page, unsigned event)
{
    struct fwlab_nfc_page_v2_lab_stats s = stats(f);
    for (uint32_t i = 0; i < s.trace_count; ++i) {
        struct fwlab_nfc_page_v2_lab_trace t;
        CHECK(fwlab_nfc_page_v2_lab_trace_at(f->model, i, &t) == FWLAB_NFC_API_OK);
        if (t.operation_uid == uid && t.ppa.page == page && t.event == event) return t.now_ns;
    }
    CHECK(0); return 0;
}
static void destroy(struct fixture *f)
{
    bool quiet = false;
    CHECK(f->provider.ops->reset_begin(f->model, 71, 3) == FWLAB_NFC_API_OK);
    CHECK(f->provider.ops->quiescent(f->model, 71, 3, &quiet) == FWLAB_NFC_API_OK && quiet);
    free(f->arena); free(f);
}
static void preparation(void)
{
    struct fixture *f = create();
    struct fwlab_nfc_page_v2_request r = request(9, 0, 0, 1);
    struct fwlab_nfc_page_v2_result result;
    struct fwlab_nfc_page_v2_provider original = f->provider;
    r.kind = FWLAB_NFC_PAGE_V2_ERASE; submit(f, &r);
    CHECK(fwlab_nfc_page_v2_lab_begin_timed_read(f->model) == FWLAB_NFC_API_WRONG_STATE);
    drive(f);
    CHECK(fwlab_nfc_page_v2_lab_begin_timed_read(f->model) == FWLAB_NFC_API_WRONG_STATE);
    CHECK(f->provider.ops->take_result(f->model, &r.operation, &result, NULL) == FWLAB_NFC_API_OK);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS && f->fake.erases == 1);
    r = request(10, 0, 0, 2); r.kind = FWLAB_NFC_PAGE_V2_PROGRAM_GROUP;
    r.main = f->main + 1; r.main_bytes = 2 * MAIN; r.oob = f->oob + 1; r.oob_bytes = 2 * OOB;
    memset(f->main, 0x3c, sizeof(f->main)); memset(f->oob, 0x7d, sizeof(f->oob));
    submit(f, &r); drive(f);
    CHECK(f->provider.ops->take_result(f->model, &r.operation, &result, NULL) == FWLAB_NFC_API_OK);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS && f->fake.programs == 1);
    begin(f);
    CHECK(f->provider.ops == original.ops && f->provider.context == original.context);
    CHECK(stats(f).now_ns == 0 && stats(f).transitions == 0 && stats(f).accepted_reads == 0);
    CHECK(fwlab_nfc_page_v2_lab_begin_timed_read(f->model) == FWLAB_NFC_API_WRONG_STATE);
    CHECK(f->provider.ops->try_submit(f->model, &r).reason == FWLAB_NFC_REASON_STALE);
    r.operation.operation_uid = 11;
    CHECK(f->provider.ops->try_submit(f->model, &r).reason == FWLAB_NFC_REASON_UNSUPPORTED);
    r = request(11, 0, 0, 2); submit(f, &r); drive(f); (void)take(f, &r, true);
    CHECK(stats(f).now_ns == 260 && f->fake.last_count == 1);
    r = request(12, 0, 0, 1); r.first.page = 63;
    submit(f, &r); drive(f); result = take(f, &r, true);
    CHECK(result.page[0].page_state == FWLAB_NAND_PAGE_ERASED && !result.page[0].program_count);
    destroy(f);
}
static void resources(void)
{
    struct fixture *f = create();
    struct fwlab_nfc_page_v2_request r[5] = {
        request(1, 0, 0, 2), request(2, 0, 1, 1), request(3, 1, 0, 1),
        request(4, 0, 0, 1), request(5, 1, 1, 1)
    };
    struct fwlab_nfc_page_v2_lab_stats s;
    struct fwlab_nfc_page_v2_result result;
    struct fwlab_nfc_page_v2_output invalid = {f->arena, MAIN, f->oob, OOB};
    begin(f); r[3].first.plane = 1;
    for (unsigned i = 0; i < 4; ++i) submit(f, &r[i]);
    CHECK(f->provider.ops->try_submit(f->model, &r[4]).disposition == FWLAB_NFC_BACKPRESSURE);
    submit(f, &r[0]);
    r[0].page_count = 3;
    CHECK(f->provider.ops->try_submit(f->model, &r[0]).reason == FWLAB_NFC_REASON_STALE);
    r[0].page_count = 2; drive(f); s = stats(f);
    CHECK(s.now_ns == 410 && s.materialized_pages == 5 && s.transitions == 25 && s.results_pending == 4);
    CHECK(s.channel_busy_ns[0] == 120 && s.channel_busy_ns[1] == 30 && !s.busy_channels && !s.held_luns);
    CHECK(s.register_busy_ns[0] == 390 && s.register_busy_ns[1] == 140);
    CHECK(event_time(f, 1, 0, FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN) == 0);
    CHECK(event_time(f, 3, 0, FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN) == 0);
    CHECK(event_time(f, 2, 0, FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN) == 10);
    CHECK(event_time(f, 2, 0, FWLAB_NFC_PAGE_V2_LAB_ARRAY_READY) == 120);
    CHECK(event_time(f, 2, 0, FWLAB_NFC_PAGE_V2_LAB_DATA_BEGIN) == 130);
    CHECK(event_time(f, 1, 1, FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN) == 150);
    CHECK(event_time(f, 4, 0, FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN) == 280);
    CHECK(event_time(f, 3, 0, FWLAB_NFC_PAGE_V2_LAB_TERMINAL) == 130);
    CHECK(f->provider.ops->take_result(f->model, &r[2].operation, &result, &invalid) == FWLAB_NFC_API_INVALID_CONTRACT);
    CHECK(stats(f).results_pending == 4);
    (void)take(f, &r[2], true); (void)take(f, &r[1], true); (void)take(f, &r[0], true); (void)take(f, &r[3], true);
    CHECK(f->provider.ops->try_submit(f->model, &r[0]).reason == FWLAB_NFC_REASON_STALE);
    submit(f, &r[4]);
    CHECK(event_time(f, 5, 0, FWLAB_NFC_PAGE_V2_LAB_ADMIT) == 410);
    drive(f); (void)take(f, &r[4], true); destroy(f);
}
static void cancellation(void)
{
    struct fixture *f = create();
    struct fwlab_nfc_page_v2_request a = request(1, 0, 0, 3), b = request(2, 0, 0, 1);
    struct fwlab_nfc_page_v2_step_result step;
    struct fwlab_nfc_page_v2_result r;
    bool quiet = true;
    begin(f); submit(f, &a); submit(f, &b);
    CHECK(f->provider.ops->cancel(f->model, &b.operation) == FWLAB_NFC_API_OK);
    for (unsigned i = 0; i < 3; ++i) CHECK(f->provider.ops->step(f->model, 1, &step) == FWLAB_NFC_API_OK);
    CHECK(stats(f).now_ns == 10 && stats(f).held_luns == 1 && !f->fake.reads);
    CHECK(f->provider.ops->cancel(f->model, &a.operation) == FWLAB_NFC_API_OK);
    CHECK(f->provider.ops->reset_begin(f->model, 71, 3) == FWLAB_NFC_API_OK);
    CHECK(f->provider.ops->quiescent(f->model, 71, 3, &quiet) == FWLAB_NFC_API_OK && !quiet);
    drive(f);
    CHECK(f->fake.reads == 1 && stats(f).now_ns == 130 && !stats(f).held_luns);
    CHECK(f->provider.ops->quiescent(f->model, 71, 3, &quiet) == FWLAB_NFC_API_OK && !quiet);
    r = take(f, &a, false); CHECK(r.terminal == FWLAB_NFC_TERMINAL_CANCELLED && r.reason == FWLAB_NFC_REASON_RESET);
    r = take(f, &b, false); CHECK(r.terminal == FWLAB_NFC_TERMINAL_CANCELLED);
    CHECK(f->provider.ops->quiescent(f->model, 71, 3, &quiet) == FWLAB_NFC_API_OK && quiet);
    CHECK(f->provider.ops->step(f->model, 1, &step) == FWLAB_NFC_API_OK && !step.units_used);
    destroy(f);
}
static void errors(void)
{
    for (unsigned mode = 1; mode <= 3; ++mode) {
        struct fixture *f = create();
        struct fwlab_nfc_page_v2_request a = request(1, 0, 0, 2), b = request(2, 1, 0, 1);
        struct fwlab_nfc_page_v2_result r;
        begin(f); f->fake.error = (uint8_t)mode; submit(f, &a); submit(f, &b); drive(f);
        CHECK(f->fake.reads == 2 && !stats(f).held_luns && !stats(f).busy_channels);
        r = take(f, &a, false); CHECK(r.terminal == FWLAB_NFC_TERMINAL_FAILED);
        CHECK(r.reason == (mode == 3 ? FWLAB_NFC_REASON_ECC_UNCORRECTABLE : FWLAB_NFC_REASON_INTERNAL));
        CHECK(r.backend_status == (mode == 3 ? 0u : FWLAB_NFC_API_INVARIANT_FAILURE));
        (void)take(f, &b, mode == 3);
        CHECK(stats(f).quarantined == (mode != 3)); destroy(f);
    }
}
static void late_cancel(void)
{
    struct fixture *f = create();
    struct fwlab_nfc_page_v2_request r = request(1, 0, 0, 1);
    struct fwlab_nfc_page_v2_result result;
    uint32_t trace_count;
    bool quiet = true;
    begin(f); submit(f, &r); drive(f); trace_count = stats(f).trace_count;
    CHECK(f->provider.ops->cancel(f->model, &r.operation) == FWLAB_NFC_API_OK);
    CHECK(f->provider.ops->reset_begin(f->model, 71, 3) == FWLAB_NFC_API_OK);
    CHECK(f->provider.ops->quiescent(f->model, 71, 3, &quiet) == FWLAB_NFC_API_OK && !quiet);
    CHECK(stats(f).trace_count == trace_count);
    /* Upper cancellation discards; lower completed outcome stays exact. */
    CHECK(f->provider.ops->take_result(f->model, &r.operation, &result, NULL) == FWLAB_NFC_API_OK);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS && result.read_valid && !result.reason && !result.delivered_pages);
    CHECK(f->provider.ops->quiescent(f->model, 71, 3, &quiet) == FWLAB_NFC_API_OK && quiet);
    destroy(f);
}
static void bounds(void)
{
    struct fixture *f = create();
    struct fwlab_nfc_page_v2_lab *unused;
    struct fwlab_nfc_page_v2_request r = request(1, 0, 0, PAGES);
    f->config.command_ns = UINT64_MAX;
    CHECK(fwlab_nfc_page_v2_lab_init(f->arena, fwlab_nfc_page_v2_lab_arena_size(), &f->config,
                                    &f->media, &unused) == FWLAB_NFC_API_INVALID_CONTRACT);
    f->config.command_ns = 10; f->config.lun[1].target_lun = 0;
    CHECK(fwlab_nfc_page_v2_lab_init(f->arena, fwlab_nfc_page_v2_lab_arena_size(), &f->config,
                                    &f->media, &unused) == FWLAB_NFC_API_INVALID_CONTRACT);
    begin(f); submit(f, &r); drive(f); (void)take(f, &r, true);
    CHECK(stats(f).now_ns == 8320 && stats(f).materialized_pages == 64 && f->fake.last_count == 1);
    CHECK(stats(f).trace_count == FWLAB_NFC_PAGE_V2_LAB_TRACE_CAPACITY && stats(f).trace_dropped > 0);
    destroy(f);
    f = create(); f->config.command_ns = UINT64_MAX - 2u; f->config.array_read_ns = 1;
    f->config.channel_bytes_per_second = (uint64_t)(MAIN + OOB) * UINT64_C(1000000000);
    f->config.virtual_ns_limit = UINT64_MAX;
    CHECK(fwlab_nfc_page_v2_lab_init(f->arena, fwlab_nfc_page_v2_lab_arena_size(), &f->config,
                                    &f->media, &f->model) == FWLAB_NFC_API_OK);
    f->provider = fwlab_nfc_page_v2_lab_provider(f->model); begin(f);
    r = request(1, 0, 0, 2);
    CHECK(f->provider.ops->try_submit(f->model, &r).reason == FWLAB_NFC_REASON_RANGE);
    r.page_count = 1; submit(f, &r); drive(f); (void)take(f, &r, true);
    CHECK(stats(f).now_ns == UINT64_MAX);
    r.operation.operation_uid = 2;
    CHECK(f->provider.ops->try_submit(f->model, &r).reason == FWLAB_NFC_REASON_RANGE);
    destroy(f);
}
int main(void)
{
    preparation(); resources(); cancellation(); errors(); late_cancel(); bounds();
    puts("NFC_PAGE_V2_LAB_PASS|profile=LAB-READ-R1|synthetic_ns=1|slots=4|prep_same_provider=1|uid_continuous=1|bus_and_register=1|group_sequential=1|actual_read_facts=1|cancel_drain=1|errors_no_output=1|disk_io=0");
    return 0;
}
