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
#include <pthread.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <sys/vfs.h>

#define REQUIRE(x) do { if (!(x)) { \
    fprintf(stderr, "NATIVE_SCALED %s:%d: %s (errno=%d)\n", __FILE__, __LINE__, #x, errno); \
    exit(1); \
} } while (0)
#define OFFLINE_DESCRIPTOR (-179)
#define HOST_ROWS 4u
#if FWLAB_NATIVE_LARGE
#define TEST_IO_BYTES 1048576u
#else
#define TEST_IO_BYTES 8192u
#endif
#define TEST_IO_LBAS (TEST_IO_BYTES / 512u)
#if FWLAB_NATIVE_MQ2
#define TEST_HOST_PROFILE FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL
#elif FWLAB_NATIVE_LARGE
#define TEST_HOST_PROFILE FWLAB_M4_HOST_PROFILE_LARGE_SERIAL
#else
#define TEST_HOST_PROFILE FWLAB_M4_HOST_PROFILE_SMALL
#endif

struct host_row {
    struct fwlab_m4_native_message capture;
    uint8_t bytes[TEST_IO_BYTES]; /* fake Host memory, not a DUT payload pool */
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
    uint32_t drain_acks, recovery_pumps;
    uint8_t delivered, service_fault_once, reset_pending;
    uint32_t shape_losses;
} host;
static struct fwlab_m4_attachment attached_identity;
static unsigned owner_identity_fault;
static enum native_nand_profile test_nand_profile = NATIVE_NAND_R0;
static uint32_t test_workers;

#if FWLAB_NATIVE_MQ2
#include "ftl_scale_internal.h"

/* Link wrappers control real Linux thread entry/return, never actor results or
 * join success. Every successful creation is matched to a real successful
 * join, including failure cleanup and new runtime incarnations. The bounds
 * cover only this finite fixture; they are not product lifetime counters. */
#define TEST_THREAD_RECORDS 64u
struct test_thread_record {
    pthread_t thread;
    void *(*entry)(void *);
    void *argument;
    atomic_uint entered, returned, release_start, release_return;
    uint32_t generation, prior_closed_epoch, start_pumps, return_pumps, busy;
    uint8_t created, joined, hold_start, hold_return;
};
static struct {
    struct native_context *context;
    struct native_scaled_media *media;
    struct test_thread_record record[TEST_THREAD_RECORDS];
    atomic_uint wait_timeout;
    uint32_t calls, created, joined, tryjoins, blocking_joins, generations;
    uint32_t in_generation, expected_workers, fail_create_at;
    uint32_t start_holds, return_holds, busy_returns, fail_allocation, allocation_failures;
    uint8_t hold_next_start, hold_next_return;
} thread_gate;

int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int __real_pthread_tryjoin_np(pthread_t, void **);
int __real_pthread_join(pthread_t, void **);
int __real_nanosleep(const struct timespec *, struct timespec *);
void *__real_aligned_alloc(size_t, size_t);

static void test_thread_wait(atomic_uint *release)
{
    struct timespec begin, now, delay = {0, 100000};
    REQUIRE(clock_gettime(CLOCK_MONOTONIC, &begin) == 0);
    while (!atomic_load_explicit(release, memory_order_acquire)) {
        REQUIRE(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
        if (now.tv_sec - begin.tv_sec >= 5) {
            /* A broken coordinator must fail the test, not hang its runner.
             * Let the real worker proceed so no forged join is needed. */
            atomic_store_explicit(&thread_gate.wait_timeout, 1, memory_order_release);
            return;
        }
        REQUIRE(__real_nanosleep(&delay, NULL) == 0);
    }
}

static void *test_thread_entry(void *opaque)
{
    struct test_thread_record *record = opaque;
    void *result;
    atomic_store_explicit(&record->entered, 1, memory_order_release);
    if (record->hold_start) test_thread_wait(&record->release_start);
    result = record->entry(record->argument);
    atomic_store_explicit(&record->returned, 1, memory_order_release);
    if (record->hold_return) test_thread_wait(&record->release_return);
    return result;
}

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                         void *(*entry)(void *), void *argument)
{
    struct test_thread_record *record;
    int result;
    if (!test_workers) return __real_pthread_create(thread, attributes, entry, argument);
    ++thread_gate.calls;
    if (thread_gate.fail_create_at == thread_gate.calls) {
        thread_gate.fail_create_at = 0;
        thread_gate.in_generation = thread_gate.expected_workers;
        return EAGAIN;
    }
    REQUIRE(thread_gate.created < TEST_THREAD_RECORDS);
    if (!thread_gate.in_generation || thread_gate.in_generation == thread_gate.expected_workers) {
        REQUIRE(thread_gate.created == thread_gate.joined);
        thread_gate.in_generation = 0;
        ++thread_gate.generations;
    }
    record = &thread_gate.record[thread_gate.created];
    memset(record, 0, sizeof(*record));
    atomic_init(&record->entered, 0);
    atomic_init(&record->returned, 0);
    atomic_init(&record->release_start, 0);
    atomic_init(&record->release_return, 0);
    record->entry = entry;
    record->argument = argument;
    record->generation = thread_gate.generations;
    record->prior_closed_epoch = thread_gate.context->last_closed_epoch;
    record->hold_start = thread_gate.hold_next_start;
    record->hold_return = thread_gate.hold_next_return;
    thread_gate.hold_next_start = thread_gate.hold_next_return = 0;
    result = __real_pthread_create(thread, attributes, test_thread_entry, record);
    if (!result) {
        record->thread = *thread;
        record->created = 1;
        ++thread_gate.created;
        ++thread_gate.in_generation;
    }
    return result;
}

static struct test_thread_record *test_join_record(pthread_t thread)
{
    for (uint32_t index = 0; index < thread_gate.created; ++index) {
        struct test_thread_record *record = &thread_gate.record[index];
        if (record->created && !record->joined && pthread_equal(record->thread, thread))
            return record;
    }
    REQUIRE(0 && "join must name a currently owned real thread");
    return NULL;
}

