/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Linux POSIX adapter fixture, outside the portable media source boundary. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "physical_nand_codec.h"
#include "physical_nand_batch.h"
#include "fwlab/private/nfc_page_v2_model.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "POSIX OPERATION %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
#define MAIN_BYTES 4096u
#define OOB_BYTES 128u
#define GROUP_PAGES 2u
#define NFC_NONCE UINT64_C(0x504f5349584f5045)

struct fixture {
    char directory[512];
    int directory_fd, change_fd;
    struct fwlab_file_nand_v2_config config;
    struct fwlab_file_nand_holder_v2 holder;
    struct fwlab_file_nand_v2 *media;
    struct fwlab_nand_batch_v2 batch;
    struct fnv2_io saved_io;
    void *arena, *nfc_arena;
    struct fwlab_nfc_page_v2_model *nfc;
    struct fwlab_nfc_page_v2_provider provider;
    uint8_t main[GROUP_PAGES * MAIN_BYTES], oob[GROUP_PAGES * OOB_BYTES];
    uint64_t reads, writes, syncs;
    uint32_t nfc_epoch;
    uint8_t arm_mode, commit_written, mode_changed;
};

/* All byte IO is forwarded to the actual owned POSIX descriptor. The only
 * disturbance is a persistent metadata change after a real COMMIT sync. */
static enum fwlab_nfc_api_result observed_read(void *opaque, uint64_t at, void *out, size_t n)
{
    struct fixture *f = opaque; ++f->reads;
    return f->saved_io.read(f->saved_io.context, at, out, n);
}
static enum fwlab_nfc_api_result observed_write(void *opaque, uint64_t at, const void *in, size_t n)
{
    struct fixture *f = opaque;
    enum fwlab_nfc_api_result result;
    ++f->writes;
    result = f->saved_io.write(f->saved_io.context, at, in, n);
    if (result == FWLAB_NFC_API_OK && f->arm_mode && n == FNV2_TERMINAL_BYTES &&
        at == FNV2_BANK_BASE + FNV2_BANK_BYTES + FNV2_INTENT_BYTES) {
        struct fnv2_terminal terminal;
        CHECK(fnv2_terminal_decode(f->media, in, 1, &terminal));
        CHECK(terminal.sequence == 1 && terminal.disposition == FNV2_COMMIT &&
              terminal.kind == FNV2_PROGRAM && !f->commit_written);
        f->commit_written = 1;
    }
    return result;
}
static enum fwlab_nfc_api_result observed_sync(void *opaque)
{
    struct fixture *f = opaque;
    enum fwlab_nfc_api_result result;
    ++f->syncs;
    result = f->saved_io.sync(f->saved_io.context);
    if (result == FWLAB_NFC_API_OK && f->arm_mode && f->commit_written) {
        CHECK(f->syncs == 3 && f->writes == 5 && !f->mode_changed);
        CHECK(fchmod(f->change_fd, 0400) == 0);
        f->mode_changed = 1;
        f->arm_mode = 0;
    }
    return result;
}
static enum fwlab_nfc_api_result observed_resize(void *opaque, uint64_t n)
{ struct fixture *f = opaque; return f->saved_io.resize(f->saved_io.context, n); }
static enum fwlab_nfc_api_result observed_size(void *opaque, uint64_t *n)
{ struct fixture *f = opaque; return f->saved_io.size(f->saved_io.context, n); }
static enum fwlab_nfc_api_result observed_close(void *opaque)
{ struct fixture *f = opaque; return f->saved_io.close(f->saved_io.context); }

