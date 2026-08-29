/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "c31_fake_provider.h"
#include "fwlab/portable/c31.h"
#include "fwlab/portable/c31_codec.h"

#define MODEL_ARENA_BYTES 131072u

enum model_action {
    MODEL_COMPLETE = 0,
    MODEL_ABORT = 1,
    MODEL_RESET = 2,
    MODEL_TEARDOWN = 3
};

union model_arena {
    max_align_t alignment;
    uint8_t bytes[MODEL_ARENA_BYTES];
};

static union model_arena arena;
static struct c31_fake_provider_context dma;
static struct c31_fake_provider_context nfc;

static uint64_t hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        hash ^= (uint8_t)(value >> (index * 8));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void capacity_make(struct fwlab_c31_capacity *capacity)
{
    memset(capacity, 0, sizeof(*capacity));
    capacity->version = FWLAB_C31_CONTRACT_VERSION;
    capacity->size = (uint16_t)sizeof(*capacity);
    capacity->commands = 2;
    capacity->abort_tickets = 2;
    capacity->event_batch = 2;
    capacity->trace_entries = 96;
    capacity->scratch_bytes = FWLAB_C31_DESCRIPTOR_WIRE_SIZE;
    capacity->slot_generation_limit = 8;
    capacity->operation_generation_limit = 8;
    capacity->lease_generation_limit = 8;
    capacity->ticket_generation_limit = 8;
    capacity->controller_epoch_limit = 8;
    capacity->command_uid_limit = 8;
}

static struct fwlab_c31_command_descriptor descriptor_make(
    uint8_t provider_kind,
    uint64_t case_id
)
{
    struct fwlab_c31_command_descriptor descriptor;

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.version = FWLAB_C31_CONTRACT_VERSION;
    descriptor.size = (uint16_t)sizeof(descriptor);
    descriptor.origin.word[0] = UINT64_C(0x1000) + case_id;
    descriptor.origin.word[1] = UINT64_C(0x2000) + case_id;
    descriptor.trace_cookie = UINT64_C(0x3000) + case_id;
    descriptor.provider_kind = provider_kind;
    if (provider_kind != FWLAB_C31_PROVIDER_NONE) {
        descriptor.provider_request.word[0] = case_id;
        descriptor.provider_request.word[1] = ~case_id;
    }
    if (provider_kind == FWLAB_C31_PROVIDER_DMA) {
        descriptor.capability.word[0] = UINT64_C(0x4000) + case_id;
        descriptor.capability.word[1] = UINT64_C(0x5000) + case_id;
        descriptor.dma_direction = FWLAB_C31_DMA_TO_CONTROLLER;
        descriptor.length = 8;
    }
    return descriptor;
}

static int step_one(struct fwlab_c31 *instance)
{
    struct fwlab_c31_step_result step;

    return fwlab_c31_step(instance, 1, &step) == FWLAB_C31_API_OK;
}

static int consume_if_ready(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_command_handle *command,
    unsigned int *consumed
)
{
    enum fwlab_c31_lifecycle_state state;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;

    if (fwlab_c31_command_state(instance, command, &state) !=
        FWLAB_C31_API_OK) {
        return 1;
    }
    if (state != FWLAB_C31_CMD_COMPLETION_READY) {
        return 1;
    }
    if (*consumed != 0 ||
        fwlab_c31_completion_acquire(instance, command, &lease, &intent) !=
            FWLAB_C31_API_OK ||
        fwlab_c31_completion_consume(instance, &lease) != FWLAB_C31_API_OK) {
        return 0;
    }
    ++*consumed;
    return 1;
}