int __wrap_pthread_tryjoin_np(pthread_t thread, void **value)
{
    struct test_thread_record *record = test_workers ? test_join_record(thread) : NULL;
    int result = __real_pthread_tryjoin_np(thread, value);
    if (!record) return result;
    ++thread_gate.tryjoins;
    if (!result) {
        REQUIRE(atomic_load_explicit(&record->returned, memory_order_acquire));
        REQUIRE(!record->hold_return || atomic_load_explicit(&record->release_return, memory_order_acquire));
        record->joined = 1;
        ++thread_gate.joined;
    } else if (result == EBUSY && record->hold_return &&
               atomic_load_explicit(&record->returned, memory_order_acquire) &&
               !atomic_load_explicit(&record->release_return, memory_order_acquire)) {
        ++record->busy;
        ++thread_gate.busy_returns;
    }
    return result; /* EBUSY and success are both the real libc result. */
}

int __wrap_pthread_join(pthread_t thread, void **value)
{
    struct test_thread_record *record = test_workers ? test_join_record(thread) : NULL;
    int result = __real_pthread_join(thread, value);
    if (record && !result) {
        REQUIRE(atomic_load_explicit(&record->returned, memory_order_acquire));
        record->joined = 1;
        ++thread_gate.joined;
        ++thread_gate.blocking_joins;
    }
    return result;
}

void *__wrap_aligned_alloc(size_t alignment, size_t bytes)
{
    if (thread_gate.fail_allocation && alignment == fwlab_nfc_channel_v2_arena_alignment() &&
        bytes == fwlab_nfc_channel_v2_arena_size()) {
        thread_gate.fail_allocation = 0;
        ++thread_gate.allocation_failures;
        errno = ENOMEM;
        return NULL; /* One ordinary allocation failure, before any NAND job. */
    }
    return __real_aligned_alloc(alignment, bytes);
}

static void thread_control_pump(void)
{
    struct native_context *context = thread_gate.context;
    if (!test_workers) return;
    REQUIRE(context && !atomic_load_explicit(&thread_gate.wait_timeout, memory_order_acquire));
    for (uint32_t index = 0; index < thread_gate.created; ++index) {
        struct test_thread_record *record = &thread_gate.record[index];
        if (record->hold_start && atomic_load_explicit(&record->entered, memory_order_acquire) &&
            !atomic_load_explicit(&record->release_start, memory_order_acquire)) {
            REQUIRE(context->runtime_media == &thread_gate.media->native &&
                    (!context->runtime || !context->runtime->ready));
            REQUIRE(!host.occupied && !host.delivered);
            for (uint32_t slot = 0; slot < NATIVE_COMMANDS; ++slot)
                REQUIRE(!context->slot[slot].occupied && !context->slot[slot].admitted);
            REQUIRE(thread_gate.media->options.channel_executor == NULL);
            if (++record->start_pumps == 4) {
                ++thread_gate.start_holds;
                atomic_store_explicit(&record->release_start, 1, memory_order_release);
            }
        }
        if (record->hold_return && atomic_load_explicit(&record->returned, memory_order_acquire) &&
            !atomic_load_explicit(&record->release_return, memory_order_acquire)) {
            REQUIRE(context->runtime_media == &thread_gate.media->native &&
                    context->last_closed_epoch == record->prior_closed_epoch && !record->joined &&
                    !native_scaled_media_close(thread_gate.media));
            if (++record->return_pumps >= 4 && record->busy) {
                ++thread_gate.return_holds;
                atomic_store_explicit(&record->release_return, 1, memory_order_release);
            }
        }
    }
}

static void test_thread_balance(uint32_t live)
{
    REQUIRE(!atomic_load_explicit(&thread_gate.wait_timeout, memory_order_acquire));
    REQUIRE(thread_gate.created == thread_gate.joined + live);
}

/* Select the actual MQ2 runtime while reusing a single-I/O fake Host.
 * This tests construction/progress, not two kernel queues or IRQ routing. */
static struct {
    struct native_context *context;
    enum fwlab_spine_result_v0 (*real_step)(void *, uint32_t, uint32_t *,
                                          struct fwlab_execution_progress *);
    uint32_t waiting, inserted, sleeps, advanced_sleeps, storage_advanced;
    uint32_t pump_before, status_before, pump_during_wait, status_during_wait;
} progress_gate;

static enum fwlab_spine_result_v0 progress_gate_step(void *opaque, uint32_t budget,
    uint32_t *used, struct fwlab_execution_progress *progress)
{
    enum fwlab_spine_result_v0 result;
    struct fwlab_ftl_scale_status status;
    if (progress_gate.waiting) {
        *used = budget; /* Deliberately spent budget, zero actual progress. */
        memset(progress, 0, sizeof(*progress));
        if (!--progress_gate.waiting) {
            progress_gate.pump_during_wait = host.pump_ticks - progress_gate.pump_before;
            progress_gate.status_during_wait = host.iterations - progress_gate.status_before;
        }
        return FWLAB_SPINE_V0_OK;
    }
    result = progress_gate.real_step(opaque, budget, used, progress);
    if (result == FWLAB_SPINE_V0_OK && progress->advanced) {
        progress_gate.storage_advanced = 1;
        REQUIRE(fwlab_ftl_scale_query(progress_gate.context->runtime->block.context,
                                     &status) == FWLAB_SPINE_V0_OK);
        if (!progress_gate.inserted && status.busy) {
            progress_gate.inserted = 1; progress_gate.waiting = 96;
            progress_gate.pump_before = host.pump_ticks;
            progress_gate.status_before = host.iterations;
        }
    }
    return result;
}

int __real_nanosleep(const struct timespec *request, struct timespec *remaining);
int __wrap_nanosleep(const struct timespec *request, struct timespec *remaining)
{
    if (progress_gate.context) {
        if (progress_gate.waiting) ++progress_gate.sleeps;
        if (progress_gate.storage_advanced) ++progress_gate.advanced_sleeps;
    }
    return __real_nanosleep(request, remaining); /* Observe, do not remove sleep. */
}

static void install_progress_gate(void)
{
    struct j0_runtime *runtime = progress_gate.context ? progress_gate.context->runtime : NULL;
    if (runtime && runtime->storage.step_report != progress_gate_step) {
        REQUIRE(runtime->storage.step_report);
        progress_gate.real_step = runtime->storage.step_report;
        runtime->storage.step_report = progress_gate_step;
    }
}
#endif

