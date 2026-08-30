/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_support.h"

#include <string.h>

static struct c42_counter_seed seed(uint64_t next)
{
    struct c42_counter_seed value = {0};

    value.next = next;
    value.maximum = next + UINT64_C(1000000);
    return value;
}

static struct c42_queue_memory_cap capability(
    const struct c42_test_fixture *fixture,
    uint16_t queue_id,
    uint8_t role,
    uint32_t ring_generation,
    uint32_t mapping_generation)
{
    struct c42_queue_memory_cap value = {0};

    value.instance_nonce = fixture->config.instance_nonce;
    value.owner_epoch = fixture->config.owner_epoch;
    value.memory_uid = UINT64_C(0x90000000) +
                       (uint64_t)role * UINT64_C(0x10000) + queue_id;
    value.controller_epoch = fixture->config.initial_controller_epoch;
    value.ring_generation = ring_generation;
    value.mapping_generation = mapping_generation;
    value.exact_bytes = (uint32_t)fixture->depth *
                        (role == C42_MEMORY_SQ_READ ?
                         C42_SQE_BYTES : C42_CQE_BYTES);
    value.queue_id = queue_id;
    value.role = role;
    return value;
}

static int create_queue(
    struct c42_test_fixture *fixture,
    const struct c42_queue_memory_cap *cap,
    uint8_t kind,
    uint8_t queue_class)
{
    struct c42_queue_descriptor descriptor = {0};
    struct c42_operation_token token = {0};
    struct c42_candidate_status status = {0};

    descriptor.version = C42_COMPONENT_VERSION;
    descriptor.size = sizeof(descriptor);
    descriptor.queue_id = cap->queue_id;
    descriptor.associated_cq_id = cap->queue_id;
    descriptor.depth = fixture->depth;
    descriptor.kind = kind;
    descriptor.queue_class = queue_class;
    descriptor.memory = *cap;
    if (c42_candidate_prepare(
            fixture->controller, &descriptor, &token) != C42_OK) {
        return 0;
    }
    if (kind == C42_QUEUE_CQ &&
        c42_candidate_progress(fixture->controller, &token, 4) != C42_OK) {
        return 0;
    }
    if (c42_candidate_query(
            fixture->controller, &token, &status) != C42_OK ||
        status.state != C42_CANDIDATE_READY) {
        return 0;
    }
    if (c42_candidate_commit(fixture->controller, &token) != C42_OK ||
        c42_candidate_retire(fixture->controller, &token) != C42_OK) {
        return 0;
    }
    return 1;
}

int c42_test_create_pair(
    struct c42_test_fixture *fixture,
    uint16_t queue_id,
    uint32_t ring_generation,
    uint32_t mapping_generation)
{
    uint8_t queue_class = queue_id == 0 ?
                          FWLAB_NVME_QUEUE_ADMIN : FWLAB_NVME_QUEUE_IO;

    if (fixture == NULL || queue_id >= C42_MAX_QUEUE_PAIRS) {
        return 0;
    }
    fixture->cq_cap[queue_id] = capability(
        fixture, queue_id, C42_MEMORY_CQ_PUBLISH,
        ring_generation, mapping_generation
    );
    fixture->sq_cap[queue_id] = capability(
        fixture, queue_id, C42_MEMORY_SQ_READ,
        ring_generation, mapping_generation
    );
    if (c42_fake_memory_map(
            &fixture->memory, &fixture->cq_cap[queue_id], fixture->depth) !=
            C42_OK ||
        c42_fake_memory_map(
            &fixture->memory, &fixture->sq_cap[queue_id], fixture->depth) !=
            C42_OK ||
        !create_queue(
            fixture, &fixture->cq_cap[queue_id], C42_QUEUE_CQ, queue_class) ||
        !create_queue(
            fixture, &fixture->sq_cap[queue_id], C42_QUEUE_SQ, queue_class)) {
        return 0;
    }
    return 1;
}

