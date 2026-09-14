/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* D212 A Linux/POSIX physical assembly fixture; no FTL formatting or timed/NVMe claim. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "channel_volume.h"
#include "physical_nand.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/magic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "CHANNEL_VOLUME %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
#define MAIN_BYTES 4096u
#define OOB_BYTES 128u

/* One actual publication failure, after all real child formats and pending
 * fsync. Other calls reach the real filesystem. No fake NAND or LBA backend. */
static bool fail_publication, publication_failed;
int __real_linkat(int, const char *, int, const char *, int);
int __wrap_linkat(int from_fd, const char *from, int to_fd, const char *to, int flags)
{
    if (fail_publication && strcmp(from, FWLAB_NAND_CHANNEL_VOLUME_PENDING) == 0 &&
        strcmp(to, FWLAB_NAND_CHANNEL_VOLUME_MANIFEST) == 0) {
        fail_publication = false; publication_failed = true; errno = EIO; return -1;
    }
    return __real_linkat(from_fd, from, to_fd, to, flags);
}

struct fixture {
    struct fwlab_nand_channel_volume_config config;
    struct fwlab_nand_channel_volume *volume;
    struct fwlab_nand_channel_v2 binding;
    void *arena;
    char directory[PATH_MAX];
    int directory_fd;
    struct stat directory_identity, file_identity[4];
};

static void *new_arena(void)
{
    size_t bytes = fwlab_nand_channel_volume_arena_size();
    size_t alignment = fwlab_nand_channel_volume_arena_alignment();
    void *arena;
    CHECK(bytes != 0 && alignment != 0 && bytes % alignment == 0);
    arena = aligned_alloc(alignment, bytes); CHECK(arena); return arena;
}

static const char *file_name(uint32_t i, bool pending)
{
    if (i == 0) return FWLAB_NAND_CHANNEL_VOLUME_LOCK;
    if (i == 1) return pending ? FWLAB_NAND_CHANNEL_VOLUME_PENDING : FWLAB_NAND_CHANNEL_VOLUME_MANIFEST;
    return fwlab_nand_channel_volume_shard_name(i - 2);
}

