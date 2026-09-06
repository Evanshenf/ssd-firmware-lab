/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "compact_nand_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

/* A bounded byte substrate, not another NAND implementation. Successful sync
 * persists all preceding bytes. One selected failing write can persist a
 * prefix, and a failed sync can either persist everything or nothing new.
 * Power interruption discards the working copy and every volatile arena. */
struct fixture {
    struct fwlab_file_nand_v1_config config;
    struct fwlab_file_nand_v1 arena;
    struct fwlab_file_nand_v1 *media;
    struct fwlab_nand_media port;
    struct fnv1_io io;
    uint8_t *working;
    uint8_t *durable;
    size_t capacity;
    size_t length;
    size_t durable_length;
    uint64_t writes;
    uint64_t syncs;
    uint64_t bank_writes[2];
    uint64_t fail_offset;
    uint64_t fail_sync;
    size_t prefix;
    unsigned skip;
    int write_armed;
    int sync_persists;
    int fired;
    int closed;
};

struct observed {
    uint8_t main[4096];
    uint8_t oob[128];
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
};

static int in_range(const struct fixture *f, uint64_t offset, size_t size)
{
    return !f->closed && offset <= f->length && size <= f->length - offset;
}

static enum fwlab_nfc_api_result bytes_read(
    void *opaque, uint64_t offset, void *buffer, size_t size)
{
    struct fixture *f = opaque;
    if (buffer == NULL || !in_range(f, offset, size))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    memcpy(buffer, f->working + (size_t)offset, size);
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result bytes_write(
    void *opaque, uint64_t offset, const void *buffer, size_t size)
{
    struct fixture *f = opaque;
    size_t prefix;
    if (buffer == NULL || !in_range(f, offset, size))
        return FWLAB_NFC_API_INVALID_CONTRACT;
    ++f->writes;
    if (offset == FNV1_BANK_BASE)
        ++f->bank_writes[0];
    if (offset == FNV1_BANK_BASE + FNV1_BANK_BYTES)
        ++f->bank_writes[1];
    if (f->write_armed && offset == f->fail_offset) {
        if (f->skip != 0) {
            --f->skip;
        } else {
            CHECK(f->prefix <= size);
            prefix = f->prefix;
            memcpy(f->working + (size_t)offset, buffer, prefix);
            memcpy(f->durable + (size_t)offset, buffer, prefix);
            f->write_armed = 0;
            f->fired = 1;
            return FWLAB_NFC_API_NO_CAPACITY;
        }
    }
    memcpy(f->working + (size_t)offset, buffer, size);
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result bytes_sync(void *opaque)
{
    struct fixture *f = opaque;
    int failure;
    if (f->closed)
        return FWLAB_NFC_API_WRONG_STATE;
    ++f->syncs;
    failure = f->fail_sync != 0 && f->syncs == f->fail_sync;
    if (!failure || f->sync_persists) {
        memcpy(f->durable, f->working, f->length);
        f->durable_length = f->length;
    }
    if (failure) {
        f->fail_sync = 0;
        f->fired = 1;
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result bytes_resize(void *opaque, uint64_t size)
{
    struct fixture *f = opaque;
    if (f->closed || size > f->capacity)
        return FWLAB_NFC_API_NO_CAPACITY;
    if (size > f->length)
        memset(f->working + f->length, 0, (size_t)size - f->length);
    f->length = (size_t)size;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result bytes_size(void *opaque, uint64_t *size)
{
    struct fixture *f = opaque;
    if (f->closed || size == NULL)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    *size = f->length;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result bytes_close(void *opaque)
{
    struct fixture *f = opaque;
    if (f->closed)
        return FWLAB_NFC_API_WRONG_STATE;
    f->closed = 1;
    return FWLAB_NFC_API_OK;
}

static struct fwlab_file_nand_v1_config config(void)
{
    struct fwlab_file_nand_v1_config c;
    memset(&c, 0, sizeof(c));
    c.geometry.version = FWLAB_NFC_CONTRACT_VERSION;
    c.geometry.size = (uint16_t)sizeof(c.geometry);
    c.geometry.channels = 1;
    c.geometry.luns_per_channel = 1;
    c.geometry.planes_per_lun = 1;
    c.geometry.blocks_per_plane = 16;
    c.geometry.pages_per_block = 32;
    c.geometry.plane_parallelism_per_lun = 1;
    c.geometry.main_bytes_per_page = 4096;
    c.geometry.oob_bytes_per_page = 128;
    c.geometry.max_programs_per_erase = 1;
    c.geometry.program_order = FWLAB_NFC_PROGRAM_ASCENDING;
    memcpy(c.media_uuid, "compact-cut-test", 16);
    return c;
}

static struct fixture *fixture_new(void)
{
    struct fixture *f = calloc(1, sizeof(*f));
    CHECK(f != NULL);
    f->config = config();
    f->capacity = (size_t)fwlab_file_nand_v1_image_bytes(&f->config);
    CHECK(f->capacity == 2314240);
    CHECK(fwlab_file_nand_v1_arena_size() < 32768);
    f->working = calloc(1, f->capacity);
    f->durable = calloc(1, f->capacity);
    CHECK(f->working != NULL && f->durable != NULL);
    f->io.context = f;
    f->io.read = bytes_read;
    f->io.write = bytes_write;
    f->io.sync = bytes_sync;
    f->io.resize = bytes_resize;
    f->io.size = bytes_size;
    f->io.close = bytes_close;
    CHECK(fnv1_engine_open(&f->arena, sizeof(f->arena), &f->config,
                           &f->io, 1, &f->media) == FWLAB_NFC_API_OK);
    f->port = fwlab_file_nand_v1_media(f->media);
    CHECK(f->port.ops != NULL && f->port.context == f->media);
    return f;
}

static void fixture_free(struct fixture *f)
{
    free(f->working);
    free(f->durable);
    free(f);
}

static void crash(struct fixture *f)
{
    memcpy(f->working, f->durable, f->capacity);
    f->length = f->durable_length;
    memset(&f->arena, 0, sizeof(f->arena));
    f->media = NULL;
    f->write_armed = 0;
    f->fail_sync = 0;
    f->fired = 0;
    f->closed = 0;
}

static enum fwlab_nfc_api_result restart(struct fixture *f)
{
    enum fwlab_nfc_api_result result = fnv1_engine_open(
        &f->arena, sizeof(f->arena), &f->config, &f->io, 0, &f->media);
    if (result == FWLAB_NFC_API_OK) {
        f->port = fwlab_file_nand_v1_media(f->media);
        CHECK(f->port.ops != NULL);
    } else {
        CHECK(f->media == NULL);
    }
    return result;
}

static void reboot(struct fixture *f)
{
    crash(f);
    CHECK(restart(f) == FWLAB_NFC_API_OK);
}

static struct fwlab_nfc_ppa address(uint16_t block, uint16_t page)
{
    struct fwlab_nfc_ppa ppa;
    memset(&ppa, 0, sizeof(ppa));
    ppa.block = block;
    ppa.page = page;
    return ppa;
}

static uint8_t pattern(size_t index, uint8_t seed)
{
    return (uint8_t)((index * 37u + seed) & 255u);
}

static enum fwlab_nfc_api_result program(
    struct fixture *f, uint16_t block, uint16_t page, uint8_t seed,
    uint32_t main_prefix, uint32_t oob_prefix, uint8_t integrity,
    struct fwlab_nand_media_result *result)
{
    uint8_t main[4096], oob[128];
    struct fwlab_nfc_ppa ppa = address(block, page);
    size_t i;
    for (i = 0; i < sizeof(main); ++i)
        main[i] = pattern(i, seed);
    for (i = 0; i < sizeof(oob); ++i)
        oob[i] = pattern(i, (uint8_t)(seed ^ 0xa5u));
    return f->port.ops->program(f->port.context, &ppa, main, sizeof(main),
        oob, sizeof(oob), main_prefix, oob_prefix, integrity, result);
}

static void program_ok(struct fixture *f, uint16_t page, uint8_t seed)
{
    struct fwlab_nand_media_result result;
    CHECK(program(f, 0, page, seed, 4096, 128,
                  FWLAB_NFC_INTEGRITY_COMPLETE, &result) == FWLAB_NFC_API_OK);
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_APPLIED);
    CHECK(result.integrity == FWLAB_NFC_INTEGRITY_COMPLETE);
    CHECK(result.applied_main_bytes == 4096 && result.applied_oob_bytes == 128);
}

static enum fwlab_nfc_api_result erase(
    struct fixture *f, uint32_t pages, uint8_t integrity,
    struct fwlab_nand_media_result *result)
{
    struct fwlab_nfc_ppa ppa = address(0, 0);
    return f->port.ops->erase(f->port.context, &ppa, pages, integrity, result);
}

static void read_at(struct fixture *f, uint16_t block, uint16_t page,
                    struct observed *seen)
{
    struct fwlab_nfc_ppa ppa = address(block, page);
    memset(seen, 0, sizeof(*seen));
    CHECK(f->port.ops->read_page(f->port.context, &ppa, seen->main,
        sizeof(seen->main), seen->oob, sizeof(seen->oob), &seen->page,
        &seen->block) == FWLAB_NFC_API_OK);
    CHECK(seen->page.version == FWLAB_NFC_CONTRACT_VERSION);
    CHECK(seen->page.size == sizeof(seen->page));
    CHECK(seen->block.version == FWLAB_NFC_CONTRACT_VERSION);
    CHECK(seen->block.size == sizeof(seen->block));
}

static void check_bytes(const struct observed *seen, uint8_t seed,
                        size_t main_prefix, size_t oob_prefix)
{
    size_t i;
    for (i = 0; i < sizeof(seen->main); ++i)
        CHECK(seen->main[i] == (i < main_prefix ? pattern(i, seed) : 0xff));
    for (i = 0; i < sizeof(seen->oob); ++i)
        CHECK(seen->oob[i] == (i < oob_prefix ?
              pattern(i, (uint8_t)(seed ^ 0xa5u)) : 0xff));
}

static void expect_value(struct fixture *f, uint16_t page, uint8_t seed)
{
    struct observed seen;
    read_at(f, 0, page, &seen);
    check_bytes(&seen, seed, 4096, 128);
    CHECK(seen.page.state == FWLAB_NAND_PAGE_VALID);
    CHECK(seen.page.program_count == 1);
}

static void expect_erased(struct fixture *f, uint16_t page)
{
    struct observed seen;
    read_at(f, 0, page, &seen);
    check_bytes(&seen, 0, 0, 0);
    CHECK(seen.page.state == FWLAB_NAND_PAGE_ERASED);
    CHECK(seen.page.program_count == 0);
}

static void arm_write(struct fixture *f, uint64_t offset, unsigned skip,
                      size_t prefix)
{
    CHECK(!f->write_armed && !f->fail_sync);
    f->write_armed = 1;
    f->fail_offset = offset;
    f->skip = skip;
    f->prefix = prefix;
    f->fired = 0;
}

static void arm_sync(struct fixture *f, uint64_t relative, int persists)
{
    CHECK(!f->write_armed && !f->fail_sync);
    f->fail_sync = f->syncs + relative;
    f->sync_persists = persists;
    f->fired = 0;
}

static uint64_t next_bank(const struct fixture *f)
{
    return FNV1_BANK_BASE +
        ((fwlab_file_nand_v1_sequence(&f->arena) + 1u) & 1u) * FNV1_BANK_BYTES;
}

static void expect_closed_admission(struct fixture *f)
{
    struct fwlab_nand_media_result result;
    struct fwlab_nfc_ppa ppa = address(0, 0);
    uint64_t writes = f->writes, syncs = f->syncs;
    CHECK(f->fired);
    CHECK(fwlab_file_nand_v1_media(&f->arena).ops == NULL);
    CHECK(program(f, 0, 0, 91, 4096, 128, FWLAB_NFC_INTEGRITY_COMPLETE,
                  &result) != FWLAB_NFC_API_OK);
    CHECK(erase(f, 32, FWLAB_NFC_INTEGRITY_COMPLETE, &result) !=
          FWLAB_NFC_API_OK);
    CHECK(f->port.ops->mark_runtime_bad(f->port.context, &ppa) !=
          FWLAB_NFC_API_OK);
    CHECK(f->writes == writes && f->syncs == syncs);
}

static void semantics(void)
{
    struct fixture *f = fixture_new();
    struct fwlab_nand_media_result result;
    struct observed seen;
    struct fwlab_nfc_ppa bad = address(1, 0);
    uint64_t sequence, writes;
    expect_erased(f, 0);
    writes = f->writes;
    CHECK(program(f, 0, 1, 1, 4096, 128, FWLAB_NFC_INTEGRITY_COMPLETE,
                  &result) == FWLAB_NFC_API_OK);
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT);
    CHECK(result.reason == FWLAB_NFC_REASON_PROGRAM_ORDER);
    CHECK(f->writes == writes);
    CHECK(program(f, 0, 0, 19, 137, 19, FWLAB_NFC_INTEGRITY_TORN,
                  &result) == FWLAB_NFC_API_OK);
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_APPLIED);
    CHECK(result.applied_main_bytes == 137 && result.applied_oob_bytes == 19);
    CHECK(result.integrity == FWLAB_NFC_INTEGRITY_TORN);
    read_at(f, 0, 0, &seen);
    check_bytes(&seen, 19, 137, 19);
    CHECK(seen.page.state == FWLAB_NAND_PAGE_TORN);
    CHECK(seen.page.program_count == 1 && seen.block.next_program_page == 1);
    CHECK(program(f, 0, 0, 20, 4096, 128, FWLAB_NFC_INTEGRITY_COMPLETE,
                  &result) == FWLAB_NFC_API_OK);
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT);
    CHECK(result.reason == FWLAB_NFC_REASON_NOT_ERASED);
    program_ok(f, 1, 31);
    program_ok(f, 2, 47);
    sequence = fwlab_file_nand_v1_sequence(f->media);
    CHECK(program(f, 0, 3, 51, 0, 0, FWLAB_NFC_INTEGRITY_TORN,
                  &result) == FWLAB_NFC_API_OK);
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT);
    CHECK(erase(f, 0, FWLAB_NFC_INTEGRITY_TORN, &result) == FWLAB_NFC_API_OK);
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT);
    CHECK(fwlab_file_nand_v1_sequence(f->media) == sequence);
    CHECK(erase(f, 1, FWLAB_NFC_INTEGRITY_TORN, &result) == FWLAB_NFC_API_OK);
    CHECK(erase(f, 2, FWLAB_NFC_INTEGRITY_TORN, &result) == FWLAB_NFC_API_OK);
    CHECK(erase(f, 1, FWLAB_NFC_INTEGRITY_TORN, &result) == FWLAB_NFC_API_OK);
    reboot(f);
    read_at(f, 0, 1, &seen);
    check_bytes(&seen, 0, 0, 0);
    CHECK(seen.page.state == FWLAB_NAND_PAGE_TORN && !seen.page.program_count);
    CHECK(seen.block.erase_state == FWLAB_NAND_ERASE_TORN);
    CHECK(seen.block.erase_generation == 0);
    CHECK(seen.block.erase_attempt_count == 3);
    CHECK(seen.block.successful_erase_count == 0);
    CHECK(seen.block.next_program_page == 3);
    expect_value(f, 2, 47);
    CHECK(program(f, 0, 3, 55, 4096, 128, FWLAB_NFC_INTEGRITY_COMPLETE,
                  &result) == FWLAB_NFC_API_OK);
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT);
    CHECK(result.reason == FWLAB_NFC_REASON_NOT_ERASED);
    CHECK(erase(f, 32, FWLAB_NFC_INTEGRITY_COMPLETE, &result) == FWLAB_NFC_API_OK);
    CHECK(result.base_erase_generation == 0 && result.final_erase_generation == 1);
    CHECK(result.applied_pages == 32);
    expect_erased(f, 0);
    expect_erased(f, 1);
    expect_erased(f, 2);
    program_ok(f, 0, 61);
    CHECK(f->port.ops->mark_runtime_bad(f->port.context, &bad) == FWLAB_NFC_API_OK);
    sequence = fwlab_file_nand_v1_sequence(f->media);
    CHECK(f->port.ops->mark_runtime_bad(f->port.context, &bad) == FWLAB_NFC_API_OK);
    CHECK(fwlab_file_nand_v1_sequence(f->media) == sequence);
    CHECK(program(f, 1, 0, 62, 4096, 128, FWLAB_NFC_INTEGRITY_COMPLETE,
                  &result) == FWLAB_NFC_API_OK);
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT);
    CHECK(result.reason == FWLAB_NFC_REASON_BAD_BLOCK);
    reboot(f);
    expect_value(f, 0, 61);
    read_at(f, 0, 0, &seen);
    CHECK(seen.block.erase_generation == 1 && seen.page.erase_generation_seen == 1);
    CHECK(seen.block.erase_attempt_count == 4);
    CHECK(seen.block.successful_erase_count == 1);
    CHECK(seen.block.erase_state == FWLAB_NAND_ERASE_CLEAN);
    CHECK(seen.block.next_program_page == 1);
    read_at(f, 1, 0, &seen);
    CHECK(seen.block.health == FWLAB_NFC_BLOCK_RUNTIME_BAD);
    CHECK(seen.block.erase_attempt_count == 0);
    CHECK(f->bank_writes[0] >= 3 && f->bank_writes[1] >= 3);
    CHECK(fwlab_file_nand_v1_close(f->media) == FWLAB_NFC_API_OK);
    CHECK(fwlab_file_nand_v1_close(f->media) == FWLAB_NFC_API_WRONG_STATE);
    puts("MEDIA semantics PASS: program/order/torn/erase/bad/neighbor/reuse");
    fixture_free(f);
}

