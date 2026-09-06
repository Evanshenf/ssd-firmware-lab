/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "fwlab/private/nfc_page_v2_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
#define MAIN FWLAB_NFC_PAGE_V2_MAIN_BYTES
#define OOB FWLAB_NFC_PAGE_V2_OOB_BYTES
#define PAGES FWLAB_NFC_PAGE_V2_MAX_PAGES

struct fake {
    uint8_t main[PAGES * MAIN];
    uint8_t oob[PAGES * OOB];
    struct fwlab_nand_page_info page[PAGES];
    struct fwlab_nand_block_info block;
    uint32_t reads, programs, erases, last_read_count, last_program_count;
    uint8_t fail_read, fail_program, no_effect, incomplete;
};
struct fixture {
    struct fake fake;
    struct fwlab_nand_batch_v2 media;
    struct fwlab_nfc_page_v2_config config;
    struct fwlab_nfc_page_v2_model *model;
    struct fwlab_nfc_page_v2_provider provider;
    void *arena;
    uint8_t input_main[PAGES * MAIN], input_oob[PAGES * OOB];
    uint8_t output_main[PAGES * MAIN], output_oob[PAGES * OOB];
};

static enum fwlab_nfc_api_result read_pages(void *opaque,
    const struct fwlab_nfc_ppa *p, uint32_t count, uint8_t *main, size_t mn,
    uint8_t *oob, size_t on, struct fwlab_nand_page_info *page, size_t capacity,
    struct fwlab_nand_block_info *block)
{
    struct fake *f = opaque;
    CHECK(!p->channel && !p->lun && !p->plane && !p->block && count && count <= PAGES - p->page);
    CHECK(mn == count * MAIN && on == count * OOB && capacity >= count);
    ++f->reads; f->last_read_count = count;
    if (f->fail_read) { memset(main, 0xa9, mn); return FWLAB_NFC_API_INVARIANT_FAILURE; }
    memcpy(main, f->main + p->page * MAIN, mn);
    memcpy(oob, f->oob + p->page * OOB, on);
    memcpy(page, f->page + p->page, count * sizeof(*page));
    *block = f->block;
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result scalar_read(void *o, const struct fwlab_nfc_ppa *p,
    uint8_t *m, uint32_t mn, uint8_t *b, uint32_t bn,
    struct fwlab_nand_page_info *pi, struct fwlab_nand_block_info *bi)
{ return read_pages(o, p, 1, m, mn, b, bn, pi, 1, bi); }

static void result_init(struct fwlab_nand_media_result *r, const struct fake *f)
{
    memset(r, 0, sizeof(*r)); r->version = FWLAB_NFC_CONTRACT_VERSION; r->size = sizeof(*r);
    r->base_erase_generation = r->final_erase_generation = f->block.erase_generation;
    r->block_health = f->block.health;
}
static enum fwlab_nfc_api_result program_pages(void *opaque,
    const struct fwlab_nfc_ppa *p, uint32_t count, const uint8_t *main, size_t mn,
    const uint8_t *oob, size_t on, struct fwlab_nand_media_result *out, size_t cap)
{
    struct fake *f = opaque;
    CHECK(count && p->page == f->block.next_program_page && count <= PAGES - p->page);
    CHECK(mn == count * MAIN && on == count * OOB && cap >= count);
    ++f->programs; f->last_program_count = count;
    if (f->fail_program) {
        /* Deliberately write misleading result bytes: not valid on API error. */
        memset(out, 0, cap * sizeof(*out));
        memcpy(f->main + p->page * MAIN, main, mn / 2);
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    for (uint32_t i = 0; i < count; ++i) {
        result_init(&out[i], f);
        if (f->no_effect) { out[i].reason = FWLAB_NFC_REASON_PROGRAM_FAILURE; continue; }
        out[i].physical_outcome = FWLAB_NFC_PHYS_APPLIED;
        out[i].integrity = f->incomplete ? FWLAB_NFC_INTEGRITY_TORN : FWLAB_NFC_INTEGRITY_COMPLETE;
        out[i].applied_region_mask = FWLAB_NFC_REGION_MASK;
        out[i].applied_main_bytes = MAIN; out[i].applied_oob_bytes = OOB;
        f->page[p->page + i].state = f->incomplete ? FWLAB_NAND_PAGE_TORN : FWLAB_NAND_PAGE_VALID;
        f->page[p->page + i].program_count = 1;
    }
    if (!f->no_effect) {
        memcpy(f->main + p->page * MAIN, main, mn); memcpy(f->oob + p->page * OOB, oob, on);
        f->block.next_program_page = (uint16_t)(p->page + count);
    }
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result scalar_program(void *o, const struct fwlab_nfc_ppa *p,
    const uint8_t *m, uint32_t mn, const uint8_t *b, uint32_t bn,
    uint32_t am, uint32_t ab, uint8_t integrity, struct fwlab_nand_media_result *r)
{
    CHECK(am == mn && ab == bn && integrity == FWLAB_NFC_INTEGRITY_COMPLETE);
    return program_pages(o, p, 1, m, mn, b, bn, r, 1);
}
static enum fwlab_nfc_api_result erase(void *opaque, const struct fwlab_nfc_ppa *p,
    uint32_t count, uint8_t integrity, struct fwlab_nand_media_result *r)
{
    struct fake *f = opaque;
    CHECK(!p->page && count == PAGES && integrity == FWLAB_NFC_INTEGRITY_COMPLETE);
    ++f->erases; result_init(r, f);
    r->physical_outcome = FWLAB_NFC_PHYS_APPLIED; r->integrity = integrity; r->applied_pages = count;
    ++f->block.erase_generation; ++f->block.successful_erase_count; ++f->block.erase_attempt_count;
    r->final_erase_generation = f->block.erase_generation; f->block.next_program_page = 0;
    memset(f->main, 0xff, sizeof(f->main)); memset(f->oob, 0xff, sizeof(f->oob));
    for (uint32_t i = 0; i < PAGES; ++i) {
        f->page[i].state = FWLAB_NAND_PAGE_ERASED; f->page[i].program_count = 0;
        f->page[i].erase_generation_seen = f->block.erase_generation;
    }
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result bad(void *opaque, const struct fwlab_nfc_ppa *p)
{ (void)opaque; (void)p; CHECK(0); return FWLAB_NFC_API_INVARIANT_FAILURE; }
static uint64_t hash(void *opaque)
{ (void)opaque; CHECK(0); return 0; }
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
    CHECK(f);
    f->arena = calloc(1, fwlab_nfc_page_v2_arena_size()); CHECK(f->arena);
    CHECK((uintptr_t)f->arena % fwlab_nfc_page_v2_arena_alignment() == 0);
    f->config.version = FWLAB_NFC_PAGE_V2_VERSION; f->config.size = sizeof(f->config);
    f->config.profile = FWLAB_NFC_PAGE_V2_PROFILE_R0;
    f->config.instance_nonce = 71; f->config.operation_uid_limit = UINT64_MAX;
    f->config.controller_epoch = 3; f->config.generation = 5; f->config.media_uuid[0] = 9;
    f->config.geometry = (struct fwlab_nfc_geometry){
        .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(struct fwlab_nfc_geometry),
        .channels = 1, .luns_per_channel = 1, .planes_per_lun = 1, .blocks_per_plane = 1,
        .pages_per_block = PAGES, .plane_parallelism_per_lun = 1,
        .main_bytes_per_page = MAIN, .oob_bytes_per_page = OOB,
        .max_programs_per_erase = 1, .program_order = FWLAB_NFC_PROGRAM_ASCENDING
    };
    f->media.version = FWLAB_NAND_BATCH_V2_VERSION; f->media.size = sizeof(f->media);
    f->media.ops = &batch_ops; f->media.scalar = (struct fwlab_nand_media){&scalar_ops, &f->fake};
    f->media.geometry = f->config.geometry; memcpy(f->media.media_uuid, f->config.media_uuid, 16);
    f->fake.block.version = FWLAB_NFC_CONTRACT_VERSION; f->fake.block.size = sizeof(f->fake.block);
    for (uint32_t i = 0; i < PAGES; ++i) {
        f->fake.page[i].version = FWLAB_NFC_CONTRACT_VERSION; f->fake.page[i].size = sizeof(f->fake.page[i]);
    }
    memset(f->fake.main, 0xff, sizeof(f->fake.main)); memset(f->fake.oob, 0xff, sizeof(f->fake.oob));
    for (size_t i = 0; i < sizeof(f->input_main); ++i) f->input_main[i] = (uint8_t)(i ^ (i >> 10));
    for (size_t i = 0; i < sizeof(f->input_oob); ++i) f->input_oob[i] = (uint8_t)(i * 73u + 5u);
    CHECK(fwlab_nfc_page_v2_init(f->arena, fwlab_nfc_page_v2_arena_size(), &f->config,
                               &f->media, &f->model) == FWLAB_NFC_API_OK);
    f->provider = fwlab_nfc_page_v2_provider(f->model); CHECK(f->provider.ops);
    return f;
}
static void destroy(struct fixture *f)
{
    bool q = false;
    CHECK(f->provider.ops->reset_begin(f->model, 71, 3) == FWLAB_NFC_API_OK);
    CHECK(f->provider.ops->quiescent(f->model, 71, 3, &q) == FWLAB_NFC_API_OK && q);
    free(f->arena); free(f);
}
static struct fwlab_nfc_page_v2_request request(struct fixture *f, uint16_t kind,
    uint64_t uid, uint32_t first, uint32_t count)
{
    struct fwlab_nfc_page_v2_request r = {0};
    r.version = FWLAB_NFC_PAGE_V2_VERSION; r.size = sizeof(r); r.kind = kind;
    r.operation = (struct fwlab_nfc_operation_token){71, uid, 3, 5};
    r.first.page = (uint16_t)first; r.page_count = count;
    if (kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP) {
        r.main = f->input_main; r.main_bytes = count * MAIN;
        r.oob = f->input_oob; r.oob_bytes = count * OOB;
    }
    return r;
}
static void execute(struct fixture *f)
{
    struct fwlab_nfc_page_v2_step_result s;
    CHECK(f->provider.ops->step(f->model, 100, &s) == FWLAB_NFC_API_OK && s.units_used == 1 && s.results_pending == 1);
    CHECK(f->provider.ops->step(f->model, 1, &s) == FWLAB_NFC_API_OK && !s.units_used && s.results_pending == 1);
}
static struct fwlab_nfc_page_v2_result take(struct fixture *f, const struct fwlab_nfc_page_v2_request *r,
    const struct fwlab_nfc_page_v2_output *output)
{
    struct fwlab_nfc_page_v2_result result;
    CHECK(f->provider.ops->take_result(f->model, &r->operation, &result, output) == FWLAB_NFC_API_OK);
    CHECK(result.version == FWLAB_NFC_PAGE_V2_VERSION && result.size == sizeof(result) &&
          result.operation.operation_uid == r->operation.operation_uid && result.kind == r->kind &&
          result.page_count == r->page_count && result.first.page == r->first.page);
    CHECK(f->provider.ops->take_result(f->model, &r->operation, &result, output) == FWLAB_NFC_API_STALE_TOKEN);
    return result;
}

static void snapshot_and_read(void)
{
    struct fixture *f = create();
    struct fwlab_nfc_page_v2_request r = request(f, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, 1, 0, 64), next;
    struct fwlab_nfc_page_v2_result result;
    struct fwlab_nfc_page_v2_output output = {f->output_main, sizeof(f->output_main), f->output_oob, sizeof(f->output_oob)};
    memcpy(f->output_main, f->input_main, sizeof(f->input_main)); memcpy(f->output_oob, f->input_oob, sizeof(f->input_oob));
    CHECK(f->provider.ops->try_submit(f->model, &r).disposition == FWLAB_NFC_ACCEPTED);
    CHECK(!f->fake.reads && !f->fake.programs);
    memset(f->input_main, 0x19, sizeof(f->input_main)); memset(f->input_oob, 0x20, sizeof(f->input_oob));
    CHECK(f->provider.ops->try_submit(f->model, &r).disposition == FWLAB_NFC_ACCEPTED);
    next = request(f, FWLAB_NFC_PAGE_V2_READ_GROUP, 2, 0, 64);
    CHECK(f->provider.ops->try_submit(f->model, &next).disposition == FWLAB_NFC_BACKPRESSURE);
    CHECK(f->provider.ops->take_result(f->model, &r.operation, &result, NULL) == FWLAB_NFC_API_WRONG_STATE);
    execute(f);
    CHECK(f->fake.programs == 1 && f->fake.last_program_count == 64 && f->fake.reads == 1);
    CHECK(!memcmp(f->fake.main, f->output_main, sizeof(f->output_main)) &&
          !memcmp(f->fake.oob, f->output_oob, sizeof(f->output_oob)));
    CHECK(f->provider.ops->try_submit(f->model, &next).disposition == FWLAB_NFC_BACKPRESSURE);
    CHECK(f->provider.ops->cancel(f->model, &r.operation) == FWLAB_NFC_API_OK);
    result = take(f, &r, NULL);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS && result.effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE);
    CHECK(result.page[63].applied_main_bytes == MAIN && result.page[63].applied_pages == 0 && !result.delivered_pages);
    CHECK(f->provider.ops->try_submit(f->model, &r).disposition == FWLAB_NFC_REJECTED);
    CHECK(f->provider.ops->try_submit(f->model, &next).disposition == FWLAB_NFC_ACCEPTED);
    execute(f); CHECK(f->fake.last_read_count == 64);
    --output.main_bytes;
    CHECK(f->provider.ops->take_result(f->model, &next.operation, &result, &output) == FWLAB_NFC_API_INVALID_CONTRACT);
    ++output.main_bytes;
    memset(f->output_main, 0, sizeof(f->output_main)); memset(f->output_oob, 0, sizeof(f->output_oob));
    result = take(f, &next, &output);
    CHECK(result.read_valid && result.delivered_pages == 64 && result.page[63].ecc_status == FWLAB_NFC_ECC_CLEAN);
    CHECK(!memcmp(f->fake.main, f->output_main, sizeof(f->output_main)) && !memcmp(f->fake.oob, f->output_oob, sizeof(f->output_oob)));
    r = request(f, FWLAB_NFC_PAGE_V2_ERASE, 3, 0, 1);
    CHECK(f->provider.ops->try_submit(f->model, &r).disposition == FWLAB_NFC_ACCEPTED); execute(f); result = take(f, &r, NULL);
    CHECK(result.effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE && result.page[0].applied_pages == 64 &&
          result.page[0].base_erase_generation == 0 && result.page[0].final_erase_generation == 1);
    destroy(f);
}

static void cancel_reset(void)
{
    for (unsigned reset = 0; reset < 2; ++reset) {
        struct fixture *f = create(); bool q = true;
        struct fwlab_nfc_page_v2_request r = request(f, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, UINT64_MAX, 0, 2);
        struct fwlab_nfc_page_v2_result result;
        CHECK(f->provider.ops->try_submit(f->model, &r).disposition == FWLAB_NFC_ACCEPTED);
        if (reset) CHECK(f->provider.ops->reset_begin(f->model, 71, 3) == FWLAB_NFC_API_OK);
        else CHECK(f->provider.ops->cancel(f->model, &r.operation) == FWLAB_NFC_API_OK);
        CHECK(f->provider.ops->quiescent(f->model, 71, 3, &q) == FWLAB_NFC_API_OK && !q);
        CHECK(f->provider.ops->try_submit(f->model, &r).disposition == FWLAB_NFC_ACCEPTED);
        execute(f); result = take(f, &r, NULL);
        CHECK(result.effect == FWLAB_NFC_PAGE_V2_EFFECT_NONE && result.terminal == FWLAB_NFC_TERMINAL_CANCELLED &&
              !f->fake.reads && !f->fake.programs && !f->fake.erases);
        CHECK(f->provider.ops->try_submit(f->model, &r).disposition == FWLAB_NFC_REJECTED);
        destroy(f);
    }
    struct fixture *f = create();
    struct fwlab_nfc_page_v2_request r = request(f, FWLAB_NFC_PAGE_V2_READ_GROUP, 9, 0, 2);
    CHECK(f->provider.ops->try_submit(f->model, &r).disposition == FWLAB_NFC_ACCEPTED); execute(f);
    CHECK(f->provider.ops->reset_begin(f->model, 71, 4) == FWLAB_NFC_API_STALE_TOKEN);
    CHECK(f->provider.ops->reset_begin(f->model, 71, 3) == FWLAB_NFC_API_OK);
    struct fwlab_nfc_page_v2_result result = take(f, &r, NULL);
    CHECK(result.read_valid && !result.delivered_pages && result.terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(result.page[0].integrity == FWLAB_NFC_INTEGRITY_COMPLETE &&
          result.page[0].page_state == FWLAB_NAND_PAGE_ERASED && !result.page[0].program_count);
    destroy(f);
}

static void failures(void)
{
    for (unsigned mode = 0; mode < 5; ++mode) {
        struct fixture *f = create();
        struct fwlab_nfc_page_v2_request r = request(f, mode < 3 ? FWLAB_NFC_PAGE_V2_PROGRAM_GROUP : FWLAB_NFC_PAGE_V2_READ_GROUP, 1, 0, 2);
        struct fwlab_nfc_page_v2_output output = {f->output_main, 2 * MAIN, f->output_oob, 2 * OOB};
        struct fwlab_nfc_page_v2_result result;
        if (mode == 0) f->fake.fail_program = 1;
        if (mode == 1) f->fake.no_effect = 1;
        if (mode == 2) f->fake.incomplete = 1;
        if (mode == 3) f->fake.fail_read = 1;
        if (mode == 4) {
            f->fake.block.next_program_page = 2;
            f->fake.page[0].state = FWLAB_NAND_PAGE_VALID; f->fake.page[0].program_count = 1;
            f->fake.page[1].state = FWLAB_NAND_PAGE_TORN; f->fake.page[1].program_count = 1;
        }
        memset(f->output_main, 0xe3, sizeof(f->output_main)); memset(f->output_oob, 0xd4, sizeof(f->output_oob));
        CHECK(f->provider.ops->try_submit(f->model, &r).disposition == FWLAB_NFC_ACCEPTED); execute(f);
        result = take(f, &r, mode < 3 ? NULL : &output);
        CHECK(result.terminal == FWLAB_NFC_TERMINAL_FAILED && !result.read_valid && !result.delivered_pages);
        for (size_t i = 0; i < sizeof(f->output_main); ++i) CHECK(f->output_main[i] == 0xe3);
        for (size_t i = 0; i < sizeof(f->output_oob); ++i) CHECK(f->output_oob[i] == 0xd4);
        CHECK(result.effect == (mode == 0 ? FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN :
            mode == 2 ? FWLAB_NFC_PAGE_V2_EFFECT_NONCOMPLETE : FWLAB_NFC_PAGE_V2_EFFECT_NONE));
        if (mode == 0) CHECK(result.page[0].facts_valid == FWLAB_NFC_PAGE_V2_FACT_EFFECT);
        if (mode == 4) CHECK(result.page[0].ecc_status == FWLAB_NFC_ECC_CLEAN &&
                            result.page[1].ecc_status == FWLAB_NFC_ECC_UNCORRECTABLE);
        destroy(f);
    }
}

static void rejection(void)
{
    struct fixture *f = create();
    struct fwlab_nfc_page_v2_request r = request(f, FWLAB_NFC_PAGE_V2_READ_GROUP, 1, 0, 1), bad_request;
    struct fwlab_nfc_page_v2_model *unused = NULL;
    for (unsigned mode = 0; mode < 8; ++mode) {
        bad_request = r;
        switch (mode) {
        case 0: bad_request.page_count = 65; break;
        case 1: bad_request.first.page = 63; bad_request.page_count = 2; break;
        case 2: bad_request.retry_step = 1; break;
        case 3: bad_request.fault_tag = 1; break;
        case 4: bad_request.operation.generation = 6; break;
        case 5: bad_request.operation.controller_epoch = 4; break;
        case 6: bad_request.main = f->input_main; break;
        case 7: bad_request.operation.operation_uid = 0; break;
        }
        CHECK(f->provider.ops->try_submit(f->model, &bad_request).disposition == FWLAB_NFC_REJECTED);
    }
    CHECK(f->provider.ops->try_submit(f->model, &r).disposition == FWLAB_NFC_ACCEPTED);
    bad_request = r; bad_request.first.page = 1;
    CHECK(f->provider.ops->try_submit(f->model, &bad_request).disposition == FWLAB_NFC_REJECTED);
    execute(f); (void)take(f, &r, NULL);
    CHECK(f->fake.reads == 1 && !f->fake.programs && !f->fake.erases);
    f->config.fault_flags = 1;
    CHECK(fwlab_nfc_page_v2_init(f->arena, fwlab_nfc_page_v2_arena_size(), &f->config, &f->media, &unused) == FWLAB_NFC_API_INVALID_CONTRACT);
    f->config.fault_flags = 0; f->config.timing_flags = 1;
    CHECK(fwlab_nfc_page_v2_init(f->arena, fwlab_nfc_page_v2_arena_size(), &f->config, &f->media, &unused) == FWLAB_NFC_API_INVALID_CONTRACT);
    f->config.timing_flags = 0; ++f->media.media_uuid[0];
    CHECK(fwlab_nfc_page_v2_init(f->arena, fwlab_nfc_page_v2_arena_size(), &f->config, &f->media, &unused) == FWLAB_NFC_API_INVALID_CONTRACT);
    destroy(f);
}

int main(void)
{
    snapshot_and_read(); cancel_reset(); failures(); rejection();
    printf("NFC_PAGE_V2_PASS|profile=PAGE2-R0|owned_payload=270336|scratch=4224|arena=%zu|"
           "snapshot_once=1|group64=1|all_valid_or_no_output=1|cancel_reset_drain=1|unknown=1|disk_io=0\n",
           fwlab_nfc_page_v2_arena_size());
    return 0;
}