static struct fixture *create(void)
{
    const char *root = getenv("FWLAB_TEST_MEDIA_DIR");
    struct fixture *f = calloc(1, sizeof(*f));
    struct fwlab_file_nand_v2_config child = {0};
    struct statfs fs;
    int root_fd, length;
    CHECK(f && geteuid() == 1000);
    if (root == NULL || root[0] != '/') {
        fputs("CHANNEL_VOLUME_PREFLIGHT_ERROR|FWLAB_TEST_MEDIA_DIR_required|no_disk_fallback=1\n", stderr);
        exit(1);
    }
    root_fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(root_fd >= 0 && fstatfs(root_fd, &fs) == 0 && fs.f_bsize > 0);
    CHECK(fs.f_type == TMPFS_MAGIC &&
        (uint64_t)fs.f_blocks * (uint64_t)fs.f_bsize <= (UINT64_C(1) << 30) &&
        (uint64_t)fs.f_bavail * (uint64_t)fs.f_bsize >= (UINT64_C(16) << 20));
    CHECK(close(root_fd) == 0);
    length = snprintf(f->directory, sizeof(f->directory), "%s/fwlab-channel-volume.XXXXXX", root);
    CHECK(length > 0 && (size_t)length < sizeof(f->directory) && mkdtemp(f->directory));
    f->directory_fd = open(f->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(f->directory_fd >= 0 && fstat(f->directory_fd, &f->directory_identity) == 0);
    f->config.version = FWLAB_NAND_CHANNEL_VOLUME_VERSION;
    f->config.size = sizeof(f->config);
    f->config.geometry = (struct fwlab_nfc_geometry){
        .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(struct fwlab_nfc_geometry),
        .channels = 2, .luns_per_channel = 2, .planes_per_lun = 1,
        .blocks_per_plane = 2, .pages_per_block = 64, .plane_parallelism_per_lun = 1,
        .main_bytes_per_page = MAIN_BYTES, .oob_bytes_per_page = OOB_BYTES,
        .max_programs_per_erase = 1, .program_order = FWLAB_NFC_PROGRAM_ASCENDING
    };
    memcpy(f->config.media_uuid, "CHANNEL-VOLUME01", 16);
    memcpy(f->config.child_uuid[0], "CHANNEL-SHARD001", 16);
    memcpy(f->config.child_uuid[1], "CHANNEL-SHARD002", 16);
    child.geometry = f->config.geometry; child.geometry.channels = 1;
    memcpy(child.media_uuid, f->config.child_uuid[0], 16);
    CHECK(fwlab_file_nand_v2_image_bytes(&child) * 2 < (UINT64_C(16) << 20));
    printf("CHANNEL_VOLUME_PREFLIGHT|medium=local_tmpfs|shards=2|total_image_bytes=%llu|directory=%s\n",
        (unsigned long long)(fwlab_file_nand_v2_image_bytes(&child) * 2), f->directory);
    fflush(stdout);
    f->arena = new_arena();
    return f;
}

static void snapshot_files(struct fixture *f, bool pending)
{
    for (uint32_t i = 0; i < 4; ++i) {
        CHECK(fstatat(f->directory_fd, file_name(i, pending), &f->file_identity[i], AT_SYMLINK_NOFOLLOW) == 0);
        CHECK(S_ISREG(f->file_identity[i].st_mode) && f->file_identity[i].st_uid == geteuid() &&
            f->file_identity[i].st_nlink == 1 && (f->file_identity[i].st_mode & 07777) == 0600);
    }
}

static void verify_files(struct fixture *f, bool pending)
{
    struct stat status;
    for (uint32_t i = 0; i < 4; ++i) {
        CHECK(fstatat(f->directory_fd, file_name(i, pending), &status, AT_SYMLINK_NOFOLLOW) == 0 &&
            status.st_dev == f->file_identity[i].st_dev && status.st_ino == f->file_identity[i].st_ino &&
            status.st_size == f->file_identity[i].st_size && status.st_nlink == 1);
    }
}

static void destroy(struct fixture *f, bool pending)
{
    struct stat status;
    CHECK(f->volume == NULL);
    verify_files(f, pending);
    for (uint32_t i = 0; i < 4; ++i) CHECK(unlinkat(f->directory_fd, file_name(i, pending), 0) == 0);
    CHECK(fstatat(AT_FDCWD, f->directory, &status, AT_SYMLINK_NOFOLLOW) == 0 &&
        status.st_dev == f->directory_identity.st_dev && status.st_ino == f->directory_identity.st_ino);
    CHECK(close(f->directory_fd) == 0 && rmdir(f->directory) == 0);
    free(f->arena); free(f);
}

static void open_volume(struct fixture *f, bool format)
{
    CHECK((format ? fwlab_nand_channel_volume_posix_format(f->arena,
        fwlab_nand_channel_volume_arena_size(), f->directory_fd, &f->config, &f->volume) :
        fwlab_nand_channel_volume_posix_restart(f->arena,
        fwlab_nand_channel_volume_arena_size(), f->directory_fd, f->config.media_uuid, &f->volume)) == FWLAB_NFC_API_OK);
    f->binding = fwlab_nand_channel_volume_binding(f->volume);
    CHECK(f->binding.version == FWLAB_NAND_CHANNEL_V2_VERSION && f->binding.size == sizeof(f->binding) &&
        f->binding.aggregate.ops && memcmp(&f->binding.geometry, &f->config.geometry, sizeof(f->config.geometry)) == 0 &&
        memcmp(f->binding.media_uuid, f->config.media_uuid, 16) == 0);
    for (uint32_t c = 0; c < 2; ++c) {
        struct fwlab_nfc_geometry local = f->config.geometry;
        local.channels = 1;
        CHECK(f->binding.channel[c].ops && f->binding.channel[c].scalar.ops &&
            memcmp(&f->binding.channel[c].geometry, &local, sizeof(local)) == 0 &&
            memcmp(f->binding.channel[c].media_uuid, f->config.child_uuid[c], 16) == 0);
    }
    CHECK(f->binding.channel[0].scalar.context != f->binding.channel[1].scalar.context &&
        !f->binding.channel[2].ops && !f->binding.channel[3].ops);
}

static void close_volume(struct fixture *f)
{
    CHECK(fwlab_nand_channel_volume_close(f->volume) == FWLAB_NFC_API_OK);
    CHECK(!fwlab_nand_channel_volume_binding(f->volume).aggregate.ops);
    f->volume = NULL;
}

static void expect_restart_rejected(struct fixture *f, const uint8_t uuid[16])
{
    void *arena = new_arena();
    struct fwlab_nand_channel_volume *volume = NULL;
    CHECK(fwlab_nand_channel_volume_posix_restart(arena,
        fwlab_nand_channel_volume_arena_size(), f->directory_fd, uuid, &volume) != FWLAB_NFC_API_OK);
    CHECK(volume == NULL); free(arena);
}

static uint64_t sequence(const struct fixture *f, uint32_t channel)
{ return fwlab_file_nand_v2_sequence(f->binding.channel[channel].scalar.context); }

static void payload(uint8_t *main, uint8_t *oob, uint32_t channel, uint32_t page)
{
    for (size_t i = 0; i < MAIN_BYTES; ++i) main[i] = (uint8_t)(channel * 37 + page * 19 + i * 7 + (i >> 8));
    for (size_t i = 0; i < OOB_BYTES; ++i) oob[i] = (uint8_t)(channel * 29 + page * 11 + i * 3);
    /* OOB carries global identity and is never rewritten to the child UUID. */
    memcpy(oob, "CHANNEL-VOLUME01", 16);
}

static void verify_payload(struct fixture *f)
{
    uint8_t main[MAIN_BYTES], oob[OOB_BYTES], expected_main[MAIN_BYTES], expected_oob[OOB_BYTES];
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
    for (uint32_t c = 0; c < 2; ++c) {
        struct fwlab_nfc_ppa ppa = { .channel = (uint16_t)c, .lun = 1 };
        for (uint32_t p = 0; p < c + 1; ++p) {
            ppa.page = (uint16_t)p; payload(expected_main, expected_oob, c, p);
            CHECK(f->binding.aggregate.ops->read_page(f->binding.aggregate.context, &ppa,
                main, sizeof(main), oob, sizeof(oob), &page, &block) == FWLAB_NFC_API_OK);
            CHECK(page.state == FWLAB_NAND_PAGE_VALID &&
                memcmp(main, expected_main, sizeof(main)) == 0 && memcmp(oob, expected_oob, sizeof(oob)) == 0);
            ppa.channel = 0;
            CHECK(f->binding.channel[c].scalar.ops->read_page(f->binding.channel[c].scalar.context, &ppa,
                main, sizeof(main), oob, sizeof(oob), &page, &block) == FWLAB_NFC_API_OK);
            CHECK(memcmp(main, expected_main, sizeof(main)) == 0 && memcmp(oob, expected_oob, sizeof(oob)) == 0);
            ppa.channel = (uint16_t)c;
        }
    }
}

static void full_assembly(void)
{
    struct fixture *f = create();
    uint8_t main[2 * MAIN_BYTES], oob[2 * OOB_BYTES], wrong_uuid[16];
    struct fwlab_nand_media_result result[2];
    struct fwlab_nfc_ppa ppa = { .lun = 1 };
    uint64_t hash;
    open_volume(f, true); snapshot_files(f, false);
    expect_restart_rejected(f, f->config.media_uuid); /* real OFD ownership */
    CHECK(sequence(f, 0) == 0 && sequence(f, 1) == 0);
    payload(main, oob, 0, 0);
    CHECK(f->binding.aggregate.ops->program(f->binding.aggregate.context, &ppa,
        main, MAIN_BYTES, oob, OOB_BYTES, MAIN_BYTES, OOB_BYTES,
        FWLAB_NFC_INTEGRITY_COMPLETE, &result[0]) == FWLAB_NFC_API_OK);
    CHECK(result[0].physical_outcome == FWLAB_NFC_PHYS_APPLIED &&
        result[0].integrity == FWLAB_NFC_INTEGRITY_COMPLETE && sequence(f, 0) == 1 && sequence(f, 1) == 0);
    payload(main, oob, 1, 0); payload(main + MAIN_BYTES, oob + OOB_BYTES, 1, 1);
    CHECK(f->binding.channel[1].ops->program_pages(f->binding.channel[1].scalar.context, &ppa,
        2, main, sizeof(main), oob, sizeof(oob), result, 2) == FWLAB_NFC_API_OK);
    CHECK(result[0].physical_outcome == FWLAB_NFC_PHYS_APPLIED &&
        result[1].physical_outcome == FWLAB_NFC_PHYS_APPLIED && sequence(f, 0) == 1 && sequence(f, 1) == 1);
    ppa.lun = 0; ppa.block = 1;
    CHECK(f->binding.aggregate.ops->erase(f->binding.aggregate.context, &ppa, 64,
        FWLAB_NFC_INTEGRITY_COMPLETE, &result[0]) == FWLAB_NFC_API_OK && sequence(f, 0) == 2 && sequence(f, 1) == 1);
    ppa.channel = 1;
    CHECK(f->binding.aggregate.ops->mark_runtime_bad(f->binding.aggregate.context, &ppa) == FWLAB_NFC_API_OK &&
        sequence(f, 0) == 2 && sequence(f, 1) == 2);
    verify_payload(f); hash = f->binding.aggregate.ops->hash(f->binding.aggregate.context); CHECK(hash);
    close_volume(f);
    memcpy(wrong_uuid, f->config.media_uuid, 16); wrong_uuid[0] ^= 1;
    expect_restart_rejected(f, wrong_uuid); verify_files(f, false);
    CHECK(renameat(f->directory_fd, fwlab_nand_channel_volume_shard_name(1), f->directory_fd, "held-shard") == 0);
    expect_restart_rejected(f, f->config.media_uuid);
    CHECK(renameat(f->directory_fd, "held-shard", f->directory_fd, fwlab_nand_channel_volume_shard_name(1)) == 0);
    CHECK(renameat(f->directory_fd, fwlab_nand_channel_volume_shard_name(0), f->directory_fd, "held-shard") == 0);
    CHECK(renameat(f->directory_fd, fwlab_nand_channel_volume_shard_name(1), f->directory_fd,
        fwlab_nand_channel_volume_shard_name(0)) == 0);
    CHECK(renameat(f->directory_fd, "held-shard", f->directory_fd, fwlab_nand_channel_volume_shard_name(1)) == 0);
    expect_restart_rejected(f, f->config.media_uuid); /* valid physical images, wrong child identities */
    CHECK(renameat(f->directory_fd, fwlab_nand_channel_volume_shard_name(0), f->directory_fd, "held-shard") == 0);
    CHECK(renameat(f->directory_fd, fwlab_nand_channel_volume_shard_name(1), f->directory_fd,
        fwlab_nand_channel_volume_shard_name(0)) == 0);
    CHECK(renameat(f->directory_fd, "held-shard", f->directory_fd, fwlab_nand_channel_volume_shard_name(1)) == 0);
    verify_files(f, false);
    open_volume(f, false); verify_payload(f);
    CHECK(f->binding.aggregate.ops->hash(f->binding.aggregate.context) == hash && sequence(f, 0) == 2 && sequence(f, 1) == 2);
    close_volume(f);
    CHECK(fwlab_nand_channel_volume_posix_format(f->arena, fwlab_nand_channel_volume_arena_size(),
        f->directory_fd, &f->config, &f->volume) != FWLAB_NFC_API_OK && f->volume == NULL);
    destroy(f, false);
    puts("CHANNEL_VOLUME|routing=global_to_local0|opaque_oob=exact|children=independent_sequence|reopen=exact|identity_reject=PASS|owners_closed=1|cleanup=own_success_only");
}

static void incomplete_publication(void)
{
    struct fixture *f = create();
    struct stat status;
    struct flock lock = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
    int fd;
    fail_publication = true;
    CHECK(fwlab_nand_channel_volume_posix_format(f->arena, fwlab_nand_channel_volume_arena_size(),
        f->directory_fd, &f->config, &f->volume) == FWLAB_NFC_API_INVARIANT_FAILURE && f->volume == NULL);
    CHECK(publication_failed && !fail_publication);
    CHECK(fstatat(f->directory_fd, FWLAB_NAND_CHANNEL_VOLUME_MANIFEST, &status, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT);
    snapshot_files(f, true);
    expect_restart_rejected(f, f->config.media_uuid); verify_files(f, true);
    fd = openat(f->directory_fd, FWLAB_NAND_CHANNEL_VOLUME_LOCK, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    CHECK(fd >= 0 && fcntl(fd, F_OFD_SETLK, &lock) == 0 && close(fd) == 0);
    /* Failed assembly relinquished each real shard OFD owner, too. */
    for (uint32_t c = 0; c < 2; ++c) {
        struct fwlab_file_nand_v2_config child = {0};
        struct fwlab_file_nand_holder_v2 holder = {0};
        struct fwlab_file_nand_v2 *media;
        void *arena = aligned_alloc(fwlab_file_nand_v2_arena_alignment(), fwlab_file_nand_v2_arena_size());
        CHECK(arena);
        child.geometry = f->config.geometry; child.geometry.channels = 1;
        memcpy(child.media_uuid, f->config.child_uuid[c], 16);
        holder.device = (uint64_t)f->file_identity[c + 2].st_dev;
        holder.inode = (uint64_t)f->file_identity[c + 2].st_ino;
        memcpy(holder.media_uuid, child.media_uuid, 16);
        CHECK(fwlab_file_nand_v2_posix_restart(arena, fwlab_file_nand_v2_arena_size(), f->directory_fd,
            fwlab_nand_channel_volume_shard_name(c), &child, &holder, &media) == FWLAB_NFC_API_OK);
        CHECK(fwlab_file_nand_v2_close(media) == FWLAB_NFC_API_OK); free(arena);
    }
    destroy(f, true);
    puts("CHANNEL_VOLUME|publication_failure=preserved_pending_and_shards|restart=no_create_no_resize|all_holders_released=1|cleanup=own_verified_fixture_only");
}

int main(void)
{
    full_assembly(); incomplete_publication();
    puts("CHANNEL_VOLUME PASS"); return 0;
}