int c42_test_fixture_init_profile(
    struct c42_test_fixture *fixture,
    uint16_t depth,
    int with_io,
    uint64_t instance_nonce,
    uint32_t controller_epoch,
    uint32_t active_generation)
{
    size_t required;

    if (fixture == NULL || depth < 2 || depth > C42_MAX_QUEUE_DEPTH) {
        return 0;
    }
    memset(fixture, 0, sizeof(*fixture));
    fixture->depth = depth;
    fixture->config.version = C42_COMPONENT_VERSION;
    fixture->config.size = sizeof(fixture->config);
    fixture->config.maximum_queue_depth = C42_MAX_QUEUE_DEPTH;
    fixture->config.command_capacity = 8;
    fixture->config.target_capacity = 8;
    fixture->config.worst_case_actions = 8;
    fixture->config.safety_generation = 71;
    fixture->config.instance_nonce = instance_nonce;
    fixture->config.owner_epoch = UINT64_C(0x2000000000000001);
    fixture->config.origin_domain_nonce = UINT64_C(0x3000000000000001);
    fixture->config.origin_uid = seed(10001);
    fixture->config.client_uid = seed(20001);
    fixture->config.release_uid = seed(25001);
    fixture->config.trace_uid = seed(30001);
    fixture->config.publication_uid = seed(40001);
    fixture->config.notification_uid = seed(50001);
    fixture->config.candidate_uid = seed(60001);
    fixture->config.target_uid = seed(70001);
    fixture->config.control_uid = seed(80001);
    fixture->config.reset_uid = seed(90001);
    fixture->config.teardown_uid = seed(100001);
    fixture->config.initial_controller_epoch = controller_epoch;
    fixture->config.initial_active_generation = active_generation;
    c42_fake_memory_init(
        &fixture->memory, fixture->config.instance_nonce,
        fixture->config.owner_epoch,
        fixture->config.initial_controller_epoch
    );
    c42_fake_command_init(
        &fixture->command, fixture->config.instance_nonce,
        fixture->config.initial_controller_epoch, 4
    );
    fixture->providers.memory = c42_fake_memory_port(&fixture->memory);
    fixture->providers.command = c42_fake_command_port(&fixture->command);
    required = c42_arena_size(&fixture->config);
    if (required == 0 || required > sizeof(fixture->arena.bytes) ||
        c42_init(
            fixture->arena.bytes, sizeof(fixture->arena.bytes),
            &fixture->config, &fixture->providers,
            &fixture->controller) != C42_OK ||
        !c42_test_create_pair(fixture, 0, 1, 1) ||
        c42_enable(fixture->controller) != C42_OK) {
        return 0;
    }
    if (with_io != 0 && !c42_test_create_pair(fixture, 1, 1, 1)) {
        return 0;
    }
    return 1;
}

int c42_test_fixture_init_with_identity(
    struct c42_test_fixture *fixture,
    uint16_t depth,
    int with_io,
    uint64_t instance_nonce,
    uint32_t controller_epoch)
{
    return c42_test_fixture_init_profile(
        fixture, depth, with_io, instance_nonce, controller_epoch, 101
    );
}

int c42_test_fixture_init_with_nonce(
    struct c42_test_fixture *fixture,
    uint16_t depth,
    int with_io,
    uint64_t instance_nonce)
{
    return c42_test_fixture_init_with_identity(
        fixture, depth, with_io, instance_nonce, 11
    );
}

int c42_test_fixture_init(
    struct c42_test_fixture *fixture,
    uint16_t depth,
    int with_io)
{
    return c42_test_fixture_init_with_nonce(
        fixture, depth, with_io, UINT64_C(0x1122334455667788)
    );
}

void c42_test_sqe(
    uint8_t bytes[C42_SQE_BYTES],
    uint8_t opcode,
    uint16_t command_id,
    uint32_t namespace_id,
    uint32_t dword10)
{
    memset(bytes, 0, C42_SQE_BYTES);
    bytes[0] = opcode;
    bytes[2] = (uint8_t)command_id;
    bytes[3] = (uint8_t)(command_id >> 8);
    bytes[4] = (uint8_t)namespace_id;
    bytes[5] = (uint8_t)(namespace_id >> 8);
    bytes[6] = (uint8_t)(namespace_id >> 16);
    bytes[7] = (uint8_t)(namespace_id >> 24);
    bytes[40] = (uint8_t)dword10;
    bytes[41] = (uint8_t)(dword10 >> 8);
    bytes[42] = (uint8_t)(dword10 >> 16);
    bytes[43] = (uint8_t)(dword10 >> 24);
}

int c42_test_submit(
    struct c42_test_fixture *fixture,
    uint16_t queue_id,
    uint16_t slot,
    uint16_t new_tail,
    uint16_t command_id)
{
    uint8_t bytes[C42_SQE_BYTES];
    struct c42_sq_tail_event event = {0};

    if (fixture == NULL || queue_id >= C42_MAX_QUEUE_PAIRS) {
        return 0;
    }
    c42_test_sqe(bytes, 0x02, command_id, 1, command_id);
    if (c42_fake_memory_write_sqe(
            &fixture->memory, queue_id, slot, bytes) != C42_OK) {
        return 0;
    }
    event.instance_nonce = fixture->config.instance_nonce;
    event.controller_epoch = fixture->config.initial_controller_epoch;
    event.ring_generation = fixture->sq_cap[queue_id].ring_generation;
    event.queue_id = queue_id;
    event.new_tail = new_tail;
    return c42_sq_tail_event_apply(fixture->controller, &event) == C42_OK;
}

int c42_test_run(
    struct c42_test_fixture *fixture,
    uint32_t calls,
    uint32_t budget)
{
    uint32_t index;

    if (fixture == NULL) {
        return 0;
    }
    for (index = 0; index < calls; ++index) {
        struct c42_step_result result = {0};
        enum c42_result step = c42_step(
            fixture->controller, budget, &result
        );

        if (step != C42_OK && step != C42_FAULTED) {
            return 0;
        }
        if (result.units_executed == 0) {
            break;
        }
    }
    return 1;
}