/* Host ioctls are emulated; the explicit thread/allocation controls above
 * still execute real worker jobs/joins. This Host owns byte buffers/transport
 * tuples, never NAND mappings, namespace data or storage-success decisions. */
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
    else if (request == FWLAB_M4_ATTACH_PROFILE)
        argument = va_arg(arguments, struct fwlab_m4_attach_profile_message *);
    else if (request == FWLAB_M4_PUMP)
        argument = va_arg(arguments, struct fwlab_m4_pump_message *);
    else if (request == FWLAB_M4_OWNER_EXCHANGE)
        argument = va_arg(arguments, struct fwlab_m4_owner_message *);
    else if (request == FWLAB_M4_NATIVE_EXCHANGE)
        argument = va_arg(arguments, struct fwlab_m4_native_message *);
    else
        argument = va_arg(arguments, void *);
    va_end(arguments);
    if (request == FWLAB_M4_ATTACH_PROFILE) {
        struct fwlab_m4_attach_profile_message *attach = argument;
        REQUIRE(FWLAB_NATIVE_LARGE && fwlab_m4_attach_profile_request_valid(attach));
        REQUIRE(attach->host_profile_id == TEST_HOST_PROFILE &&
                attach->producer_mode == FWLAB_M4_PRODUCER_PUMP);
        attach->result = fwlab_m4_attach_pin(&attached_identity,
            attach->media_format_version, attach->media_uuid, attach->binding_sha256);
        if (!attach->result) {
            attach->function_nonce = host.function;
            attach->controller_epoch = host.epoch;
        }
        return 0;
    }
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
#if FWLAB_NATIVE_MQ2
        install_progress_gate();
#endif
        struct fwlab_m4_pump_message *pump = argument;
        REQUIRE(FWLAB_NATIVE_PUMP && fwlab_m4_pump_request_valid(pump));
        REQUIRE(attached_identity.media_format_version == FWLAB_M4_MEDIA_SCALED &&
                pump->function_nonce == host.function);
        ++host.pump_ticks;
#if FWLAB_NATIVE_MQ2
        thread_control_pump();
#endif
        if (host.reset_pending && host.drain_acks) {
            REQUIRE(!host.occupied && !host.next);
            ++host.recovery_pumps;
        }
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
#if FWLAB_NATIVE_MQ2
        progress_gate.storage_advanced = 0;
#endif
        if (++host.iterations > 200000u) {
            errno = ETIMEDOUT;
            return -1;
        }
        message->event = host.reset_pending ? FWLAB_M4_NATIVE_RESET : FWLAB_M4_NATIVE_IDLE;
        message->controller_epoch = host.epoch + (host.reset_pending ? 1u : 0u);
        return 0;
    }
    if (message->operation == FWLAB_M4_NATIVE_DRAIN_ACK) {
        REQUIRE(FWLAB_NATIVE_LARGE && host.reset_pending && !host.occupied && !host.next);
        REQUIRE(message->controller_epoch == host.epoch + 1u && !host.drain_acks);
#if FWLAB_NATIVE_MQ2
        if (test_workers) {
            test_thread_balance(0);
            REQUIRE(!thread_gate.media->workers && !thread_gate.context->runtime_media);
        }
#endif
        ++host.drain_acks;
        return 0;
    }
    if (message->operation == FWLAB_M4_NATIVE_RESET_ACK) {
        REQUIRE(FWLAB_NATIVE_PUMP && host.reset_pending && !host.occupied && !host.next);
        REQUIRE(message->controller_epoch == host.epoch + 1u);
        REQUIRE(!FWLAB_NATIVE_LARGE || (host.drain_acks == 1 && host.recovery_pumps));
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
        message->dma_state = FWLAB_M4_NATIVE_DMA_RESERVED;
        if (host.shape_losses && row->length == TEST_IO_BYTES) {
            --host.shape_losses;
            memset(message, 0x5a, sizeof(*message) / 2);
            errno = EFAULT;
            return -1;
        }
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
        put32(capture->sqe + 48, TEST_IO_LBAS - 1);
        row->length = TEST_IO_BYTES;
        row->direction = opcode == 1 ? FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER
                                    : FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST;
    } else REQUIRE(opcode == 0); /* Flush has no data transfer. */
    /* The fake syscall assumes an accepted Host graph. For large requests PRP2
     * names a list, not a direct second page. Actual parser/IOAS proof is separate. */
    if (row->length) put32(capture->sqe + 24, 4096);
    if (row->length > 4096) put32(capture->sqe + 32, 8192);
    if (opcode == 1) pattern(row->bytes, row->length, seed);
}

static void check_runtime_profile(const struct native_context *context)
{
    const struct fwlab_linux_profile_limits *limits;
    REQUIRE(context->runtime && context->runtime->ready);
    REQUIRE(context->host_profile_id == TEST_HOST_PROFILE);
    limits = &context->runtime->config.linux_limits;
    REQUIRE(limits->max_io_bytes == TEST_IO_BYTES &&
            limits->controller_page_bytes == 4096 && limits->queue_depth == 32);
    REQUIRE(limits->max_admin_bytes == (FWLAB_NATIVE_LARGE ? 4096u : 8192u));
    REQUIRE(limits->io_queue_pairs == (FWLAB_NATIVE_MQ2 ? 2u : 1u) &&
            limits->vectors == (FWLAB_NATIVE_MQ2 ? 3u : 1u));
    REQUIRE(context->runtime->config.buffer_profile ==
            (FWLAB_NATIVE_LARGE ? J0_BUFFER_LARGE_SERIAL : J0_BUFFER_REFERENCE));
#if FWLAB_NATIVE_MQ2
    if (test_nand_profile == NATIVE_NAND_CHANNEL_LAB4K) {
        const struct fwlab_ftl_scale *ftl = context->runtime->block.context;
        struct fwlab_nfc_channel_v2_stats stats;
        REQUIRE(ftl && ftl->disk_format == SF_MULTIHEAD_FORMAT_VERSION &&
                ftl->root.disk_format == SF_MULTIHEAD_FORMAT_VERSION &&
                ftl->reads && ftl->writes && ftl->parallel_reads && !ftl->read_only &&
                !ftl->quarantined && !ftl->admission_closed);
        REQUIRE(scale_storage_channel_snapshot(context->runtime, &stats) == FWLAB_SPINE_V0_OK &&
                !stats.closed && !stats.quarantined && !stats.poisoned && !stats.counters_saturated);
        for (uint32_t channel = 0; channel < 4; ++channel)
            REQUIRE(stats.channel[channel].read_policy == FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE &&
                    !stats.channel[channel].quarantined && !stats.channel[channel].counters_saturated);
    }
#endif
}

