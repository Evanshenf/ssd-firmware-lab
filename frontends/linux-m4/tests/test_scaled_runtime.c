/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#define _GNU_SOURCE
/* Preload headers before renaming only the production CLI entry. No private
 * structure member named main is changed, and no executor source is copied. */
#include "../native_scaled_media.h"
#include "../native_owner.h"
#include "../../../kernel/m4-native/m4_attach_identity.h"
#define main native_legacy_cli_not_called
#include "../native_worker.c"
#undef main

#include <linux/magic.h>
#include <stdarg.h>
#include <sys/vfs.h>

#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "NATIVE_SCALED %s:%d: %s (errno=%d)\n", __FILE__, __LINE__, #x, errno); \
    exit(1); \
} } while (0)
#define OFFLINE_DESCRIPTOR (-179)
#define HOST_ROWS 4u

struct host_row {
    struct fwlab_m4_native_message capture;
    uint8_t bytes[FWLAB_M4_NATIVE_MAX_BYTES];
    uint32_t direction, length, copied, code, code_type;
    uint8_t shaped, dma_done, dma_retired, authority_released, published, retired;
};
static struct {
    struct host_row row[HOST_ROWS];
    uint32_t count, next, active, iterations, ioctls, dma_in, dma_out;
    uint8_t occupied;
    uint64_t next_uid, function;
    uint32_t epoch;
    uint32_t pump_ticks, pump_captures, pump_losses, reset_acks;
    uint8_t delivered, service_fault_once, reset_pending;
} host;
static struct fwlab_m4_attachment attached_identity;
static unsigned owner_identity_fault;

/* Only the ioctl boundary is fake. It owns Host byte buffers/transport tuples,
 * never NAND media, mappings, namespace data or storage-success decisions. */
