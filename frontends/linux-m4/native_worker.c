/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _GNU_SOURCE
#include "native_internal.h"
#include "native_owner.h"

#ifndef FWLAB_NATIVE_SCALED
#define FWLAB_NATIVE_SCALED 0
#endif
#if FWLAB_NATIVE_SCALED != 0 && FWLAB_NATIVE_SCALED != 1
#error "FWLAB_NATIVE_SCALED must be 0 or 1"
#endif
#if FWLAB_NATIVE_SCALED
#include "native_scaled_media.h"
#endif
#ifndef FWLAB_NATIVE_PUMP
#define FWLAB_NATIVE_PUMP 0
#endif
#if (FWLAB_NATIVE_PUMP != 0 && FWLAB_NATIVE_PUMP != 1) || \
    (FWLAB_NATIVE_PUMP && !FWLAB_NATIVE_SCALED)
#error "FWLAB_NATIVE_PUMP requires the explicitly selected scaled worker"
#endif
#ifndef FWLAB_NATIVE_LARGE
#define FWLAB_NATIVE_LARGE 0
#endif
#if (FWLAB_NATIVE_LARGE != 0 && FWLAB_NATIVE_LARGE != 1) || \
    (FWLAB_NATIVE_LARGE && (!FWLAB_NATIVE_SCALED || !FWLAB_NATIVE_PUMP))
#error "FWLAB_NATIVE_LARGE requires the selected scaled PUMP worker"
#endif
#ifndef FWLAB_NATIVE_MQ2
#define FWLAB_NATIVE_MQ2 0
#endif
#if (FWLAB_NATIVE_MQ2 != 0 && FWLAB_NATIVE_MQ2 != 1) || \
    (FWLAB_NATIVE_MQ2 && !FWLAB_NATIVE_LARGE)
#error "FWLAB_NATIVE_MQ2 requires the selected large scaled PUMP worker"
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

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

#if !FWLAB_NATIVE_SCALED
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
#endif

static uint32_t runtime_iteration_limit(const struct native_context *context)
{
    const struct j0_runtime *runtime = context->runtime;
    uint64_t lbas = runtime->ready ? runtime->volume.lba_count :
        runtime->config.media_mode == J0_MEDIA_FORMAT ? runtime->config.format_lba_count :
                                                      runtime->config.expected_lba_count;

    /* Only startup/drain allowances grow. In-flight work, per-step budgets,
     * lifecycle limits and the ready firmware loop remain unchanged. */
    return lbas > UINT64_C(256) * 2048u ? UINT32_C(1000000000) : 800000u;
}

static int runtime_progress(const struct native_context *context,
    const char *phase, uint32_t iteration, uint64_t started, uint64_t *last)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    if (!iteration || (uint64_t)now.tv_sec - *last >= 30u) {
        printf("NATIVE_RUNTIME_PROGRESS|phase=%s|epoch=%u|iterations=%u|elapsed_s=%" PRIu64 "\n",
               phase, context->epoch, iteration, (uint64_t)now.tv_sec - started);
        *last = (uint64_t)now.tv_sec;
    }
    return 1;
}

static void runtime_complete(const struct native_context *context,
    const char *phase, uint32_t iterations, const struct timespec *started)
{
    struct timespec finished;
    int64_t elapsed_ns;

    if (clock_gettime(CLOCK_MONOTONIC, &finished))
        return;
    elapsed_ns = (int64_t)(finished.tv_sec - started->tv_sec) * INT64_C(1000000000) +
        finished.tv_nsec - started->tv_nsec;
    if (elapsed_ns < 0)
        return;
    printf("NATIVE_RUNTIME_COMPLETE|phase=%s|epoch=%u|iterations=%u|elapsed_ns=%" PRIu64 "\n",
           phase, context->epoch, iterations, (uint64_t)elapsed_ns);
}

int native_runtime_create(struct native_context *context,
                          struct native_media *media, int format)
{
    struct j0_host_factory factory = { native_host_bind, context };
    struct j0_runtime_config config;
    struct timespec now;
    uint64_t started, last;
    uint32_t iteration, limit;

