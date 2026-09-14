/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "channel_volume.h"
#include "physical_nand_internal.h"
#include "physical_nand_codec.h"
#include "physical_nand_batch.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CV_MAGIC UINT64_C(0x46574c4142435631)
#define CV_MANIFEST_BYTES 1024u
#define CV_ENTRY_BASE 128u
#define CV_ENTRY_BYTES 128u
#define CV_GEOMETRY_BYTES 36u

/* Assembly state owns each real engine; no shared physical transaction state,
 * scratch, sequence or bank is introduced between the channel children. */
struct fwlab_nand_channel_volume {
    uint64_t magic;
    int directory_fd, lock_fd, manifest_fd;
    uint8_t ready, closed;
    struct fwlab_nand_channel_volume_config config;
    struct fwlab_file_nand_holder_v2 holder[FWLAB_NAND_CHANNEL_V2_MAX];
    struct fwlab_file_nand_v2 *child[FWLAB_NAND_CHANNEL_V2_MAX];
    struct fwlab_file_nand_v2 storage[FWLAB_NAND_CHANNEL_V2_MAX];
};

static const char *const shard_names[FWLAB_NAND_CHANNEL_V2_MAX] = {
    "channel-0.nand", "channel-1.nand", "channel-2.nand", "channel-3.nand"
};

size_t fwlab_nand_channel_volume_arena_size(void)
{ return sizeof(struct fwlab_nand_channel_volume); }
size_t fwlab_nand_channel_volume_arena_alignment(void)
{ return _Alignof(struct fwlab_nand_channel_volume); }
const char *fwlab_nand_channel_volume_shard_name(uint32_t channel)
{ return channel < FWLAB_NAND_CHANNEL_V2_MAX ? shard_names[channel] : NULL; }

static struct fwlab_file_nand_v2_config child_config(
    const struct fwlab_nand_channel_volume_config *config, uint32_t channel)
{
    struct fwlab_file_nand_v2_config out = {0};
    out.geometry = config->geometry;
    out.geometry.channels = 1;
    memcpy(out.media_uuid, config->child_uuid[channel], 16);
    return out;
}

static bool config_valid(const struct fwlab_nand_channel_volume_config *config)
{
    if (config == NULL || config->version != FWLAB_NAND_CHANNEL_VOLUME_VERSION ||
        config->size != sizeof(*config) || config->reserved != 0 ||
        config->geometry.channels == 0 ||
        config->geometry.channels > FWLAB_NAND_CHANNEL_V2_MAX ||
        fnv2_all(config->media_uuid, 16, 0)) return false;
    for (uint32_t c = 0; c < FWLAB_NAND_CHANNEL_V2_MAX; ++c) {
        if (c >= config->geometry.channels) {
            if (!fnv2_all(config->child_uuid[c], 16, 0)) return false;
        } else {
            struct fwlab_file_nand_v2_config child = child_config(config, c);
            if (fwlab_file_nand_v2_image_bytes(&child) == 0 ||
                memcmp(config->media_uuid, child.media_uuid, 16) == 0) return false;
            for (uint32_t previous = 0; previous < c; ++previous)
                if (memcmp(config->child_uuid[previous], child.media_uuid, 16) == 0)
                    return false;
        }
    }
    return true;
}

static void geometry_encode(uint8_t *out, const struct fwlab_nfc_geometry *g)
{
    fnv2_put16(out, g->version); fnv2_put16(out + 2, g->size);
    fnv2_put16(out + 4, g->channels); fnv2_put16(out + 6, g->luns_per_channel);
    fnv2_put16(out + 8, g->planes_per_lun); fnv2_put16(out + 10, g->blocks_per_plane);
    fnv2_put16(out + 12, g->pages_per_block);
    fnv2_put16(out + 14, g->plane_parallelism_per_lun);
    fnv2_put32(out + 16, g->main_bytes_per_page);
    fnv2_put32(out + 20, g->oob_bytes_per_page);
    out[24] = g->max_programs_per_erase; out[25] = g->program_order;
    fnv2_put16(out + 26, g->reserved0);
    fnv2_put32(out + 28, g->reserved1[0]); fnv2_put32(out + 32, g->reserved1[1]);
}

