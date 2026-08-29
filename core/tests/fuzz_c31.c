/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdbool.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fwlab/portable/c31.h"
#include "fwlab/portable/c31_codec.h"

#define FUZZ_ITERATIONS 5000u
#define FUZZ_ARENA_BYTES 65536u

union fuzz_arena {
    max_align_t alignment;
    uint8_t bytes[FUZZ_ARENA_BYTES];
};

struct fuzz_provider {
    struct fwlab_c31_provider_request request;
    struct fwlab_c31_provider_event event;
    bool active;
    bool emitted;
    bool cancelled;
};

static union fuzz_arena arena;
static struct fuzz_provider provider_context;

static uint64_t next_random(uint64_t *state)
{
    uint64_t value = *state;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

static uint64_t mix(uint64_t hash, uint64_t value)
{
    hash ^= value + UINT64_C(0x9e3779b97f4a7c15) +
            (hash << 6) + (hash >> 2);
    return hash;
}

static struct fwlab_c31_provider_submit_result fuzz_submit(
    void *opaque,
    const struct fwlab_c31_provider_request *request
)
{
    struct fuzz_provider *provider = opaque;
    struct fwlab_c31_provider_submit_result result;

    memset(&result, 0, sizeof(result));
    if (provider == NULL || request == NULL || provider->active) {
        result.disposition = FWLAB_C31_PROVIDER_BACKPRESSURE;
        return result;
    }
    provider->request = *request;
    provider->active = true;
    provider->emitted = false;
    provider->cancelled = false;
    result.disposition = FWLAB_C31_PROVIDER_ACCEPTED;
    return result;
}

static enum fwlab_c31_api_result fuzz_cancel(
    void *opaque,
    const struct fwlab_c31_operation_token *operation
)
{
    struct fuzz_provider *provider = opaque;

    if (provider == NULL || operation == NULL) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    provider->cancelled = true;
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result fuzz_poll(
    void *opaque,
    uint32_t budget,
    struct fwlab_c31_provider_event *events,
    uint32_t event_capacity,
    uint32_t *event_count
)
{
    struct fuzz_provider *provider = opaque;

    if (provider == NULL || events == NULL || event_count == NULL ||
        budget == 0 || event_capacity == 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    *event_count = 0;
    if (provider->active && !provider->emitted) {
        events[0] = provider->event;
        provider->emitted = true;
        provider->active = false;
        *event_count = 1;
    }
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result fuzz_reset(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch
)
{
    struct fuzz_provider *provider = opaque;

    if (provider == NULL || instance_nonce == 0 || old_epoch == 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    provider->cancelled = true;
    return FWLAB_C31_API_OK;
}

static enum fwlab_c31_api_result fuzz_quiescent(
    void *opaque,
    uint64_t instance_nonce,
    uint32_t old_epoch,
    bool *quiescent
)
{
    struct fuzz_provider *provider = opaque;

    if (provider == NULL || quiescent == NULL || instance_nonce == 0 ||
        old_epoch == 0) {
        return FWLAB_C31_API_INVALID_CONTRACT;
    }
    *quiescent = !provider->active;
    return FWLAB_C31_API_OK;
}

static const struct fwlab_c31_provider_ops fuzz_ops = {
    .version = FWLAB_C31_PROVIDER_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_c31_provider_ops),
    .reserved = 0,
    .try_submit = fuzz_submit,
    .cancel = fuzz_cancel,
    .poll = fuzz_poll,
    .reset_begin = fuzz_reset,
    .quiescent = fuzz_quiescent,
};

static void capacity_make(struct fwlab_c31_capacity *capacity)
{
    memset(capacity, 0, sizeof(*capacity));
    capacity->version = FWLAB_C31_CONTRACT_VERSION;
    capacity->size = (uint16_t)sizeof(*capacity);
    capacity->commands = 2;
    capacity->abort_tickets = 2;
    capacity->event_batch = 2;
    capacity->trace_entries = 32;
    capacity->scratch_bytes = FWLAB_C31_DESCRIPTOR_WIRE_SIZE;
    capacity->slot_generation_limit = 4;
    capacity->operation_generation_limit = 4;
    capacity->lease_generation_limit = 4;
    capacity->ticket_generation_limit = 4;
    capacity->controller_epoch_limit = 4;
    capacity->command_uid_limit = 4;
}

static struct fwlab_c31_command_descriptor descriptor_make(uint64_t iteration)
{
    struct fwlab_c31_command_descriptor descriptor;

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.version = FWLAB_C31_CONTRACT_VERSION;
    descriptor.size = (uint16_t)sizeof(descriptor);
    descriptor.origin.word[0] = UINT64_C(0x100000) + iteration;
    descriptor.origin.word[1] = UINT64_C(0x200000) + iteration;
    descriptor.trace_cookie = UINT64_C(0x300000) + iteration;
    descriptor.provider_request.word[0] = UINT64_C(0x400000) + iteration;
    descriptor.provider_request.word[1] = UINT64_C(0x500000) + iteration;
    descriptor.capability.word[0] = UINT64_C(0x600000) + iteration;
    descriptor.capability.word[1] = UINT64_C(0x700000) + iteration;
    descriptor.provider_kind = FWLAB_C31_PROVIDER_DMA;
    descriptor.dma_direction = FWLAB_C31_DMA_TO_CONTROLLER;
    descriptor.length = 8;
    return descriptor;
}

static void mutate_event(
    struct fwlab_c31_provider_event *event,
    uint64_t value
)
{
    switch (value % 12u) {
    case 0:
        break;
    case 1:
        event->operation.command.instance_nonce ^= UINT64_C(1);
        break;
    case 2:
        ++event->operation.command.command_uid;
        break;
    case 3:
        ++event->operation.command.controller_epoch;
        break;
    case 4:
        ++event->operation.command.slot_generation;
        break;
    case 5:
        ++event->operation.operation_generation;
        break;
    case 6:
        event->operation.cookie ^= UINT64_C(0x100);
        break;
    case 7:
        event->version = 2;
        break;
    case 8:
        event->reserved0 = 1;
        break;
    case 9:
        event->terminal = 7;
        break;
    case 10:
        event->fault.domain = 99;
        break;
    default:
        event->fault.effect_class = FWLAB_C31_EFFECT_EXACT_PREFIX;
        event->fault.prefix_length = UINT32_MAX;
        break;
    }
}

static int fuzz_one(uint64_t iteration, uint64_t *random, uint64_t *hash)
{
    struct fwlab_c31_capacity capacity;
    struct fwlab_c31_provider_set providers;
    struct fwlab_c31_command_descriptor descriptor = descriptor_make(iteration);
    struct fwlab_c31_command_descriptor decoded;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;
    struct fwlab_c31_step_result step;
    struct fwlab_c31 *instance = NULL;
    uint8_t wire[FWLAB_C31_DESCRIPTOR_WIRE_SIZE];
    uint32_t mutations;
    uint32_t index;
    enum fwlab_c31_api_result result;

    if (fwlab_c31_descriptor_encode(&descriptor, wire, sizeof(wire)) !=
        FWLAB_C31_API_OK) {
        return 0;
    }
    mutations = (uint32_t)(next_random(random) % 5u);
    for (index = 0; index < mutations; ++index) {
        uint32_t offset = (uint32_t)(next_random(random) % sizeof(wire));

        wire[offset] ^= (uint8_t)(UINT8_C(1) <<
                                  (next_random(random) % 8u));
    }
    result = fwlab_c31_descriptor_decode(wire, sizeof(wire), &decoded);
    *hash = mix(*hash, result);
    if (result == FWLAB_C31_API_OK) {
        uint8_t roundtrip[FWLAB_C31_DESCRIPTOR_WIRE_SIZE];

        if (fwlab_c31_descriptor_encode(&decoded, roundtrip,
                                        sizeof(roundtrip)) !=
                FWLAB_C31_API_OK ||
            memcmp(roundtrip, wire, sizeof(wire)) != 0) {
            return 0;
        }
    }

    memset(&provider_context, 0, sizeof(provider_context));
    providers.dma.ops = &fuzz_ops;
    providers.dma.context = &provider_context;
    providers.nfc.ops = NULL;
    providers.nfc.context = NULL;
    capacity_make(&capacity);
    if (fwlab_c31_init(arena.bytes, sizeof(arena.bytes), &capacity,
                       UINT64_C(0x80000000) + iteration, &providers,
                       &instance) != FWLAB_C31_API_OK ||
        fwlab_c31_submit(instance, &descriptor, &command) !=
            FWLAB_C31_API_OK ||
        fwlab_c31_step(instance, 1, &step) != FWLAB_C31_API_OK ||
        fwlab_c31_step(instance, 1, &step) != FWLAB_C31_API_OK) {
        return 0;
    }
    memset(&provider_context.event, 0, sizeof(provider_context.event));
    provider_context.event.version = FWLAB_C31_PROVIDER_CONTRACT_VERSION;
    provider_context.event.size =
        (uint16_t)sizeof(provider_context.event);
    provider_context.event.operation = provider_context.request.operation;
    provider_context.event.terminal = FWLAB_C31_PROVIDER_SUCCESS;
    provider_context.event.fault.effect_class = FWLAB_C31_EFFECT_FULL;
    mutate_event(&provider_context.event, next_random(random));

    if ((next_random(random) & UINT64_C(3)) == 0) {
        struct fwlab_c31_abort_ticket ticket;
        enum fwlab_c31_abort_outcome outcome;

        result = fwlab_c31_abort_request(instance, &command, &ticket, &outcome);
        *hash = mix(*hash, result);
    }
    result = fwlab_c31_step(instance, 4, &step);
    if (result != FWLAB_C31_API_OK &&
        result != FWLAB_C31_API_INVARIANT_FAILURE) {
        return 0;
    }
    *hash = mix(*hash, result);
    *hash = mix(*hash, fwlab_c31_phase(instance));
    *hash = mix(*hash, fwlab_c31_trace_count(instance));
    if (fwlab_c31_phase(instance) == FWLAB_C31_INSTANCE_READY &&
        fwlab_c31_completion_acquire(instance, &command, &lease, &intent) ==
            FWLAB_C31_API_OK) {
        *hash = mix(*hash, intent.result);
        *hash = mix(*hash, intent.fault.effect_class);
        result = fwlab_c31_completion_consume(instance, &lease);
        if (result != FWLAB_C31_API_OK) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    uint64_t random = UINT64_C(0x9b6d3e7a4c2158f1);
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    uint32_t iteration;

    for (iteration = 1; iteration <= FUZZ_ITERATIONS; ++iteration) {
        if (!fuzz_one(iteration, &random, &hash)) {
            fprintf(stderr, "C3.1 fuzz failed: iteration=%u\n", iteration);
            return 1;
        }
    }
    printf("C3.1 deterministic fuzz: PASS (seed=9b6d3e7a4c2158f1 iterations=%u hash=%016llx)\n",
           FUZZ_ITERATIONS, (unsigned long long)hash);
    return 0;
}