static void cut_unsealed(void)
{
    unsigned variant;
    for (variant = 0; variant < 3; ++variant) {
        struct fixture *f = fixture_new();
        struct fwlab_nand_media_result result;
        program_ok(f, 0, 71);
        if (variant == 0)
            arm_write(f, next_bank(f) + 2u * FNV1_SECTOR, 0, 127);
        else if (variant == 1)
            arm_write(f, next_bank(f) + 4u * FNV1_SECTOR, 1, 61);
        else
            arm_sync(f, 2, 0);
        CHECK(program(f, 0, 1, 72, 4096, 128, FWLAB_NFC_INTEGRITY_COMPLETE,
                      &result) != FWLAB_NFC_API_OK);
        expect_closed_admission(f);
        reboot(f);
        CHECK(fwlab_file_nand_v1_sequence(f->media) == 1);
        expect_value(f, 0, 71);
        expect_erased(f, 1);
        program_ok(f, 1, 73);
        reboot(f);
        expect_value(f, 0, 71);
        expect_value(f, 1, 73);
        fixture_free(f);
    }
    puts("CUT UNSEALED PASS: body-prefix/seal-prefix/failed-body-barrier");
}

static void cut_sealed_no_reply(void)
{
    struct fixture *f = fixture_new();
    struct fwlab_nand_media_result result;
    program_ok(f, 0, 81);
    arm_sync(f, 3, 1);
    CHECK(program(f, 0, 1, 82, 4096, 128, FWLAB_NFC_INTEGRITY_COMPLETE,
                  &result) != FWLAB_NFC_API_OK);
    expect_closed_admission(f);
    reboot(f);
    CHECK(fwlab_file_nand_v1_sequence(f->media) == 2);
    expect_value(f, 0, 81);
    expect_value(f, 1, 82);
    reboot(f);
    expect_value(f, 0, 81);
    expect_value(f, 1, 82);
    puts("CUT SEALED_NO_REPLY PASS: committed successor and neighboring page");
    fixture_free(f);
}