    if (context->runtime || context->next_runtime_seed >= UINT64_C(0xfffff))
        return 0;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    context->runtime = calloc(1, sizeof(*context->runtime));
    if (!context->runtime)
        return 0;
    memset(&config, 0, sizeof(config));
    config.version = J0_RUNTIME_VERSION;
    config.size = (uint16_t)sizeof(config);
    memcpy(config.media_uuid, media->uuid, sizeof(config.media_uuid));
    config.file = media->file;
    config.media_binding = media->media_binding;
    config.storage_factory = media->storage_factory;
    config.format_lba_count = format ? media->format_lba_count : 0;
    config.expected_lba_count = format ? 0 : media->expected_lba_count;
    config.media_mode = format ? J0_MEDIA_FORMAT : J0_MEDIA_RECOVER;
    config.budget_profile = media->storage_factory ? J0_BUDGET_SCALE : J0_BUDGET_LAB;
    config.generation = context->epoch;
    config.execution_epoch = context->epoch;
    /* Failed pre-grant construction may retry the same unpublished controller
     * epoch. Its internal objects still need fresh, non-reused identities. */
    config.volatile_nonce_seed = ++context->next_runtime_seed;
    config.host_factory = &factory;
    if (context->host_profile_id == FWLAB_M4_HOST_PROFILE_LARGE_SERIAL ||
        context->host_profile_id == FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL) {
        config.linux_limits = context->host_profile_id == FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL
            ? fwlab_linux_profile_mq2_limits() : fwlab_linux_profile_large_limits();
        config.buffer_profile = J0_BUFFER_LARGE_SERIAL;
    }
    if (j0_runtime_init(context->runtime, &config) != FWLAB_SPINE_V0_OK) {
        /* J0 has released all partial construction state; no lower step ran. */
        free(context->runtime);
        context->runtime = NULL;
        memset(&context->buffer, 0, sizeof(context->buffer));
        return 0;
    }
    started = last = (uint64_t)now.tv_sec;
    limit = runtime_iteration_limit(context);
    for (iteration = 0; iteration < limit; ++iteration) {
        uint32_t units;
#if FWLAB_NATIVE_LARGE
        if (context->recovery_pump && !(iteration & 1023u)) {
            int service_result = 0;
            if (native_pump(context, &service_result) || service_result)
                return 0;
        }
#endif
        if (!(iteration & 1023u) && !runtime_progress(context,
                format ? "format" : "recovery", iteration, started, &last))
            return 0;
        if (j0_runtime_step(context->runtime, 3, &units) != FWLAB_SPINE_V0_OK)
            return 0;
        if (context->runtime->ready) {
            runtime_complete(context, format ? "format" : "recovery", iteration + 1u, &now);
            return 1;
        }
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
           "cdw12=%08x cdw13=%08x cid=%u\n",
           context->epoch, (uint64_t)message.origin_uid, message.queue_id,
           available->command.opcode, available->command.command_dword10_15[0],
           available->command.command_dword10_15[1],
           available->command.command_dword10_15[2],
           available->command.command_dword10_15[3], (unsigned)message.command_id);
    context->progressed = 1;
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
            context->progressed = 1;
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

int native_canary_control(struct native_context *context,
                          struct fwlab_m4_canary_message *message)
{
    uint32_t operation = message->operation;
    int result;

    message->version = FWLAB_M4_CANARY_VERSION;
    message->size = (uint32_t)sizeof(*message);
    message->function_nonce = context->function_nonce;
    message->result = INT32_MIN;
    memset(context->canary.probe_bytes, 0x3c, sizeof(context->canary.probe_bytes));
    message->data_pointer = (uintptr_t)context->canary.probe_bytes;
    result = ioctl(context->descriptor, FWLAB_M4_CANARY_EXCHANGE, message);
    if (result < 0 || message->result)
        return -1;
    if (operation == FWLAB_M4_CANARY_ARM) {
        memset(&context->canary, 0, sizeof(context->canary));
        context->canary.capture = 1;
    } else if (operation == FWLAB_M4_CANARY_HOLD) {
        if (!context->canary.saved)
            return -1;
        context->canary.hold = 1;
        context->canary.held_origin = 0;
    } else if (operation == FWLAB_M4_CANARY_RELEASE) {
        context->canary.hold = 0;
    } else if (operation == FWLAB_M4_CANARY_DISARM) {
        memset(&context->canary, 0, sizeof(context->canary));
    }
    if (operation == FWLAB_M4_CANARY_PROBE) {
        if (!context->runtime || !context->canary.saved || !context->canary.held_origin)
            return -1;
        message->firmware_lease_result = (int32_t)j0_runtime_publication_finish(
            context->runtime, &context->canary.ticket, &context->canary.lease,
            FWLAB_SPINE_PUBLICATION_V1_COMMITTED);
    }
    message->old_completion_uid = context->canary.saved ? context->canary.lease.lease_uid : 0;
    message->new_completion_uid = context->canary.new_lease_uid;
    message->data_pointer = 0;
    return 0;
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
            if (context->canary.capture && !context->canary.saved &&
                !slot->capture.queue_id && slot->command.opcode == 6 && slot->bytes == 4096) {
                context->canary.ticket = slot->ticket;
                context->canary.lease = slot->completion;
                context->canary.saved = 1;
                context->canary.capture = 0;
            }
        }
        if (!closing && context->canary.hold && !slot->capture.queue_id &&
            slot->command.opcode == 6 && slot->bytes == 4096) {
            if (!context->canary.held_origin) {
                struct fwlab_m4_canary_message held = { 0 };
                held.operation = FWLAB_M4_CANARY_HELD;
                held.origin_uid = slot->capture.origin_uid;
                held.controller_epoch = slot->capture.controller_epoch;
                if (native_canary_control(context, &held))
                    return 0;
                context->canary.held_origin = slot->capture.origin_uid;
                context->canary.new_lease_uid = slot->completion.lease_uid;
            }
            if (context->canary.held_origin == slot->capture.origin_uid)
                continue;
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
        if (slot->frame_held)
            return 0; /* Kernel ingress cannot be reused before endpoint release. */
        native_message_init(context, slot, FWLAB_M4_NATIVE_RETIRE, &message);
        if (native_exchange(context, &message))
            continue;
        printf("COMPLETE epoch=%u uid=%" PRIu64 " sct=%u sc=%u publication=%u\n",
               slot->capture.controller_epoch, (uint64_t)slot->capture.origin_uid,
               slot->intent.status_code_type, slot->intent.status_code, slot->publication_known);
        memset(slot, 0, sizeof(*slot));
        context->progressed = 1;
    }
    return 1;
}