static void run_script(struct native_context *context, struct native_scaled_media *media)
{
#if FWLAB_NATIVE_MQ2
    uint32_t prior_created = thread_gate.created, prior_joined = thread_gate.joined;
    memset(&progress_gate, 0, sizeof(progress_gate));
    if (!test_workers) progress_gate.context = context;
#endif
#if FWLAB_NATIVE_PUMP
    host.service_fault_once = 1;
    host.pump_losses = 1;
#endif
#if FWLAB_NATIVE_LARGE
    host.shape_losses = 1;
#endif
    REQUIRE(firmware_loop(context, &media->native, NULL));
    /* The loop also rebuilds the runtime after the existing service-fault cut. */
    check_runtime_profile(context);
#if FWLAB_NATIVE_MQ2
    if (!test_workers) {
        REQUIRE(progress_gate.inserted && !progress_gate.waiting && progress_gate.sleeps &&
                !progress_gate.advanced_sleeps && progress_gate.pump_during_wait >= 4 &&
                progress_gate.status_during_wait >= 4);
        REQUIRE(context->runtime->storage.step_report == progress_gate_step);
        context->runtime->storage.step_report = progress_gate.real_step;
        printf("NATIVE_PROGRESS_LOOP_PASS|host_profile=3|configured_io_pairs=2|configured_vectors=3|fake_io_queues_exercised=1|actual_large_parent=1|controlled_wait_visits=96|idle_sleeps=%u|sleep_after_storage_progress=0|pump_during_wait=%u|status_during_wait=%u|not_two_queue_kernel_proof=1\n",
               progress_gate.sleeps, progress_gate.pump_during_wait, progress_gate.status_during_wait);
    } else {
        /* The ordinary service-fault reset must join all old threads before
         * creating this exact number of successor threads. No fabricated
         * storage wait is inserted into the threaded fixture. */
        REQUIRE(thread_gate.created == prior_created + test_workers &&
                thread_gate.joined == prior_joined + test_workers);
        test_thread_balance(test_workers);
        printf("NATIVE_THREAD_RESET_PASS|workers=%u|old_threads_actually_joined=%u|new_threads_actually_created=%u|same_media=1|continued_IO=1|not_native_kernel_reset_proof=1\n",
               test_workers, thread_gate.joined - prior_joined, thread_gate.created - prior_created);
    }
    progress_gate.context = NULL;
#endif
    REQUIRE(host.next == host.count && !host.occupied);
    for (uint32_t index = 0; index < host.count; ++index) {
        const struct host_row *row = &host.row[index];
        REQUIRE(row->retired && row->published && !row->code && !row->code_type);
        if (row->length)
            REQUIRE(row->copied == row->length && row->dma_retired && row->authority_released);
    }
    for (uint32_t index = 0; index < NATIVE_COMMANDS; ++index)
        REQUIRE(!context->slot[index].occupied);
#if FWLAB_NATIVE_LARGE
    REQUIRE(!host.shape_losses && native_frames_quiescent(context));
    REQUIRE(host.drain_acks == 1 && host.recovery_pumps && !context->recovery_pump);
    puts("NATIVE_DRAIN_READY_PASS|drain_before_recovery=1|pump_while_closed=1|no_early_capture=1|final_ready_after_recovery=1|not_kernel_SHST_proof=1");
    puts("NATIVE_LARGE_LOOP_PASS|one_MiB_real_storage=1|same_key_SHAPE_reply_loss=1|separate_frames_returned=1|not_kernel_graph_proof=1");
#endif
#if FWLAB_NATIVE_PUMP
    REQUIRE(host.pump_captures == host.count && host.pump_ticks > host.count &&
            !host.pump_losses && host.reset_acks == 1 && !host.reset_pending);
    puts("NATIVE_PUMP_LOOP_PASS|actual_worker_reset_drain_ack=1|retained_NEXT_after_lost_reply=1|not_kernel_fault_proof=1");
#endif
}