static void cut_torn_erase_install(void)
{
    struct fixture *f = fixture_new();
    struct fwlab_nand_media_result result;
    struct observed seen;
    program_ok(f, 0, 91);
    program_ok(f, 1, 92);
    program_ok(f, 2, 93);
    arm_write(f, f->media->block_metadata_offset, 0, 37);
    CHECK(erase(f, 1, FWLAB_NFC_INTEGRITY_TORN, &result) != FWLAB_NFC_API_OK);
    expect_closed_admission(f);
    reboot(f);
    CHECK(fwlab_file_nand_v1_sequence(f->media) == 4);
    read_at(f, 0, 0, &seen);
    check_bytes(&seen, 0, 0, 0);
    CHECK(seen.page.state == FWLAB_NAND_PAGE_TORN && !seen.page.program_count);
    CHECK(seen.block.erase_state == FWLAB_NAND_ERASE_TORN);
    CHECK(seen.block.erase_generation == 0 && seen.block.erase_attempt_count == 1);
    CHECK(seen.block.successful_erase_count == 0 && seen.block.next_program_page == 3);
    expect_value(f, 1, 92);
    expect_value(f, 2, 93);
    read_at(f, 1, 0, &seen);
    CHECK(seen.block.erase_generation == 0 && seen.block.erase_attempt_count == 0);
    reboot(f);
    read_at(f, 0, 0, &seen);
    CHECK(seen.block.erase_attempt_count == 1);
    puts("CUT TORN_ERASE_INSTALL PASS: exact prefix/suffix and block counters");
    fixture_free(f);
}