int __wrap_ioctl(int descriptor, unsigned long request, ...)
{
    struct fwlab_m4_native_message *message;
    struct host_row *row;
    va_list arguments;
    void *argument;

    if (descriptor != OFFLINE_DESCRIPTOR) {
        errno = ENOTTY;
        return -1;
    }
    va_start(arguments, request);
    if (request == FWLAB_M4_ATTACH_IDENTITY)
        argument = va_arg(arguments, struct fwlab_m4_attach_message *);
    else if (request == FWLAB_M4_ATTACH_MODE)
        argument = va_arg(arguments, struct fwlab_m4_attach_mode_message *);
    else if (request == FWLAB_M4_PUMP)
        argument = va_arg(arguments, struct fwlab_m4_pump_message *);
    else if (request == FWLAB_M4_OWNER_EXCHANGE)
        argument = va_arg(arguments, struct fwlab_m4_owner_message *);
    else if (request == FWLAB_M4_NATIVE_EXCHANGE)
        argument = va_arg(arguments, struct fwlab_m4_native_message *);
    else
        argument = va_arg(arguments, void *);
    va_end(arguments);
    if (request == FWLAB_M4_ATTACH_MODE) {
        struct fwlab_m4_attach_mode_message *attach = argument;
        REQUIRE(FWLAB_NATIVE_PUMP && fwlab_m4_attach_mode_request_valid(attach));
        attach->result = fwlab_m4_attach_pin_mode(&attached_identity,
            FWLAB_M4_PRODUCER_PUMP, attach->producer_mode,
            attach->media_format_version, attach->media_uuid, attach->binding_sha256);
        if (!attach->result) {
            attach->function_nonce = host.function;
            attach->controller_epoch = host.epoch;
        }
        return 0;
    }
    if (request == FWLAB_M4_PUMP) {
        struct fwlab_m4_pump_message *pump = argument;
        REQUIRE(FWLAB_NATIVE_PUMP && fwlab_m4_pump_request_valid(pump));
        REQUIRE(attached_identity.media_format_version == FWLAB_M4_MEDIA_SCALED &&
                pump->function_nonce == host.function);
        ++host.pump_ticks;
        pump->result = 0;
        if (host.service_fault_once) {
            REQUIRE(!host.occupied && !host.next);
            host.service_fault_once = 0;
            host.reset_pending = 1;
            pump->service_result = -EIO;
            return 0;
        }
        if (!host.reset_pending && !host.occupied && host.next < host.count) {
            host.active = host.next++;
            host.occupied = 1;
            host.delivered = 0;
            ++host.pump_captures;
            pump->captured = 1;
        }
        if (pump->captured && host.pump_losses) {
            --host.pump_losses;
            /* A retained command survives a failed reply. The next pump
             * observes zero capture; NEXT must still deliver this exact row. */
            memset(pump, 0x5a, sizeof(*pump) / 2);
            errno = EFAULT;
            return -1;
        }
        return 0;
    }
    if (request == FWLAB_M4_ATTACH_IDENTITY) {
        struct fwlab_m4_attach_message *attach = argument;
        REQUIRE(fwlab_m4_attach_request_valid(attach));
        attach->result = fwlab_m4_attach_pin(&attached_identity,
            attach->media_format_version, attach->media_uuid, attach->binding_sha256);
        if (!attach->result) {
            attach->media_format_version = attached_identity.media_format_version;
            memcpy(attach->media_uuid, attached_identity.media_uuid, 16);
            memcpy(attach->binding_sha256, attached_identity.binding_sha256, 32);
            attach->function_nonce = host.function;
            attach->controller_epoch = host.epoch;
        }
        return 0;
    }
    if (request == FWLAB_M4_OWNER_EXCHANGE) {
        struct fwlab_m4_owner_message *owner = argument;
        REQUIRE(owner->version == FWLAB_M4_OWNER_VERSION && owner->size == sizeof(*owner));
        if (owner->operation != FWLAB_M4_OWNER_OBSERVE) { errno = ENOTTY; return -1; }
        owner->result = 0;
        owner->function_nonce = host.function;
        owner->controller_epoch = owner->execution_epoch = host.epoch;
        owner->owner_epoch = 1;
        owner->owner_kind = 1;
        owner->phase = FWLAB_M4_OWNER_OWNED;
        owner->generation = 1;
        owner->media_format_version = attached_identity.media_format_version;
        memcpy(owner->media_uuid, attached_identity.media_uuid, 16);
        memcpy(owner->binding_sha256, attached_identity.binding_sha256, 32);
        if (owner_identity_fault == 1) ++owner->media_format_version;
        if (owner_identity_fault == 2) owner->media_uuid[15] ^= 1;
        if (owner_identity_fault == 3) owner->binding_sha256[31] ^= 1;
        return 0;
    }
    if (request != FWLAB_M4_NATIVE_EXCHANGE) { errno = ENOTTY; return -1; }
    message = argument;
    REQUIRE(message && message->version == FWLAB_M4_NATIVE_VERSION &&
            message->size == sizeof(*message));
    REQUIRE(message->function_nonce == host.function);
    ++host.ioctls;
    message->result = 0;
    if (message->operation == FWLAB_M4_NATIVE_STATUS) {
        if (++host.iterations > 200000u) {
            errno = ETIMEDOUT;
            return -1;
        }
        message->event = host.reset_pending ? FWLAB_M4_NATIVE_RESET : FWLAB_M4_NATIVE_IDLE;
        message->controller_epoch = host.epoch + (host.reset_pending ? 1u : 0u);
        return 0;
    }
    if (message->operation == FWLAB_M4_NATIVE_RESET_ACK) {
        REQUIRE(FWLAB_NATIVE_PUMP && host.reset_pending && !host.occupied && !host.next);
        REQUIRE(message->controller_epoch == host.epoch + 1u);
        ++host.epoch;
        ++host.reset_acks;
        host.reset_pending = 0;
        /* These are still unsubmitted Host script rows. The fake Host creates
         * their successor transport epoch only after the real worker's ACK. */
        for (uint32_t index = 0; index < host.count; ++index)
            host.row[index].capture.controller_epoch = host.epoch;
        return 0;
    }
    REQUIRE(message->controller_epoch == host.epoch && !host.reset_pending);
    if (message->operation == FWLAB_M4_NATIVE_NEXT) {
        message->event = FWLAB_M4_NATIVE_IDLE;
#if FWLAB_NATIVE_PUMP
        if (host.occupied && !host.delivered) {
            host.delivered = 1;
            *message = host.row[host.active].capture;
            message->result = 0;
            message->event = FWLAB_M4_NATIVE_COMMAND;
        }
#else
        if (!host.occupied && host.next < host.count) {
            host.active = host.next++;
            host.occupied = 1;
            *message = host.row[host.active].capture;
            message->result = 0;
            message->event = FWLAB_M4_NATIVE_COMMAND;
        }
#endif
        return 0;
    }
    /* No real-ioctl fallback, even for unexpected control or canary requests. */
    switch (message->operation) {
    case FWLAB_M4_NATIVE_SHAPE:
    case FWLAB_M4_NATIVE_DMA:
    case FWLAB_M4_NATIVE_DMA_QUERY:
    case FWLAB_M4_NATIVE_DMA_RETIRE:
    case FWLAB_M4_NATIVE_AUTHORITY_RELEASE:
    case FWLAB_M4_NATIVE_PUBLISH:
    case FWLAB_M4_NATIVE_PUBLISH_QUERY:
    case FWLAB_M4_NATIVE_RETIRE:
        break;
    default:
        errno = ENOTTY;
        return -1;
    }
    REQUIRE(host.occupied);
    row = &host.row[host.active];
    REQUIRE(message->origin_uid == row->capture.origin_uid);
    switch (message->operation) {
    case FWLAB_M4_NATIVE_SHAPE:
        REQUIRE(row->length && message->bytes == row->length && message->direction == row->direction);
        REQUIRE(get_le64(row->capture.sqe + 24) == 4096 &&
                get_le64(row->capture.sqe + 32) == (row->length > 4096 ? 8192u : 0u));
        row->shaped = 1;
        message->authority_uid = UINT64_C(0xa000) + message->origin_uid;
        message->dma_uid = UINT64_C(0xd000) + message->origin_uid;
        return 0;
    case FWLAB_M4_NATIVE_DMA:
    case FWLAB_M4_NATIVE_DMA_QUERY:
        REQUIRE(row->shaped && message->bytes == row->length && message->direction == row->direction &&
                message->authority_uid == UINT64_C(0xa000) + message->origin_uid &&
                message->dma_uid == UINT64_C(0xd000) + message->origin_uid);
        if (!row->dma_done && message->operation == FWLAB_M4_NATIVE_DMA) {
            REQUIRE(message->data_pointer && row->length <= sizeof(row->bytes));
            if (row->direction == FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER) {
                memcpy((void *)(uintptr_t)message->data_pointer, row->bytes, row->length);
                ++host.dma_in;
            } else {
                REQUIRE(row->direction == FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST);
                memcpy(row->bytes, (const void *)(uintptr_t)message->data_pointer, row->length);
                ++host.dma_out;
            }
            row->copied = row->length;
            row->dma_done = 1;
        }
        message->dma_state = row->dma_done ? FWLAB_M4_NATIVE_DMA_DONE : FWLAB_M4_NATIVE_DMA_RESERVED;
        message->bytes_done = row->dma_done ? row->length : 0;
        return 0;
    case FWLAB_M4_NATIVE_DMA_RETIRE:
        REQUIRE(row->dma_done);
        row->dma_retired = 1;
        return 0;
    case FWLAB_M4_NATIVE_AUTHORITY_RELEASE:
        REQUIRE(row->shaped);
        row->authority_released = 1;
        return 0;
    case FWLAB_M4_NATIVE_PUBLISH:
        REQUIRE(message->completion_uid);
        row->code = message->status_code;
        row->code_type = message->status_code_type;
        row->published = 1;
        message->publication = FWLAB_M4_NATIVE_COMMITTED;
        return 0;
    case FWLAB_M4_NATIVE_PUBLISH_QUERY:
        message->publication = row->published ? FWLAB_M4_NATIVE_COMMITTED : FWLAB_M4_NATIVE_UNPUBLISHED;
        return 0;
    case FWLAB_M4_NATIVE_RETIRE:
        REQUIRE(row->published);
        row->retired = 1;
        host.occupied = 0;
        /* End the test's finite Host script through the real stop handler,
         * only after transport retirement. No READY/storage state is altered. */
        if (host.next == host.count)
            stop_signal(0);
        return 0;
    default:
        abort();
    }
}

