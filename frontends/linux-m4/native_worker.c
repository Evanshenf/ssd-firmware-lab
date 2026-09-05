/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _GNU_SOURCE
#include "native_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct native_media {
    int directory_fd;
    void *arena;
    struct fwlab_file_nand_v0 *file;
    struct fwlab_file_nand_holder_v0 holder;
    uint8_t uuid[16];
};

static volatile sig_atomic_t stop_requested;

static void stop_signal(int number)
{
    (void)number;
    stop_requested = 1;
}

static uint32_t get_le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static uint64_t get_le64(const uint8_t *bytes)
{
    return get_le32(bytes) | (uint64_t)get_le32(bytes + 4) << 32;
}

static int hex_bytes(const char *text, uint8_t *bytes, size_t count)
{
    size_t index;
    if (!text || strlen(text) != count * 2)
        return 0;
    for (index = 0; index < count; ++index) {
        unsigned value = 0, digit;
        for (digit = 0; digit < 2; ++digit) {
            char c = text[index * 2 + digit];
            if (c >= '0' && c <= '9')
                value = value * 16u + (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f')
                value = value * 16u + (unsigned)(c - 'a' + 10);
            else
                return 0;
        }
        bytes[index] = (uint8_t)value;
    }
    return !j0_bytes_zero(bytes, count);
}

static void *allocate_arena(size_t alignment, size_t size)
{
    if (!alignment || !size || size > SIZE_MAX - alignment + 1)
        return NULL;
    return aligned_alloc(alignment, (size + alignment - 1u) & ~(alignment - 1u));
}

static int media_open(struct native_media *media, const char *directory, int format)
{
    struct stat st;
    enum fwlab_nfc_api_result result;

    media->directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (media->directory_fd < 0)
        return 0;
    media->arena = allocate_arena(fwlab_file_nand_v0_arena_alignment(),
                                  fwlab_file_nand_v0_arena_size());
    if (!media->arena)
        return 0;
    if (format) {
        result = fwlab_file_nand_v0_posix_format(media->arena,
            fwlab_file_nand_v0_arena_size(), media->directory_fd, "nand.bin",
            media->uuid, &media->file, &media->holder);
    } else {
        if (fstatat(media->directory_fd, "nand.bin", &st, AT_SYMLINK_NOFOLLOW) ||
            !S_ISREG(st.st_mode))
            return 0;
        media->holder.device = (uint64_t)st.st_dev;
        media->holder.inode = (uint64_t)st.st_ino;
        memcpy(media->holder.media_uuid, media->uuid, sizeof(media->uuid));
        result = fwlab_file_nand_v0_posix_restart(media->arena,
            fwlab_file_nand_v0_arena_size(), media->directory_fd, "nand.bin",
            &media->holder, &media->file);
    }
    if (result != FWLAB_NFC_API_OK) {
        fprintf(stderr, "physical media open failed: %u\n", (unsigned)result);
        return 0;
    }
    return 1;
}

static int runtime_create(struct native_context *context,
                          struct native_media *media, int format)
{
    struct j0_host_factory factory = { native_host_bind, context };
    struct j0_runtime_config config;
    uint32_t iteration;

    context->runtime = calloc(1, sizeof(*context->runtime));
    if (!context->runtime)
        return 0;
    memset(&config, 0, sizeof(config));
    config.version = J0_RUNTIME_VERSION;
    config.size = (uint16_t)sizeof(config);
    memcpy(config.media_uuid, media->uuid, sizeof(config.media_uuid));
    config.file = media->file;
    config.media_mode = format ? J0_MEDIA_FORMAT : J0_MEDIA_RECOVER;
    config.generation = context->epoch;
    config.execution_epoch = context->epoch;
    config.volatile_nonce_seed = context->epoch;
    config.host_factory = &factory;
    if (j0_runtime_init(context->runtime, &config) != FWLAB_SPINE_V0_OK)
        return 0;
    for (iteration = 0; iteration < 800000; ++iteration) {
        uint32_t units;
        if (j0_runtime_step(context->runtime, 3, &units) != FWLAB_SPINE_V0_OK)
            return 0;
        if (context->runtime->ready)
            return 1;
    }
    return 0;
}

static void decode_capture(struct native_slot *slot)
{
    struct fwlab_nvme_command *command = &slot->command;
    const uint8_t *sqe = slot->capture.sqe;
    uint32_t index;

    memset(command, 0, sizeof(*command));
    command->version = FWLAB_NVME_COMMAND_VERSION;
    command->size = (uint16_t)sizeof(*command);
    command->handle.instance_nonce = slot->capture.function_nonce;
    command->handle.command_uid = slot->capture.origin_uid;
    command->handle.controller_epoch = slot->capture.controller_epoch;
    command->handle.generation = slot->capture.controller_epoch;
    command->origin.word[0] = slot->capture.function_nonce ^ UINT64_C(0x4f52494700000000);
    command->origin.word[1] = slot->capture.origin_uid;
    command->trace_cookie = slot->capture.origin_uid;
    command->safety_generation = slot->capture.controller_epoch;
    command->namespace_id = get_le32(sqe + 4);
    command->command_dword2 = get_le32(sqe + 8);
    command->command_dword3 = get_le32(sqe + 12);
    for (index = 0; index < 6; ++index)
        command->command_dword10_15[index] = get_le32(sqe + 40 + index * 4);
    command->opcode = sqe[0];
    command->queue_class = slot->capture.queue_id == 0 ? FWLAB_NVME_QUEUE_ADMIN
                                                      : FWLAB_NVME_QUEUE_IO;
    command->fuse = sqe[1] & 3u;
    command->data_pointer_format = sqe[1] >> 6;
    command->command_flags_reserved = sqe[1] & 0x3cu;
    command->metadata_address_present = get_le64(sqe + 16) != 0;
    command->data_address_present = get_le64(sqe + 24) != 0 || get_le64(sqe + 32) != 0;
}

static int receive_command(struct native_context *context)
{
    struct fwlab_m4_native_message message;
    struct native_slot *available = NULL;
    uint32_t index;

    native_message_init(context, NULL, FWLAB_M4_NATIVE_NEXT, &message);
    if (native_exchange(context, &message))
        return -1;
    if (message.event != FWLAB_M4_NATIVE_COMMAND)
        return 0;
    if (message.controller_epoch != context->epoch || !message.origin_uid)
        return -1;
    for (index = 0; index < NATIVE_COMMANDS; ++index) {
        struct native_slot *slot = &context->slot[index];
        if (slot->occupied && slot->capture.origin_uid == message.origin_uid)
            return memcmp(slot->capture.sqe, message.sqe, sizeof(message.sqe)) ? -1 : 0;
        if (!slot->occupied && !available)
            available = slot;
    }
    if (!available)
        return 0;
    memset(available, 0, sizeof(*available));
    available->occupied = 1;
    available->capture = message;
    decode_capture(available);
    printf("CAPTURE epoch=%u uid=%" PRIu64 " q=%u op=%02x cdw10=%08x cdw11=%08x "
           "cdw12=%08x cdw13=%08x\n",
           context->epoch, (uint64_t)message.origin_uid, message.queue_id,
           available->command.opcode, available->command.command_dword10_15[0],
           available->command.command_dword10_15[1],
           available->command.command_dword10_15[2],
           available->command.command_dword10_15[3]);
    return 1;
}

static int admit_commands(struct native_context *context)
{
    uint32_t index;

    for (index = 0; index < NATIVE_COMMANDS; ++index) {
        struct native_slot *slot = &context->slot[index];
        enum fwlab_spine_result_v0 result;
        if (!slot->occupied || slot->admitted)
            continue;
        result = j0_runtime_admit_referenced(context->runtime, J0_PROFILE_LINUX_V1,
                                             &slot->command, &slot->ticket);
        if (result == FWLAB_SPINE_V0_OK) {
            slot->admitted = 1;
        } else if (result != FWLAB_SPINE_V0_NO_CAPACITY && result != FWLAB_SPINE_V0_IN_PROGRESS) {
            if (context->runtime->poisoned || slot->command.transport_fault) {
                fprintf(stderr, "admission failed uid=%" PRIu64 " result=%u\n",
                        (uint64_t)slot->capture.origin_uid, (unsigned)result);
                return 0;
            }
            slot->command.transport_fault = FWLAB_NVME_TRANSPORT_UNSAFE_GRAPH;
            slot->bytes = 0;
            slot->direction = 0;
        }
    }
    return 1;
}

static int publication_observe(struct native_context *context,
                               struct native_slot *slot, int closing)
{
    struct fwlab_m4_native_message message;
    int result;

    native_message_init(context, slot,
        closing || slot->publication_started ? FWLAB_M4_NATIVE_PUBLISH_QUERY
                                             : FWLAB_M4_NATIVE_PUBLISH, &message);
    message.completion_uid = slot->completion.lease_uid;
    message.result_dword0 = slot->intent.result_dword0;
    message.status_code = slot->intent.status_code;
    message.status_code_type = slot->intent.status_code_type;
    message.do_not_retry = slot->intent.do_not_retry;
    message.more = slot->intent.more;
    message.retry_delay = slot->intent.command_retry_delay;
    slot->publication_started = 1;
    result = native_exchange(context, &message);
    if (result)
        return message.result == INT32_MIN ? 1 : 0;
    if (message.publication == FWLAB_M4_NATIVE_UNPUBLISHED) {
        slot->publication_started = 0;
        return 1;
    }
    if (message.publication != FWLAB_M4_NATIVE_COMMITTED &&
        message.publication != FWLAB_M4_NATIVE_DISCARDED)
        return 0;
    slot->publication_known = (uint8_t)message.publication;
    return 1;
}

static int finish_commands(struct native_context *context, int closing)
{
    uint32_t index;

    for (index = 0; index < NATIVE_COMMANDS; ++index) {
        struct native_slot *slot = &context->slot[index];
        struct fwlab_m4_native_message message;
        enum fwlab_spine_result_v0 result;
        if (!slot->occupied || !slot->admitted)
            continue;
        if (!slot->completion_acquired) {
            if (closing)
                continue;
            result = j0_runtime_publication_acquire(context->runtime, &slot->ticket,
                                                    &slot->completion, &slot->intent);
            if (result == FWLAB_SPINE_V0_IN_PROGRESS)
                continue;
            if (result != FWLAB_SPINE_V0_OK)
                return 0;
            slot->completion_acquired = 1;
        }
        if (!slot->publication_known && !publication_observe(context, slot, closing))
            return 0;
        if (!slot->publication_known ||
            (!closing && slot->publication_known == FWLAB_M4_NATIVE_DISCARDED))
            continue;
        if (!slot->firmware_retired) {
            result = j0_runtime_publication_finish(context->runtime, &slot->ticket,
                &slot->completion, slot->publication_known == FWLAB_M4_NATIVE_COMMITTED
                    ? FWLAB_SPINE_PUBLICATION_V1_COMMITTED : FWLAB_SPINE_PUBLICATION_V1_DISCARDED);
            if (result == FWLAB_SPINE_V0_IN_PROGRESS)
                continue;
            if (result != FWLAB_SPINE_V0_OK)
                return 0;
            slot->firmware_retired = 1;
        }
        native_message_init(context, slot, FWLAB_M4_NATIVE_RETIRE, &message);
        if (native_exchange(context, &message))
            continue;
        printf("COMPLETE epoch=%u uid=%" PRIu64 " sct=%u sc=%u publication=%u\n",
               slot->capture.controller_epoch, (uint64_t)slot->capture.origin_uid,
               slot->intent.status_code_type, slot->intent.status_code, slot->publication_known);
        memset(slot, 0, sizeof(*slot));
    }
    return 1;
}

static int runtime_close(struct native_context *context)
{
    uint32_t iteration;
    enum fwlab_spine_result_v0 start = j0_runtime_close_start(context->runtime);

    if (start != FWLAB_SPINE_V0_OK) {
        fprintf(stderr, "drain start result=%u\n", (unsigned)start);
        return 0;
    }
    for (iteration = 0; iteration < 800000; ++iteration) {
        uint32_t units;
        struct j0_close_status closed;
        enum fwlab_spine_result_v0 result;
        if (!finish_commands(context, 1)) {
            fprintf(stderr, "drain publication failed iteration=%u\n", iteration);
            return 0;
        }
        result = j0_runtime_close_query(context->runtime, &closed);
        if (result != FWLAB_SPINE_V0_OK)
            return 0;
        result = j0_runtime_fini(context->runtime);
        if (result == FWLAB_SPINE_V0_OK) {
            printf("EPOCH_DRAINED epoch=%u quiescent=%u authorities=%u dma=%u "
                   "buffers=%u block=%u nfc=%u\n", context->epoch, closed.quiescent,
                   closed.host_authorities, closed.dma_operations, closed.buffers,
                   closed.block_operations, closed.nfc_operations);
            free(context->runtime);
            context->runtime = NULL;
            memset(context->slot, 0, sizeof(context->slot));
            return 1;
        }
        if (result != FWLAB_SPINE_V0_IN_PROGRESS) {
            fprintf(stderr, "drain fini result=%u iteration=%u\n", (unsigned)result, iteration);
            return 0;
        }
        result = j0_runtime_step(context->runtime, 48, &units);
        if (result != FWLAB_SPINE_V0_OK) {
            fprintf(stderr, "drain step result=%u iteration=%u units=%u cursor=%u "
                    "active=%u intents=%u buffers=%u\n", (unsigned)result, iteration,
                    units, context->runtime->fair_cursor, context->runtime->active_admissions,
                    context->runtime->retained_intents, context->runtime->buffer.active_leases);
            return 0;
        }
    }
    fprintf(stderr, "drain progress bound exhausted\n");
    return 0;
}

static int firmware_loop(struct native_context *context, struct native_media *media)
{
    const struct timespec idle = { 0, 100000 };

    while (!stop_requested) {
        struct fwlab_m4_native_message message;
        uint32_t units;
        native_message_init(context, NULL, FWLAB_M4_NATIVE_STATUS, &message);
        if (native_exchange(context, &message))
            return 0;
        if (message.event == FWLAB_M4_NATIVE_RESET) {
            uint32_t next_epoch = message.controller_epoch;
            if (!runtime_close(context)) {
                fprintf(stderr, "epoch drain failed at %u\n", context->epoch);
                return 0;
            }
            context->epoch = next_epoch;
            if (!runtime_create(context, media, 0))
                return 0;
            native_message_init(context, NULL, FWLAB_M4_NATIVE_RESET_ACK, &message);
            if (native_exchange(context, &message))
                return 0;
            printf("EPOCH_READY epoch=%u\n", context->epoch);
            continue;
        }
        if (receive_command(context) < 0 || !admit_commands(context))
            return 0;
        if (j0_runtime_step(context->runtime, 48, &units) != FWLAB_SPINE_V0_OK ||
            !finish_commands(context, 0)) {
            fprintf(stderr, "firmware progress failed at epoch %u\n", context->epoch);
            return 0;
        }
        nanosleep(&idle, NULL);
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *device = NULL, *directory = NULL, *uuid = NULL, *digest = NULL;
    struct native_context *context = NULL;
    struct native_media media;
    struct fwlab_m4_native_message message;
    struct sigaction action;
    struct stat st;
    uint8_t binding[32];
    int format = 0, index, result = 1;

    memset(&media, 0, sizeof(media));
    media.directory_fd = -1;
    for (index = 1; index < argc; ++index) {
        if (!strcmp(argv[index], "--format")) { format = 1; continue; }
        if (index + 1 >= argc) goto usage;
        if (!strcmp(argv[index], "--device")) device = argv[++index];
        else if (!strcmp(argv[index], "--media-dir")) directory = argv[++index];
        else if (!strcmp(argv[index], "--uuid")) uuid = argv[++index];
        else if (!strcmp(argv[index], "--binding-sha")) digest = argv[++index];
        else goto usage;
    }
    if (!device || strncmp(device, "/dev/fwlab-native-", 18) || !directory ||
        !hex_bytes(uuid, media.uuid, sizeof(media.uuid)) ||
        !hex_bytes(digest, binding, sizeof(binding)))
        goto usage;
    setvbuf(stdout, NULL, _IOLBF, 0);
    context = calloc(1, sizeof(*context));
    if (!context) goto done;
    context->descriptor = open(device, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    if (context->descriptor < 0 || fstat(context->descriptor, &st) || !S_ISCHR(st.st_mode))
        goto done;
    if (!media_open(&media, directory, format))
        goto done;
    native_message_init(context, NULL, FWLAB_M4_NATIVE_ATTACH, &message);
    memcpy(message.media_uuid, media.uuid, sizeof(media.uuid));
    memcpy(message.binding_sha256, binding, sizeof(binding));
    if (native_exchange(context, &message) || !message.function_nonce || !message.controller_epoch)
        goto done;
    context->function_nonce = message.function_nonce;
    context->epoch = message.controller_epoch;
    if (!runtime_create(context, &media, format))
        goto done;
    native_message_init(context, NULL, FWLAB_M4_NATIVE_RESET_ACK, &message);
    if (native_exchange(context, &message))
        goto done;
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    printf("NATIVE_READY function=%" PRIu64 " epoch=%u\n", context->function_nonce, context->epoch);
    result = firmware_loop(context, &media) ? 0 : 1;
done:
    if (context && context->runtime && context->runtime->magic == J0_RUNTIME_MAGIC) {
        native_message_init(context, NULL, FWLAB_M4_NATIVE_REVOKE, &message);
        (void)native_exchange(context, &message);
        if (!runtime_close(context))
            result = 1;
    }
    if (media.file)
        (void)fwlab_file_nand_v0_close(media.file);
    free(media.arena);
    if (media.directory_fd >= 0)
        close(media.directory_fd);
    if (context && context->descriptor >= 0)
        close(context->descriptor);
    free(context);
    if (result)
        fprintf(stderr, "native firmware stopped with an error\n");
    return result;
usage:
    fprintf(stderr, "usage: %s --device /dev/fwlab-native-BDF --media-dir DIR "
                    "--uuid 32hex --binding-sha 64hex [--format]\n", argv[0]);
    return 2;
}