static void geometry_decode(const uint8_t *in, struct fwlab_nfc_geometry *g)
{
    memset(g, 0, sizeof(*g));
    g->version = fnv2_get16(in); g->size = fnv2_get16(in + 2);
    g->channels = fnv2_get16(in + 4); g->luns_per_channel = fnv2_get16(in + 6);
    g->planes_per_lun = fnv2_get16(in + 8); g->blocks_per_plane = fnv2_get16(in + 10);
    g->pages_per_block = fnv2_get16(in + 12);
    g->plane_parallelism_per_lun = fnv2_get16(in + 14);
    g->main_bytes_per_page = fnv2_get32(in + 16);
    g->oob_bytes_per_page = fnv2_get32(in + 20);
    g->max_programs_per_erase = in[24]; g->program_order = in[25];
    g->reserved0 = fnv2_get16(in + 26);
    g->reserved1[0] = fnv2_get32(in + 28); g->reserved1[1] = fnv2_get32(in + 32);
}

static void manifest_encode(uint8_t out[CV_MANIFEST_BYTES],
    const struct fwlab_nand_channel_volume_config *config)
{
    memset(out, 0, CV_MANIFEST_BYTES);
    memcpy(out, "FWCHV201", 8);
    fnv2_put32(out + 8, FWLAB_NAND_CHANNEL_VOLUME_VERSION);
    fnv2_put32(out + 12, CV_MANIFEST_BYTES);
    memcpy(out + 16, config->media_uuid, 16);
    geometry_encode(out + 32, &config->geometry);
    fnv2_put32(out + 32 + CV_GEOMETRY_BYTES, config->geometry.channels);
    for (uint32_t c = 0; c < config->geometry.channels; ++c) {
        struct fwlab_file_nand_v2_config child = child_config(config, c);
        uint8_t *entry = out + CV_ENTRY_BASE + c * CV_ENTRY_BYTES;
        fnv2_put32(entry, c); fnv2_put32(entry + 4, 2); /* physical format 2 */
        fnv2_put64(entry + 8, fwlab_file_nand_v2_image_bytes(&child));
        memcpy(entry + 16, child.media_uuid, 16);
        geometry_encode(entry + 32, &child.geometry);
        memcpy(entry + 68, shard_names[c], strlen(shard_names[c]) + 1);
    }
    fnv2_put32(out + CV_MANIFEST_BYTES - 4, fnv2_crc(out, CV_MANIFEST_BYTES - 4));
}

static bool manifest_decode(const uint8_t in[CV_MANIFEST_BYTES],
    struct fwlab_nand_channel_volume_config *config)
{
    uint8_t canonical[CV_MANIFEST_BYTES];
    if (memcmp(in, "FWCHV201", 8) != 0 ||
        fnv2_get32(in + 8) != FWLAB_NAND_CHANNEL_VOLUME_VERSION ||
        fnv2_get32(in + 12) != CV_MANIFEST_BYTES ||
        fnv2_get32(in + CV_MANIFEST_BYTES - 4) != fnv2_crc(in, CV_MANIFEST_BYTES - 4))
        return false;
    memset(config, 0, sizeof(*config));
    config->version = FWLAB_NAND_CHANNEL_VOLUME_VERSION;
    config->size = sizeof(*config);
    memcpy(config->media_uuid, in + 16, 16);
    geometry_decode(in + 32, &config->geometry);
    for (uint32_t c = 0; c < FWLAB_NAND_CHANNEL_V2_MAX; ++c)
        memcpy(config->child_uuid[c], in + CV_ENTRY_BASE + c * CV_ENTRY_BYTES + 16, 16);
    if (!config_valid(config)) return false;
    manifest_encode(canonical, config);
    /* Exact names/order/local geometry/format/size and every reserved byte. */
    return memcmp(in, canonical, CV_MANIFEST_BYTES) == 0;
}

