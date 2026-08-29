/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdalign.h>
#include <stdint.h>
#include <string.h>

#include "c31_fake_dma.h"
#include "c31_fake_nfc.h"
#include "c31_fake_provider.h"
#include "fwlab/portable/c31.h"

union smoke_arena {
    max_align_t alignment;
    uint8_t bytes[262144];
};

static int drive_completion(
    struct fwlab_c31 *instance,
    const struct fwlab_c31_command_handle *command
)
{
    unsigned int iteration;

    for (iteration = 0; iteration < 64; ++iteration) {
        struct fwlab_c31_step_result step;
        enum fwlab_c31_lifecycle_state state;

        if (fwlab_c31_step(instance, 1, &step) != FWLAB_C31_API_OK ||
            fwlab_c31_command_state(instance, command, &state) !=
                FWLAB_C31_API_OK) {
            return 0;
        }
        if (state == FWLAB_C31_CMD_COMPLETION_READY) {
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    static union smoke_arena arena;
    static struct c31_fake_dma_context dma;
    static struct c31_fake_nfc_context nfc;
    struct fwlab_c31_capacity capacity;
    struct fwlab_c31_provider_set providers;
    struct fwlab_c31_command_descriptor descriptor;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;
    struct fwlab_c31 *instance = NULL;

    memset(&capacity, 0, sizeof(capacity));
    capacity.version = FWLAB_C31_CONTRACT_VERSION;
    capacity.size = (uint16_t)sizeof(capacity);
    capacity.commands = 4;
    capacity.abort_tickets = 4;
    capacity.event_batch = 2;
    capacity.trace_entries = 64;
    capacity.scratch_bytes = 128;
    capacity.slot_generation_limit = 100;
    capacity.operation_generation_limit = 100;
    capacity.lease_generation_limit = 100;
    capacity.ticket_generation_limit = 100;
    capacity.controller_epoch_limit = 100;
    capacity.command_uid_limit = 1000;

    c31_fake_dma_init(&dma);
    c31_fake_nfc_init(&nfc);
    providers.dma = c31_fake_dma_provider(&dma);
    providers.nfc = c31_fake_nfc_provider(&nfc);
    if (fwlab_c31_init(arena.bytes, sizeof(arena.bytes), &capacity,
                       UINT64_C(0x12345678), &providers, &instance) !=
        FWLAB_C31_API_OK) {
        return 1;
    }

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.version = FWLAB_C31_CONTRACT_VERSION;
    descriptor.size = (uint16_t)sizeof(descriptor);
    descriptor.origin.word[0] = 1;
    descriptor.origin.word[1] = 2;
    descriptor.trace_cookie = 3;
    descriptor.provider_kind = FWLAB_C31_PROVIDER_NONE;
    if (fwlab_c31_submit(instance, &descriptor, &command) !=
            FWLAB_C31_API_OK ||
        !drive_completion(instance, &command) ||
        fwlab_c31_completion_acquire(instance, &command, &lease, &intent) !=
            FWLAB_C31_API_OK ||
        intent.result != FWLAB_C31_COMPLETION_SUCCESS ||
        fwlab_c31_completion_consume(instance, &lease) != FWLAB_C31_API_OK ||
        fwlab_c31_teardown_begin(instance) != FWLAB_C31_API_OK ||
        fwlab_c31_phase(instance) != FWLAB_C31_INSTANCE_TEARDOWN_ACK ||
        fwlab_c31_teardown_ack(instance) != FWLAB_C31_API_OK) {
        return 2;
    }
    return 0;
}
