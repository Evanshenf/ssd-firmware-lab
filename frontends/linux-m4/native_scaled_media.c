/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#define _GNU_SOURCE
#include "native_scaled_media.h"
#include "physical_nand_batch.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void release_unbound(struct native_scaled_media *media)
{
    free(media->native.arena);
    if (media->native.directory_fd >= 0)
        (void)close(media->native.directory_fd);
    memset(media, 0, sizeof(*media));
    media->native.directory_fd = -1;
}

int native_scaled_media_open(struct native_scaled_media *media,
    struct native_context *owner, const char *directory,
    const uint8_t uuid[16], int format)
{
    size_t alignment, bytes;
    struct stat status;
    enum fwlab_nfc_api_result result;

    if (!media || media->opened || !owner || owner->runtime || !directory ||
        !uuid || j0_bytes_zero(uuid, 16) || (format != 0 && format != 1))
        return 0;
    memset(media, 0, sizeof(*media));
    media->native.directory_fd = -1;
    media->owner = owner;
    memcpy(media->native.uuid, uuid, 16);
    memcpy(media->config.media_uuid, uuid, 16);
    media->config.geometry = (struct fwlab_nfc_geometry){
        .version = FWLAB_NFC_CONTRACT_VERSION,
        .size = sizeof(struct fwlab_nfc_geometry),
        .channels = 1, .luns_per_channel = 1, .planes_per_lun = 1,
        .blocks_per_plane = 320, .pages_per_block = 64,
        .plane_parallelism_per_lun = 1, .main_bytes_per_page = 4096,
        .oob_bytes_per_page = 128, .max_programs_per_erase = 1,
        .program_order = FWLAB_NFC_PROGRAM_ASCENDING
    };
    media->native.directory_fd = open(directory,
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (media->native.directory_fd < 0)
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
    media->native.format_lba_count = NATIVE_SCALED_LBA_COUNT;
    media->native.expected_lba_count = NATIVE_SCALED_LBA_COUNT;
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