static void put32(uint8_t *at, uint32_t value)
{
    for (unsigned byte = 0; byte < 4; ++byte)
        at[byte] = (uint8_t)(value >> (byte * 8u));
}

static void pattern(uint8_t *bytes, size_t count, uint8_t seed)
{
    for (size_t byte = 0; byte < count; ++byte)
        bytes[byte] = (uint8_t)(seed ^ (uint8_t)(byte * 37u) ^ (uint8_t)(byte >> 7));
}

static void script_begin(const struct native_context *context)
{
    uint64_t uid = host.next_uid;
    memset(&host, 0, sizeof(host));
    host.next_uid = uid;
    host.function = context->function_nonce;
    host.epoch = context->epoch;
    stop_requested = 0;
}

static void add_command(uint8_t opcode, uint64_t lba, uint8_t seed)
{
    struct host_row *row;
    struct fwlab_m4_native_message *capture;
    REQUIRE(host.count < HOST_ROWS);
    row = &host.row[host.count++];
    capture = &row->capture;
    capture->version = FWLAB_M4_NATIVE_VERSION;
    capture->size = sizeof(*capture);
    capture->function_nonce = host.function;
    capture->controller_epoch = host.epoch;
    capture->origin_uid = ++host.next_uid;
    capture->command_id = (uint32_t)capture->origin_uid;
    capture->queue_id = opcode == 6 ? 0 : 1;
    capture->sqe[0] = opcode;
    capture->sqe[2] = (uint8_t)capture->command_id;
    put32(capture->sqe + 4, 1);
    if (opcode == 6) {
        /* Identify Namespace CNS=0; capacity must come from ready-volume. */
        row->length = 4096;
        row->direction = FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST;
    } else if (opcode == 1 || opcode == 2) {
        put32(capture->sqe + 40, (uint32_t)lba);
        put32(capture->sqe + 44, (uint32_t)(lba >> 32));
        put32(capture->sqe + 48, 15);
        row->length = 8192;
        row->direction = opcode == 1 ? FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER
                                    : FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST;
    } else REQUIRE(opcode == 0); /* Flush has no data transfer. */
    /* Legal aligned PRP1/direct PRP2 values in the fake Host graph; the fake
     * does not claim to implement a real IOMMU or kernel graph validator. */
    if (row->length) put32(capture->sqe + 24, 4096);
    if (row->length > 4096) put32(capture->sqe + 32, 8192);
    if (opcode == 1) pattern(row->bytes, row->length, seed);
}

