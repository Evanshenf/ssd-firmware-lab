/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _GNU_SOURCE

#include "fwlab/private/nfc_scaled_model.h"
#include "fwlab/private/nfc_trace_window.h"
#include "compact_nand.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

/* Two bounded staging regions. This fake owns bytes, never physical effects. */
struct staging {
    uint8_t main[4096];
    uint8_t oob[128];
    uint64_t reads;
    uint64_t writes;
};

static uint8_t *buffer_span(struct staging *staging,
                            const struct fwlab_nfc_buffer_ref *ref,
                            uint32_t length)
{
    uint8_t *bytes;
    uint32_t capacity;
    if (!staging || !ref || ref->reserved || ref->length != length)
        return NULL;
    if (ref->controller_region == 1) {
        bytes = staging->main;
        capacity = sizeof(staging->main);
    } else if (ref->controller_region == 2) {
        bytes = staging->oob;
        capacity = sizeof(staging->oob);
    } else {
        return NULL;
    }
    return ref->offset <= capacity && length <= capacity - ref->offset ?
               bytes + ref->offset : NULL;
}

static enum fwlab_nfc_api_result buffer_read(
    void *opaque, const struct fwlab_nfc_buffer_ref *ref,
    uint8_t *destination, uint32_t length)
{
    struct staging *staging = opaque;
    uint8_t *source = buffer_span(staging, ref, length);
    if (!source || !destination)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    memcpy(destination, source, length);
    ++staging->reads;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result buffer_write(
    void *opaque, const struct fwlab_nfc_buffer_ref *ref,
    const uint8_t *source, uint32_t length)
{
    struct staging *staging = opaque;
    uint8_t *destination = buffer_span(staging, ref, length);
    if (!source || !destination)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    memcpy(destination, source, length);
    ++staging->writes;
    return FWLAB_NFC_API_OK;
}

static const struct fwlab_nfc_buffer_ops buffer_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_nfc_buffer_ops),
    .read = buffer_read,
    .write = buffer_write,
};

struct fixture {
    struct fwlab_file_nand_v1_config media_config;
    struct fwlab_nfc_model_config config;
    struct fwlab_file_nand_holder_v1 holder;
    struct fwlab_file_nand_v1 *media;
    struct fwlab_nand_media physical;
    struct fwlab_nfc_model *model;
    struct fwlab_nfc_provider provider;
    struct staging staging;
    void *media_arena;
    void *model_arena;
    size_t model_bytes;
    uint64_t nonce;
    uint64_t next_uid;
    uint64_t completion_hash;
    uint32_t retirements;
    int original;
    int directory_fd;
    char directory[64];
};

static struct fwlab_nfc_geometry geometry(unsigned profile)
{
    struct fwlab_nfc_geometry g;
    memset(&g, 0, sizeof(g));
    g.version = FWLAB_NFC_CONTRACT_VERSION;
    g.size = sizeof(g);
    g.channels = profile == 2 ? 2 : 1;
    g.luns_per_channel = profile == 2 ? 2 : 1;
    g.planes_per_lun = profile == 2 ? 2 : 1;
    g.blocks_per_plane = profile == 0 ? 16 : (profile == 1 ? 320 : 160);
    g.pages_per_block = profile == 0 ? 32 : 64;
    g.plane_parallelism_per_lun = g.planes_per_lun;
    g.main_bytes_per_page = 4096;
    g.oob_bytes_per_page = 128;
    g.max_programs_per_erase = 1;
    g.program_order = FWLAB_NFC_PROGRAM_ASCENDING;
    return g;
}