enum fwlab_spine_result_v0 native_runtime_close_step(
    struct native_context *context, uint32_t budget)
{
    struct j0_close_status closed;
    enum fwlab_spine_result_v0 result;
    uint32_t units;

    if (!context || !budget)
        return FWLAB_SPINE_V0_INVALID;
    if (!context->runtime)
        return FWLAB_SPINE_V0_OK;
    result = j0_runtime_close_start(context->runtime);
    if (result != FWLAB_SPINE_V0_OK)
        return result;
    if (!finish_commands(context, 1))
        return FWLAB_SPINE_V0_POISONED;
    result = j0_runtime_close_query(context->runtime, &closed);
    if (result != FWLAB_SPINE_V0_OK)
        return result;
    result = j0_runtime_fini(context->runtime);
    if (result == FWLAB_SPINE_V0_OK) {
        if (!native_frames_quiescent(context))
            return FWLAB_SPINE_V0_POISONED;
        context->last_closed = closed;
        context->last_closed.profiles_retired = context->runtime->profiles_retired;
        context->last_ftl_nonce = context->runtime->m3p_instance_nonce;
        context->last_nfc_nonce = context->runtime->nfc_instance_nonce;
        context->last_closed_epoch = context->epoch;
        printf("EPOCH_DRAINED epoch=%u quiescent=%u authorities=%u dma=%u "
               "buffers=%u block=%u nfc=%u\n", context->epoch, closed.quiescent,
               closed.host_authorities, closed.dma_operations, closed.buffers,
               closed.block_operations, closed.nfc_operations);
        free(context->runtime);
        context->runtime = NULL;
        memset(context->slot, 0, sizeof(context->slot));
        return FWLAB_SPINE_V0_OK;
    }
    if (result != FWLAB_SPINE_V0_IN_PROGRESS)
        return result;
    result = j0_runtime_step(context->runtime, budget, &units);
    return result == FWLAB_SPINE_V0_OK ? FWLAB_SPINE_V0_IN_PROGRESS : result;
}

static int runtime_close(struct native_context *context)
{
    struct timespec now;
    uint64_t started, last;
    uint32_t iteration, limit;

    if (!context->runtime)
        return 1;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return 0;
    started = last = (uint64_t)now.tv_sec;
    limit = runtime_iteration_limit(context);
    for (iteration = 0; iteration < limit; ++iteration) {
        if (!(iteration & 1023u) &&
            !runtime_progress(context, "drain", iteration, started, &last))
            return 0;
        enum fwlab_spine_result_v0 result = native_runtime_close_step(context, 48);
        if (result == FWLAB_SPINE_V0_OK) {
            runtime_complete(context, "drain", iteration + 1u, &now);
            return 1;
        }
        if (result != FWLAB_SPINE_V0_IN_PROGRESS) {
            fprintf(stderr, "drain result=%u iteration=%u\n", (unsigned)result, iteration);
            return 0;
        }
    }
    fprintf(stderr, "drain progress bound exhausted\n");
    return 0;
}

