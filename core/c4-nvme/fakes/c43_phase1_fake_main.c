/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_fake_services.h"

#include "fwlab/portable/nvme_codec.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static struct fwlab_c43_graph_config fixed_config(void)
{
    struct fwlab_c43_graph_config config = {0};
    struct fwlab_c43_counter_seed seed = {1, UINT64_C(0xffffffff)};

    config.version = FWLAB_C43_GRAPH_VERSION;
    config.size = sizeof(config);
    fwlab_nvme_profile_fixed(&config.profile);
    config.command_capacity = FWLAB_C43_MAX_COMMANDS;
    config.actions_per_command = FWLAB_C43_ACTIONS_PER_COMMAND;
    config.queue_mailbox_capacity = 8;
    config.target_mailbox_capacity = 4;
    config.block_mailbox_capacity = 20;
    config.service_gap_maximum = FWLAB_C43_SERVICE_GAP_MAXIMUM;
    config.ordinary_progress_maximum = FWLAB_C43_PROGRESS_MAXIMUM;
    config.control_progress_maximum = FWLAB_C43_CONTROL_PROGRESS_MAXIMUM;
    config.safety_generation = 1;
    config.instance_nonce = UINT64_C(0xc430000000000001);
    config.controller_epoch = 1;
    config.command_uid = seed;
    config.action_uid = seed;
    config.transaction_uid = seed;
    config.lease_uid = seed;
    config.consume_uid = seed;
    config.finalizer_uid = seed;
    return config;
}

static int bytes_equal(const void *left, const void *right, size_t size)
{
    return memcmp(left, right, size) == 0;
}

static int check_init_failures(
    size_t required,
    const struct fwlab_c43_graph_config *config,
    const struct fwlab_c43_graph_providers *providers)
{
    _Alignas(max_align_t) uint8_t arena[8193];
    uint8_t before[sizeof(arena)];
    struct fwlab_c43_graph *output = (void *)(uintptr_t)UINT64_C(0x1111);

    memset(arena, 0xa5, sizeof(arena));
    memcpy(before, arena, sizeof(before));
    if (fwlab_c43_graph_init(arena, required - 1, config, providers,
                             &output) != FWLAB_C43_GRAPH_INVALID ||
        output != (void *)(uintptr_t)UINT64_C(0x1111) ||
        !bytes_equal(arena, before, sizeof(arena))) {
        return 0;
    }
    if (fwlab_c43_graph_init(arena, required + 1, config, providers,
                             &output) != FWLAB_C43_GRAPH_INVALID ||
        output != (void *)(uintptr_t)UINT64_C(0x1111) ||
        !bytes_equal(arena, before, sizeof(arena))) {
        return 0;
    }
    if (fwlab_c43_graph_init(arena + 1, required, config, providers,
                             &output) != FWLAB_C43_GRAPH_INVALID ||
        output != (void *)(uintptr_t)UINT64_C(0x1111) ||
        !bytes_equal(arena, before, sizeof(arena))) {
        return 0;
    }

    memset(arena, 0xa5, sizeof(arena));
    memcpy(before, arena, sizeof(before));
    if (fwlab_c43_graph_init(arena, required, config, providers,
                             (struct fwlab_c43_graph **)arena) !=
            FWLAB_C43_GRAPH_INVALID ||
        !bytes_equal(arena, before, sizeof(arena))) {
        return 0;
    }

    memset(arena, 0, sizeof(arena));
    memcpy(arena, config, sizeof(*config));
    memcpy(before, arena, sizeof(before));
    if (fwlab_c43_graph_init(arena, required,
                             (const struct fwlab_c43_graph_config *)arena,
                             providers, &output) !=
            FWLAB_C43_GRAPH_INVALID ||
        !bytes_equal(arena, before, sizeof(arena))) {
        return 0;
    }

    memset(arena, 0, sizeof(arena));
    memcpy(arena, providers, sizeof(*providers));
    memcpy(before, arena, sizeof(before));
    if (fwlab_c43_graph_init(
            arena, required, config,
            (const struct fwlab_c43_graph_providers *)arena, &output) !=
            FWLAB_C43_GRAPH_INVALID ||
        !bytes_equal(arena, before, sizeof(arena))) {
        return 0;
    }
    return 1;
}