static uint64_t trace_hash(struct fwlab_c31 *instance, uint64_t hash)
{
    uint32_t count = fwlab_c31_trace_count(instance);
    uint32_t index;

    for (index = 0; index < count; ++index) {
        struct fwlab_c31_trace_entry entry;

        if (fwlab_c31_trace_read(instance, index, &entry) !=
            FWLAB_C31_API_OK) {
            return 0;
        }
        hash = hash_u64(hash, entry.sequence);
        hash = hash_u64(hash, entry.command.instance_nonce);
        hash = hash_u64(hash, entry.command.command_uid);
        hash = hash_u64(hash, entry.command.controller_epoch);
        hash = hash_u64(hash, entry.command.slot);
        hash = hash_u64(hash, entry.command.slot_generation);
        hash = hash_u64(hash, entry.kind);
        hash = hash_u64(hash, entry.from_state);
        hash = hash_u64(hash, entry.to_state);
        hash = hash_u64(hash, entry.detail);
    }
    return hash;
}

static int run_case(
    uint64_t case_id,
    uint8_t provider_kind,
    uint32_t backpressure,
    uint32_t delay,
    uint8_t terminal,
    enum model_action action,
    uint32_t cut,
    uint64_t *aggregate
)
{
    struct fwlab_c31_capacity capacity;
    struct fwlab_c31_provider_set providers;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_abort_ticket ticket;
    enum fwlab_c31_abort_outcome abort_outcome = FWLAB_C31_ABORT_PENDING;
    struct fwlab_c31 *instance = NULL;
    struct c31_fake_scenario scenario;
    unsigned int consumed = 0;
    unsigned int iteration;
    int ticket_valid = 0;

    capacity_make(&capacity);
    c31_fake_provider_init(&dma, FWLAB_C31_PROVIDER_DMA);
    c31_fake_provider_init(&nfc, FWLAB_C31_PROVIDER_NFC);
    providers.dma = c31_fake_provider(&dma);
    providers.nfc = c31_fake_provider(&nfc);
    if (fwlab_c31_init(arena.bytes, sizeof(arena.bytes), &capacity,
                       UINT64_C(0x90000000) + case_id, &providers,
                       &instance) != FWLAB_C31_API_OK) {
        return 0;
    }
    descriptor = descriptor_make(provider_kind, case_id);
    if (provider_kind != FWLAB_C31_PROVIDER_NONE) {
        memset(&scenario, 0, sizeof(scenario));
        scenario.request = descriptor.provider_request;
        scenario.backpressure_count = backpressure;
        scenario.delay_polls = delay;
        scenario.submit_disposition = FWLAB_C31_PROVIDER_ACCEPTED;
        scenario.terminal = terminal;
        scenario.cancel_wins = true;
        if (terminal == FWLAB_C31_PROVIDER_SUCCESS) {
            scenario.terminal_fault.effect_class = FWLAB_C31_EFFECT_FULL;
        } else {
            scenario.terminal_fault.domain = FWLAB_C31_FAULT_PROVIDER;
            scenario.terminal_fault.retry_class = FWLAB_C31_RETRY_NEVER;
            scenario.terminal_fault.effect_class = FWLAB_C31_EFFECT_NONE;
            scenario.terminal_fault.reason =
                terminal == FWLAB_C31_PROVIDER_CANCELLED ?
                    FWLAB_C31_REASON_CANCELLED :
                    FWLAB_C31_REASON_PROVIDER_FAILED;
        }
        if (!(provider_kind == FWLAB_C31_PROVIDER_DMA ?
              c31_fake_provider_add(&dma, &scenario) :
              c31_fake_provider_add(&nfc, &scenario))) {
            return 0;
        }
    }
    if (fwlab_c31_submit(instance, &descriptor, &command) !=
        FWLAB_C31_API_OK) {
        return 0;
    }
    for (iteration = 0; iteration < cut; ++iteration) {
        if (!step_one(instance)) {
            return 0;
        }
    }

    if (action == MODEL_ABORT) {
        if (fwlab_c31_abort_request(instance, &command, &ticket,
                                    &abort_outcome) != FWLAB_C31_API_OK) {
            return 0;
        }
        ticket_valid = 1;
    } else if (action == MODEL_RESET) {
        if (fwlab_c31_reset_begin(instance) != FWLAB_C31_API_OK) {
            return 0;
        }
    } else if (action == MODEL_TEARDOWN) {
        if (fwlab_c31_teardown_begin(instance) != FWLAB_C31_API_OK) {
            return 0;
        }
    }

    for (iteration = 0; iteration < 96; ++iteration) {
        enum fwlab_c31_instance_phase phase = fwlab_c31_phase(instance);

        if (phase == FWLAB_C31_INSTANCE_FAULTED) {
            return 0;
        }
        if (action == MODEL_RESET && phase == FWLAB_C31_INSTANCE_RESET_ACK) {
            if (fwlab_c31_reset_ack(instance) != FWLAB_C31_API_OK) {
                return 0;
            }
            break;
        }
        if (action == MODEL_TEARDOWN &&
            phase == FWLAB_C31_INSTANCE_TEARDOWN_ACK) {
            if (fwlab_c31_teardown_ack(instance) != FWLAB_C31_API_OK) {
                return 0;
            }
            break;
        }
        if ((action == MODEL_COMPLETE || action == MODEL_ABORT) &&
            !consume_if_ready(instance, &command, &consumed)) {
            return 0;
        }
        if (consumed == 1 && action == MODEL_COMPLETE) {
            break;
        }
        if (ticket_valid) {
            if (fwlab_c31_abort_query(instance, &ticket, &abort_outcome) !=
                FWLAB_C31_API_OK) {
                return 0;
            }
            if (abort_outcome != FWLAB_C31_ABORT_PENDING && consumed == 1) {
                if (fwlab_c31_abort_ack(instance, &ticket) !=
                    FWLAB_C31_API_OK) {
                    return 0;
                }
                ticket_valid = 0;
                break;
            }
        }
        if (!step_one(instance)) {
            return 0;
        }
    }
    if (iteration == 96 || consumed > 1 ||
        (action == MODEL_COMPLETE && consumed != 1) ||
        (action == MODEL_ABORT && (consumed != 1 || ticket_valid)) ||
        (action == MODEL_RESET &&
         fwlab_c31_phase(instance) != FWLAB_C31_INSTANCE_READY) ||
        (action == MODEL_TEARDOWN &&
         fwlab_c31_phase(instance) != FWLAB_C31_INSTANCE_DEAD)) {
        return 0;
    }
    *aggregate = trace_hash(instance, *aggregate);
    return *aggregate != 0;
}