static int firmware_loop(struct native_context *context, struct native_media *media,
                         struct native_owner_server *server)
{
    const struct timespec idle = { 0, 100000 };

    while (!stop_requested) {
        struct fwlab_m4_native_message message;
        uint32_t units;
        int received, service_result = 0;
#if FWLAB_NATIVE_MQ2
        struct fwlab_execution_progress progress = {0};
        context->progressed = 0;
#endif
#if FWLAB_NATIVE_PUMP
        /* Control/FLR/PBA service must continue even with no runtime/owner.
         * A service fault latches the existing reset path; it is not fd loss. */
        if (native_pump(context, &service_result))
            return 0;
#endif
        native_message_init(context, NULL, FWLAB_M4_NATIVE_STATUS, &message);
        if (native_exchange(context, &message))
            return 0;
        if (message.event == FWLAB_M4_NATIVE_RESET && context->runtime &&
            (!server || !native_owner_blocks_commands(&server->owner))) {
            uint32_t next_epoch = message.controller_epoch;
            if (!runtime_close(context)) {
                fprintf(stderr, "epoch drain failed at %u\n", context->epoch);
                return 0;
            }
            context->epoch = next_epoch;
#if FWLAB_NATIVE_LARGE
            native_message_init(context, NULL, FWLAB_M4_NATIVE_DRAIN_ACK, &message);
            if (native_exchange(context, &message))
                return 0;
            context->recovery_pump = 1;
#endif
            int recovered = native_runtime_create(context, media, 0);
            context->recovery_pump = 0;
            if (!recovered)
                return 0;
            native_message_init(context, NULL, FWLAB_M4_NATIVE_RESET_ACK, &message);
            if (native_exchange(context, &message))
                return 0;
            printf("EPOCH_READY epoch=%u\n", context->epoch);
            continue;
        }
        if (server && !native_owner_server_poll(server))
            return 0;
        if (service_result || !context->runtime ||
            (server && native_owner_blocks_commands(&server->owner))) {
            nanosleep(&idle, NULL);
            continue;
        }
        received = receive_command(context);
        if (received < 0 || !admit_commands(context))
            return 0;
#if FWLAB_NATIVE_MQ2
        if (j0_runtime_step_report(context->runtime, 48, &units, &progress) != FWLAB_SPINE_V0_OK ||
#else
        if (j0_runtime_step(context->runtime, 48, &units) != FWLAB_SPINE_V0_OK ||
#endif
            !finish_commands(context, 0)) {
            fprintf(stderr, "firmware progress failed at epoch %u\n", context->epoch);
            return 0;
        }
        /* A new capture deserves its next progress turn without an artificial
         * wait. Mere occupied slots or budget consumption are not this signal. */
#if FWLAB_NATIVE_MQ2
        if (!context->progressed && !progress.advanced && !progress.runnable)
#else
        if (!received)
#endif
            nanosleep(&idle, NULL);
    }
    return 1;
}

int main(int argc, char **argv)
{
    const char *device = NULL, *directory = NULL, *uuid = NULL, *digest = NULL;
    const char *owner_directory = NULL;
    struct native_context *context = NULL;
    struct native_owner_server *server = NULL;
#if FWLAB_NATIVE_SCALED
    struct native_scaled_media media_owner;
    struct native_media *media = &media_owner.native;
#else
    struct native_media media_owner;
    struct native_media *media = &media_owner;
#endif
    struct fwlab_m4_native_message message;
    struct sigaction action;
    struct stat st;
    uint8_t binding[32], media_uuid[16];
    int format = 0, index, result = 1;
#if FWLAB_NATIVE_SCALED
    uint32_t logical_mib = NATIVE_SCALED_DEFAULT_MIB;
#endif

    memset(&media_owner, 0, sizeof(media_owner));
    media->directory_fd = -1;
    for (index = 1; index < argc; ++index) {
        if (!strcmp(argv[index], "--format")) { format = 1; continue; }
        if (index + 1 >= argc) goto usage;
        if (!strcmp(argv[index], "--device")) device = argv[++index];
        else if (!strcmp(argv[index], "--media-dir")) directory = argv[++index];
        else if (!strcmp(argv[index], "--uuid")) uuid = argv[++index];
        else if (!strcmp(argv[index], "--binding-sha")) digest = argv[++index];
        else if (!strcmp(argv[index], "--owner-dir")) owner_directory = argv[++index];
#if FWLAB_NATIVE_SCALED
        else if (!strcmp(argv[index], "--namespace-mib")) {
            const char *value = argv[++index];
            if (!strcmp(value, "64")) logical_mib = 64;
            else if (!strcmp(value, "256")) logical_mib = 256;
            else if (!strcmp(value, "65536")) logical_mib = 65536;
            else goto usage;
        }
#endif
        else goto usage;
    }
    if (!device || strncmp(device, "/dev/fwlab-native-", 18) || !directory ||
        !hex_bytes(uuid, media_uuid, sizeof(media_uuid)) ||
        !hex_bytes(digest, binding, sizeof(binding)))
        goto usage;
    setvbuf(stdout, NULL, _IOLBF, 0);
    context = calloc(1, sizeof(*context));
    if (!context) goto done;
    context->descriptor = -1;
    context->descriptor = open(device, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    if (context->descriptor < 0 || fstat(context->descriptor, &st) || !S_ISCHR(st.st_mode))
        goto done;
#if FWLAB_NATIVE_SCALED
    if (!native_scaled_media_open(&media_owner, context, directory, media_uuid, format, logical_mib))
        goto done;
#if FWLAB_NATIVE_LARGE
    if (native_attach_profile(context, FWLAB_NATIVE_MQ2
            ? FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL : FWLAB_M4_HOST_PROFILE_LARGE_SERIAL,
            FWLAB_M4_PRODUCER_PUMP, FWLAB_M4_MEDIA_SCALED, media->uuid, binding))
#elif FWLAB_NATIVE_PUMP
    if (native_attach_mode(context, FWLAB_M4_PRODUCER_PUMP,
            FWLAB_M4_MEDIA_SCALED, media->uuid, binding))
#else
    if (native_attach_explicit(context, FWLAB_M4_MEDIA_SCALED, media->uuid, binding))
#endif
        goto done;
#else
    memcpy(media->uuid, media_uuid, sizeof(media_uuid));
    if (!media_open(media, directory, format) ||
        native_attach_legacy(context, media->uuid, binding))
        goto done;
#endif
    if (!native_runtime_create(context, media, format))
        goto done;
    native_message_init(context, NULL, FWLAB_M4_NATIVE_RESET_ACK, &message);
    if (native_exchange(context, &message))
        goto done;
    if (owner_directory) {
        server = calloc(1, sizeof(*server));
        if (!server || !native_owner_server_open(server, context, media, owner_directory))
            goto done;
        printf("OWNER_CONTROL_READY directory=%s\n", owner_directory);
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    printf("NATIVE_READY function=%" PRIu64 " epoch=%u\n", context->function_nonce, context->epoch);
    result = firmware_loop(context, media, server) ? 0 : 1;
done:
    if (server) {
        native_owner_server_close(server);
        free(server);
    }
    if (context && context->runtime) {
        if (context->runtime->magic != J0_RUNTIME_MAGIC)
            goto unresolved_close;
        native_message_init(context, NULL, FWLAB_M4_NATIVE_REVOKE, &message);
        (void)native_exchange(context, &message);
        if (!runtime_close(context))
            goto unresolved_close;
    }
    if (context && !native_frame_storage_fini(context))
        goto unresolved_close;
#if FWLAB_NATIVE_SCALED
    /* The server is stopped and no reset/grant can reuse the process-lived
     * holder now. A NULL runtime during NO_OWNER alone was not permission. */
    if (!native_scaled_media_close(&media_owner))
        goto unresolved_close;
#else
    if (media->file && fwlab_file_nand_v0_close(media->file) != FWLAB_NFC_API_OK)
        goto unresolved_close;
    free(media->arena);
    if (media->directory_fd >= 0)
        close(media->directory_fd);
#endif
    if (context && context->descriptor >= 0)
        close(context->descriptor);
    free(context);
    if (result)
        fprintf(stderr, "native firmware stopped with an error\n");
    return result;
unresolved_close:
    /* Do not free the media/factory/context beneath unresolved internal work.
     * Closing the HIF descriptor quarantines an accepted attachment. Process
     * exit reclaims memory; this is an error, never a clean-drain certificate. */
    fprintf(stderr, "native shutdown unresolved; retaining objects until process exit\n");
    if (context && context->descriptor >= 0)
        (void)close(context->descriptor);
    fflush(stderr);
    _Exit(1);
usage:
    fprintf(stderr, "usage: %s --device /dev/fwlab-native-BDF --media-dir DIR "
                    "--uuid 32hex --binding-sha 64hex [--format] [--owner-dir DIR]"
#if FWLAB_NATIVE_SCALED
                    " [--namespace-mib 64|256|65536]"
#endif
                    "\n", argv[0]);
    return 2;
}