static void cut_home_before_applied(void)
{
    struct fixture *f = fixture_new();
    struct fwlab_nand_media_result result;
    struct observed seen;
    uint64_t block_home;
    program_ok(f, 0, 101);
    block_home = f->media->block_metadata_offset;
    /* Sequence two uses the even applied record. Homes have been synced. */
    arm_write(f, 8192, 0, 32);
    CHECK(erase(f, 32, FWLAB_NFC_INTEGRITY_COMPLETE, &result) != FWLAB_NFC_API_OK);
    expect_closed_admission(f);
    crash(f);
    arm_write(f, block_home, 0, 23);
    CHECK(restart(f) != FWLAB_NFC_API_OK);
    expect_closed_admission(f);
    reboot(f);
    CHECK(fwlab_file_nand_v1_sequence(f->media) == 2);
    expect_erased(f, 0);
    read_at(f, 0, 0, &seen);
    CHECK(seen.block.erase_generation == 1);
    CHECK(seen.block.successful_erase_count == 1 && seen.block.erase_attempt_count == 1);
    CHECK(seen.block.next_program_page == 0 && seen.block.erase_state == FWLAB_NAND_ERASE_CLEAN);
    reboot(f);
    read_at(f, 0, 0, &seen);
    CHECK(seen.block.erase_generation == 1 && seen.block.erase_attempt_count == 1);
    program_ok(f, 0, 102);
    expect_value(f, 0, 102);
    puts("CUT HOME_BEFORE_APPLIED PASS: second replay interruption is idempotent");
    fixture_free(f);
}