static bool private_directory(int fd)
{
    struct stat status;
    return fstat(fd, &status) == 0 && S_ISDIR(status.st_mode) &&
        status.st_uid == geteuid() && (status.st_mode & 07777) == 0700;
}

static bool private_file(const struct stat *status, uint64_t bytes)
{
    return S_ISREG(status->st_mode) && status->st_uid == geteuid() &&
        (status->st_mode & 07777) == 0600 && status->st_nlink == 1 &&
        status->st_size >= 0 && (uint64_t)status->st_size == bytes;
}

static bool sync_fd(int fd)
{
    int rc;
    do { rc = fsync(fd); } while (rc != 0 && errno == EINTR);
    return rc == 0;
}

static bool lock_volume(int fd)
{
    struct flock lock = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
    int rc;
    do { rc = fcntl(fd, F_OFD_SETLK, &lock); } while (rc != 0 && errno == EINTR);
    return rc == 0;
}

static bool manifest_io(int fd, uint8_t bytes[CV_MANIFEST_BYTES], bool writing)
{
    size_t at = 0;
    while (at < CV_MANIFEST_BYTES) {
        ssize_t count = writing ? pwrite(fd, bytes + at, CV_MANIFEST_BYTES - at, (off_t)at) :
                                 pread(fd, bytes + at, CV_MANIFEST_BYTES - at, (off_t)at);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        at += (size_t)count;
    }
    return true;
}

/* An independent directory description avoids changing the caller's offset. */
static bool directory_entries(int fd, uint32_t channels, bool empty)
{
    int scan_fd = openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    DIR *scan;
    struct dirent *entry;
    uint32_t seen = 0;
    bool valid = true;
    if (scan_fd < 0) return false;
    scan = fdopendir(scan_fd);
    if (scan == NULL) { (void)close(scan_fd); return false; }
    errno = 0;
    while ((entry = readdir(scan)) != NULL) {
        uint32_t bit = 0;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if (!empty) {
            if (strcmp(entry->d_name, FWLAB_NAND_CHANNEL_VOLUME_LOCK) == 0) bit = 1;
            if (strcmp(entry->d_name, FWLAB_NAND_CHANNEL_VOLUME_MANIFEST) == 0) bit = 2;
            for (uint32_t c = 0; c < channels; ++c)
                if (strcmp(entry->d_name, shard_names[c]) == 0) bit = 4u << c;
        }
        if (bit == 0 || (seen & bit) != 0) { valid = false; break; }
        seen |= bit;
    }
    if (entry == NULL && errno != 0) valid = false;
    if (!empty && seen != ((4u << channels) - 1u)) valid = false;
    if (closedir(scan) != 0) valid = false;
    return valid;
}

static bool live(const struct fwlab_nand_channel_volume *volume)
{ return volume != NULL && volume->magic == CV_MAGIC && volume->ready && !volume->closed; }

static struct fwlab_nand_media route(struct fwlab_nand_channel_volume *volume,
    const struct fwlab_nfc_ppa *global, struct fwlab_nfc_ppa *local)
{
    struct fwlab_nand_media none = {0};
    if (!live(volume) || global == NULL || global->channel >= volume->config.geometry.channels)
        return none;
    *local = *global; local->channel = 0;
    return fwlab_file_nand_v2_media(volume->child[global->channel]);
}

static enum fwlab_nfc_api_result aggregate_read(void *context,
    const struct fwlab_nfc_ppa *ppa, uint8_t *main, uint32_t main_length,
    uint8_t *oob, uint32_t oob_length, struct fwlab_nand_page_info *page,
    struct fwlab_nand_block_info *block)
{
    struct fwlab_nfc_ppa local;
    struct fwlab_nand_media child = route(context, ppa, &local);
    return child.ops == NULL ? FWLAB_NFC_API_INVALID_CONTRACT :
        child.ops->read_page(child.context, &local, main, main_length, oob, oob_length, page, block);
}

