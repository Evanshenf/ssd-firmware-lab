/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#define _GNU_SOURCE
#include "native_scaled_media.h"
#include "physical_nand_batch.h"

#include <fcntl.h>
#include <inttypes.h>
#include <linux/magic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

static void release_unbound(struct native_scaled_media *media)
{
    free(media->native.arena);
    if (media->native.directory_fd >= 0)
        (void)close(media->native.directory_fd);
    memset(media, 0, sizeof(*media));
    media->native.directory_fd = -1;
}

static int format_preflight(const struct native_scaled_media *media,
                            uint32_t logical_mib)
{
    struct statfs fs;
    FILE *stream;
    char line[256];
    unsigned long long kib = 0;
    uint64_t image_bytes = fwlab_file_nand_v2_image_bytes(&media->config);
    uint64_t available, ram;
    const uint64_t reserve = logical_mib > 256 ? UINT64_C(2) << 30 :
                                               UINT64_C(128) << 20;

    if (!image_bytes || fstatfs(media->native.directory_fd, &fs) ||
        fs.f_type != TMPFS_MAGIC || fs.f_bsize <= 0 ||
        (uint64_t)fs.f_bavail > UINT64_MAX / (uint64_t)fs.f_bsize)
        return 0;
    available = (uint64_t)fs.f_bavail * (uint64_t)fs.f_bsize;
    stream = fopen("/proc/meminfo", "r");
    if (!stream)
        return 0;
    while (fgets(line, sizeof(line), stream))
        if (sscanf(line, "MemAvailable: %llu kB", &kib) == 1)
            break;
    if (fclose(stream) || !kib || kib > UINT64_MAX / 1024u)
        return 0;
    ram = (uint64_t)kib * 1024u;
    if (image_bytes > UINT64_MAX - reserve ||
        available < image_bytes + (UINT64_C(64) << 20) ||
        ram < image_bytes + reserve) {
        fprintf(stderr, "NATIVE_FORMAT_PREFLIGHT_ERROR|logical_mib=%u|image_bytes=%" PRIu64
                "|fs_available=%" PRIu64 "|mem_available=%" PRIu64 "|no_disk_fallback=1\n",
                logical_mib, image_bytes, available, ram);
        return 0;
    }
    printf("NATIVE_FORMAT_PREFLIGHT_OK|logical_mib=%u|image_bytes=%" PRIu64
           "|fs_available=%" PRIu64 "|mem_available=%" PRIu64 "|medium=tmpfs\n",
           logical_mib, image_bytes, available, ram);
    return 1;
}

int native_scaled_media_open(struct native_scaled_media *media,
    struct native_context *owner, const char *directory,
    const uint8_t uuid[16], int format, uint32_t logical_mib)
{
    size_t alignment, bytes;
    uint64_t lba_count;
    struct fwlab_nfc_geometry geometry;
    struct stat status;
    enum fwlab_nfc_api_result result;

    if (!media || media->opened || !owner || owner->runtime || !directory ||
        !uuid || j0_bytes_zero(uuid, 16) || (format != 0 && format != 1) ||
        !scale_storage_capacity_mib(logical_mib, &geometry, &lba_count))
        return 0;
    memset(media, 0, sizeof(*media));
    media->native.directory_fd = -1;
    media->owner = owner;
    memcpy(media->native.uuid, uuid, 16);
    memcpy(media->config.media_uuid, uuid, 16);
    media->config.geometry = geometry;
    if (logical_mib == 65536)
        media->config.mapped_budget_bytes = UINT64_C(90) << 30;
    media->native.directory_fd = open(directory,
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (media->native.directory_fd < 0)
        goto failed;
    /* Fresh format reserves the complete mapped image before lower allocation.
     * Recovery reuses that allocation and must not require another image's RAM. */
    if (format && !format_preflight(media, logical_mib))
        goto failed;
    alignment = fwlab_file_nand_v2_arena_alignment();
    bytes = fwlab_file_nand_v2_arena_size();
    if (!alignment || !bytes || bytes > SIZE_MAX - alignment + 1u)
        goto failed;
    media->native.arena = aligned_alloc(alignment,
        (bytes + alignment - 1u) & ~(alignment - 1u));
    if (!media->native.arena)
        goto failed;
    if (format) {
        result = fwlab_file_nand_v2_posix_mapped_format(media->native.arena,
            bytes, media->native.directory_fd, "nand.bin", &media->config,
            &media->physical, &media->holder);
    } else {
        if (fstatat(media->native.directory_fd, "nand.bin", &status,
                    AT_SYMLINK_NOFOLLOW) || !S_ISREG(status.st_mode))
            goto failed;
        media->holder.device = (uint64_t)status.st_dev;
        media->holder.inode = (uint64_t)status.st_ino;
        memcpy(media->holder.media_uuid, uuid, 16);
        result = fwlab_file_nand_v2_posix_mapped_restart(media->native.arena,
            bytes, media->native.directory_fd, "nand.bin", &media->config,
            &media->holder, &media->physical);
    }
    if (result != FWLAB_NFC_API_OK)
        goto failed;
    media->batch = fwlab_file_nand_v2_posix_operation_batch(media->physical);
    media->binding.media = media->batch.scalar;
    media->binding.geometry = media->batch.geometry;
    memcpy(media->binding.media_uuid, media->batch.media_uuid, 16);
    media->options.page_v2_media = &media->batch;
    scale_storage_window_v2_factory_init(&media->factory, &media->options);
    media->native.media_binding = &media->binding;
    media->native.storage_factory = &media->factory;
    media->native.format_lba_count = lba_count;
    media->native.expected_lba_count = lba_count;
    media->opened = 1;
    return 1;

failed:
    /* The POSIX open API closes its partial holder before returning failure. */
    release_unbound(media);
    return 0;
}

int native_scaled_media_close(struct native_scaled_media *media)
{
    if (!media)
        return 0;
    if (!media->opened)
        return 1;
    if (!media->owner || media->owner->runtime)
        return 0;
    if (fwlab_file_nand_v2_close(media->physical) != FWLAB_NFC_API_OK)
        return 0;
    media->physical = NULL;
    release_unbound(media);
    return 1;
}