static struct fwlab_nfc_model_config config_for(struct fwlab_nfc_geometry g)
{
    struct fwlab_nfc_model_config c;
    memset(&c, 0, sizeof(c));
    c.version = FWLAB_NFC_CONTRACT_VERSION;
    c.size = sizeof(c);
    c.geometry = g;
    c.ecc.version = FWLAB_NFC_CONTRACT_VERSION;
    c.ecc.size = sizeof(c.ecc);
    c.ecc.main_covered_bytes = 4096;
    c.ecc.oob_covered_bytes = 128;
    c.ecc.main_step_bytes = 512;
    c.ecc.oob_step_bytes = 16;
    c.ecc.main_strength_bits = 8;
    c.ecc.oob_strength_bits = 4;
    c.ecc.max_retry_step = 3;
    c.timing.version = FWLAB_NFC_CONTRACT_VERSION;
    c.timing.size = sizeof(c.timing);
    c.timing.command_ticks = 1;
    c.timing.transfer_ticks_per_unit = 1;
    c.timing.read_array_ticks = 8;
    c.timing.program_setup_ticks = 2;
    c.timing.program_ticks_per_unit = 4;
    c.timing.program_status_ticks = 1;
    c.timing.erase_setup_ticks = 2;
    c.timing.erase_ticks_per_page = 2;
    c.timing.erase_status_ticks = 1;
    c.timing.status_ticks = 1;
    c.fault.version = FWLAB_NFC_FAULT_PROFILE_VERSION;
    c.fault.size = sizeof(c.fault);
    c.fault.profile_version = 1;
    c.fault.seed = UINT64_C(0x5152535455565758);
    c.capacity.version = FWLAB_NFC_CONTRACT_VERSION;
    c.capacity.size = sizeof(c.capacity);
    c.capacity.operations = 4;
    c.capacity.request_registry = 4;
    c.capacity.terminal_events = 4;
    c.capacity.result_slots = 4;
    c.capacity.trace_entries = 1024;
    c.capacity.scratch_main_bytes = 4096;
    c.capacity.scratch_oob_bytes = 128;
    c.capacity.operation_generation_limit = UINT32_MAX;
    c.capacity.cache_generation_limit = UINT32_MAX;
    c.capacity.controller_epoch_limit = UINT32_MAX;
    c.capacity.submit_sequence_limit = UINT32_MAX;
    c.capacity.operation_uid_limit = UINT64_MAX;
    c.capacity.virtual_tick_limit = UINT64_C(1000000000);
    c.successful_erase_limit = 4096;
    return c;
}

static void construct_model(struct fixture *f)
{
    struct fwlab_nfc_buffer_provider buffers = {&buffer_ops, &f->staging};
    enum fwlab_nfc_api_result result;
    f->physical = fwlab_file_nand_v1_media(f->media);
    f->model_bytes = f->original ? fwlab_nfc_model_arena_size(&f->config) :
                                   fwlab_nfc_scaled_arena_size(&f->config);
    CHECK(f->model_bytes != 0);
    f->model_arena = calloc(1, f->model_bytes);
    CHECK(f->model_arena != NULL);
    result = f->original ? fwlab_nfc_model_init(
        f->model_arena, f->model_bytes, &f->config, f->nonce, &buffers,
        &f->physical, &f->model) : fwlab_nfc_scaled_init(
        f->model_arena, f->model_bytes, &f->config, f->nonce, &buffers,
        &f->physical, &f->model);
    CHECK(result == FWLAB_NFC_API_OK);
    f->provider = fwlab_nfc_model_provider(f->model);
    CHECK(f->provider.ops != NULL && f->provider.context == f->model);
    f->next_uid = 1;
}