static enum fwlab_nfc_api_result aggregate_program(void *context,
    const struct fwlab_nfc_ppa *ppa, const uint8_t *main, uint32_t main_length,
    const uint8_t *oob, uint32_t oob_length, uint32_t applied_main_bytes,
    uint32_t applied_oob_bytes, uint8_t integrity, struct fwlab_nand_media_result *result)
{
    struct fwlab_nfc_ppa local;
    struct fwlab_nand_media child = route(context, ppa, &local);
    return child.ops == NULL ? FWLAB_NFC_API_INVALID_CONTRACT :
        child.ops->program(child.context, &local, main, main_length, oob, oob_length,
            applied_main_bytes, applied_oob_bytes, integrity, result);
}

static enum fwlab_nfc_api_result aggregate_erase(void *context,
    const struct fwlab_nfc_ppa *ppa, uint32_t applied_pages, uint8_t integrity,
    struct fwlab_nand_media_result *result)
{
    struct fwlab_nfc_ppa local;
    struct fwlab_nand_media child = route(context, ppa, &local);
    return child.ops == NULL ? FWLAB_NFC_API_INVALID_CONTRACT :
        child.ops->erase(child.context, &local, applied_pages, integrity, result);
}

static enum fwlab_nfc_api_result aggregate_bad(void *context, const struct fwlab_nfc_ppa *ppa)
{
    struct fwlab_nfc_ppa local;
    struct fwlab_nand_media child = route(context, ppa, &local);
    return child.ops == NULL ? FWLAB_NFC_API_INVALID_CONTRACT :
        child.ops->mark_runtime_bad(child.context, &local);
}

static uint64_t aggregate_hash(void *context)
{
    struct fwlab_nand_channel_volume *volume = context;
    uint64_t hash = UINT64_C(14695981039346656037);
    uint8_t bytes[16];
    if (!live(volume)) return 0;
    for (size_t i = 0; i < 16; ++i)
        hash = (hash ^ volume->config.media_uuid[i]) * UINT64_C(1099511628211);
    for (uint32_t c = 0; c < volume->config.geometry.channels; ++c) {
        struct fwlab_nand_media child = fwlab_file_nand_v2_media(volume->child[c]);
        uint64_t child_hash;
        if (child.ops == NULL || (child_hash = child.ops->hash(child.context)) == 0) return 0;
        fnv2_put64(bytes, c); fnv2_put64(bytes + 8, child_hash);
        for (size_t i = 0; i < sizeof(bytes); ++i)
            hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    /* Ordered quiescent content hash only, never a global media sequence. */
    return hash;
}

static const struct fwlab_nand_media_ops aggregate_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(aggregate_ops),
    .read_page = aggregate_read, .program = aggregate_program,
    .erase = aggregate_erase, .mark_runtime_bad = aggregate_bad, .hash = aggregate_hash
};

struct fwlab_nand_channel_v2 fwlab_nand_channel_volume_binding(
    struct fwlab_nand_channel_volume *volume)
{
    struct fwlab_nand_channel_v2 out = {0};
    if (!live(volume)) return out;
    out.version = FWLAB_NAND_CHANNEL_V2_VERSION; out.size = sizeof(out);
    out.geometry = volume->config.geometry;
    memcpy(out.media_uuid, volume->config.media_uuid, 16);
    out.aggregate.ops = &aggregate_ops; out.aggregate.context = volume;
    for (uint32_t c = 0; c < volume->config.geometry.channels; ++c) {
        out.channel[c] = fwlab_file_nand_v2_batch(volume->child[c]);
        if (out.channel[c].ops == NULL) { memset(&out, 0, sizeof(out)); break; }
    }
    return out;
}