int main(void)
{
    uint64_t aggregate = UINT64_C(1469598103934665603);
    uint64_t case_id = 1;
    uint32_t provider;
    uint32_t backpressure;
    uint32_t delay;
    uint32_t terminal;
    uint32_t action;
    uint32_t cut;

    for (provider = FWLAB_C31_PROVIDER_NONE;
         provider <= FWLAB_C31_PROVIDER_NFC; ++provider) {
        for (backpressure = 0; backpressure <= 1; ++backpressure) {
            for (delay = 0; delay <= 2; ++delay) {
                for (terminal = FWLAB_C31_PROVIDER_SUCCESS;
                     terminal <= FWLAB_C31_PROVIDER_FAILED; ++terminal) {
                    for (action = MODEL_COMPLETE;
                         action <= MODEL_TEARDOWN; ++action) {
                        for (cut = 0; cut <= 8; ++cut) {
                            if (!run_case(case_id, (uint8_t)provider,
                                          backpressure, delay,
                                          (uint8_t)terminal,
                                          (enum model_action)action, cut,
                                          &aggregate)) {
                                fprintf(stderr,
                                        "C3.1 model failed: case=%llu provider=%u backpressure=%u delay=%u terminal=%u action=%u cut=%u\n",
                                        (unsigned long long)case_id,
                                        provider, backpressure, delay, terminal,
                                        action, cut);
                                return 1;
                            }
                            ++case_id;
                        }
                    }
                }
            }
        }
    }
    printf("C3.1 bounded model: PASS (%llu traces, hash=%016llx)\n",
           (unsigned long long)(case_id - 1),
           (unsigned long long)aggregate);
    return 0;
}