static void cut_reuse(void)
{
    struct fixture *f = fixture_new();
    struct fwlab_nand_media_result result;
    uint16_t page;
    for (page = 0; page < 5; ++page)
        program_ok(f, page, (uint8_t)(111u + page));
    CHECK(f->bank_writes[0] >= 2 && f->bank_writes[1] >= 3);
    arm_write(f, next_bank(f) + FNV1_SECTOR, 0, 2048);
    CHECK(program(f, 0, 5, 116, 4096, 128, FWLAB_NFC_INTEGRITY_COMPLETE,
                  &result) != FWLAB_NFC_API_OK);
    expect_closed_admission(f);
    reboot(f);
    CHECK(fwlab_file_nand_v1_sequence(f->media) == 5);
    for (page = 0; page < 5; ++page)
        expect_value(f, page, (uint8_t)(111u + page));
    expect_erased(f, 5);
    for (page = 5; page < 8; ++page)
        program_ok(f, page, (uint8_t)(121u + page));
    reboot(f);
    CHECK(fwlab_file_nand_v1_sequence(f->media) == 8);
    for (page = 0; page < 5; ++page)
        expect_value(f, page, (uint8_t)(111u + page));
    for (page = 5; page < 8; ++page)
        expect_value(f, page, (uint8_t)(121u + page));
    puts("CUT REUSE PASS: A retained, half B absent, two banks reused and continued");
    fixture_free(f);
}