enum fwlab_nfc_api_result fwlab_nand_channel_volume_close(struct fwlab_nand_channel_volume *volume)
{
    enum fwlab_nfc_api_result result = FWLAB_NFC_API_OK;
    if (volume == NULL || volume->magic != CV_MAGIC || volume->closed)
        return FWLAB_NFC_API_WRONG_STATE;
    /* Do not consume any owner if a synchronous child callback is still live.
     * The outer composition additionally owns all queued work/frame ACKs. */
    for (uint32_t c = 0; c < FWLAB_NAND_CHANNEL_V2_MAX; ++c)
        if (volume->child[c] != NULL && volume->child[c]->busy) return FWLAB_NFC_API_WRONG_STATE;
    volume->ready = 0;
    for (uint32_t c = FWLAB_NAND_CHANNEL_V2_MAX; c != 0; --c) {
        if (volume->child[c - 1] != NULL) {
            if (fwlab_file_nand_v2_close(volume->child[c - 1]) != FWLAB_NFC_API_OK)
                result = FWLAB_NFC_API_INVARIANT_FAILURE;
            volume->child[c - 1] = NULL;
        }
    }
    if (volume->manifest_fd >= 0 && close(volume->manifest_fd) != 0)
        result = FWLAB_NFC_API_INVARIANT_FAILURE;
    if (volume->lock_fd >= 0 && close(volume->lock_fd) != 0)
        result = FWLAB_NFC_API_INVARIANT_FAILURE;
    if (volume->directory_fd >= 0 && close(volume->directory_fd) != 0)
        result = FWLAB_NFC_API_INVARIANT_FAILURE;
    volume->manifest_fd = volume->lock_fd = volume->directory_fd = -1;
    volume->closed = 1;
    return result;
}