static void media_open(struct fixture *f, int format)
{
    size_t bytes = fwlab_file_nand_v2_arena_size();
    f->arena = calloc(1, bytes); CHECK(f->arena);
    CHECK((format ? fwlab_file_nand_v2_posix_format(f->arena, bytes, f->directory_fd,
        "nand.bin", &f->config, &f->media, &f->holder) :
        fwlab_file_nand_v2_posix_restart(f->arena, bytes, f->directory_fd,
        "nand.bin", &f->config, &f->holder, &f->media)) == FWLAB_NFC_API_OK);
    f->saved_io = f->media->io;
    f->media->io = (struct fnv2_io){f, observed_read, observed_write, observed_sync,
        observed_resize, observed_size, observed_close};
    f->batch = fwlab_file_nand_v2_posix_operation_batch(f->media);
    struct fwlab_nand_batch_v2 strict = fwlab_file_nand_v2_batch(f->media);
    CHECK(f->batch.ops && f->batch.scalar.ops && f->batch.scalar.context == f->media &&
          strict.ops && strict.ops != f->batch.ops && strict.scalar.ops != f->batch.scalar.ops &&
          strict.scalar.context == f->batch.scalar.context &&
          !memcmp(&f->batch.geometry, &f->config.geometry, sizeof(f->batch.geometry)) &&
          !memcmp(f->batch.media_uuid, f->holder.media_uuid, sizeof(f->batch.media_uuid)));
}
static void media_close(struct fixture *f)
{
    CHECK(!f->media->busy && fwlab_file_nand_v2_close(f->media) == FWLAB_NFC_API_OK);
    CHECK(!fwlab_file_nand_v2_posix_operation_batch(f->media).ops);
    free(f->arena); f->arena = NULL; f->media = NULL;
}
static struct fixture *create(void)
{
    const char *root = getenv("FWLAB_TEST_MEDIA_DIR");
    struct fixture *f = calloc(1, sizeof(*f));
    struct statfs fs; struct stat status; struct flock lock = {0};
    int root_fd, length;
    CHECK(f && root && root[0] == '/');
    root_fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(root_fd >= 0 && fstatfs(root_fd, &fs) == 0 && fs.f_bsize > 0);
    CHECK(fs.f_type == TMPFS_MAGIC &&
          (uint64_t)fs.f_blocks * (uint64_t)fs.f_bsize <= (UINT64_C(1) << 30) &&
          (uint64_t)fs.f_bavail * (uint64_t)fs.f_bsize >= (UINT64_C(64) << 20));
    CHECK(close(root_fd) == 0);
    length = snprintf(f->directory, sizeof(f->directory), "%s/fwlab-posix-op.XXXXXX", root);
    CHECK(length > 0 && (size_t)length < sizeof(f->directory) && mkdtemp(f->directory));
    f->directory_fd = open(f->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(f->directory_fd >= 0 && fstat(f->directory_fd, &status) == 0 &&
          S_ISDIR(status.st_mode) && status.st_uid == geteuid() && (status.st_mode & 07777) == 0700 &&
          fstatfs(f->directory_fd, &fs) == 0 && fs.f_type == TMPFS_MAGIC);
    f->config.geometry = (struct fwlab_nfc_geometry){
        .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(struct fwlab_nfc_geometry),
        .channels = 1, .luns_per_channel = 1, .planes_per_lun = 1,
        .blocks_per_plane = 16, .pages_per_block = 64, .plane_parallelism_per_lun = 1,
        .main_bytes_per_page = MAIN_BYTES, .oob_bytes_per_page = OOB_BYTES,
        .max_programs_per_erase = 1, .program_order = FWLAB_NFC_PROGRAM_ASCENDING
    };
    memcpy(f->config.media_uuid, "POSIX-OP-NAND001", 16);
    CHECK(fwlab_file_nand_v2_image_bytes(&f->config) == UINT64_C(4473856));
    media_open(f, 1);
    f->change_fd = openat(f->directory_fd, "nand.bin", O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    CHECK(f->change_fd >= 0 && fstat(f->change_fd, &status) == 0 &&
          S_ISREG(status.st_mode) && status.st_uid == geteuid() &&
          (status.st_mode & 07777) == 0600 && status.st_nlink == 1 &&
          (uint64_t)status.st_dev == f->holder.device && (uint64_t)status.st_ino == f->holder.inode &&
          (uint64_t)status.st_size == fwlab_file_nand_v2_image_bytes(&f->config));
    lock.l_type = F_WRLCK; lock.l_whence = SEEK_SET;
    CHECK(fcntl(f->change_fd, F_OFD_SETLK, &lock) == -1 && (errno == EAGAIN || errno == EACCES));
    for (size_t i = 0; i < sizeof(f->main); ++i) f->main[i] = (uint8_t)(i ^ (i >> 9) ^ 0x6d);
    for (size_t i = 0; i < sizeof(f->oob); ++i) f->oob[i] = (uint8_t)(i * 7u + 0x43);
    printf("POSIX_OPERATION_PREFLIGHT|image_bytes=4473856|fresh_private_file=1|holder_bound=1|exclusive_OFD=1|tmpfs_only=1|directory=%s\n", f->directory);
    return f;
}
static void destroy(struct fixture *f)
{
    struct stat status;
    CHECK(!f->media && !f->nfc_arena && fstatat(f->directory_fd, "nand.bin", &status,
        AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(status.st_mode) && status.st_nlink == 1 &&
        (uint64_t)status.st_dev == f->holder.device && (uint64_t)status.st_ino == f->holder.inode);
    CHECK(close(f->change_fd) == 0 && unlinkat(f->directory_fd, "nand.bin", 0) == 0 &&
          close(f->directory_fd) == 0 && rmdir(f->directory) == 0);
    free(f);
}

static void before_entry_truncate(void)
{
    struct fixture *f = create();
    struct fwlab_nfc_ppa first = {0};
    struct fwlab_nand_media_result results[GROUP_PAGES];
    uint8_t byte = 0xa5; struct stat status;
    off_t truncated = (off_t)(fwlab_file_nand_v2_image_bytes(&f->config) - 1u);
    CHECK(ftruncate(f->change_fd, truncated) == 0);
    CHECK(f->saved_io.read(f->saved_io.context, 0, &byte, 1) == FWLAB_NFC_API_INVALID_CONTRACT && byte == 0xa5);
    CHECK(f->batch.ops->program_pages(f->batch.scalar.context, &first, GROUP_PAGES,
        f->main, sizeof(f->main), f->oob, sizeof(f->oob), results, GROUP_PAGES) == FWLAB_NFC_API_INVARIANT_FAILURE);
    CHECK(f->media->quarantined && !f->media->busy && !f->reads && !f->writes && !f->syncs &&
          !fwlab_file_nand_v2_sequence(f->media) && fstat(f->change_fd, &status) == 0 &&
          status.st_size == truncated);
    media_close(f); destroy(f);
    puts("POSIX_OPERATION_ENTRY_PASS|truncate_before_entry=1|no_physical_byte_callbacks=1|strict_callback_still_checks=1|quarantined=1|scope_clear_close=1");
}

static void nfc_open(struct fixture *f)
{
    struct fwlab_nfc_page_v2_config c = {0};
    c.version = FWLAB_NFC_PAGE_V2_VERSION; c.size = sizeof(c);
    c.profile = FWLAB_NFC_PAGE_V2_PROFILE_R0; c.geometry = f->config.geometry;
    memcpy(c.media_uuid, f->config.media_uuid, sizeof(c.media_uuid));
    c.instance_nonce = NFC_NONCE; c.operation_uid_limit = UINT64_MAX;
    c.controller_epoch = ++f->nfc_epoch; c.generation = 1;
    f->nfc_arena = calloc(1, fwlab_nfc_page_v2_arena_size()); CHECK(f->nfc_arena);
    CHECK(fwlab_nfc_page_v2_init(f->nfc_arena, fwlab_nfc_page_v2_arena_size(), &c,
        &f->batch, &f->nfc) == FWLAB_NFC_API_OK);
    f->provider = fwlab_nfc_page_v2_provider(f->nfc); CHECK(f->provider.ops);
}
static void nfc_close(struct fixture *f)
{
    bool quiet = false;
    CHECK(f->provider.ops->reset_begin(f->provider.context, NFC_NONCE, f->nfc_epoch) == FWLAB_NFC_API_OK);
    CHECK(f->provider.ops->quiescent(f->provider.context, NFC_NONCE, f->nfc_epoch, &quiet) == FWLAB_NFC_API_OK && quiet);
    free(f->nfc_arena); f->nfc_arena = NULL; f->nfc = NULL;
}
static struct fwlab_nfc_page_v2_request request(struct fixture *f, uint16_t kind,
                                               uint64_t uid, uint16_t first)
{
    struct fwlab_nfc_page_v2_request r = {0};
    r.version = FWLAB_NFC_PAGE_V2_VERSION; r.size = sizeof(r); r.kind = kind;
    r.operation = (struct fwlab_nfc_operation_token){NFC_NONCE, uid, f->nfc_epoch, 1};
    r.first.page = first; r.page_count = GROUP_PAGES;
    if (kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP) {
        r.main = f->main; r.main_bytes = sizeof(f->main);
        r.oob = f->oob; r.oob_bytes = sizeof(f->oob);
    }
    return r;
}
static struct fwlab_nfc_page_v2_result execute(struct fixture *f,
    const struct fwlab_nfc_page_v2_request *r, const struct fwlab_nfc_page_v2_output *out)
{
    struct fwlab_nfc_page_v2_result result;
    struct fwlab_nfc_page_v2_step_result step;
    CHECK(f->provider.ops->try_submit(f->provider.context, r).disposition == FWLAB_NFC_ACCEPTED);
    CHECK(f->provider.ops->step(f->provider.context, 1, &step) == FWLAB_NFC_API_OK &&
          step.units_used == 1 && step.results_pending == 1);
    CHECK(f->provider.ops->take_result(f->provider.context, &r->operation, &result, out) == FWLAB_NFC_API_OK &&
          result.operation.operation_uid == r->operation.operation_uid && result.kind == r->kind);
    return result;
}
static void readback(struct fixture *f, uint64_t uid, uint16_t first)
{
    uint8_t main[GROUP_PAGES * MAIN_BYTES], oob[GROUP_PAGES * OOB_BYTES];
    struct fwlab_nfc_page_v2_output out = {main, sizeof(main), oob, sizeof(oob)};
    struct fwlab_nfc_page_v2_request r = request(f, FWLAB_NFC_PAGE_V2_READ_GROUP, uid, first);
    struct fwlab_nfc_page_v2_result result = execute(f, &r, &out);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS && result.read_valid &&
          result.delivered_pages == GROUP_PAGES && !memcmp(main, f->main, sizeof(main)) &&
          !memcmp(oob, f->oob, sizeof(oob)));
}

static void after_commit_mode_change(void)
{
    struct fixture *f = create();
    struct fwlab_nfc_page_v2_result result;
    struct fwlab_nfc_page_v2_request r;
    struct stat status; uint8_t byte = 0xa5;
    nfc_open(f); f->arm_mode = 1;
    r = request(f, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, 1, 0);
    result = execute(f, &r, NULL);
    CHECK(f->commit_written && f->mode_changed && !f->arm_mode && f->syncs == 3 && f->writes == 5 &&
          fwlab_file_nand_v2_sequence(f->media) == 1 && f->media->quarantined && !f->media->busy);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_FAILED &&
          result.backend_status == FWLAB_NFC_API_INVARIANT_FAILURE &&
          result.effect == FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN && !result.read_valid && !result.delivered_pages);
    for (uint32_t i = 0; i < GROUP_PAGES; ++i)
        CHECK(result.page[i].effect == FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN &&
              result.page[i].facts_valid == FWLAB_NFC_PAGE_V2_FACT_EFFECT &&
              !result.page[i].applied_main_bytes && !result.page[i].applied_oob_bytes &&
              !result.page[i].applied_pages);
    r = request(f, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, 2, GROUP_PAGES);
    CHECK(f->provider.ops->try_submit(f->provider.context, &r).disposition == FWLAB_NFC_REJECTED);
    CHECK(fstat(f->change_fd, &status) == 0 && (status.st_mode & 07777) == 0400);
    CHECK(f->saved_io.read(f->saved_io.context, 0, &byte, 1) == FWLAB_NFC_API_INVALID_CONTRACT && byte == 0xa5);
    nfc_close(f); media_close(f); /* Must clear reuse even after successful lower effects. */
    CHECK(fchmod(f->change_fd, 0600) == 0);
    media_open(f, 0); CHECK(fwlab_file_nand_v2_sequence(f->media) == 1);
    nfc_open(f); readback(f, 1, 0);
    r = request(f, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, 2, GROUP_PAGES);
    result = execute(f, &r, NULL);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS && result.effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE &&
          fwlab_file_nand_v2_sequence(f->media) == 2);
    readback(f, 3, GROUP_PAGES);
    nfc_close(f); media_close(f); destroy(f);
    puts("POSIX_OPERATION_EXIT_PASS|real_group_fdatasyncs=3|final_COMMIT_synced=1|persistent_mode_change=1|API_error_after_effect=1|NFC_UNKNOWN=1|media_NFC_quarantined=1|scope_clear_close=1|same_holder_restart_readback_continue=1");
}

int main(void)
{
    CHECK(setvbuf(stdout, NULL, _IOLBF, 0) == 0);
    before_entry_truncate(); after_commit_mode_change();
    puts("POSIX_OPERATION_PASS|owned_images_removed=1|tmpfs_process_restart_only=1");
    return 0;
}
