/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "physical_nand_internal.h"
#include "physical_nand_batch.h"
#include "fwlab/portable/crc32c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "Cprime %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define PAGE_BYTES 4096u
#define OOB_BYTES 128u
#define PAGE_COUNT 1024u
#define BLOCK_COUNT 16u
#define PAGES_PER_BLOCK 64u

/* Same bounded working/durable-byte method as compact-v1 adjacency tests.
 * This is not POSIX, physical host-power-loss or a throughput benchmark. */
struct fixture {
    struct fwlab_file_nand_v2_config config;
    struct fwlab_file_nand_v2 arena;
    struct fwlab_file_nand_v2 *media;
    struct fwlab_nand_media port;
    struct fnv2_io io;
    uint8_t *working, *durable;
    size_t capacity, length, durable_length;
    uint64_t reads, read_bytes, writes, write_bytes, syncs;
    uint64_t bank_writes[2], fail_offset, fail_sync;
    size_t prefix;
    uint8_t write_armed, sync_persists, fired, closed;
};
struct observed {
    uint8_t main[PAGE_BYTES], oob[OOB_BYTES];
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
};

static uint16_t get16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8)); }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t get64(const uint8_t *p)
{ return get32(p) | ((uint64_t)get32(p + 4) << 32); }
static void put32(uint8_t *p, uint32_t value)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8u * i)); }
static int all(const uint8_t *p, size_t n, uint8_t value)
{ for (size_t i = 0; i < n; ++i) if (p[i] != value) return 0; return 1; }
static uint64_t page_meta(uint32_t linear)
{ return FNV2_HOME_BASE + (uint64_t)PAGE_COUNT * PAGE_BYTES + (uint64_t)linear * FNV2_PAGE_RECORD_BYTES; }
static uint64_t block_meta(uint32_t block)
{ return page_meta(PAGE_COUNT) + (uint64_t)block * FNV2_BLOCK_RECORD_BYTES; }
static uint64_t bank(uint64_t sequence)
{ return FNV2_BANK_BASE + (sequence & 1u) * FNV2_BANK_BYTES; }
static int span(const struct fixture *f, uint64_t offset, size_t length)
{ return !f->closed && offset <= f->length && length <= f->length - offset; }
static enum fwlab_nfc_api_result bytes_read(void *p, uint64_t offset, void *out, size_t n)
{
    struct fixture *f = p;
    if (!out || !span(f, offset, n)) return FWLAB_NFC_API_INVALID_CONTRACT;
    ++f->reads; f->read_bytes += n;
    memcpy(out, f->working + (size_t)offset, n); return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result bytes_write(void *p, uint64_t offset, const void *in, size_t n)
{
    struct fixture *f = p; size_t done = n;
    if (!in || !span(f, offset, n)) return FWLAB_NFC_API_INVALID_CONTRACT;
    ++f->writes;
    if (offset == bank(0)) ++f->bank_writes[0];
    if (offset == bank(1)) ++f->bank_writes[1];
    if (f->write_armed && f->fail_offset >= offset && f->fail_offset - offset < n) {
        done = (size_t)(f->fail_offset - offset);
        CHECK(f->prefix <= n - done); done += f->prefix;
        memcpy(f->working + (size_t)offset, in, done);
        memcpy(f->durable + (size_t)offset, in, done);
        f->write_bytes += done; f->write_armed = 0; f->fired = 1;
        return FWLAB_NFC_API_NO_CAPACITY;
    }
    memcpy(f->working + (size_t)offset, in, done); f->write_bytes += done;
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result bytes_sync(void *p)
{
    struct fixture *f = p; int failure;
    if (f->closed) return FWLAB_NFC_API_WRONG_STATE;
    ++f->syncs; failure = f->fail_sync && f->fail_sync == f->syncs;
    if (!failure || f->sync_persists) {
        memcpy(f->durable, f->working, f->length); f->durable_length = f->length;
    }
    if (failure) { f->fail_sync = 0; f->fired = 1; return FWLAB_NFC_API_INVARIANT_FAILURE; }
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result bytes_resize(void *p, uint64_t n)
{
    struct fixture *f = p;
    if (f->closed || n > f->capacity) return FWLAB_NFC_API_NO_CAPACITY;
    if (n > f->length) memset(f->working + f->length, 0, (size_t)n - f->length);
    f->length = (size_t)n; return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result bytes_size(void *p, uint64_t *out)
{
    struct fixture *f = p;
    if (!out || f->closed) return FWLAB_NFC_API_INVALID_CONTRACT;
    *out = f->length; return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result bytes_close(void *p)
{
    struct fixture *f = p;
    if (f->closed) return FWLAB_NFC_API_WRONG_STATE;
    f->closed = 1; return FWLAB_NFC_API_OK;
}
static struct fwlab_file_nand_v2_config config(void)
{
    struct fwlab_file_nand_v2_config c = {0};
    c.geometry.version = FWLAB_NFC_CONTRACT_VERSION; c.geometry.size = sizeof(c.geometry);
    c.geometry.channels = c.geometry.luns_per_channel = c.geometry.planes_per_lun = 1;
    c.geometry.blocks_per_plane = BLOCK_COUNT; c.geometry.pages_per_block = PAGES_PER_BLOCK;
    c.geometry.plane_parallelism_per_lun = 1;
    c.geometry.main_bytes_per_page = PAGE_BYTES; c.geometry.oob_bytes_per_page = OOB_BYTES;
    c.geometry.max_programs_per_erase = 1; c.geometry.program_order = FWLAB_NFC_PROGRAM_ASCENDING;
    memcpy(c.media_uuid, "Cprime-byte-0001", 16); return c;
}
static struct fixture *create(void)
{
    struct fixture *f = calloc(1, sizeof(*f)); CHECK(f);
    f->config = config(); f->capacity = (size_t)fwlab_file_nand_v2_image_bytes(&f->config);
    CHECK(f->capacity == block_meta(BLOCK_COUNT) && f->capacity == 4473856);
    CHECK(fwlab_file_nand_v2_arena_size() == sizeof(f->arena) && sizeof(f->arena) < 32768);
    f->working = calloc(1, f->capacity); f->durable = calloc(1, f->capacity); CHECK(f->working && f->durable);
    f->io = (struct fnv2_io){f, bytes_read, bytes_write, bytes_sync, bytes_resize, bytes_size, bytes_close};
    CHECK(fnv2_engine_open(&f->arena, sizeof(f->arena), &f->config, &f->io, 1, &f->media) == FWLAB_NFC_API_OK);
    f->port = fwlab_file_nand_v2_media(f->media); CHECK(f->port.ops && f->port.context == f->media);
    return f;
}
static void destroy(struct fixture *f)
{ free(f->working); free(f->durable); free(f); }
static void crash(struct fixture *f)
{
    memcpy(f->working, f->durable, f->capacity); f->length = f->durable_length;
    memset(&f->arena, 0, sizeof(f->arena)); f->media = NULL;
    f->write_armed = f->fired = f->closed = 0; f->fail_sync = 0;
}
static enum fwlab_nfc_api_result restart(struct fixture *f)
{
    enum fwlab_nfc_api_result r = fnv2_engine_open(&f->arena, sizeof(f->arena), &f->config, &f->io, 0, &f->media);
    if (r == FWLAB_NFC_API_OK) { f->port = fwlab_file_nand_v2_media(f->media); CHECK(f->port.ops); }
    else CHECK(!f->media);
    return r;
}
static void reboot(struct fixture *f)
{ crash(f); CHECK(restart(f) == FWLAB_NFC_API_OK); }
static void stable_reboot(struct fixture *f, uint64_t sequence)
{
    uint64_t writes = f->writes, syncs = f->syncs;
    reboot(f); CHECK(fwlab_file_nand_v2_sequence(f->media) == sequence);
    CHECK(f->writes == writes && f->syncs == syncs);
}
static void arm_write(struct fixture *f, uint64_t offset, size_t prefix)
{
    CHECK(!f->write_armed && !f->fail_sync);
    f->write_armed = 1; f->fail_offset = offset; f->prefix = prefix; f->fired = 0;
}
static void arm_sync(struct fixture *f, uint32_t relative, int persists)
{
    CHECK(!f->write_armed && !f->fail_sync);
    f->fail_sync = f->syncs + relative; f->sync_persists = (uint8_t)persists; f->fired = 0;
}
static struct fwlab_nfc_ppa address(uint16_t block, uint16_t page)
{ struct fwlab_nfc_ppa ppa = {0}; ppa.block = block; ppa.page = page; return ppa; }
static uint8_t pattern(uint8_t seed, size_t offset)
{ return (uint8_t)(seed + offset * 17u + (offset >> 3)); }
static void payload(uint8_t *main, uint8_t *oob, uint8_t seed)
{
    for (size_t i = 0; i < PAGE_BYTES; ++i) main[i] = pattern(seed, i);
    for (size_t i = 0; i < OOB_BYTES; ++i) oob[i] = pattern((uint8_t)(seed ^ 0xa5), i);
}
static enum fwlab_nfc_api_result program(struct fixture *f, uint16_t block, uint16_t page,
    uint8_t seed, uint32_t main_applied, uint32_t oob_applied, uint8_t integrity,
    struct fwlab_nand_media_result *result)
{
    uint8_t main[PAGE_BYTES], oob[OOB_BYTES]; struct fwlab_nfc_ppa ppa = address(block, page);
    payload(main, oob, seed);
    return f->port.ops->program(f->port.context, &ppa, main, sizeof(main), oob, sizeof(oob),
                                 main_applied, oob_applied, integrity, result);
}
static void program_ok(struct fixture *f, uint16_t block, uint16_t page, uint8_t seed)
{
    struct fwlab_nand_media_result result;
    CHECK(program(f, block, page, seed, PAGE_BYTES, OOB_BYTES, FWLAB_NFC_INTEGRITY_COMPLETE, &result) == FWLAB_NFC_API_OK);
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_APPLIED && result.integrity == FWLAB_NFC_INTEGRITY_COMPLETE);
}
static enum fwlab_nfc_api_result erase(struct fixture *f, uint16_t block, uint32_t prefix,
    uint8_t integrity, struct fwlab_nand_media_result *result)
{
    struct fwlab_nfc_ppa ppa = address(block, 0);
    return f->port.ops->erase(f->port.context, &ppa, prefix, integrity, result);
}
static void observe(struct fixture *f, uint16_t block, uint16_t page, struct observed *out)
{
    struct fwlab_nfc_ppa ppa = address(block, page); memset(out, 0, sizeof(*out));
    CHECK(f->port.ops->read_page(f->port.context, &ppa, out->main, PAGE_BYTES, out->oob, OOB_BYTES,
                                  &out->page, &out->block) == FWLAB_NFC_API_OK);
    CHECK(out->page.version == FWLAB_NFC_CONTRACT_VERSION && out->page.size == sizeof(out->page));
    CHECK(out->block.version == FWLAB_NFC_CONTRACT_VERSION && out->block.size == sizeof(out->block));
}
static void expect_payload(const struct observed *out, uint8_t seed, uint32_t main_n, uint32_t oob_n)
{
    for (size_t i = 0; i < PAGE_BYTES; ++i) CHECK(out->main[i] == (i < main_n ? pattern(seed, i) : 0xff));
    for (size_t i = 0; i < OOB_BYTES; ++i) CHECK(out->oob[i] == (i < oob_n ? pattern((uint8_t)(seed ^ 0xa5), i) : 0xff));
}
static void value(struct fixture *f, uint16_t block, uint16_t page, uint8_t seed)
{
    struct observed out; observe(f, block, page, &out); expect_payload(&out, seed, PAGE_BYTES, OOB_BYTES);
    CHECK(out.page.state == FWLAB_NAND_PAGE_VALID && out.page.program_count == 1);
}
static void erased(struct fixture *f, uint16_t block, uint16_t page)
{
    struct observed out; observe(f, block, page, &out); expect_payload(&out, 0, 0, 0);
    CHECK(out.page.state == FWLAB_NAND_PAGE_ERASED && !out.page.program_count);
}
static void quarantined(struct fixture *f)
{
    struct fwlab_nand_media_result result; struct fwlab_nfc_ppa ppa = address(0, 0);
    uint64_t writes = f->writes, syncs = f->syncs;
    CHECK(f->fired && !fwlab_file_nand_v2_media(&f->arena).ops);
    CHECK(program(f, 0, 0, 99, PAGE_BYTES, OOB_BYTES, FWLAB_NFC_INTEGRITY_COMPLETE, &result) != FWLAB_NFC_API_OK);
    CHECK(erase(f, 0, PAGES_PER_BLOCK, FWLAB_NFC_INTEGRITY_COMPLETE, &result) != FWLAB_NFC_API_OK);
    CHECK(f->port.ops->mark_runtime_bad(f->port.context, &ppa) != FWLAB_NFC_API_OK);
    CHECK(f->writes == writes && f->syncs == syncs);
}
static void aborted_program(struct fixture *f, uint16_t page, uint16_t cursor)
{
    struct observed out; observe(f, 0, page, &out);
    CHECK(out.page.state == FWLAB_NAND_PAGE_TORN && out.page.program_count == 1);
    CHECK(out.page.erase_generation_seen == 0 && out.block.erase_generation == 0);
    CHECK(out.block.next_program_page == cursor && out.block.health == FWLAB_NFC_BLOCK_GOOD);
    CHECK(out.block.erase_state == FWLAB_NAND_ERASE_CLEAN && !out.block.erase_attempt_count && !out.block.successful_erase_count);
    /* Abort payload is intentionally unknown: never assert that FF is valid. */
}

static void initial_format(void)
{
    struct fixture *f = create(); struct observed out;
    CHECK(fwlab_file_nand_v2_sequence(f->media) == 0 && f->length == f->capacity && f->durable_length == f->capacity);
    CHECK(!memcmp(f->durable, f->durable + FNV2_SUPER_BYTES, FNV2_SUPER_BYTES));
    CHECK(get16(f->durable + 4) == 2 && get16(f->durable + 6) == FNV2_SUPER_BYTES);
    CHECK(!memcmp(f->durable + 8, f->config.media_uuid, 16));
    CHECK(get32(f->durable + 24) == BLOCK_COUNT && get32(f->durable + 28) == PAGE_COUNT);
    CHECK(get16(f->durable + 32) == 1 && get16(f->durable + 34) == 1 && get16(f->durable + 36) == 1);
    CHECK(get16(f->durable + 38) == BLOCK_COUNT && get16(f->durable + 40) == PAGES_PER_BLOCK);
    CHECK(get64(f->durable + 64) == FNV2_HOME_BASE && get64(f->durable + 72) == page_meta(0));
    CHECK(get64(f->durable + 80) == block_meta(0) && get64(f->durable + 88) == f->capacity);
    CHECK(get64(f->durable + 96) == FNV2_BANK_BASE && get64(f->durable + 104) == FNV2_BANK_BYTES);
    CHECK(all(f->durable + 128, 4092 - 128, 0));
    CHECK(get32(f->durable + 4092) == fwlab_crc32c(f->durable, 4092));
    CHECK(all(f->durable + (size_t)page_meta(0), PAGE_COUNT * FNV2_PAGE_RECORD_BYTES, 0));
    for (unsigned b = 0; b < 2; ++b) {
        const uint8_t *t = f->durable + (size_t)bank(b) + FNV2_INTENT_BYTES;
        CHECK(get16(t + 6) == 3 && !get64(t + 8) && !get64(t + 16));
        CHECK(!memcmp(t + 24, f->config.media_uuid, 16) && !get32(t + 40) && !get32(t + 44));
        CHECK(all(t + 48, 460, 0) && get32(t + 508) == fwlab_crc32c(t, 508));
    }
    for (unsigned b = 0; b < BLOCK_COUNT; ++b) {
        const uint8_t *r = f->durable + (size_t)block_meta(b);
        CHECK(get32(r + 4) == b && all(r + 8, 52, 0) && get32(r + 60) == fwlab_crc32c(r, 60));
        observe(f, (uint16_t)b, 63, &out); expect_payload(&out, 0, 0, 0);
        CHECK(out.page.state == FWLAB_NAND_PAGE_ERASED && !out.block.next_program_page && !out.block.erase_generation);
    }
    stable_reboot(f, 0); erased(f, 0, 0); erased(f, 15, 63);
    CHECK(fwlab_file_nand_v2_close(f->media) == FWLAB_NFC_API_OK);
    CHECK(fwlab_file_nand_v2_close(f->media) == FWLAB_NFC_API_WRONG_STATE);
    puts("CPRIME_FORMAT_PASS|exact_layout=1|bootstrap_pair=1|block_records=16|restart_readonly=1"); destroy(f);
}
static void full_batch_cost(void)
{
    struct fixture *f = create(); struct fwlab_nfc_ppa first = address(2, 0);
    uint8_t *main = malloc(PAGES_PER_BLOCK * PAGE_BYTES), *oob = malloc(PAGES_PER_BLOCK * OOB_BYTES);
    struct fwlab_nand_media_result result[PAGES_PER_BLOCK]; CHECK(main && oob);
    for (unsigned i = 0; i < PAGES_PER_BLOCK; ++i) payload(main + i * PAGE_BYTES, oob + i * OOB_BYTES, (uint8_t)(31 + i));
    uint64_t calls = f->writes, bytes = f->write_bytes, syncs = f->syncs, reads = f->reads;
    CHECK(fwlab_file_nand_v2_program_pages(f->media, &first, PAGES_PER_BLOCK, main, PAGES_PER_BLOCK * PAGE_BYTES,
        oob, PAGES_PER_BLOCK * OOB_BYTES, result, PAGES_PER_BLOCK) == FWLAB_NFC_API_OK);
    CHECK(f->writes - calls == 5 && f->write_bytes - bytes == UINT64_C(4352) * PAGES_PER_BLOCK + 1600);
    CHECK(f->syncs - syncs == 3 && fwlab_file_nand_v2_sequence(f->media) == 1);
    printf("CPRIME_BATCH_COST|pages=64|read_calls=%llu|write_calls=%llu|write_bytes=%llu|syncs=%llu|formula=4352*n+1600\n",
        (unsigned long long)(f->reads - reads), (unsigned long long)(f->writes - calls),
        (unsigned long long)(f->write_bytes - bytes), (unsigned long long)(f->syncs - syncs));
    uint8_t *read_main = malloc(64 * PAGE_BYTES), *read_oob = malloc(64 * OOB_BYTES);
    struct fwlab_nand_page_info pages[64]; struct fwlab_nand_block_info block;
    struct fwlab_nand_batch_v2 batch = fwlab_file_nand_v2_batch(f->media);
    CHECK(read_main && read_oob && batch.ops && batch.scalar.context == f->port.context);
    CHECK(batch.scalar.ops == f->port.ops && !memcmp(batch.media_uuid, f->config.media_uuid, 16));
    CHECK(!memcmp(&batch.geometry, &f->config.geometry, sizeof(batch.geometry)));
    reads = f->reads; uint64_t read_bytes = f->read_bytes;
    calls = f->writes; syncs = f->syncs;
    CHECK(batch.ops->read_pages(batch.scalar.context, &first, 64, read_main, 64 * PAGE_BYTES,
        read_oob, 64 * OOB_BYTES, pages, 64, &block) == FWLAB_NFC_API_OK);
    CHECK(f->reads - reads == 3 && f->read_bytes - read_bytes == 278592);
    CHECK(f->writes == calls && f->syncs == syncs);
    CHECK(!memcmp(main, read_main, 64 * PAGE_BYTES) && !memcmp(oob, read_oob, 64 * OOB_BYTES));
    CHECK(block.next_program_page == 64 && !block.erase_generation);
    for (unsigned i = 0; i < 64; ++i)
        CHECK(pages[i].state == FWLAB_NAND_PAGE_VALID && pages[i].program_count == 1 &&
            !pages[i].erase_generation_seen);
    puts("CPRIME_BATCH_READ_COST|pages=64|read_calls=3|read_bytes=278592|writes=0|syncs=0|same_instance_binding=1");
    free(read_main); free(read_oob);
    for (unsigned i = 0; i < PAGES_PER_BLOCK; ++i) {
        CHECK(result[i].physical_outcome == FWLAB_NFC_PHYS_APPLIED && result[i].integrity == FWLAB_NFC_INTEGRITY_COMPLETE);
        CHECK(result[i].applied_main_bytes == PAGE_BYTES && result[i].applied_oob_bytes == OOB_BYTES);
        value(f, 2, (uint16_t)i, (uint8_t)(31 + i));
    }
    stable_reboot(f, 1); value(f, 2, 0, 31); value(f, 2, 63, 94); erased(f, 1, 63); erased(f, 3, 0);
    calls = f->writes; syncs = f->syncs; first = address(3, 1);
    CHECK(fwlab_file_nand_v2_program_pages(f->media, &first, 64, main, 64 * PAGE_BYTES, oob,
        64 * OOB_BYTES, result, 64) != FWLAB_NFC_API_OK);
    CHECK(f->writes == calls && f->syncs == syncs); erased(f, 3, 1);
    free(main); free(oob); destroy(f);
}
static void singleton_semantics(void)
{
    struct fixture *f = create(); struct fwlab_nand_media_result result; struct observed out;
    uint64_t calls = f->writes, bytes = f->write_bytes, syncs = f->syncs;
    CHECK(program(f, 0, 0, 19, 137, 19, FWLAB_NFC_INTEGRITY_TORN, &result) == FWLAB_NFC_API_OK);
    CHECK(f->writes - calls == 5 && f->write_bytes - bytes == 5952 && f->syncs - syncs == 3);
    CHECK(result.applied_main_bytes == 137 && result.applied_oob_bytes == 19 && result.integrity == FWLAB_NFC_INTEGRITY_TORN);
    observe(f, 0, 0, &out); expect_payload(&out, 19, 137, 19);
    CHECK(out.page.state == FWLAB_NAND_PAGE_TORN && out.page.program_count == 1 && out.block.next_program_page == 1);
    program_ok(f, 0, 1, 20); program_ok(f, 0, 2, 21); program_ok(f, 1, 0, 22);
    uint64_t sequence = fwlab_file_nand_v2_sequence(f->media); calls = f->writes;
    CHECK(program(f, 0, 0, 30, PAGE_BYTES, OOB_BYTES, FWLAB_NFC_INTEGRITY_COMPLETE, &result) == FWLAB_NFC_API_OK);
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT && result.reason == FWLAB_NFC_REASON_NOT_ERASED);
    CHECK(program(f, 0, 3, 30, 0, 0, FWLAB_NFC_INTEGRITY_TORN, &result) == FWLAB_NFC_API_OK);
    CHECK(erase(f, 0, 0, FWLAB_NFC_INTEGRITY_TORN, &result) == FWLAB_NFC_API_OK);
    CHECK(f->writes == calls && fwlab_file_nand_v2_sequence(f->media) == sequence);
    CHECK(erase(f, 0, 1, FWLAB_NFC_INTEGRITY_TORN, &result) == FWLAB_NFC_API_OK);
    CHECK(erase(f, 0, 2, FWLAB_NFC_INTEGRITY_TORN, &result) == FWLAB_NFC_API_OK);
    reboot(f); observe(f, 0, 1, &out); expect_payload(&out, 0, 0, 0);
    CHECK(out.page.state == FWLAB_NAND_PAGE_TORN && !out.page.program_count && out.block.erase_state == FWLAB_NAND_ERASE_TORN);
    CHECK(out.block.erase_attempt_count == 2 && !out.block.successful_erase_count && !out.block.erase_generation && out.block.next_program_page == 3);
    value(f, 0, 2, 21); value(f, 1, 0, 22);
    bytes = f->write_bytes; syncs = f->syncs;
    CHECK(erase(f, 0, 64, FWLAB_NFC_INTEGRITY_COMPLETE, &result) == FWLAB_NFC_API_OK);
    CHECK(f->write_bytes - bytes == 1600 && f->syncs - syncs == 3);
    CHECK(result.base_erase_generation == 0 && result.final_erase_generation == 1);
    erased(f, 0, 0); erased(f, 0, 63); observe(f, 0, 0, &out);
    CHECK(out.block.erase_attempt_count == 3 && out.block.successful_erase_count == 1 && !out.block.next_program_page);
    program_ok(f, 0, 0, 41); struct fwlab_nfc_ppa bad = address(1, 0);
    CHECK(f->port.ops->mark_runtime_bad(f->port.context, &bad) == FWLAB_NFC_API_OK);
    sequence = fwlab_file_nand_v2_sequence(f->media); stable_reboot(f, sequence);
    value(f, 0, 0, 41); observe(f, 1, 0, &out); CHECK(out.block.health == FWLAB_NFC_BLOCK_RUNTIME_BAD);
    CHECK(program(f, 1, 1, 42, PAGE_BYTES, OOB_BYTES, FWLAB_NFC_INTEGRITY_COMPLETE, &result) == FWLAB_NFC_API_OK);
    CHECK(result.physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT && result.reason == FWLAB_NFC_REASON_BAD_BLOCK);
    puts("CPRIME_SEMANTICS_PASS|partial_program_exact=1|partial_erase_prefix_suffix=1|full_erase_generation=1|bad_mark=1|neighbor=1"); destroy(f);
}
static void cut_intent(void)
{
    for (unsigned variant = 0; variant < 3; ++variant) {
        struct fixture *f = create(); struct fwlab_nand_media_result result;
        program_ok(f, 0, 0, 51);
        if (!variant) arm_write(f, bank(2), 83);
        else arm_sync(f, 1, variant == 2);
        CHECK(program(f, 0, 1, 52, PAGE_BYTES, OOB_BYTES, FWLAB_NFC_INTEGRITY_COMPLETE, &result) != FWLAB_NFC_API_OK);
        quarantined(f); reboot(f); value(f, 0, 0, 51);
        if (variant == 2) { CHECK(fwlab_file_nand_v2_sequence(f->media) == 2); aborted_program(f, 1, 2); }
        else { CHECK(fwlab_file_nand_v2_sequence(f->media) == 1); erased(f, 0, 1); }
        erased(f, 1, 0); stable_reboot(f, variant == 2 ? 2 : 1); destroy(f);
    }
    puts("CPRIME_INTENT_CUT_PASS|partial_intent_ignored=1|failed_sync_persist_none_or_all=1|durable_intent_consumed=1");
}
static struct fixture *torn_successor_block(void)
{
    struct fixture *f = create(); struct fwlab_nand_media_result result;
    program_ok(f, 0, 0, 61);
    arm_write(f, block_meta(0), 37);
    CHECK(program(f, 0, 1, 62, PAGE_BYTES, OOB_BYTES, FWLAB_NFC_INTEGRITY_COMPLETE, &result) != FWLAB_NFC_API_OK);
    CHECK(get32(f->durable + (size_t)block_meta(0) + 60) != fwlab_crc32c(f->durable + (size_t)block_meta(0), 60));
    quarantined(f); return f;
}
static void compare_read_group(struct fixture *f, uint16_t b, uint32_t count)
{
    uint8_t main[4 * PAGE_BYTES], oob[4 * OOB_BYTES];
    struct fwlab_nand_page_info pages[4]; struct fwlab_nand_block_info block;
    struct fwlab_nfc_ppa first = address(b, 0); CHECK(count <= 4);
    uint64_t writes = f->writes, syncs = f->syncs;
    CHECK(fwlab_file_nand_v2_read_pages(f->media, &first, count, main, count * PAGE_BYTES,
        oob, count * OOB_BYTES, pages, 4, &block) == FWLAB_NFC_API_OK);
    for (uint32_t p = 0; p < count; ++p) {
        struct observed one; observe(f, b, (uint16_t)p, &one);
        CHECK(!memcmp(main + p * PAGE_BYTES, one.main, PAGE_BYTES));
        CHECK(!memcmp(oob + p * OOB_BYTES, one.oob, OOB_BYTES));
        CHECK(!memcmp(&pages[p], &one.page, sizeof(one.page)));
        CHECK(!memcmp(&block, &one.block, sizeof(block)));
    }
    CHECK(f->writes == writes && f->syncs == syncs);
}
static void batch_read_states(void)
{
    struct fixture *f = create(); struct fwlab_nand_media_result result;
    CHECK(program(f, 0, 0, 19, 137, 19, FWLAB_NFC_INTEGRITY_TORN, &result) == FWLAB_NFC_API_OK);
    program_ok(f, 0, 1, 20); program_ok(f, 0, 2, 21);
    compare_read_group(f, 0, 4);
    CHECK(erase(f, 0, 1, FWLAB_NFC_INTEGRITY_TORN, &result) == FWLAB_NFC_API_OK);
    compare_read_group(f, 0, 4);
    CHECK(erase(f, 0, 64, FWLAB_NFC_INTEGRITY_COMPLETE, &result) == FWLAB_NFC_API_OK);
    compare_read_group(f, 0, 4);
    uint8_t main[4 * PAGE_BYTES], oob[4 * OOB_BYTES];
    struct fwlab_nand_page_info pages[4]; struct fwlab_nand_block_info block;
    struct fwlab_nfc_ppa first = address(0, 0); uint64_t reads = f->reads;
    CHECK(fwlab_file_nand_v2_read_pages(f->media, &first, 4, main, sizeof(main),
        oob, sizeof(oob), pages, 4, &block) == FWLAB_NFC_API_OK);
    CHECK(f->reads - reads == 2 && block.erase_generation == 1 && all(main, sizeof(main), 0xff));
    reads = f->reads;
    CHECK(fwlab_file_nand_v2_read_pages(f->media, &first, 0, main, 0, oob, 0,
        pages, 4, &block) == FWLAB_NFC_API_INVALID_CONTRACT);
    first.page = 63;
    CHECK(fwlab_file_nand_v2_read_pages(f->media, &first, 4, main, sizeof(main),
        oob, sizeof(oob), pages, 4, &block) == FWLAB_NFC_API_INVALID_CONTRACT);
    CHECK(f->reads == reads); destroy(f);
    f = torn_successor_block(); reboot(f); compare_read_group(f, 0, 3); destroy(f);
    f = create(); program_ok(f, 0, 0, 61); program_ok(f, 0, 1, 62);
    f->working[FNV2_HOME_BASE + PAGE_BYTES + 137] ^= 1; first = address(0, 0);
    CHECK(fwlab_file_nand_v2_read_pages(f->media, &first, 2, main, 2 * PAGE_BYTES,
        oob, 2 * OOB_BYTES, pages, 4, &block) != FWLAB_NFC_API_OK);
    CHECK(!fwlab_file_nand_v2_media(f->media).ops);
    CHECK(!fwlab_file_nand_v2_batch(f->media).ops); destroy(f);
    puts("CPRIME_BATCH_READ_STATES_PASS|scalar_equivalence=1|partial_program=1|partial_erase=1|erased_generation=1|abort_unknown=1|CRC_failure_quarantines=1|bad_shape_no_IO=1");
}
static void cut_homes_and_abort(void)
{
    struct fixture *f = torn_successor_block();
    reboot(f); CHECK(fwlab_file_nand_v2_sequence(f->media) == 2);
    value(f, 0, 0, 61); aborted_program(f, 1, 2); erased(f, 0, 2); erased(f, 1, 0);
    stable_reboot(f, 2); aborted_program(f, 1, 2); program_ok(f, 0, 2, 63); value(f, 0, 2, 63); destroy(f);
    f = torn_successor_block(); crash(f);
    arm_write(f, page_meta(1), 97); CHECK(restart(f) != FWLAB_NFC_API_OK); quarantined(f);
    crash(f); arm_write(f, bank(2) + FNV2_INTENT_BYTES, 113);
    CHECK(restart(f) != FWLAB_NFC_API_OK); quarantined(f);
    reboot(f); CHECK(fwlab_file_nand_v2_sequence(f->media) == 2);
    value(f, 0, 0, 61); aborted_program(f, 1, 2); erased(f, 0, 2); erased(f, 1, 0);
    const uint8_t *terminal = f->durable + (size_t)bank(2) + FNV2_INTENT_BYTES;
    CHECK(get16(terminal + 6) == 2 && get64(terminal + 8) == 2 && get64(terminal + 16) == 1);
    stable_reboot(f, 2); aborted_program(f, 1, 2);
    puts("CPRIME_HOMES_ABORT_CUT_PASS|S_page0_Splus1_page1_blocktear=37|CONTROL_BASE_before_homes=1|abort_install_second_cut=1|ABORT_terminal_cut=1|absolute_replay=1|neighbor=1");
    destroy(f);
}
static void cut_commit(void)
{
    for (unsigned variant = 0; variant < 2; ++variant) {
        struct fixture *f = create(); struct fwlab_nand_media_result result;
        program_ok(f, 0, 0, 71);
        if (!variant) arm_write(f, bank(2) + FNV2_INTENT_BYTES, 128);
        else arm_sync(f, 3, 1);
        CHECK(program(f, 0, 1, 72, PAGE_BYTES, OOB_BYTES, FWLAB_NFC_INTEGRITY_COMPLETE, &result) != FWLAB_NFC_API_OK);
        quarantined(f); reboot(f); CHECK(fwlab_file_nand_v2_sequence(f->media) == 2); value(f, 0, 0, 71);
        if (!variant) aborted_program(f, 1, 2); else value(f, 0, 1, 72);
        stable_reboot(f, 2); destroy(f);
    }
    puts("CPRIME_COMMIT_CUT_PASS|torn_terminal_aborts=1|durable_COMMIT_lost_reply_recovers=1");
}
static void abort_erase_and_bad(void)
{
    for (unsigned full = 0; full < 2; ++full) {
        struct fixture *f = create(); struct fwlab_nand_media_result result; struct observed out;
        for (unsigned p = 0; p < 9; ++p) program_ok(f, 0, (uint16_t)p, (uint8_t)(81 + p));
        arm_write(f, block_meta(0), 25);
        CHECK(erase(f, 0, full ? 64 : 7, full ? FWLAB_NFC_INTEGRITY_COMPLETE : FWLAB_NFC_INTEGRITY_TORN, &result) != FWLAB_NFC_API_OK);
        quarantined(f); reboot(f); CHECK(fwlab_file_nand_v2_sequence(f->media) == 10);
        observe(f, 0, full ? 63 : 6, &out); expect_payload(&out, 0, 0, 0);
        CHECK(out.page.state == FWLAB_NAND_PAGE_TORN && !out.page.program_count);
        CHECK(out.block.erase_state == FWLAB_NAND_ERASE_TORN && out.block.erase_attempt_count == 1 &&
              !out.block.erase_generation && !out.block.successful_erase_count && out.block.next_program_page == 9);
        if (!full) { value(f, 0, 7, 88); value(f, 0, 8, 89); }
        stable_reboot(f, 10); observe(f, 0, 0, &out); CHECK(out.block.erase_attempt_count == 1);
        CHECK(erase(f, 0, 64, FWLAB_NFC_INTEGRITY_COMPLETE, &result) == FWLAB_NFC_API_OK);
        erased(f, 0, 0); observe(f, 0, 0, &out);
        CHECK(out.block.erase_generation == 1 && out.block.erase_attempt_count == 2 && out.block.successful_erase_count == 1);
        destroy(f);
    }
    struct fixture *f = create(); struct observed out; struct fwlab_nfc_ppa ppa = address(0, 0);
    program_ok(f, 0, 0, 91); arm_write(f, block_meta(0), 21);
    CHECK(f->port.ops->mark_runtime_bad(f->port.context, &ppa) != FWLAB_NFC_API_OK);
    quarantined(f); reboot(f); CHECK(fwlab_file_nand_v2_sequence(f->media) == 2);
    observe(f, 0, 0, &out); CHECK(out.block.health == FWLAB_NFC_BLOCK_RUNTIME_BAD && out.block.next_program_page == 1);
    CHECK(!out.block.erase_generation && !out.block.erase_attempt_count && !out.block.successful_erase_count);
    stable_reboot(f, 2); observe(f, 0, 0, &out); CHECK(out.block.health == FWLAB_NFC_BLOCK_RUNTIME_BAD);
    puts("CPRIME_ERASE_BAD_ABORT_PASS|full_reservation=1|partial_prefix_suffix=1|absolute_counters=1|bad_never_revives=1"); destroy(f);
}
static void cut_bank_reuse(void)
{
    struct fixture *f = create(); struct fwlab_nand_media_result result;
    for (unsigned p = 0; p < 4; ++p) program_ok(f, 0, (uint16_t)p, (uint8_t)(101 + p));
    CHECK(f->bank_writes[0] >= 2 && f->bank_writes[1] >= 2);
    arm_write(f, bank(5), 83);
    CHECK(program(f, 0, 4, 105, PAGE_BYTES, OOB_BYTES, FWLAB_NFC_INTEGRITY_COMPLETE, &result) != FWLAB_NFC_API_OK);
    quarantined(f); reboot(f); CHECK(fwlab_file_nand_v2_sequence(f->media) == 4);
    for (unsigned p = 0; p < 4; ++p) value(f, 0, (uint16_t)p, (uint8_t)(101 + p));
    erased(f, 0, 4); program_ok(f, 0, 4, 111); stable_reboot(f, 5); value(f, 0, 4, 111);
    puts("CPRIME_REUSE_CUT_PASS|old_selected_retained=1|obsolete_intent_not_required=1|invalid_successor_ignored=1|continued=1"); destroy(f);
}
static void rejection(void)
{
    struct fixture *f = create(); struct fwlab_file_nand_v2_config bad = f->config;
    struct fwlab_file_nand_v2 *out; uint64_t writes = f->writes;
    bad.geometry.main_bytes_per_page = 8192; CHECK(!fwlab_file_nand_v2_image_bytes(&bad));
    bad = f->config; bad.media_uuid[0] ^= 1;
    CHECK(fnv2_engine_open(&f->arena, sizeof(f->arena), &bad, &f->io, 0, &out) != FWLAB_NFC_API_OK && !out && f->writes == writes);
    reboot(f); bad = f->config; bad.geometry.channels = 2; bad.geometry.blocks_per_plane = 8;
    CHECK(fwlab_file_nand_v2_image_bytes(&bad) == f->capacity);
    CHECK(fnv2_engine_open(&f->arena, sizeof(f->arena), &bad, &f->io, 0, &out) != FWLAB_NFC_API_OK && !out && f->writes == writes);
    crash(f); --f->length; CHECK(restart(f) != FWLAB_NFC_API_OK && f->writes == writes); destroy(f);
    f = create(); program_ok(f, 0, 0, 121);
    f->durable[(size_t)bank(1) + 200] ^= 1; writes = f->writes; crash(f);
    CHECK(restart(f) != FWLAB_NFC_API_OK && f->writes == writes); destroy(f);
    f = create(); program_ok(f, 0, 0, 121);
    f->durable[(size_t)block_meta(0) + 40] ^= 1; writes = f->writes; crash(f);
    CHECK(restart(f) != FWLAB_NFC_API_OK && f->writes == writes); destroy(f);
    f = torn_successor_block();
    uint8_t *intent = f->durable + (size_t)bank(2);
    intent[64 + 60] ^= 1; put32(intent + 1020, fwlab_crc32c(intent, 1020));
    writes = f->writes; crash(f); CHECK(restart(f) != FWLAB_NFC_API_OK && f->writes == writes);
    puts("CPRIME_REJECT_PASS|UUID_geometry_length=1|required_intent_corruption=1|resolved_home_corruption=1|invalid_embedded_BASE=1|no_autoformat=1"); destroy(f);
}
int main(void)
{
    initial_format(); full_batch_cost(); batch_read_states(); singleton_semantics(); cut_intent(); cut_homes_and_abort();
    cut_commit(); abort_erase_and_bad(); cut_bank_reuse(); rejection();
    puts("CPRIME_ADJACENT_PASS|bounded_working_durable_bytes=1|three_barriers=1|no_POSIX_powerloss_or_10GB_claim=1");
    return 0;
}