static void rejected_formats_and_corruption(void)
{
    struct fixture *f = fixture_new();
    struct fwlab_file_nand_v1_config invalid = f->config;
    struct fwlab_file_nand_v1 *out = NULL;
    struct fwlab_nand_media_result result;
    uint64_t writes = f->writes, bank;
    invalid.geometry.main_bytes_per_page = 8192;
    CHECK(fwlab_file_nand_v1_image_bytes(&invalid) == 0);
    invalid = f->config;
    memset(invalid.media_uuid, 0, 16);
    CHECK(fwlab_file_nand_v1_image_bytes(&invalid) == 0);
    invalid = f->config;
    invalid.geometry.channels = UINT16_MAX;
    invalid.geometry.luns_per_channel = UINT16_MAX;
    invalid.geometry.planes_per_lun = UINT16_MAX;
    CHECK(fwlab_file_nand_v1_image_bytes(&invalid) == 0);
    CHECK(fnv1_engine_open(&f->arena, sizeof(f->arena), &f->config,
                           &f->io, 1, &out) != FWLAB_NFC_API_OK);
    CHECK(fnv1_engine_open(&f->arena, sizeof(f->arena), &f->config,
                           &f->io, 2, &out) != FWLAB_NFC_API_OK);
    CHECK(f->writes == writes);
    --f->length;
    CHECK(restart(f) != FWLAB_NFC_API_OK);
    CHECK(f->writes == writes);
    crash(f);
    invalid = f->config;
    invalid.media_uuid[0] ^= 1;
    CHECK(fnv1_engine_open(&f->arena, sizeof(f->arena), &invalid,
                           &f->io, 0, &out) != FWLAB_NFC_API_OK);
    CHECK(out == NULL && f->writes == writes);
    crash(f);
    invalid = f->config;
    invalid.geometry.blocks_per_plane = 8;
    invalid.geometry.pages_per_block = 64;
    CHECK(fwlab_file_nand_v1_image_bytes(&invalid) == f->capacity);
    CHECK(fnv1_engine_open(&f->arena, sizeof(f->arena), &invalid,
                           &f->io, 0, &out) != FWLAB_NFC_API_OK);
    CHECK(out == NULL && f->writes == writes);
    reboot(f);
    bank = next_bank(f);
    arm_sync(f, 3, 1);
    CHECK(program(f, 0, 0, 141, 4096, 128, FWLAB_NFC_INTEGRITY_COMPLETE,
                  &result) != FWLAB_NFC_API_OK);
    expect_closed_admission(f);
    f->durable[(size_t)(bank + FNV1_SECTOR) + 71u] ^= 0x80;
    writes = f->writes;
    crash(f);
    CHECK(restart(f) != FWLAB_NFC_API_OK);
    CHECK(fwlab_file_nand_v1_media(&f->arena).ops == NULL);
    CHECK(f->writes == writes);
    puts("REJECT PASS: geometry/UUID/format/length and committed-body corruption");
    fixture_free(f);
}

int main(void)
{
    semantics();
    cut_unsealed();
    cut_sealed_no_reply();
    cut_torn_erase_install();
    cut_home_before_applied();
    cut_reuse();
    rejected_formats_and_corruption();
    printf("compact-nand PASS: five named cuts, image=2314240 arena=%zu\n",
           fwlab_file_nand_v1_arena_size());
    return 0;
}