static void check_identify(const struct host_row *row, uint64_t lba_count)
{
    REQUIRE(row->length == 4096 && row->copied == 4096);
    REQUIRE(get_le64(row->bytes) == lba_count);
    REQUIRE(get_le64(row->bytes + 8) == lba_count);
    REQUIRE(get_le64(row->bytes + 16) == lba_count);
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

/* Same fixture and actual native loop; only the owned physical assembly differs.
 * Six fixed names are bounded evidence/cleanup, not another media adapter. */
#define TEST_MEDIA_FILES 6u
struct test_media_identity { struct stat file[TEST_MEDIA_FILES]; };

static uint32_t test_media_file_count(void)
{
    return test_nand_profile == NATIVE_NAND_CHANNEL_LAB4K ? TEST_MEDIA_FILES : 1u;
}

static const char *test_media_file_name(uint32_t index)
{
    REQUIRE(index < test_media_file_count());
    if (test_nand_profile == NATIVE_NAND_R0)
        return "nand.bin";
    return index < 4 ? fwlab_nand_channel_volume_shard_name(index) :
        index == 4 ? FWLAB_NAND_CHANNEL_VOLUME_MANIFEST : FWLAB_NAND_CHANNEL_VOLUME_LOCK;
}

static int test_media_open(struct native_scaled_media *media,
    struct native_context *context, const char *directory, const uint8_t uuid[16],
    int format, uint32_t logical_mib)
{
    if (test_nand_profile == NATIVE_NAND_R0)
        return native_scaled_media_open(media, context, directory, uuid, format, logical_mib);
    int opened = native_scaled_media_open_profile(media, context, directory, uuid, format,
                                                  logical_mib, test_nand_profile);
#if FWLAB_NATIVE_MQ2
    if (opened && test_workers)
        REQUIRE(native_scaled_media_enable_workers(media, test_workers));
#endif
    return opened;
}

static int test_media_present(const struct native_scaled_media *media)
{
    return media->opened && media->profile == test_nand_profile &&
        (test_nand_profile == NATIVE_NAND_R0 ? media->physical != NULL : media->volume != NULL);
}

static void test_media_snapshot(int directory_fd, struct test_media_identity *identity)
{
    for (uint32_t index = 0; index < test_media_file_count(); ++index)
        REQUIRE(fstatat(directory_fd, test_media_file_name(index), &identity->file[index],
                        AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(identity->file[index].st_mode));
    if (test_nand_profile == NATIVE_NAND_CHANNEL_LAB4K) {
        struct stat absent;
        REQUIRE(fstatat(directory_fd, "nand.bin", &absent, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
        REQUIRE(fstatat(directory_fd, FWLAB_NAND_CHANNEL_VOLUME_PENDING, &absent,
                        AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
    }
}

static void test_media_identity_check(const struct test_media_identity *before,
    const struct test_media_identity *after, int unchanged)
{
    for (uint32_t index = 0; index < test_media_file_count(); ++index) {
        const struct stat *a = &before->file[index], *b = &after->file[index];
        REQUIRE(a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_size == b->st_size);
        if (unchanged)
            REQUIRE(a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec);
    }
}

static void test_media_activity(const struct native_context *context,
                                const struct native_scaled_media *media)
{
    REQUIRE(test_media_present(media));
    if (test_nand_profile == NATIVE_NAND_R0) {
        REQUIRE(fwlab_file_nand_v2_sequence(media->physical) > 0);
        return;
    }
    struct fwlab_nfc_channel_v2_stats stats;
    REQUIRE(!media->physical &&
            ((!test_workers && media->options.channel_executor == NULL) ||
             (test_workers && media->options.channel_executor != NULL)) &&
            media->options.multihead_read_schedule == SCALE_STORAGE_READ_PARALLEL &&
            media->options.read_policy == FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE &&
            media->channels.geometry.channels == 4 && media->channels.geometry.luns_per_channel == 1 &&
            media->channels.geometry.planes_per_lun == 2 && media->channels.geometry.blocks_per_plane == 40 &&
            media->channels.geometry.pages_per_block == 64 &&
            media->channels.geometry.plane_parallelism_per_lun == 2);
    REQUIRE(scale_storage_channel_snapshot(context->runtime, &stats) == FWLAB_SPINE_V0_OK &&
            stats.now_ns && stats.accepted_requests && stats.sealed_batches && stats.joined_batches &&
            !stats.closed && !stats.quarantined && !stats.poisoned && !stats.counters_saturated);
    for (uint32_t channel = 0; channel < 4; ++channel) {
        const struct fwlab_nfc_page_v2_lab_stats *child = &stats.channel[channel];
        REQUIRE(fwlab_file_nand_v2_sequence(media->channels.channel[channel].scalar.context) > 0 &&
                child->read_policy == FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE &&
                child->accepted_reads && child->materialized_pages && child->data_out_main_bytes &&
                child->accepted_program_groups && child->successful_program_pages &&
                child->program_main_bytes && child->channel_busy_ns[0] &&
                (child->plane_array_busy_ns[0][0] || child->plane_array_busy_ns[0][1]) &&
                !child->quarantined && !child->counters_saturated);
    }
    /* Final retirement ACK may still be owned. Do not manually advance NFC or
     * require lower live-idle: the next ordinary request/close must drain it. */
    printf("NATIVE_CHANNEL_ACTIVITY_PASS|channels=4|format3_mutable_parallel_read=1|IPR_policy=1|real_child_program_read=1|model_ns=%" PRIu64 "|cooperative=%u|workers=%u|not_plane_overlap_or_throughput_proof=1\n",
           stats.now_ns, test_workers == 0, test_workers);
#if FWLAB_NATIVE_MQ2
    if (test_workers) {
        struct fwlab_nfc_channel_workers_stats workers;
        REQUIRE(media->workers && media->options.channel_executor == &media->executor &&
                fwlab_nfc_channel_workers_snapshot(media->workers, &workers) == FWLAB_NFC_API_OK &&
                workers.workers == test_workers && workers.channels == 4 &&
                workers.created_workers == test_workers && !workers.joined_workers &&
                workers.submitted_jobs && workers.returned_jobs &&
                !workers.stopping && !workers.failed);
        for (uint32_t index = 0; index < test_workers; ++index)
            REQUIRE(workers.worker[index].created && !workers.worker[index].joined &&
                    !workers.worker[index].failed && workers.worker[index].thread_id &&
                    workers.worker[index].completed_jobs && workers.worker[index].actor_quanta &&
                    workers.worker[index].channel_mask == (test_workers == 1 ? 15u : 1u << index));
        printf("NATIVE_THREAD_ACTIVITY_PASS|workers=%u|submitted=%" PRIu64 "|returned=%" PRIu64 "|wait_calls=%" PRIu64 "|real_actor_jobs=1|no_throughput_claim=1\n",
               test_workers, workers.submitted_jobs, workers.returned_jobs, workers.wait_calls);
    }
#endif
}

static void test_runtime_closed(const struct native_context *context)
{
    REQUIRE(!context->runtime && !context->runtime_media && context->last_closed.quiescent &&
            !context->last_closed.host_authorities && !context->last_closed.dma_operations &&
            !context->last_closed.buffers && !context->last_closed.block_operations &&
            !context->last_closed.nfc_operations && !context->last_closed.pending &&
            !context->last_closed.pinned);
#if FWLAB_NATIVE_MQ2
    if (test_workers) {
        test_thread_balance(0);
        REQUIRE(!thread_gate.media->workers && !thread_gate.media->options.channel_executor);
    }
#endif
}

static void test_retained_lock(struct native_context *context,
    const struct native_scaled_media *media, int directory_fd, const char *directory)
{
    if (test_nand_profile != NATIVE_NAND_CHANNEL_LAB4K)
        return;
    struct native_scaled_media *other = calloc(1, sizeof(*other));
    struct test_media_identity before, after;
    REQUIRE(other && !context->runtime && test_media_present(media));
    test_media_snapshot(directory_fd, &before);
    /* A new open description must not acquire the process-lived volume lock
     * merely because the original firmware runtime is now NULL. */
    REQUIRE(!test_media_open(other, context, directory, media->native.uuid, 0, 64) &&
            !other->opened && !other->volume);
    test_media_snapshot(directory_fd, &after);
    test_media_identity_check(&before, &after, 1);
    free(other);
    puts("NATIVE_CHANNEL_RETAINED_LOCK_PASS|runtime_null=1|competing_recovery_rejected=1|six_files_unchanged=1|not_kernel_owner_switch=1");
}

static void test_media_cleanup(int directory_fd, const struct test_media_identity *created)
{
    struct test_media_identity closed;
    test_media_snapshot(directory_fd, &closed);
    test_media_identity_check(created, &closed, 0);
    /* Shards, manifest, then lock: every exact owned name is rechecked after
     * successful close and before unlink. Failure leaves remaining evidence. */
    for (uint32_t index = 0; index < test_media_file_count(); ++index) {
        struct stat current;
        const struct stat *expected = &closed.file[index];
        REQUIRE(fstatat(directory_fd, test_media_file_name(index), &current, AT_SYMLINK_NOFOLLOW) == 0 &&
                S_ISREG(current.st_mode) && current.st_dev == expected->st_dev &&
                current.st_ino == expected->st_ino && current.st_size == expected->st_size &&
                current.st_mtim.tv_sec == expected->st_mtim.tv_sec &&
                current.st_mtim.tv_nsec == expected->st_mtim.tv_nsec);
        REQUIRE(unlinkat(directory_fd, test_media_file_name(index), 0) == 0);
    }
}

static void close_epoch(struct native_context *context, struct native_scaled_media *media)
{
    uint64_t started = wall_ns();
    REQUIRE(runtime_close(context));
    phase_end("runtime-close", context->epoch, started);
    test_runtime_closed(context);
    started = wall_ns();
    REQUIRE(native_scaled_media_close(media));
    phase_end("media-close", context->epoch, started);
}

#if FWLAB_NATIVE_MQ2
static void test_pre_step_failures(struct native_context *context,
    struct native_scaled_media *media, int directory_fd)
{
    if (test_workers != 4) return;
    for (uint32_t phase = 0; phase < 2; ++phase) {
        struct test_media_identity before, after;
        struct fwlab_nfc_channel_workers_stats workers;
        struct j0_close_status prior_closed = context->last_closed;
        uint32_t prior_epoch = context->last_closed_epoch;
        uint32_t prior_created = thread_gate.created, prior_joined = thread_gate.joined;
        uint32_t allocation_failures = thread_gate.allocation_failures;
        uint64_t sequence[4];
        test_thread_balance(0);
        test_media_snapshot(directory_fd, &before);
        for (uint32_t channel = 0; channel < 4; ++channel)
            sequence[channel] = fwlab_file_nand_v2_sequence(media->channels.channel[channel].scalar.context);
        script_begin(context);
        thread_gate.hold_next_return = 1;
        if (!phase) thread_gate.fail_create_at = thread_gate.calls + 2u;
        else thread_gate.fail_allocation = 1;
        REQUIRE(!native_runtime_create(context, &media->native, 1));
        REQUIRE(!context->runtime && context->runtime_media == &media->native &&
                media->workers && !context->runtime_finalized);
        REQUIRE(!thread_gate.fail_create_at && !thread_gate.fail_allocation);
        REQUIRE(thread_gate.created - prior_created == (phase ? 4u : 1u));
        REQUIRE(thread_gate.allocation_failures == allocation_failures + phase);
        REQUIRE(fwlab_nfc_channel_workers_snapshot(media->workers, &workers) == FWLAB_NFC_API_OK &&
                !workers.submitted_jobs && !workers.returned_jobs && !workers.occupied_mailboxes);
        /* The NULL runtime still owes resource cleanup; neither a second
         * incarnation nor final media close may hide that retained owner. */
        uint32_t calls = thread_gate.calls;
        REQUIRE(!native_runtime_create(context, &media->native, 1) && thread_gate.calls == calls);
        REQUIRE(!native_scaled_media_close(media) && test_media_present(media));
        REQUIRE(runtime_close(context));
        test_thread_balance(0);
        REQUIRE(!context->runtime && !context->runtime_media && !media->workers &&
                !media->options.channel_executor &&
                thread_gate.joined - prior_joined == (phase ? 4u : 1u));
        REQUIRE(context->last_closed_epoch == prior_epoch &&
                !memcmp(&context->last_closed, &prior_closed, sizeof(prior_closed)));
        for (uint32_t channel = 0; channel < 4; ++channel)
            REQUIRE(sequence[channel] == fwlab_file_nand_v2_sequence(media->channels.channel[channel].scalar.context));
        test_media_snapshot(directory_fd, &after);
        test_media_identity_check(&before, &after, 1);
        printf("NATIVE_THREAD_PRESTEP_PASS|failure=%s|created=%u|actually_joined=%u|no_NAND_jobs=1|six_files_unchanged=1|no_epoch_certificate=1|retained_null_runtime_cleanup=1\n",
               phase ? "NFC_arena_ENOMEM" : "second_pthread_create_EAGAIN",
               thread_gate.created - prior_created, thread_gate.joined - prior_joined);
    }
}
#endif

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

int main(int argc, char **argv)
{
    const char *root = getenv("FWLAB_TEST_MEDIA_DIR");
    const uint8_t uuid[16] = {0x4d,0x31,0x41,0x2d,0x53,0x43,0x41,0x4c,0x45,1,2,3,4,5,6,7};
    const uint8_t binding[32] = {0x4d,0x31,0x42,0x49,0x44,0x45,0x4e,0x54};
    char directory[512];
    struct statfs fs;
    struct stat absent;
    struct test_media_identity created, before, after;
    struct native_context *context = calloc(1, sizeof(*context));
    struct native_scaled_media *media = calloc(1, sizeof(*media));
    struct fwlab_m4_native_message unsupported;
    struct native_owner owner;
    struct fwlab_nand_channel_volume *retained_volume;
    struct fwlab_file_nand_v2 *retained_physical;
    uint8_t child_uuid[4][16] = {{0}};
    static uint8_t expected[TEST_IO_BYTES]; /* independent expected Host data */
    uint64_t prior_ftl, prior_nfc, started;
    uint32_t logical_mib = NATIVE_SCALED_DEFAULT_MIB;
    uint64_t lba_count;
    int directory_fd, name_length, capacity_seen = 0, profile_seen = 0, workers_seen = 0;

    for (int index = 1; index < argc; ++index) {
        REQUIRE(index + 1 < argc);
        if (!strcmp(argv[index], "--namespace-mib")) {
            REQUIRE(!capacity_seen++ &&
                    (!strcmp(argv[index + 1], "64") || !strcmp(argv[index + 1], "256")));
            logical_mib = !strcmp(argv[++index], "256") ? 256u : 64u;
        } else if (!strcmp(argv[index], "--nand-workers")) {
            REQUIRE(FWLAB_NATIVE_MQ2 && !workers_seen++ &&
                    (!strcmp(argv[index + 1], "1") || !strcmp(argv[index + 1], "4")));
            test_workers = !strcmp(argv[++index], "4") ? 4u : 1u;
        } else {
            REQUIRE(FWLAB_NATIVE_MQ2 && !profile_seen++ &&
                    !strcmp(argv[index], "--nand-profile") &&
                    !strcmp(argv[index + 1], "channel-lab4k"));
            test_nand_profile = NATIVE_NAND_CHANNEL_LAB4K;
            ++index;
        }
    }
    REQUIRE(test_nand_profile == NATIVE_NAND_R0 || logical_mib == 64);
    REQUIRE(!test_workers || test_nand_profile == NATIVE_NAND_CHANNEL_LAB4K);
#if FWLAB_NATIVE_MQ2
    thread_gate.context = context;
    thread_gate.media = media;
    thread_gate.expected_workers = test_workers;
    atomic_init(&thread_gate.wait_timeout, 0);
#endif
    lba_count = (uint64_t)logical_mib * 2048u;
    REQUIRE(root && statfs(root, &fs) == 0 && (unsigned long)fs.f_type == TMPFS_MAGIC);
    REQUIRE((uint64_t)fs.f_bavail * (uint64_t)fs.f_bsize >= UINT64_C(200000000));
    REQUIRE(context && media);
    name_length = snprintf(directory, sizeof(directory), "%s/fwlab-native-scaled.XXXXXX", root);
    REQUIRE(name_length > 0 && (size_t)name_length < sizeof(directory));
    REQUIRE(mkdtemp(directory));
    directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    REQUIRE(directory_fd >= 0);
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("NATIVE_SCALED_OFFLINE_BEGIN|media=%s/%s|logical_mib=%u|max_io_bytes=%u|workers=%u|fake_ioctl_only|no_attach_M5_or_throughput_claim\n",
           directory, test_nand_profile == NATIVE_NAND_R0 ? "nand.bin" : FWLAB_NAND_CHANNEL_VOLUME_MANIFEST,
           logical_mib, TEST_IO_BYTES, test_workers);
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
    REQUIRE(!test_media_open(media, context, directory, uuid, 0, logical_mib));
    REQUIRE(!test_media_open(media, context, directory, uuid, 1, 65));
    if (test_nand_profile == NATIVE_NAND_CHANNEL_LAB4K) {
        REQUIRE(!native_scaled_media_open_profile(media, context, directory, uuid, 1, 64,
                                                  (enum native_nand_profile)2));
        REQUIRE(!test_media_open(media, context, directory, uuid, 1, 256));
        REQUIRE(fstatat(directory_fd, FWLAB_NAND_CHANNEL_VOLUME_PENDING, &absent,
                        AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
    }
    for (uint32_t index = 0; index < test_media_file_count(); ++index)
        REQUIRE(fstatat(directory_fd, test_media_file_name(index), &absent, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
    started = wall_ns();
    REQUIRE(test_media_open(media, context, directory, uuid, 1, logical_mib));
    test_media_snapshot(directory_fd, &created);
    retained_volume = media->volume;
    retained_physical = media->physical;
    if (test_nand_profile == NATIVE_NAND_CHANNEL_LAB4K)
        for (uint32_t channel = 0; channel < 4; ++channel)
            memcpy(child_uuid[channel], media->channels.channel[channel].media_uuid, 16);
    phase_end("media-format", context->epoch, started);
#if FWLAB_NATIVE_LARGE
    REQUIRE(native_attach_profile(context, TEST_HOST_PROFILE,
        FWLAB_M4_PRODUCER_PUMP, FWLAB_M4_MEDIA_SCALED, media->native.uuid, binding) == 0);
#elif FWLAB_NATIVE_PUMP
    REQUIRE(native_attach_mode(context, FWLAB_M4_PRODUCER_PUMP,
        FWLAB_M4_MEDIA_SCALED, media->native.uuid, binding) == 0);
#else
    REQUIRE(native_attach_explicit(context, FWLAB_M4_MEDIA_SCALED, media->native.uuid, binding) == 0);
#endif
#if FWLAB_NATIVE_MQ2
    test_pre_step_failures(context, media, directory_fd);
    if (test_workers) {
        thread_gate.hold_next_start = 1;
        thread_gate.hold_next_return = 1;
    }
#endif
    script_begin(context);
    started = wall_ns();
    REQUIRE(native_runtime_create(context, &media->native, 1));
    check_runtime_profile(context);
    phase_end("runtime-format", context->epoch, started);
    REQUIRE(context->runtime->ready && context->runtime->volume.lba_count == lba_count &&
            !context->runtime->config.file && context->runtime->storage.context &&
            context->runtime->config.media_binding == &media->binding &&
            context->runtime->config.storage_factory == &media->factory);
    REQUIRE(!native_scaled_media_close(media) && test_media_present(media));
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
    add_command(1, lba_count - TEST_IO_LBAS, 0x5a);
    add_command(0, 0, 0);
    add_command(2, lba_count - TEST_IO_LBAS, 0);
    run_script(context, media);
    check_identify(&host.row[0], lba_count);
    pattern(expected, sizeof(expected), 0x5a);
    REQUIRE(memcmp(host.row[3].bytes, expected, sizeof(expected)) == 0);
    REQUIRE(host.dma_in == 1 && host.dma_out == 2);
    test_media_activity(context, media);
    REQUIRE(media->volume == retained_volume && media->physical == retained_physical);
    /* An owner may hold this exact media pointer while runtime is absent.
     * Exercise reconstruction through it without closing/reopening the holder.
     * This is not a fake certificate or an executed kernel owner transition. */
    REQUIRE(runtime_close(context) && !context->runtime && media->opened);
    test_runtime_closed(context);
    REQUIRE(owner.media == &media->native && test_media_present(media) &&
            media->volume == retained_volume && media->physical == retained_physical);
    test_retained_lock(context, media, directory_fd, directory);
    ++context->epoch;
    script_begin(context);
    REQUIRE(native_runtime_create(context, owner.media, 0));
    check_runtime_profile(context);
    REQUIRE(context->runtime->ready && context->runtime->volume.lba_count == lba_count &&
            context->runtime->m3p_instance_nonce != prior_ftl && context->runtime->nfc_instance_nonce != prior_nfc);
    REQUIRE(media->volume == retained_volume && media->physical == retained_physical);
    prior_ftl = context->runtime->m3p_instance_nonce;
    prior_nfc = context->runtime->nfc_instance_nonce;
    puts("NATIVE_RETAINED_MEDIA_PASS|same_holder_between_runtimes=1|not_kernel_owner_switch=1");
    close_epoch(context, media);
    test_media_snapshot(directory_fd, &before);
    test_media_identity_check(&created, &before, 0);
    REQUIRE(!test_media_open(media, context, directory, uuid, 1, logical_mib));
    REQUIRE(!test_media_open(media, context, directory, uuid, 0,
                            logical_mib == 64 ? 256 : 64));
    if (test_nand_profile == NATIVE_NAND_CHANNEL_LAB4K) {
        /* Closed, clean assembly only. Never treat failed opening of dirty
         * physical redo state as an automatically non-mutating operation. */
        REQUIRE(!native_scaled_media_open(media, context, directory, uuid, 0, logical_mib));
    }
    test_media_snapshot(directory_fd, &after);
    test_media_identity_check(&before, &after, 1);
    puts("NATIVE_CAPACITY_MISMATCH_PASS|recovery_rejected=1|image_identity_size_mtime_unchanged=1|no_resize_or_conversion=1");
    ++context->epoch;
    started = wall_ns();
    REQUIRE(test_media_open(media, context, directory, uuid, 0, logical_mib));
    test_media_snapshot(directory_fd, &after);
    test_media_identity_check(&created, &after, 0);
    if (test_nand_profile == NATIVE_NAND_CHANNEL_LAB4K) {
        REQUIRE(!memcmp(media->channels.media_uuid, uuid, 16));
        for (uint32_t channel = 0; channel < 4; ++channel)
            REQUIRE(!memcmp(child_uuid[channel], media->channels.channel[channel].media_uuid, 16));
        puts("NATIVE_CHANNEL_IDENTITY_PASS|same_six_files=1|manifest_child_UUIDs_preserved=1|opposite_profile_recovery_rejected=1|no_resize_or_conversion=1");
    }
    phase_end("media-recover", context->epoch, started);
    script_begin(context);
    started = wall_ns();
    REQUIRE(native_runtime_create(context, &media->native, 0));
    check_runtime_profile(context);
    phase_end("runtime-recover", context->epoch, started);
    REQUIRE(context->runtime->ready && context->runtime->volume.lba_count == lba_count &&
            context->runtime->m3p_instance_nonce != prior_ftl && context->runtime->nfc_instance_nonce != prior_nfc);
    script_begin(context);
    add_command(6, 0, 0);
    add_command(2, lba_count - TEST_IO_LBAS, 0);
    add_command(1, 0, 0xa6);
    add_command(2, 0, 0);
    run_script(context, media);
    check_identify(&host.row[0], lba_count);
    REQUIRE(memcmp(host.row[1].bytes, expected, sizeof(expected)) == 0);
    pattern(expected, sizeof(expected), 0xa6);
    REQUIRE(memcmp(host.row[3].bytes, expected, sizeof(expected)) == 0);
    REQUIRE(host.dma_in == 1 && host.dma_out == 3);
    if (test_nand_profile == NATIVE_NAND_CHANNEL_LAB4K)
        test_media_activity(context, media);
    close_epoch(context, media);
#if FWLAB_NATIVE_MQ2
    if (test_workers) {
        test_thread_balance(0);
        REQUIRE(thread_gate.start_holds == 1 &&
                thread_gate.return_holds == (test_workers == 4 ? 3u : 1u) &&
                thread_gate.busy_returns >= thread_gate.return_holds &&
                thread_gate.tryjoins && !thread_gate.blocking_joins);
        printf("NATIVE_THREAD_LIFETIME_PASS|workers=%u|real_creates=%u|real_joins=%u|generations=%u|startup_held_pump_visits=4|return_holds=%u|actual_tryjoin_EBUSY=%u|blocking_joins=0|no_early_ready_drain_or_release=1|no_M5_or_performance_claim=1\n",
               test_workers, thread_gate.created, thread_gate.joined, thread_gate.generations,
               thread_gate.return_holds, thread_gate.busy_returns);
    }
#endif
    test_media_cleanup(directory_fd, &created);
    REQUIRE(close(directory_fd) == 0 && rmdir(directory) == 0);
    free(media);
    REQUIRE(native_frame_storage_fini(context));
    free(context);
    printf("NATIVE_SCALED_OFFLINE_PASS|actual_native_constructor_host_loop=1|real_FTL_PAGE2_physical_v2=1|logical_mib=%u|SELF_Flush_Read_recovery_continue=1|new_epoch=1|runtime_media_released=1|no_kernel_format_claim=1\n", logical_mib);
    return 0;
}
