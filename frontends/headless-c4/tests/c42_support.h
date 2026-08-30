/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C42_TEST_SUPPORT_H
#define FWLAB_C42_TEST_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

#include "fakes/c42_command.h"
#include "fakes/c42_memory.h"

#define C42_TEST_ARENA_BYTES 262144u

union c42_test_arena {
    max_align_t alignment;
    uint8_t bytes[C42_TEST_ARENA_BYTES];
};

struct c42_test_fixture {
    union c42_test_arena arena;
    struct c42_fake_memory memory;
    struct c42_fake_command command;
    struct c42_config config;
    struct c42_providers providers;
    struct c42_controller *controller;
    struct c42_queue_memory_cap sq_cap[C42_MAX_QUEUE_PAIRS];
    struct c42_queue_memory_cap cq_cap[C42_MAX_QUEUE_PAIRS];
    uint16_t depth;
};

int c42_test_fixture_init(
    struct c42_test_fixture *fixture,
    uint16_t depth,
    int with_io
);
int c42_test_fixture_init_with_nonce(
    struct c42_test_fixture *fixture,
    uint16_t depth,
    int with_io,
    uint64_t instance_nonce
);
int c42_test_fixture_init_with_identity(
    struct c42_test_fixture *fixture,
    uint16_t depth,
    int with_io,
    uint64_t instance_nonce,
    uint32_t controller_epoch
);
int c42_test_fixture_init_profile(
    struct c42_test_fixture *fixture,
    uint16_t depth,
    int with_io,
    uint64_t instance_nonce,
    uint32_t controller_epoch,
    uint32_t active_generation
);
int c42_test_create_pair(
    struct c42_test_fixture *fixture,
    uint16_t queue_id,
    uint32_t ring_generation,
    uint32_t mapping_generation
);
void c42_test_sqe(
    uint8_t bytes[C42_SQE_BYTES],
    uint8_t opcode,
    uint16_t command_id,
    uint32_t namespace_id,
    uint32_t dword10
);
int c42_test_submit(
    struct c42_test_fixture *fixture,
    uint16_t queue_id,
    uint16_t slot,
    uint16_t new_tail,
    uint16_t command_id
);
int c42_test_run(
    struct c42_test_fixture *fixture,
    uint32_t calls,
    uint32_t budget
);

#endif