static int check_fake_entrypoints(
    struct c43_fake_services *services,
    const struct fwlab_c43_graph_providers *providers)
{
    struct fwlab_hif_action_submit_result submit;
    struct fwlab_hif_action_submit_result submit_before;
    struct fwlab_c43_queue_effect_terminal queue_terminal;
    struct fwlab_c43_queue_effect_terminal queue_before;
    struct fwlab_c43_target_terminal target_terminal;
    struct fwlab_c43_target_terminal target_before;
    struct fwlab_c43_block_action_terminal block_terminal;
    struct fwlab_c43_block_action_terminal block_before;
    bool ready = true;
    bool quiescent = true;
    uint32_t index;

    memset(&submit, 0xa5, sizeof(submit));
    submit_before = submit;
    if (providers->queue.ops->prepare_start(providers->queue.context, NULL,
                                            &submit) !=
            FWLAB_HIF_ACTION_REJECTED ||
        !bytes_equal(&submit, &submit_before, sizeof(submit))) {
        return 0;
    }
    memset(&queue_terminal, 0xa5, sizeof(queue_terminal));
    queue_before = queue_terminal;
    if (providers->queue.ops->prepare_query(
            providers->queue.context, NULL, &queue_terminal, &ready) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        !ready || !bytes_equal(&queue_terminal, &queue_before,
                               sizeof(queue_terminal))) {
        return 0;
    }
    if (providers->queue.ops->finish_start(providers->queue.context, NULL,
                                           &submit) !=
            FWLAB_HIF_ACTION_REJECTED ||
        !bytes_equal(&submit, &submit_before, sizeof(submit)) ||
        providers->queue.ops->finish_query(
            providers->queue.context, NULL, &queue_terminal, &ready) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        !ready || !bytes_equal(&queue_terminal, &queue_before,
                               sizeof(queue_terminal)) ||
        providers->queue.ops->cancel(providers->queue.context, NULL) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        providers->queue.ops->retire(providers->queue.context, NULL) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        providers->queue.ops->reset_begin(providers->queue.context, 1) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        providers->queue.ops->quiescent(providers->queue.context, 1,
                                        &quiescent) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        !quiescent) {
        return 0;
    }

    memset(&target_terminal, 0xa5, sizeof(target_terminal));
    target_before = target_terminal;
    if (providers->target.ops->submit(providers->target.context, NULL,
                                      &submit) !=
            FWLAB_HIF_ACTION_REJECTED ||
        providers->target.ops->query(providers->target.context, NULL,
                                     &target_terminal, &ready) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        !ready || !bytes_equal(&target_terminal, &target_before,
                               sizeof(target_terminal)) ||
        providers->target.ops->cancel(providers->target.context, NULL) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        providers->target.ops->release(providers->target.context, NULL) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        providers->target.ops->release_query(providers->target.context,
                                             NULL) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        providers->target.ops->reset_begin(providers->target.context, 1) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        providers->target.ops->quiescent(providers->target.context, 1,
                                         &quiescent) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        !quiescent || !bytes_equal(&submit, &submit_before, sizeof(submit))) {
        return 0;
    }

    memset(&block_terminal, 0xa5, sizeof(block_terminal));
    block_before = block_terminal;
    if (providers->block.ops->submit(providers->block.context, NULL,
                                     &submit) !=
            FWLAB_HIF_ACTION_REJECTED ||
        providers->block.ops->query(providers->block.context, NULL,
                                    &block_terminal, &ready) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        !ready || !bytes_equal(&block_terminal, &block_before,
                               sizeof(block_terminal)) ||
        providers->block.ops->cancel(providers->block.context, NULL) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        providers->block.ops->retire(providers->block.context, NULL) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        providers->block.ops->reset_begin(providers->block.context, 1) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        providers->block.ops->quiescent(providers->block.context, 1,
                                        &quiescent) !=
            FWLAB_C43_API_NOT_IMPLEMENTED ||
        !quiescent || !bytes_equal(&submit, &submit_before, sizeof(submit))) {
        return 0;
    }

    if (services->overflow || services->event_count != 21) {
        return 0;
    }
    for (index = 0; index < services->event_count; ++index) {
        if (services->events[index].sequence != index + 1 ||
            services->events[index].kind != index + 1) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    _Alignas(max_align_t) uint8_t arena[8192];
    uint8_t identify[FWLAB_C43_IDENTIFY_BYTES];
    uint8_t before[FWLAB_C43_IDENTIFY_BYTES];
    struct c43_fake_services services;
    struct fwlab_c43_graph_providers providers;
    struct fwlab_c43_graph_config config = fixed_config();
    struct fwlab_c43_graph_observer observer;
    struct fwlab_c43_identify_recipe recipe = {0};
    struct fwlab_c43_step_result step_result;
    struct fwlab_c43_step_result step_before;
    struct fwlab_c43_graph *graph = NULL;
    size_t alignment;
    size_t required;

    c43_fake_services_init(&services);
    c43_fake_services_providers(&services, &providers);
    required = fwlab_c43_graph_arena_size(&config);
    alignment = fwlab_c43_graph_arena_alignment();
    if (required == 0 || required > sizeof(arena) || alignment == 0 ||
        (alignment & (alignment - 1)) != 0 ||
        !check_init_failures(required, &config, &providers) ||
        fwlab_c43_graph_init(arena, required, &config, &providers, &graph) !=
            FWLAB_C43_GRAPH_OK ||
        fwlab_c43_graph_observer_read(graph, &observer) !=
            FWLAB_C43_GRAPH_OK ||
        !fwlab_c43_graph_observer_valid(&observer) ||
        services.event_count != 0) {
        return 1;
    }

    memset(&step_result, 0xa5, sizeof(step_result));
    memset(&step_before, 0, sizeof(step_before));
    step_before.version = FWLAB_C43_GRAPH_VERSION;
    step_before.size = sizeof(step_before);
    step_before.requested_budget = 1;
    if (fwlab_c43_graph_step(graph, 1, &step_result) !=
            FWLAB_C43_GRAPH_OK ||
        !bytes_equal(&step_result, &step_before, sizeof(step_result)) ||
        services.event_count != 0) {
        return 1;
    }

    recipe.version = FWLAB_C43_POLICY_VERSION;
    recipe.size = sizeof(recipe);
    recipe.kind = FWLAB_C43_IDENTIFY_CONTROLLER;
    recipe.payload_bytes = FWLAB_C43_IDENTIFY_BYTES;
    recipe.identity_version = 1;
    memset(identify, 0xa5, sizeof(identify));
    memcpy(before, identify, sizeof(before));
    if (fwlab_c43_identify_encode(&recipe, identify, sizeof(identify)) !=
            FWLAB_C43_API_OK ||
        bytes_equal(identify, before, sizeof(identify)) ||
        memcmp(identify + 4, "FWLABC43P1-000000001", 20) != 0 ||
        identify[516] != 1 || identify[517] != 0 || identify[525] != 1) {
        return 1;
    }
    recipe.identity_version = 0;
    memset(identify, 0xa5, sizeof(identify));
    memcpy(before, identify, sizeof(before));
    if (fwlab_c43_identify_encode(&recipe, identify, sizeof(identify)) !=
            FWLAB_C43_API_INVALID ||
        !bytes_equal(identify, before, sizeof(identify)) ||
        !check_fake_entrypoints(&services, &providers)) {
        return 1;
    }
    puts("C4.3 core fake-link: PASS");
    return 0;
}
