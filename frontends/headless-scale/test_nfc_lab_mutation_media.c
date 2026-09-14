/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* D210 lower binding: actual physical-v2 main/OOB/effects on bounded tmpfs.
 * Uses the existing NAND interfaces and Makefile, not an alternative backend. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "physical_nand.h"
#include "physical_nand_batch.h"
#include "fwlab/private/nfc_page_v2_lab.h"
#include <fcntl.h>
#include <linux/magic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "N2_MEDIA %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define MAIN 4096u
#define OOB 128u
#define COUNT 3u
#define NONCE UINT64_C(0x4e324d4544494131)
#ifndef MUTATION_MEDIA_PLANES
#define MUTATION_MEDIA_PLANES 1u
#endif
#ifndef MUTATION_MEDIA_PREFIX
#define MUTATION_MEDIA_PREFIX "fwlab-n2-media"
#endif
struct mutation_media {
    char directory[128]; int directory_fd;
    struct fwlab_file_nand_v2_config media_config;
    struct fwlab_file_nand_holder_v2 holder;
    struct fwlab_file_nand_v2 *media;
    struct fwlab_nand_batch_v2 actual, bound;
    struct fwlab_nand_batch_v2_ops observed_ops;
    struct fwlab_nfc_page_v2_lab_mutation_config config;
    struct fwlab_nfc_page_v2_lab *lab;
    struct fwlab_nfc_page_v2_provider port;
    void *media_arena, *lab_arena;
    uint32_t program_calls, fault;
    _Alignas(64) uint8_t main[COUNT * MAIN], oob[COUNT * OOB];
};
static struct mutation_media *active;
static enum fwlab_nfc_api_result observed_program(void *context, const struct fwlab_nfc_ppa *p,
    uint32_t n, const uint8_t *main, size_t mn, const uint8_t *oob, size_t on,
    struct fwlab_nand_media_result *out, size_t capacity)
{
    struct mutation_media *f = active;
    CHECK(f && context == f->actual.scalar.context && n == 1);
    ++f->program_calls;
    if (f->fault == 1 && f->program_calls == 2) {
        /* A real persisted bad-block transition between preflight and PROGRAM,
         * followed by the actual media's typed NONE/BAD_BLOCK result. */
        CHECK(f->actual.scalar.ops->mark_runtime_bad(context, p) == FWLAB_NFC_API_OK);
    }
    enum fwlab_nfc_api_result r = f->actual.ops->program_pages(context, p, n, main, mn, oob, on, out, capacity);
    if (f->fault == 2 && f->program_calls == 2) {
        CHECK(r == FWLAB_NFC_API_OK);
        /* Real successful effect followed by lost API certainty, not fake data.
         * Caller must not infer success from the now-untrusted output bytes. */
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    return r;
}
static void open_media(struct mutation_media *f, int format)
{
    size_t n = fwlab_file_nand_v2_arena_size();
    f->media_arena = aligned_alloc(fwlab_file_nand_v2_arena_alignment(), n); CHECK(f->media_arena);
    CHECK((format ? fwlab_file_nand_v2_posix_format(f->media_arena, n, f->directory_fd,
        "nand.bin", &f->media_config, &f->media, &f->holder) :
        fwlab_file_nand_v2_posix_restart(f->media_arena, n, f->directory_fd,
        "nand.bin", &f->media_config, &f->holder, &f->media)) == FWLAB_NFC_API_OK);
    f->actual = fwlab_file_nand_v2_batch(f->media);
}
static struct mutation_media *create_media(uint32_t fault)
{
    struct mutation_media *f;
    const char *root = getenv("FWLAB_TEST_MEDIA_DIR");
    struct statfs fs; int fd, n;
    CHECK(!active && root && root[0] == '/');
    fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(fd >= 0 && fstatfs(fd, &fs) == 0 && fs.f_bsize > 0 && fs.f_type == TMPFS_MAGIC &&
        (uint64_t)fs.f_blocks * (uint64_t)fs.f_bsize <= (UINT64_C(1) << 30) &&
        (uint64_t)fs.f_bavail * (uint64_t)fs.f_bsize >= (UINT64_C(64) << 20));
    CHECK(close(fd) == 0);
    f = aligned_alloc(64, sizeof(*f)); CHECK(f); memset(f, 0, sizeof(*f)); active = f; f->fault = fault;
    n = snprintf(f->directory, sizeof(f->directory), "%s/" MUTATION_MEDIA_PREFIX ".XXXXXX", root);
    CHECK(n > 0 && (size_t)n < sizeof(f->directory) && mkdtemp(f->directory));
    f->directory_fd = open(f->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW); CHECK(f->directory_fd >= 0);
    f->media_config.geometry = (struct fwlab_nfc_geometry){
        .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(struct fwlab_nfc_geometry),
        .channels = 2, .luns_per_channel = 2, .planes_per_lun = MUTATION_MEDIA_PLANES,
        .blocks_per_plane = 2, .pages_per_block = 64, .plane_parallelism_per_lun = MUTATION_MEDIA_PLANES,
        .main_bytes_per_page = MAIN, .oob_bytes_per_page = OOB,
        .max_programs_per_erase = 1, .program_order = FWLAB_NFC_PROGRAM_ASCENDING
    };
    memcpy(f->media_config.media_uuid, "N2-MEDIA-NAND001", 16);
    printf("N2_MEDIA_BEGIN|medium=local_tmpfs|bytes=%llu|fault=%u|directory=%s\n",
        (unsigned long long)fwlab_file_nand_v2_image_bytes(&f->media_config), fault, f->directory); fflush(stdout);
    open_media(f, 1);
    f->bound = f->actual; f->observed_ops = *f->actual.ops;
    f->observed_ops.program_pages = observed_program; f->bound.ops = &f->observed_ops;
    f->config.version = FWLAB_NFC_PAGE_V2_LAB_MUTATION_VERSION; f->config.size = sizeof(f->config);
    struct fwlab_nfc_page_v2_lab_config *r = &f->config.read;
    r->version = FWLAB_NFC_PAGE_V2_LAB_VERSION; r->size = sizeof(*r);
    r->base.version = FWLAB_NFC_PAGE_V2_VERSION; r->base.size = sizeof(r->base);
    r->base.profile = FWLAB_NFC_PAGE_V2_PROFILE_R0; r->base.geometry = f->media_config.geometry;
    memcpy(r->base.media_uuid, f->media_config.media_uuid, 16);
    r->base.instance_nonce = NONCE; r->base.controller_epoch = r->base.generation = 1;
    r->base.operation_uid_limit = UINT64_MAX;
    r->command_ns = 1000; r->array_read_ns = 10000; r->channel_bytes_per_second = UINT64_C(1000000000);
    r->virtual_ns_limit = UINT64_C(10000000000);
    for (unsigned i = 0; i < 4; ++i) {
        r->lun[i].target = r->lun[i].ce = (uint16_t)(i % 2u);
        r->lun[i].package = (uint16_t)(i / 2u); r->lun[i].die = (uint16_t)i;
    }
    f->config.program_confirm_ns = f->config.erase_command_ns = f->config.status_command_ns = 1000;
    f->config.array_program_ns = 100000; f->config.array_erase_ns = 1000000;
    f->config.status_response_bytes = 1; /* synthetic LAB fixture, not a vendor preset */
    size_t bytes = fwlab_nfc_page_v2_lab_arena_size();
    f->lab_arena = aligned_alloc(fwlab_nfc_page_v2_lab_arena_alignment(), bytes); CHECK(f->lab_arena);
    CHECK(fwlab_nfc_page_v2_lab_mutation_init(f->lab_arena, bytes, &f->config, &f->bound, &f->lab) == FWLAB_NFC_API_OK);
    f->port = fwlab_nfc_page_v2_lab_provider(f->lab); return f;
}
static struct fwlab_nfc_page_v2_lab_stats stats(struct mutation_media *f)
{
    struct fwlab_nfc_page_v2_lab_stats s;
    CHECK(fwlab_nfc_page_v2_lab_snapshot(f->lab, &s) == FWLAB_NFC_API_OK &&
        !s.counters_saturated && !s.trace_dropped); return s;
}
static void step(struct mutation_media *f)
{
    struct fwlab_nfc_page_v2_step_result s;
    CHECK(f->port.ops->step(f->port.context, 1, &s) == FWLAB_NFC_API_OK && s.units_used == 1);
}
static struct fwlab_nfc_page_v2_request request(struct mutation_media *f, uint64_t uid,
    unsigned resource, uint16_t kind, uint32_t count)
{
    struct fwlab_nfc_page_v2_request r = {0};
    r.version = FWLAB_NFC_PAGE_V2_VERSION; r.size = sizeof(r); r.kind = kind; r.page_count = count;
    r.operation = (struct fwlab_nfc_operation_token){NONCE, uid, 1, 1};
    r.first.channel = (uint16_t)(resource / 2u); r.first.lun = (uint16_t)(resource % 2u);
    if (kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP) {
        r.main = f->main; r.main_bytes = (size_t)count * MAIN; r.oob = f->oob; r.oob_bytes = (size_t)count * OOB;
    }
    return r;
}
static void submit(struct mutation_media *f, const struct fwlab_nfc_page_v2_request *r)
{ CHECK(f->port.ops->try_submit(f->port.context, r).disposition == FWLAB_NFC_ACCEPTED); }
static struct fwlab_nfc_page_v2_result take(struct mutation_media *f, const struct fwlab_nfc_page_v2_request *r, int output)
{
    struct fwlab_nfc_page_v2_result result;
    struct fwlab_nfc_page_v2_output d = {f->main, (size_t)r->page_count * MAIN, f->oob, (size_t)r->page_count * OOB};
    enum fwlab_nfc_api_result status; unsigned guard = 0;
    while ((status = f->port.ops->take_result(f->port.context, &r->operation, &result, output ? &d : NULL)) == FWLAB_NFC_API_WRONG_STATE) {
        CHECK(++guard < 10000); step(f);
    }
    CHECK(status == FWLAB_NFC_API_OK); return result;
}
static uint64_t event_time(struct mutation_media *f, uint64_t uid, uint16_t page, uint32_t event)
{
    struct fwlab_nfc_page_v2_lab_stats s = stats(f);
    for (uint32_t i = 0; i < s.trace_count; ++i) {
        struct fwlab_nfc_page_v2_lab_trace t;
        CHECK(fwlab_nfc_page_v2_lab_trace_at(f->lab, i, &t) == FWLAB_NFC_API_OK);
        if (t.operation_uid == uid && t.ppa.page == page && t.event == event) return t.now_ns;
    }
    CHECK(0); return 0;
}
static void close_lab(struct mutation_media *f)
{
    bool quiet = false;
    CHECK(f->port.ops->reset_begin(f->port.context, NONCE, 1) == FWLAB_NFC_API_OK);
    CHECK(f->port.ops->quiescent(f->port.context, NONCE, 1, &quiet) == FWLAB_NFC_API_OK && quiet);
    CHECK(!stats(f).active_slots && !stats(f).held_luns && !stats(f).busy_channels);
    free(f->lab_arena); f->lab_arena = NULL;
}
static void reopen_media_only(struct mutation_media *f)
{
    CHECK(fwlab_file_nand_v2_close(f->media) == FWLAB_NFC_API_OK); free(f->media_arena);
    open_media(f, 0);
}
static void destroy(struct mutation_media *f)
{
    struct stat image;
    if (f->lab_arena) close_lab(f);
    CHECK(fwlab_file_nand_v2_close(f->media) == FWLAB_NFC_API_OK); free(f->media_arena);
    CHECK(fstatat(f->directory_fd, "nand.bin", &image, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISREG(image.st_mode) && image.st_nlink == 1 &&
        (uint64_t)image.st_dev == f->holder.device && (uint64_t)image.st_ino == f->holder.inode);
    CHECK(unlinkat(f->directory_fd, "nand.bin", 0) == 0 && close(f->directory_fd) == 0 && rmdir(f->directory) == 0);
    free(f); active = NULL;
}
static void direct_check(struct mutation_media *f, uint32_t written)
{
    struct fwlab_nfc_ppa p = {0}; struct fwlab_nand_page_info pages[COUNT]; struct fwlab_nand_block_info block;
    CHECK(f->actual.ops->read_pages(f->actual.scalar.context, &p, COUNT, f->main, sizeof(f->main),
        f->oob, sizeof(f->oob), pages, COUNT, &block) == FWLAB_NFC_API_OK);
    for (unsigned page = 0; page < COUNT; ++page) {
        for (unsigned i = 0; i < MAIN; ++i) CHECK(f->main[page * MAIN + i] == (page < written ? 0x31 : 0xff));
        for (unsigned i = 0; i < OOB; ++i) CHECK(f->oob[page * OOB + i] == (page < written ? 0x72 : 0xff));
    }
}
static void snapshot_status_and_erase(void)
{
    struct mutation_media *f = create_media(0); struct fwlab_nfc_page_v2_result result;
    struct fwlab_nfc_page_v2_request r = request(f, 1, 0, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, COUNT);
    memset(f->main, 0x31, sizeof(f->main)); memset(f->oob, 0x72, sizeof(f->oob)); submit(f, &r);
    memset(f->main, 0xa5, sizeof(f->main)); memset(f->oob, 0xb6, sizeof(f->oob));
    unsigned guard = 0;
    while (!stats(f).attempted_program_pages) { CHECK(++guard < 1000); step(f); }
    CHECK(f->port.ops->take_result(f->port.context, &r.operation, &result, NULL) == FWLAB_NFC_API_WRONG_STATE);
    CHECK(stats(f).successful_program_pages == 1); direct_check(f, 1);
    CHECK(f->port.ops->cancel(f->port.context, &r.operation) == FWLAB_NFC_API_OK);
    while (!stats(f).results_pending) { CHECK(++guard < 1000); step(f); }
    CHECK(f->port.ops->cancel(f->port.context, &r.operation) == FWLAB_NFC_API_OK); /* DONE no-op */
    result = take(f, &r, 0);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS && !result.reason && result.effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE);
    r = request(f, 2, 0, FWLAB_NFC_PAGE_V2_READ_GROUP, COUNT); submit(f, &r); result = take(f, &r, 1);
    CHECK(result.read_valid && result.delivered_pages == COUNT); direct_check(f, COUNT);
    r = request(f, 3, 0, FWLAB_NFC_PAGE_V2_ERASE, 1); submit(f, &r); step(f);
    CHECK(stats(f).issued_erases == 1 && !stats(f).attempted_erases);
    CHECK(f->port.ops->cancel(f->port.context, &r.operation) == FWLAB_NFC_API_OK);
    result = take(f, &r, 0);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS && result.page[0].applied_pages == 64 &&
        result.page[0].final_erase_generation > result.page[0].base_erase_generation);
    close_lab(f); reopen_media_only(f); direct_check(f, 0);
    puts("N2_MEDIA_SNAPSHOT|actual_program3=1|effect_before_status_pending=1|cancel_group_drain=1|DONE_immutable=1|erase_generation=1|reopen_erased=1");
    destroy(f);
}
static void shared_resources(void)
{
    struct mutation_media *f = create_media(0);
    struct fwlab_nfc_page_v2_request r[4] = {
        request(f, 1, 0, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, 3),
        request(f, 2, 1, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, 2),
        request(f, 3, 2, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, 1),
        request(f, 4, 0, FWLAB_NFC_PAGE_V2_ERASE, 1)
    };
    memset(f->main, 0x31, sizeof(f->main)); memset(f->oob, 0x72, sizeof(f->oob));
    for (unsigned i = 0; i < 4; ++i) submit(f, &r[i]);
    for (unsigned i = 0; i < 4; ++i) CHECK(take(f, &r[i], 0).terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(event_time(f, 2, 0, FWLAB_NFC_PAGE_V2_LAB_CONFIRM_END) < event_time(f, 1, 0, FWLAB_NFC_PAGE_V2_LAB_PROGRAM_EFFECT));
    CHECK(event_time(f, 3, 0, FWLAB_NFC_PAGE_V2_LAB_LOAD_BEGIN) == 0);
    CHECK(event_time(f, 4, 0, FWLAB_NFC_PAGE_V2_LAB_ERASE_BEGIN) >= event_time(f, 1, 2, FWLAB_NFC_PAGE_V2_LAB_TERMINAL));
    struct fwlab_nfc_page_v2_lab_stats s = stats(f);
    CHECK(s.successful_program_pages == 6 && s.successful_erases == 1 && !s.active_slots &&
        !s.held_luns && !s.busy_channels && s.program_array_busy_ns[0] == 300000 &&
        s.program_array_busy_ns[1] == 200000 && s.program_array_busy_ns[2] == 100000 &&
        s.erase_array_busy_ns[0] == 1000000);
    printf("N2_MEDIA_RESOURCES|real_pages=6|two_channels=1|same_channel_LUN_overlap=1|same_LUN_excluded=1|model_ns=%llu\n", (unsigned long long)s.now_ns);
    destroy(f);
}
static void partial_facts(uint32_t fault)
{
    struct mutation_media *f = create_media(fault);
    struct fwlab_nfc_page_v2_request r = request(f, 1, 0, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, COUNT);
    memset(f->main, 0x31, sizeof(f->main)); memset(f->oob, 0x72, sizeof(f->oob)); submit(f, &r);
    struct fwlab_nfc_page_v2_result result = take(f, &r, 0);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_FAILED && f->program_calls == 2 &&
        result.page[0].effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE &&
        result.page[0].applied_main_bytes == MAIN && result.page[0].applied_oob_bytes == OOB);
    CHECK(result.page[1].effect == (fault == 1 ? FWLAB_NFC_PAGE_V2_EFFECT_NONE : FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN));
    CHECK(result.page[2].effect == FWLAB_NFC_PAGE_V2_EFFECT_NONE && result.page[2].facts_valid == FWLAB_NFC_PAGE_V2_FACT_EFFECT);
    CHECK(result.effect == (fault == 1 ? FWLAB_NFC_PAGE_V2_EFFECT_NONCOMPLETE : FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN));
    close_lab(f); reopen_media_only(f); direct_check(f, fault == 1 ? 1 : 2);
    printf("N2_MEDIA_FAILURE|fault=%u|real_effect=1|prefix_kept=1|current_typed_or_unknown=1|suffix_NONE=1|same_format_reopen=1\n", fault);
    destroy(f);
}
int main(void)
{
    snapshot_status_and_erase(); shared_resources(); partial_facts(1); partial_facts(2);
    puts("N2_MEDIA_PASS|actual_physical_v2_main_OOB=1|tmpfs_functional_not_disk_powerloss_or_native_performance=1");
    return 0;
}
