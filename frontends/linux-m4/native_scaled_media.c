/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#define _GNU_SOURCE
#include "native_scaled_media.h"
#include "physical_nand_batch.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/magic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
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
                            uint32_t logical_mib, uint64_t image_bytes)
{
    struct statfs fs;
    FILE *stream;
    char line[256];
    unsigned long long kib = 0;
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

static int open_r0(struct native_scaled_media *media,
    struct native_context *owner, const char *directory,
    const uint8_t uuid[16], int format, uint32_t logical_mib)
{
    size_t alignment, bytes;
    uint64_t lba_count;
    struct fwlab_nfc_geometry geometry;
    struct stat status;
    enum fwlab_nfc_api_result result;

    if (!media || media->opened || !owner || owner->runtime ||
        owner->runtime_media || !directory ||
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
    if (format && !format_preflight(media, logical_mib,
                                    fwlab_file_nand_v2_image_bytes(&media->config)))
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

static struct fwlab_nfc_geometry channel_geometry(void)
{
    struct fwlab_nfc_geometry geometry = {0};
    geometry.version = FWLAB_NFC_CONTRACT_VERSION;
    geometry.size = (uint16_t)sizeof(geometry);
    geometry.channels = 4;
    geometry.luns_per_channel = 1;
    geometry.planes_per_lun = 2;
    geometry.blocks_per_plane = 40;
    geometry.pages_per_block = 64;
    geometry.plane_parallelism_per_lun = 2;
    geometry.main_bytes_per_page = 4096;
    geometry.oob_bytes_per_page = 128;
    geometry.max_programs_per_erase = 1;
    geometry.program_order = FWLAB_NFC_PROGRAM_ASCENDING;
    return geometry;
}

static void channel_timing(struct fwlab_nfc_page_v2_lab_mutation_config *timing)
{
    memset(timing, 0, sizeof(*timing));
    timing->version = FWLAB_NFC_PAGE_V2_LAB_MUTATION_VERSION;
    timing->size = (uint16_t)sizeof(*timing);
    timing->read.version = FWLAB_NFC_PAGE_V2_LAB_VERSION;
    timing->read.size = (uint16_t)sizeof(timing->read);
    /* The factory supplies base geometry and fresh per-runtime identities. */
    timing->read.command_ns = 1000;
    timing->read.array_read_ns = 10000;
    timing->read.channel_bytes_per_second = UINT64_C(1000000000);
    timing->read.virtual_ns_limit = UINT64_MAX / 2u;
    timing->program_confirm_ns = 1000;
    timing->array_program_ns = 100000;
    timing->erase_command_ns = 1000;
    timing->array_erase_ns = 1000000;
    timing->status_command_ns = 1000;
    timing->status_response_bytes = 1;
    for (uint16_t channel = 0; channel < 4; ++channel) {
        timing->read.lun[channel].package = channel;
        timing->read.lun[channel].die = channel;
    }
}

static int channel_uuids(struct fwlab_nand_channel_volume_config *config)
{
    for (uint32_t channel = 0; channel < config->geometry.channels; ++channel) {
        int unique = 0;
        for (uint32_t attempt = 0; attempt < 8 && !unique; ++attempt) {
            uint8_t *uuid = config->child_uuid[channel];
            ssize_t bytes = getrandom(uuid, 16, 0);
            if (bytes < 0 && errno == EINTR)
                continue;
            if (bytes != 16)
                return 0;
            unique = !j0_bytes_zero(uuid, 16) &&
                     memcmp(uuid, config->media_uuid, 16) != 0;
            for (uint32_t previous = 0; previous < channel; ++previous)
                if (!memcmp(uuid, config->child_uuid[previous], 16))
                    unique = 0;
        }
        if (!unique)
            return 0;
    }
    return 1;
}

static uint64_t channel_image_bytes(
    const struct fwlab_nand_channel_volume_config *config)
{
    /* Immutable channel-volume v1 has a 1024-byte manifest and an empty lock
     * file. Filesystem allocation overhead is covered by the preflight margin. */
    uint64_t total = 1024;
    for (uint32_t channel = 0; channel < config->geometry.channels; ++channel) {
        struct fwlab_file_nand_v2_config child = {0};
        uint64_t bytes;
        child.geometry = config->geometry;
        child.geometry.channels = 1;
        memcpy(child.media_uuid, config->child_uuid[channel], 16);
        bytes = fwlab_file_nand_v2_image_bytes(&child);
        if (!bytes || bytes > UINT64_MAX - total)
            return 0;
        total += bytes;
    }
    return total;
}

static int open_channel_lab4k(struct native_scaled_media *media,
    struct native_context *owner, const char *directory,
    const uint8_t uuid[16], int format, uint32_t logical_mib)
{
    struct fwlab_nand_channel_volume_config config = {0};
    struct fwlab_nfc_geometry geometry = channel_geometry();
    struct statfs fs;
    enum fwlab_nfc_api_result result;
    size_t alignment, bytes;

    if (!media || media->opened || !owner || owner->runtime ||
        owner->runtime_media || !directory ||
        !uuid || j0_bytes_zero(uuid, 16) || (format != 0 && format != 1) ||
        logical_mib != 64)
        return 0;
    memset(media, 0, sizeof(*media));
    media->native.directory_fd = -1;
    media->owner = owner;
    media->profile = NATIVE_NAND_CHANNEL_LAB4K;
    memcpy(media->native.uuid, uuid, 16);
    media->native.directory_fd = open(directory,
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (media->native.directory_fd < 0 ||
        fstatfs(media->native.directory_fd, &fs) || fs.f_type != TMPFS_MAGIC)
        goto failed;
    if (format) {
        config.version = FWLAB_NAND_CHANNEL_VOLUME_VERSION;
        config.size = (uint16_t)sizeof(config);
        config.geometry = geometry;
        memcpy(config.media_uuid, uuid, 16);
        if (!channel_uuids(&config) ||
            !format_preflight(media, logical_mib, channel_image_bytes(&config)))
            goto failed;
    }
    alignment = fwlab_nand_channel_volume_arena_alignment();
    bytes = fwlab_nand_channel_volume_arena_size();
    if (!alignment || !bytes || bytes > SIZE_MAX - alignment + 1u)
        goto failed;
    media->native.arena = aligned_alloc(alignment,
        (bytes + alignment - 1u) & ~(alignment - 1u));
    if (!media->native.arena)
        goto failed;
    result = format
        ? fwlab_nand_channel_volume_posix_format(media->native.arena, bytes,
            media->native.directory_fd, &config, &media->volume)
        : fwlab_nand_channel_volume_posix_restart(media->native.arena, bytes,
            media->native.directory_fd, uuid, &media->volume);
    if (result != FWLAB_NFC_API_OK)
        goto failed;
    media->channels = fwlab_nand_channel_volume_binding(media->volume);
    if (!media->channels.aggregate.ops || !media->channels.aggregate.context ||
        memcmp(&media->channels.geometry, &geometry, sizeof(geometry)) ||
        memcmp(media->channels.media_uuid, uuid, 16))
        goto failed;
    media->binding.media = media->channels.aggregate;
    media->binding.geometry = media->channels.geometry;
    memcpy(media->binding.media_uuid, media->channels.media_uuid, 16);
    channel_timing(&media->timing);
    media->options.channel_media = &media->channels;
    media->options.mutation_lab_config = &media->timing;
    media->options.multihead_read_schedule = SCALE_STORAGE_READ_PARALLEL;
    media->options.read_policy = FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE;
    /* NULL executor deliberately selects the existing cooperative actor jobs. */
    scale_storage_multihead_lab_factory_init(&media->factory, &media->options);
    media->native.media_binding = &media->binding;
    media->native.storage_factory = &media->factory;
    media->native.format_lba_count = UINT64_C(64) * 2048u;
    media->native.expected_lba_count = media->native.format_lba_count;
    media->opened = 1;
    return 1;

failed:
    /* The lower open closes partial holders itself. A successful open followed
     * by a preset mismatch still owns a complete volume and must close it. */
    if (media->volume &&
        fwlab_nand_channel_volume_close(media->volume) != FWLAB_NFC_API_OK) {
        media->opened = 1; /* Retain an unresolved owner; never free below it. */
        return 0;
    }
    release_unbound(media);
    return 0;
}

int native_scaled_media_open_profile(struct native_scaled_media *media,
    struct native_context *owner, const char *directory,
    const uint8_t uuid[16], int format, uint32_t logical_mib,
    enum native_nand_profile profile)
{
    if (profile == NATIVE_NAND_R0)
        return open_r0(media, owner, directory, uuid, format, logical_mib);
    if (profile == NATIVE_NAND_CHANNEL_LAB4K)
        return open_channel_lab4k(media, owner, directory, uuid, format, logical_mib);
    return 0;
}

int native_scaled_media_open(struct native_scaled_media *media,
    struct native_context *owner, const char *directory,
    const uint8_t uuid[16], int format, uint32_t logical_mib)
{
    return native_scaled_media_open_profile(media, owner, directory, uuid,
                                            format, logical_mib, NATIVE_NAND_R0);
}

static int worker_resources_owned(const struct native_scaled_media *media)
{
    return media && media->opened && media->owner &&
        media->profile == NATIVE_NAND_CHANNEL_LAB4K &&
        media->owner->runtime_media == &media->native &&
        media->native.runtime_context == media &&
        (media->worker_config.workers == 1 || media->worker_config.workers == 4);
}

static enum fwlab_spine_result_v0 worker_error(enum fwlab_nfc_api_result result)
{
    return result == FWLAB_NFC_API_NO_CAPACITY ? FWLAB_SPINE_V0_NO_CAPACITY :
                                               FWLAB_SPINE_V0_POISONED;
}

static enum fwlab_spine_result_v0 worker_prepare_step(void *opaque, bool *advanced)
{
    struct native_scaled_media *media = opaque;
    enum fwlab_nfc_api_result result;
    bool complete = false, progress = false;

    if (!advanced)
        return FWLAB_SPINE_V0_INVALID;
    *advanced = false;
    if (!worker_resources_owned(media) || media->owner->runtime)
        return FWLAB_SPINE_V0_WRONG_STATE;
    if (!media->workers) {
        result = fwlab_nfc_channel_workers_prepare(&media->worker_config,
                                                   &media->workers);
        if (result != FWLAB_NFC_API_OK)
            return worker_error(result); /* Any partial handle stays owned. */
        *advanced = true;
    }
    result = fwlab_nfc_channel_workers_start_step(media->workers, &progress, &complete);
    *advanced = *advanced || progress;
    if (result != FWLAB_NFC_API_OK)
        return worker_error(result);
    if (!complete)
        return FWLAB_SPINE_V0_IN_PROGRESS;
    /* All real startup ACKs precede J0 construction and executor publication. */
    media->executor = fwlab_nfc_channel_workers_executor_polling(media->workers);
    if (!media->executor.ops || !media->executor.context)
        return FWLAB_SPINE_V0_POISONED;
    media->options.channel_executor = &media->executor;
    media->options.executor_pre_step_cleanup_by_caller = true;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 worker_release_step(void *opaque, bool *advanced)
{
    struct native_scaled_media *media = opaque;
    struct fwlab_nfc_channel_executor executor;
    enum fwlab_nfc_api_result result;
    bool complete = false;

    if (!advanced)
        return FWLAB_SPINE_V0_INVALID;
    *advanced = false;
    if (!worker_resources_owned(media) ||
        (media->owner->runtime && !media->owner->runtime_finalized))
        return FWLAB_SPINE_V0_WRONG_STATE;
    if (!media->workers)
        return FWLAB_SPINE_V0_OK;
    /* The polling view also owns a partially started, unpublished transport.
     * After normal J0 fini the hub has already joined every worker; shutdown
     * is idempotent and destruction alone returns the allocation here. */
    executor = fwlab_nfc_channel_workers_executor_polling(media->workers);
    if (!executor.ops || !executor.context)
        return FWLAB_SPINE_V0_POISONED;
    result = executor.ops->shutdown(executor.context, advanced, &complete);
    if (result != FWLAB_NFC_API_OK)
        return worker_error(result);
    if (!complete)
        return FWLAB_SPINE_V0_IN_PROGRESS;
    result = fwlab_nfc_channel_workers_destroy(media->workers);
    if (result != FWLAB_NFC_API_OK)
        return worker_error(result);
    media->workers = NULL;
    memset(&media->executor, 0, sizeof(media->executor));
    media->options.channel_executor = NULL;
    media->options.executor_pre_step_cleanup_by_caller = false;
    *advanced = true;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 worker_wait(void *opaque,
    const struct j0_runtime *runtime, uint32_t timeout_ms,
    bool *eligible, bool *notified)
{
    struct native_scaled_media *media = opaque;
    const struct fwlab_nfc_channel_v2 *hub;
    enum fwlab_nfc_api_result result;

    if (!eligible || !notified || timeout_ms > 1)
        return FWLAB_SPINE_V0_INVALID;
    *eligible = *notified = false;
    if (!worker_resources_owned(media) ||
        (runtime && runtime != media->owner->runtime))
        return FWLAB_SPINE_V0_WRONG_STATE;
    if (!media->workers)
        return FWLAB_SPINE_V0_OK;
    result = fwlab_nfc_channel_workers_lifecycle_wait(media->workers, timeout_ms,
                                                     eligible, notified);
    if (result != FWLAB_NFC_API_OK)
        return worker_error(result);
    if (*eligible || !runtime || media->owner->runtime_finalized)
        return FWLAB_SPINE_V0_OK;
    hub = scale_storage_channel_hub(runtime);
    if (!hub)
        return FWLAB_SPINE_V0_WRONG_STATE;
    result = fwlab_nfc_channel_v2_external_wait(hub, eligible);
    if (result != FWLAB_NFC_API_OK)
        return worker_error(result);
    if (*eligible) {
        result = fwlab_nfc_channel_workers_wait(media->workers, hub, timeout_ms, notified);
        if (result != FWLAB_NFC_API_OK)
            return worker_error(result);
    }
    return FWLAB_SPINE_V0_OK;
}

static const struct native_runtime_resource_ops worker_runtime_ops = {
    .prepare_step = worker_prepare_step,
    .release_step = worker_release_step,
    .wait = worker_wait
};

int native_scaled_media_enable_workers(struct native_scaled_media *media,
                                      uint32_t workers)
{
    if (!media || !media->opened || !media->owner ||
        media->profile != NATIVE_NAND_CHANNEL_LAB4K || !media->volume ||
        media->native.expected_lba_count != UINT64_C(64) * 2048u ||
        (workers != 1 && workers != 4) || media->owner->runtime ||
        media->owner->runtime_media ||
        media->workers || media->native.runtime_ops)
        return 0;
    memset(&media->worker_config, 0, sizeof(media->worker_config));
    media->worker_config.channels = media->channels.geometry.channels;
    media->worker_config.workers = workers;
    media->native.runtime_ops = &worker_runtime_ops;
    media->native.runtime_context = media;
    return 1;
}

int native_scaled_media_close(struct native_scaled_media *media)
{
    if (!media)
        return 0;
    if (!media->opened)
        return 1;
    if (!media->owner || media->owner->runtime || media->owner->runtime_media ||
        media->workers)
        return 0;
    if (media->profile == NATIVE_NAND_CHANNEL_LAB4K) {
        if (fwlab_nand_channel_volume_close(media->volume) != FWLAB_NFC_API_OK)
            return 0;
        media->volume = NULL;
    } else {
        if (fwlab_file_nand_v2_close(media->physical) != FWLAB_NFC_API_OK)
            return 0;
        media->physical = NULL;
    }
    release_unbound(media);
    return 1;
}
