/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C34_TEST_SUPPORT_H
#define FWLAB_C34_TEST_SUPPORT_H

#include "../c34.h"
#include "../fakes/c34_buffer.h"
#include "../fakes/c34_memory_media.h"
#include "fwlab/portable/c31.h"
#include "fwlab/portable/nfc_model.h"

#include <stdalign.h>

union c34_test_arena {
    max_align_t alignment;
    uint8_t bytes[262144];
};

struct c34_test_environment {
    union c34_test_arena nfc_arena;
    union c34_test_arena c34_arena;
    union c34_test_arena c31_arena;
    struct c34_memory_media media;
    struct c34_fake_buffer buffer;
    struct fwlab_nfc_model *nfc_model;
    struct c34 *c34;
    struct fwlab_c31 *c31;
    uint64_t nonce;
    uint64_t next_token;
};

struct fwlab_nfc_model_config c34_test_nfc_config(uint64_t seed);
int c34_test_init(
    struct c34_test_environment *environment,
    uint8_t cache_enabled,
    uint64_t nonce,
    uint64_t seed
);
int c34_test_init_bound(
    struct c34_test_environment *environment,
    uint8_t cache_enabled,
    uint64_t nonce,
    uint64_t seed,
    const struct fwlab_nand_media *media,
    const struct c34_physical_txn_provider *physical
);

int c34_test_submit(
    struct c34_test_environment *environment,
    const struct c34_request *request,
    struct fwlab_c31_command_handle *command,
    struct fwlab_c31_completion_lease *lease,
    struct fwlab_c31_completion_intent *intent,
    struct c34_command_result *result
);

int c34_test_consume(
    struct c34_test_environment *environment,
    const struct fwlab_c31_command_handle *command,
    const struct fwlab_c31_completion_lease *lease
);

int c34_test_pump_quiescent(
    struct c34_test_environment *environment,
    unsigned int limit
);

struct c34_request c34_test_write(
    uint8_t atom_mask,
    uint8_t durability,
    uint32_t sequence,
    uint8_t fill0,
    uint8_t fill1
);
struct c34_request c34_test_read(uint8_t atom);
struct c34_request c34_test_trim(
    uint8_t atom_mask,
    uint8_t durability,
    uint32_t sequence
);
struct c34_request c34_test_fence(uint32_t frontier);

#endif