static void run_script(struct native_context *context, struct native_scaled_media *media)
{
#if FWLAB_NATIVE_PUMP
    host.service_fault_once = 1;
    host.pump_losses = 1;
#endif
    REQUIRE(firmware_loop(context, &media->native, NULL));
    REQUIRE(host.next == host.count && !host.occupied);
    for (uint32_t index = 0; index < host.count; ++index) {
        const struct host_row *row = &host.row[index];
        REQUIRE(row->retired && row->published && !row->code && !row->code_type);
        if (row->length)
            REQUIRE(row->copied == row->length && row->dma_retired && row->authority_released);
    }
    for (uint32_t index = 0; index < NATIVE_COMMANDS; ++index)
        REQUIRE(!context->slot[index].occupied);
#if FWLAB_NATIVE_PUMP
    REQUIRE(host.pump_captures == host.count && host.pump_ticks > host.count &&
            !host.pump_losses && host.reset_acks == 1 && !host.reset_pending);
    puts("NATIVE_PUMP_LOOP_PASS|actual_worker_reset_drain_ack=1|retained_NEXT_after_lost_reply=1|not_kernel_fault_proof=1");
#endif
}

static void check_identify(const struct host_row *row)
{
    REQUIRE(row->length == 4096 && row->copied == 4096);
    REQUIRE(get_le64(row->bytes) == NATIVE_SCALED_LBA_COUNT);
    REQUIRE(get_le64(row->bytes + 8) == NATIVE_SCALED_LBA_COUNT);
    REQUIRE(get_le64(row->bytes + 16) == NATIVE_SCALED_LBA_COUNT);
}