static enum fwlab_nfc_api_result open_volume(void *arena, size_t arena_bytes,
    int directory_fd, const struct fwlab_nand_channel_volume_config *config,
    const uint8_t expected_media_uuid[16], struct fwlab_nand_channel_volume **out, bool format)
{
    struct fwlab_nand_channel_volume *volume;
    struct stat status;
    uint8_t manifest[CV_MANIFEST_BYTES];
    enum fwlab_nfc_api_result result = FWLAB_NFC_API_INVALID_CONTRACT;
    if (out != NULL) *out = NULL;
    if (out == NULL || arena == NULL || arena_bytes < sizeof(*volume) ||
        (uintptr_t)arena % _Alignof(struct fwlab_nand_channel_volume) != 0 ||
        !private_directory(directory_fd) ||
        (format ? !config_valid(config) :
            expected_media_uuid == NULL || fnv2_all(expected_media_uuid, 16, 0))) return result;
    if (format && !directory_entries(directory_fd, 0, true)) return result;
    volume = arena;
    memset(volume, 0, sizeof(*volume)); volume->magic = CV_MAGIC;
    volume->directory_fd = volume->lock_fd = volume->manifest_fd = -1;
    volume->directory_fd = fcntl(directory_fd, F_DUPFD_CLOEXEC, 0);
    if (volume->directory_fd < 0) goto failed;
    volume->lock_fd = openat(volume->directory_fd, FWLAB_NAND_CHANNEL_VOLUME_LOCK,
        O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK | (format ? O_CREAT | O_EXCL : 0), 0600);
    if (volume->lock_fd < 0 || fstat(volume->lock_fd, &status) != 0 ||
        !private_file(&status, 0) || !lock_volume(volume->lock_fd)) goto failed;
    if (format) {
        volume->config = *config;
        if (!sync_fd(volume->lock_fd) || !sync_fd(volume->directory_fd)) {
            result = FWLAB_NFC_API_INVARIANT_FAILURE; goto failed;
        }
    } else {
        volume->manifest_fd = openat(volume->directory_fd, FWLAB_NAND_CHANNEL_VOLUME_MANIFEST,
            O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        if (volume->manifest_fd < 0 || fstat(volume->manifest_fd, &status) != 0 ||
            !private_file(&status, CV_MANIFEST_BYTES) ||
            !manifest_io(volume->manifest_fd, manifest, false) ||
            !manifest_decode(manifest, &volume->config) ||
            memcmp(expected_media_uuid, volume->config.media_uuid, 16) != 0 ||
            !directory_entries(volume->directory_fd, volume->config.geometry.channels, false)) goto failed;
    }
    /* The volume lock precedes every shard owner, always in ordinal order. */
    for (uint32_t c = 0; c < volume->config.geometry.channels; ++c) {
        struct fwlab_file_nand_v2_config child = child_config(&volume->config, c);
        struct fwlab_file_nand_holder_v2 *holder = &volume->holder[c];
        if (format) {
            result = fwlab_file_nand_v2_posix_format(&volume->storage[c], sizeof(volume->storage[c]),
                volume->directory_fd, shard_names[c], &child, &volume->child[c], holder);
        } else {
            if (fstatat(volume->directory_fd, shard_names[c], &status, AT_SYMLINK_NOFOLLOW) != 0 ||
                !private_file(&status, fwlab_file_nand_v2_image_bytes(&child))) {
                result = FWLAB_NFC_API_INVALID_CONTRACT; goto failed;
            }
            holder->device = (uint64_t)status.st_dev; holder->inode = (uint64_t)status.st_ino;
            memcpy(holder->media_uuid, child.media_uuid, 16);
            for (uint32_t previous = 0; previous < c; ++previous) {
                if (volume->holder[previous].device == holder->device &&
                    volume->holder[previous].inode == holder->inode) {
                    result = FWLAB_NFC_API_INVALID_CONTRACT; goto failed;
                }
            }
            result = fwlab_file_nand_v2_posix_restart(&volume->storage[c], sizeof(volume->storage[c]),
                volume->directory_fd, shard_names[c], &child, holder, &volume->child[c]);
        }
        if (result != FWLAB_NFC_API_OK) goto failed;
    }
    if (format) {
        result = FWLAB_NFC_API_INVARIANT_FAILURE;
        if (!sync_fd(volume->directory_fd)) goto failed;
        volume->manifest_fd = openat(volume->directory_fd, FWLAB_NAND_CHANNEL_VOLUME_PENDING,
            O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
        if (volume->manifest_fd < 0) goto failed;
        manifest_encode(manifest, &volume->config);
        if (!manifest_io(volume->manifest_fd, manifest, true) || !sync_fd(volume->manifest_fd)) goto failed;
        /* A durable diagnostic pending name survives every prepublication
         * failure. linkat is atomic no-replace; a crash before unlink leaves
         * two links/pending and restart rejects that incomplete publication. */
        if (!sync_fd(volume->directory_fd) ||
            linkat(volume->directory_fd, FWLAB_NAND_CHANNEL_VOLUME_PENDING,
                   volume->directory_fd, FWLAB_NAND_CHANNEL_VOLUME_MANIFEST, 0) != 0 ||
            !sync_fd(volume->directory_fd) ||
            unlinkat(volume->directory_fd, FWLAB_NAND_CHANNEL_VOLUME_PENDING, 0) != 0 ||
            !sync_fd(volume->directory_fd)) goto failed;
        if (fstat(volume->manifest_fd, &status) != 0 || !private_file(&status, CV_MANIFEST_BYTES) ||
            !directory_entries(volume->directory_fd, volume->config.geometry.channels, false)) goto failed;
    }
    volume->ready = 1; *out = volume;
    return FWLAB_NFC_API_OK;
failed:
    (void)fwlab_nand_channel_volume_close(volume);
    /* Intentionally preserve newly created files and incomplete publication. */
    return result;
}

enum fwlab_nfc_api_result fwlab_nand_channel_volume_posix_format(
    void *arena, size_t arena_bytes, int directory_fd,
    const struct fwlab_nand_channel_volume_config *config, struct fwlab_nand_channel_volume **volume)
{ return open_volume(arena, arena_bytes, directory_fd, config, NULL, volume, true); }

enum fwlab_nfc_api_result fwlab_nand_channel_volume_posix_restart(
    void *arena, size_t arena_bytes, int directory_fd, const uint8_t expected_media_uuid[16],
    struct fwlab_nand_channel_volume **volume)
{ return open_volume(arena, arena_bytes, directory_fd, NULL, expected_media_uuid, volume, false); }