static struct fixture *fixture_new(unsigned profile, int original)
{
    struct fixture *f = calloc(1, sizeof(*f));
    CHECK(f != NULL);
    f->config = config_for(geometry(profile));
    f->media_config.geometry = f->config.geometry;
    memcpy(f->media_config.media_uuid, "scaled-nfc-test1", 16);
    f->original = original;
    f->nonce = UINT64_C(0x123456789abcdef0);
    f->completion_hash = UINT64_C(1469598103934665603);
    memcpy(f->directory, "/tmp/fwlab-nfc-geometry.XXXXXX", 31);
    CHECK(mkdtemp(f->directory) != NULL);
    f->directory_fd = open(f->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(f->directory_fd >= 0);
    f->media_arena = calloc(1, fwlab_file_nand_v1_arena_size());
    CHECK(f->media_arena != NULL);
    CHECK(fwlab_file_nand_v1_posix_format(
        f->media_arena, fwlab_file_nand_v1_arena_size(), f->directory_fd,
        "nand.bin", &f->media_config, &f->media, &f->holder) == FWLAB_NFC_API_OK);
    construct_model(f);
    return f;
}

static void reopen(struct fixture *f)
{
    CHECK(fwlab_file_nand_v1_close(f->media) == FWLAB_NFC_API_OK);
    free(f->model_arena);
    f->model_arena = NULL;
    f->model = NULL;
    CHECK(fwlab_file_nand_v1_posix_restart(
        f->media_arena, fwlab_file_nand_v1_arena_size(), f->directory_fd,
        "nand.bin", &f->media_config, &f->holder, &f->media) == FWLAB_NFC_API_OK);
    f->nonce += 17;
    construct_model(f);
}

static void fixture_free(struct fixture *f)
{
    CHECK(fwlab_file_nand_v1_close(f->media) == FWLAB_NFC_API_OK);
    CHECK(close(f->directory_fd) == 0);
    free(f->model_arena);
    free(f->media_arena);
    free(f);
}

static struct fwlab_nfc_request request(
    struct fixture *f, uint8_t kind, struct fwlab_nfc_ppa ppa)
{
    struct fwlab_nfc_request r;
    memset(&r, 0, sizeof(r));
    r.version = FWLAB_NFC_CONTRACT_VERSION;
    r.size = sizeof(r);
    r.operation.instance_nonce = f->nonce;
    r.operation.operation_uid = f->next_uid++;
    r.operation.controller_epoch = 1;
    r.operation.generation = 1;
    r.kind = kind;
    r.ppa = ppa;
    r.cookie = r.operation.operation_uid;
    if (kind != FWLAB_NFC_ERASE && kind != FWLAB_NFC_STATUS)
        r.region_mask = FWLAB_NFC_REGION_MASK;
    if (kind == FWLAB_NFC_PROGRAM_TRANSFER || kind == FWLAB_NFC_READ_TRANSFER) {
        r.main = (struct fwlab_nfc_buffer_ref){1, 0, 4096, 0};
        r.oob = (struct fwlab_nfc_buffer_ref){2, 0, 128, 0};
    }
    return r;
}

static struct fwlab_nfc_completion execute(
    struct fixture *f, const struct fwlab_nfc_request *r)
{
    struct fwlab_nfc_completion c;
    struct fwlab_nfc_step_result step;
    uint32_t count = 0, retired;
    unsigned attempt;
    size_t i;
    CHECK(f->provider.ops->try_submit(f->provider.context, r).disposition ==
          FWLAB_NFC_ACCEPTED);
    memset(&c, 0, sizeof(c));
    for (attempt = 0; attempt < 4096 && count == 0; ++attempt) {
        CHECK(f->provider.ops->step(f->provider.context, 1, &step) == FWLAB_NFC_API_OK);
        CHECK(f->provider.ops->poll(f->provider.context, 1, &c, 1, &count) == FWLAB_NFC_API_OK);
    }
    CHECK(count == 1 && c.terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(memcmp(&c.operation, &r->operation, sizeof(c.operation)) == 0);
    CHECK(memcmp(&c.ppa, &r->ppa, sizeof(c.ppa)) == 0);
    CHECK(c.operation_kind == r->kind);
    for (i = 0; i < sizeof(c); ++i) {
        f->completion_hash ^= ((const uint8_t *)&c)[i];
        f->completion_hash *= UINT64_C(1099511628211);
    }
    if (fwlab_nfc_model_trace_count(f->model) > 512) {
        CHECK(fwlab_nfc_trace_window_retire(f->model, &retired) == FWLAB_NFC_API_OK);
        CHECK(retired != 0);
        ++f->retirements;
    }
    return c;
}

static uint8_t pattern(size_t i, uint8_t seed)
{
    return (uint8_t)((i * 29u + seed) & 255u);
}

static void program_page(struct fixture *f, struct fwlab_nfc_ppa ppa, uint8_t seed)
{
    struct fwlab_nfc_request r;
    struct fwlab_nfc_completion c;
    size_t i;
    for (i = 0; i < sizeof(f->staging.main); ++i)
        f->staging.main[i] = pattern(i, seed);
    for (i = 0; i < sizeof(f->staging.oob); ++i)
        f->staging.oob[i] = pattern(i, (uint8_t)(seed ^ 0xa5u));
    r = request(f, FWLAB_NFC_PROGRAM_TRANSFER, ppa);
    c = execute(f, &r);
    r = request(f, FWLAB_NFC_PROGRAM_EXECUTE, ppa);
    r.cache = c.cache;
    c = execute(f, &r);
    CHECK(c.physical_outcome == FWLAB_NFC_PHYS_APPLIED);
    CHECK(c.integrity == FWLAB_NFC_INTEGRITY_COMPLETE);
}

static void read_page(struct fixture *f, struct fwlab_nfc_ppa ppa,
                      uint8_t seed, int erased)
{
    struct fwlab_nfc_request r = request(f, FWLAB_NFC_READ_TRIGGER, ppa);
    struct fwlab_nfc_completion c = execute(f, &r);
    size_t i;
    CHECK(c.ecc_status == FWLAB_NFC_ECC_CLEAN);
    memset(f->staging.main, 0xa5, sizeof(f->staging.main));
    memset(f->staging.oob, 0x5a, sizeof(f->staging.oob));
    r = request(f, FWLAB_NFC_READ_TRANSFER, ppa);
    r.cache = c.cache;
    (void)execute(f, &r);
    for (i = 0; i < sizeof(f->staging.main); ++i)
        CHECK(f->staging.main[i] == (erased ? 0xff : pattern(i, seed)));
    for (i = 0; i < sizeof(f->staging.oob); ++i)
        CHECK(f->staging.oob[i] == (erased ? 0xff : pattern(i, (uint8_t)(seed ^ 0xa5u))));
}

static void erase_block(struct fixture *f, struct fwlab_nfc_ppa ppa)
{
    struct fwlab_nfc_request r;
    struct fwlab_nfc_completion c;
    ppa.page = 0;
    r = request(f, FWLAB_NFC_ERASE, ppa);
    c = execute(f, &r);
    CHECK(c.physical_outcome == FWLAB_NFC_PHYS_APPLIED);
    CHECK(c.integrity == FWLAB_NFC_INTEGRITY_COMPLETE);
    CHECK(c.final_erase_generation == c.base_erase_generation + 1u);
}

static void validation_cases(void)
{
    struct fwlab_nfc_model_config c = config_for(geometry(0)), invalid;
    CHECK(fwlab_nfc_scaled_config_validate(&c) == FWLAB_NFC_API_OK);
    CHECK(fwlab_nfc_model_arena_size(&c) == fwlab_nfc_scaled_arena_size(&c));
    invalid = c; invalid.geometry.pages_per_block = 256;
    CHECK(fwlab_nfc_scaled_arena_size(&invalid) == 0);
    invalid = c; invalid.geometry.channels = 5;
    CHECK(fwlab_nfc_scaled_arena_size(&invalid) == 0);
    invalid = c; invalid.geometry.main_bytes_per_page = 8192;
    CHECK(fwlab_nfc_scaled_arena_size(&invalid) == 0);
    invalid = c; invalid.ecc.main_step_bytes = 0;
    CHECK(fwlab_nfc_scaled_arena_size(&invalid) == 0);
    invalid = c; invalid.capacity.operations = 33;
    CHECK(fwlab_nfc_scaled_arena_size(&invalid) == 0);
    invalid = c; invalid.capacity.virtual_tick_limit = 66;
    CHECK(fwlab_nfc_scaled_arena_size(&invalid) == 0);
    invalid = c; invalid.timing.command_ticks = 0;
    CHECK(fwlab_nfc_scaled_arena_size(&invalid) == 0);
    invalid = c; invalid.fault.profile_version = 0;
    CHECK(fwlab_nfc_scaled_arena_size(&invalid) == 0);
    invalid = c; invalid.capacity.reserved1[1] = 1;
    CHECK(fwlab_nfc_scaled_arena_size(&invalid) == 0);
    invalid = c; invalid.version = 2;
    CHECK(fwlab_nfc_scaled_config_validate(&invalid) == FWLAB_NFC_API_UNSUPPORTED_VERSION);
    invalid = c; invalid.geometry.blocks_per_plane = UINT16_MAX;
    CHECK(fwlab_nfc_scaled_config_validate(&invalid) == FWLAB_NFC_API_OK);
    CHECK(fwlab_nfc_scaled_arena_size(&invalid) == fwlab_nfc_scaled_arena_size(&c));
    puts("NFC validation PASS: unchanged reference limits, checked scaled construction");
}

static void tiny_equivalence(void)
{
    struct fixture *old = fixture_new(0, 1), *scaled = fixture_new(0, 0);
    struct fwlab_nfc_ppa ppa = {0, 0, 0, 1, 0, 0};
    struct fwlab_nfc_buffer_provider buffers = {&buffer_ops, &scaled->staging};
    struct fwlab_nfc_model *out = scaled->model;
    uint64_t state = fwlab_nfc_model_state_hash(scaled->model);
    CHECK(old->provider.ops == scaled->provider.ops);
    CHECK(fwlab_nfc_scaled_init(scaled->model_arena, scaled->model_bytes - 1,
        &scaled->config, scaled->nonce, &buffers, &scaled->physical, &out) != FWLAB_NFC_API_OK);
    CHECK(out == NULL && fwlab_nfc_model_state_hash(scaled->model) == state);
    program_page(old, ppa, 17); program_page(scaled, ppa, 17);
    read_page(old, ppa, 17, 0); read_page(scaled, ppa, 17, 0);
    erase_block(old, ppa); erase_block(scaled, ppa);
    program_page(old, ppa, 29); program_page(scaled, ppa, 29);
    read_page(old, ppa, 29, 0); read_page(scaled, ppa, 29, 0);
    CHECK(old->completion_hash == scaled->completion_hash);
    CHECK(fwlab_nfc_model_state_hash(old->model) == fwlab_nfc_model_state_hash(scaled->model));
    CHECK(fwlab_nfc_model_media_hash(old->model) == fwlab_nfc_model_media_hash(scaled->model));
    CHECK(old->staging.reads == scaled->staging.reads && old->staging.writes == scaled->staging.writes);
    printf("NFC tiny equivalence PASS: shared ops, completion/state/media hashes; arena=%zu\n", old->model_bytes);
    fixture_free(old);
    fixture_free(scaled);
}

static struct fwlab_nfc_ppa last_address(const struct fwlab_nfc_geometry *g)
{
    struct fwlab_nfc_ppa ppa;
    memset(&ppa, 0, sizeof(ppa));
    ppa.channel = (uint16_t)(g->channels - 1u);
    ppa.lun = (uint16_t)(g->luns_per_channel - 1u);
    ppa.plane = (uint16_t)(g->planes_per_lun - 1u);
    ppa.block = (uint16_t)(g->blocks_per_plane - 1u);
    ppa.page = (uint16_t)(g->pages_per_block - 1u);
    return ppa;
}

static void reject_boundaries(struct fixture *f)
{
    const struct fwlab_nfc_geometry *g = &f->config.geometry;
    struct fwlab_nfc_ppa invalid[5] = {
        {g->channels, 0, 0, 0, 0, 0}, {0, g->luns_per_channel, 0, 0, 0, 0},
        {0, 0, g->planes_per_lun, 0, 0, 0}, {0, 0, 0, g->blocks_per_plane, 0, 0},
        {0, 0, 0, 0, g->pages_per_block, 0}
    };
    uint64_t sequence = fwlab_file_nand_v1_sequence(f->media);
    uint64_t state = fwlab_nfc_model_state_hash(f->model);
    size_t i;
    for (i = 0; i < 5; ++i) {
        struct fwlab_nfc_request r = request(f, FWLAB_NFC_READ_TRIGGER, invalid[i]);
        struct fwlab_nfc_submit_result result = f->provider.ops->try_submit(f->provider.context, &r);
        CHECK(result.disposition == FWLAB_NFC_REJECTED);
        CHECK(result.reason == FWLAB_NFC_REASON_RANGE);
    }
    CHECK(fwlab_file_nand_v1_sequence(f->media) == sequence);
    CHECK(fwlab_nfc_model_state_hash(f->model) == state);
}

static void check_partial_state(struct fixture *f, struct fwlab_nfc_ppa ppa)
{
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
    uint8_t main[4096], oob[128];
    size_t i;
    ppa.page = 2;
    CHECK(f->physical.ops->read_page(f->physical.context, &ppa, main, sizeof(main),
        oob, sizeof(oob), &page, &block) == FWLAB_NFC_API_OK);
    CHECK(page.state == FWLAB_NAND_PAGE_TORN && !page.program_count);
    CHECK(block.erase_state == FWLAB_NAND_ERASE_TORN && block.erase_generation == 0);
    CHECK(block.erase_attempt_count == 1 && block.successful_erase_count == 0);
    for (i = 0; i < sizeof(main); ++i) CHECK(main[i] == 0xff);
    for (i = 0; i < sizeof(oob); ++i) CHECK(oob[i] == 0xff);
    ppa.page = 3;
    CHECK(f->physical.ops->read_page(f->physical.context, &ppa, main, sizeof(main),
        oob, sizeof(oob), &page, &block) == FWLAB_NFC_API_OK);
    CHECK(page.state == FWLAB_NAND_PAGE_VALID && page.program_count == 1);
    for (i = 0; i < sizeof(main); ++i) CHECK(main[i] == pattern(i, 43));
    for (i = 0; i < sizeof(oob); ++i) CHECK(oob[i] == pattern(i, 43 ^ 0xa5));
}

static void partial_erase_cut(struct fixture *f, struct fwlab_nfc_ppa ppa)
{
    struct fwlab_nfc_request r;
    struct fwlab_nfc_step_result step;
    struct fwlab_nfc_trace_entry trace;
    uint32_t ordinal = fwlab_nfc_model_trace_count(f->model);
    unsigned attempt;
    int found = 0;
    ppa.page = 0;
    r = request(f, FWLAB_NFC_ERASE, ppa);
    CHECK(f->provider.ops->try_submit(f->provider.context, &r).disposition == FWLAB_NFC_ACCEPTED);
    for (attempt = 0; attempt < 256 && !found; ++attempt) {
        CHECK(f->provider.ops->step(f->provider.context, 1, &step) == FWLAB_NFC_API_OK);
        while (ordinal < fwlab_nfc_model_trace_count(f->model)) {
            CHECK(fwlab_nfc_model_trace_read(f->model, ordinal++, &trace) == FWLAB_NFC_API_OK);
            if (trace.kind == FWLAB_NFC_TRACE_EFFECT && trace.detail == 3 &&
                trace.operation.operation_uid == r.operation.operation_uid)
                found = 1;
        }
    }
    CHECK(found);
    CHECK(fwlab_nfc_model_inject_cut(f->model, FWLAB_NFC_CUT_SSD_POWER_LOSS) == FWLAB_NFC_API_OK);
    check_partial_state(f, ppa);
    reopen(f);
    check_partial_state(f, ppa);
    puts("NFC high-address power cut PASS: persisted three-page erase prefix and untouched suffix");
}

static void large_journey(unsigned profile)
{
    struct fixture *f = fixture_new(profile, 0);
    struct fwlab_nfc_ppa low = {0, 0, 0, 0, 0, 0};
    struct fwlab_nfc_ppa last = last_address(&f->config.geometry), ppa = last;
    struct stat status;
    uint64_t pages = (uint64_t)f->config.geometry.channels * f->config.geometry.luns_per_channel *
        f->config.geometry.planes_per_lun * f->config.geometry.blocks_per_plane * f->config.geometry.pages_per_block;
    uint16_t page;
    CHECK(fwlab_nfc_model_config_validate(&f->config) != FWLAB_NFC_API_OK);
    CHECK(fwlab_nfc_model_arena_size(&f->config) == 0);
    CHECK(pages == (profile == 1 ? 20480 : 81920));
    CHECK(profile != 1 || last.block == 319);
    CHECK(profile != 2 || (last.channel == 1 && last.lun == 1 && last.plane == 1));
    program_page(f, low, 7);
    for (page = 0; page < 64; ++page) {
        ppa.page = page;
        program_page(f, ppa, (uint8_t)(40u + page));
    }
    read_page(f, last, 103, 0);
    read_page(f, low, 7, 0);
    reject_boundaries(f);
    reopen(f);
    read_page(f, last, 103, 0);
    read_page(f, low, 7, 0);
    if (profile == 2)
        partial_erase_cut(f, last);
    erase_block(f, last);
    read_page(f, last, 0, 1);
    ppa = last; ppa.page = 0;
    program_page(f, ppa, 117);
    read_page(f, ppa, 117, 0);
    read_page(f, low, 7, 0);
    reopen(f);
    read_page(f, ppa, 117, 0);
    read_page(f, low, 7, 0);
    CHECK(f->staging.reads && f->staging.writes && f->retirements);
    CHECK(fstatat(f->directory_fd, "nand.bin", &status, AT_SYMLINK_NOFOLLOW) == 0);
    CHECK((uint64_t)status.st_size == fwlab_file_nand_v1_image_bytes(&f->media_config));
    printf("NFC %uMiB physical-address journey PASS: last-linear=%llu block=%u arena=%zu image-bytes=%llu allocated-bytes=%llu path=%s/nand.bin\n",
        profile == 1 ? 80u : 320u, (unsigned long long)(pages - 1u), last.block,
        f->model_bytes, (unsigned long long)status.st_size,
        (unsigned long long)status.st_blocks * 512u, f->directory);
    fixture_free(f);
}

int main(void)
{
    validation_cases();
    tiny_equivalence();
    large_journey(1);
    large_journey(2);
    puts("NFC scaled construction PASS; physical-address evidence, no large namespace/full-allocation claim");
    return 0;
}