static uint64_t wall_ns(void)
{
    struct timespec now;
    REQUIRE(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static void phase_end(const char *phase, uint32_t epoch, uint64_t start)
{
    printf("NATIVE_SCALED_PHASE|phase=%s|epoch=%u|elapsed_ns=%" PRIu64 "|offline_not_kernel_timeout_proof=1\n",
           phase, epoch, wall_ns() - start);
}

static void close_epoch(struct native_context *context, struct native_scaled_media *media)
{
    uint64_t started = wall_ns();
    REQUIRE(runtime_close(context));
    phase_end("runtime-close", context->epoch, started);
    REQUIRE(!context->runtime && context->last_closed.quiescent &&
            !context->last_closed.host_authorities && !context->last_closed.dma_operations &&
            !context->last_closed.buffers && !context->last_closed.block_operations &&
            !context->last_closed.nfc_operations && !context->last_closed.pending &&
            !context->last_closed.pinned);
    started = wall_ns();
    REQUIRE(native_scaled_media_close(media));
    phase_end("media-close", context->epoch, started);
}

#if !FWLAB_NATIVE_SCALED
static void legacy_constructor_smoke(int directory_fd, const char *directory,
                                      const uint8_t uuid[16])
{
    struct native_media media = { .directory_fd = -1 };
    struct native_context *context = calloc(1, sizeof(*context));
    char path[512];
    int length = snprintf(path, sizeof(path), "%s/legacy", directory);
    REQUIRE(context && length > 0 && (size_t)length < sizeof(path));
    REQUIRE(mkdirat(directory_fd, "legacy", 0700) == 0);
    context->descriptor = OFFLINE_DESCRIPTOR;
    context->function_nonce = UINT64_C(0x4c45474143594c41);
    context->epoch = 1;
    memcpy(media.uuid, uuid, sizeof(media.uuid));
    REQUIRE(media_open(&media, path, 1));
    REQUIRE(native_runtime_create(context, &media, 1));
    REQUIRE(context->runtime->ready && context->runtime->config.file == media.file &&
            !context->runtime->config.media_binding && !context->runtime->config.storage_factory &&
            context->runtime->volume.lba_count == 2048);
    REQUIRE(runtime_close(context) && !context->runtime && context->last_closed.quiescent);
    REQUIRE(fwlab_file_nand_v0_close(media.file) == FWLAB_NFC_API_OK);
    free(media.arena);
    REQUIRE(unlinkat(media.directory_fd, "nand.bin", 0) == 0);
    REQUIRE(close(media.directory_fd) == 0);
    REQUIRE(unlinkat(directory_fd, "legacy", AT_REMOVEDIR) == 0);
    free(context);
    puts("LEGACY_NATIVE_CONSTRUCTOR_PASS|default_file_v0_M3P_C3=1|constructor_close_only=1");
}
#endif

int main(void)
{
    const char *root = getenv("FWLAB_TEST_MEDIA_DIR");
    const uint8_t uuid[16] = {0x4d,0x31,0x41,0x2d,0x53,0x43,0x41,0x4c,0x45,1,2,3,4,5,6,7};
    const uint8_t binding[32] = {0x4d,0x31,0x42,0x49,0x44,0x45,0x4e,0x54};
    char directory[512];
    struct statfs fs;
    struct stat before, after;
    struct native_context *context = calloc(1, sizeof(*context));
    struct native_scaled_media *media = calloc(1, sizeof(*media));
    struct fwlab_m4_native_message unsupported;
    struct native_owner owner;
    uint8_t expected[8192];
    uint64_t prior_ftl, prior_nfc, started;
    int directory_fd, name_length;

    REQUIRE(root && statfs(root, &fs) == 0 && (unsigned long)fs.f_type == TMPFS_MAGIC);
    REQUIRE((uint64_t)fs.f_bavail * (uint64_t)fs.f_bsize >= UINT64_C(200000000));
    REQUIRE(context && media);
    name_length = snprintf(directory, sizeof(directory), "%s/fwlab-native-scaled.XXXXXX", root);
    REQUIRE(name_length > 0 && (size_t)name_length < sizeof(directory));
    REQUIRE(mkdtemp(directory));
    directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    REQUIRE(directory_fd >= 0);
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("NATIVE_SCALED_OFFLINE_BEGIN|media=%s/nand.bin|64MiB|8KiB|fake_ioctl_only|no_attach_M5_or_throughput_claim\n", directory);
    context->descriptor = OFFLINE_DESCRIPTOR;
    context->function_nonce = UINT64_C(0x4d31414f46464c49);
    context->epoch = 1;
#if !FWLAB_NATIVE_SCALED
    legacy_constructor_smoke(directory_fd, directory, uuid);
#endif
    script_begin(context);
    native_message_init(context, NULL, UINT32_MAX, &unsupported);
    REQUIRE(native_exchange(context, &unsupported) == -ENOTTY && unsupported.result == INT32_MIN);
    errno = 0;
    REQUIRE(__wrap_ioctl(OFFLINE_DESCRIPTOR, 0UL, NULL) == -1 && errno == ENOTTY);
    REQUIRE(!native_scaled_media_open(media, context, directory, uuid, 0));
    REQUIRE(fstatat(directory_fd, "nand.bin", &before, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
    started = wall_ns();
    REQUIRE(native_scaled_media_open(media, context, directory, uuid, 1));
    phase_end("media-format", context->epoch, started);
#if FWLAB_NATIVE_PUMP
    REQUIRE(native_attach_mode(context, FWLAB_M4_PRODUCER_PUMP,
        FWLAB_M4_MEDIA_SCALED, media->native.uuid, binding) == 0);
#else
    REQUIRE(native_attach_explicit(context, FWLAB_M4_MEDIA_SCALED, media->native.uuid, binding) == 0);
#endif
    started = wall_ns();
    REQUIRE(native_runtime_create(context, &media->native, 1));
    phase_end("runtime-format", context->epoch, started);
    REQUIRE(context->runtime->ready && context->runtime->volume.lba_count == NATIVE_SCALED_LBA_COUNT &&
            !context->runtime->config.file && context->runtime->storage.context &&
            context->runtime->config.media_binding == &media->binding &&
            context->runtime->config.storage_factory == &media->factory);
    REQUIRE(!native_scaled_media_close(media) && media->opened && media->physical);
    REQUIRE(native_owner_init(&owner, context, &media->native) &&
            owner.port.stable.media_format_version == FWLAB_M4_MEDIA_SCALED &&
            owner.media == &media->native);
    for (owner_identity_fault = 1; owner_identity_fault <= 3; ++owner_identity_fault)
        REQUIRE(!native_owner_init(&owner, context, &media->native));
    owner_identity_fault = 0;
    REQUIRE(native_owner_init(&owner, context, &media->native));
    prior_ftl = context->runtime->m3p_instance_nonce;
    prior_nfc = context->runtime->nfc_instance_nonce;
    script_begin(context);
    add_command(6, 0, 0);
    add_command(1, NATIVE_SCALED_LBA_COUNT - 16u, 0x5a);
    add_command(0, 0, 0);
    add_command(2, NATIVE_SCALED_LBA_COUNT - 16u, 0);
    run_script(context, media);
    check_identify(&host.row[0]);
    pattern(expected, sizeof(expected), 0x5a);
    REQUIRE(memcmp(host.row[3].bytes, expected, sizeof(expected)) == 0);
    REQUIRE(host.dma_in == 1 && host.dma_out == 2);
    REQUIRE(fwlab_file_nand_v2_sequence(media->physical) > 0);
    /* An owner may hold this exact media pointer while runtime is absent.
     * Exercise reconstruction through it without closing/reopening the holder.
     * This is not a fake certificate or an executed kernel owner transition. */
    REQUIRE(runtime_close(context) && !context->runtime && media->opened);
    REQUIRE(owner.media == &media->native && media->physical);
    ++context->epoch;
    REQUIRE(native_runtime_create(context, owner.media, 0));
    REQUIRE(context->runtime->ready && context->runtime->volume.lba_count == NATIVE_SCALED_LBA_COUNT &&
            context->runtime->m3p_instance_nonce != prior_ftl && context->runtime->nfc_instance_nonce != prior_nfc);
    prior_ftl = context->runtime->m3p_instance_nonce;
    prior_nfc = context->runtime->nfc_instance_nonce;
    puts("NATIVE_RETAINED_MEDIA_PASS|same_holder_between_runtimes=1|not_kernel_owner_switch=1");
    close_epoch(context, media);
    REQUIRE(fstatat(directory_fd, "nand.bin", &before, AT_SYMLINK_NOFOLLOW) == 0);
    REQUIRE(!native_scaled_media_open(media, context, directory, uuid, 1));
    REQUIRE(fstatat(directory_fd, "nand.bin", &after, AT_SYMLINK_NOFOLLOW) == 0 &&
            after.st_ino == before.st_ino && after.st_size == before.st_size);
    ++context->epoch;
    started = wall_ns();
    REQUIRE(native_scaled_media_open(media, context, directory, uuid, 0));
    phase_end("media-recover", context->epoch, started);
    started = wall_ns();
    REQUIRE(native_runtime_create(context, &media->native, 0));
    phase_end("runtime-recover", context->epoch, started);
    REQUIRE(context->runtime->ready && context->runtime->volume.lba_count == NATIVE_SCALED_LBA_COUNT &&
            context->runtime->m3p_instance_nonce != prior_ftl && context->runtime->nfc_instance_nonce != prior_nfc);
    script_begin(context);
    add_command(6, 0, 0);
    add_command(2, NATIVE_SCALED_LBA_COUNT - 16u, 0);
    add_command(1, 0, 0xa6);
    add_command(2, 0, 0);
    run_script(context, media);
    check_identify(&host.row[0]);
    REQUIRE(memcmp(host.row[1].bytes, expected, sizeof(expected)) == 0);
    pattern(expected, sizeof(expected), 0xa6);
    REQUIRE(memcmp(host.row[3].bytes, expected, sizeof(expected)) == 0);
    REQUIRE(host.dma_in == 1 && host.dma_out == 3);
    close_epoch(context, media);
    REQUIRE(unlinkat(directory_fd, "nand.bin", 0) == 0);
    REQUIRE(close(directory_fd) == 0 && rmdir(directory) == 0);
    free(media);
    free(context);
    puts("NATIVE_SCALED_OFFLINE_PASS|actual_native_constructor_host_loop=1|real_FTL_PAGE2_physical_v2=1|capacity64MiB=1|SELF_Flush_Read_recovery_continue=1|new_epoch=1|runtime_media_released=1|no_kernel_format_claim=1");
    return 0;
}
