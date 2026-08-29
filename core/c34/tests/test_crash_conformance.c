/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_test_support.h"
#include "c34_file_media.h"
#include "c34_file_test_support.h"

#include <stdio.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static const uint8_t uuid[16] = {
    0x34, 0x04, 0x43, 0x52, 0x41, 0x53, 0x48, 0x01,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
};

static int recover_kind(
    const struct c34f_memory_substrate *source,
    uint64_t nonce,
    uint8_t *kind
)
{
    struct c34f_memory_substrate restart_substrate;
    struct c34_file_substrate provider;
    union c34f_test_arena file_arena;
    struct c34_file_media *file;
    struct fwlab_nand_media media;
    struct c34_physical_txn_provider physical;
    struct c34_test_environment environment;
    struct c34_logical_entry entry;

    c34f_memory_substrate_restart_image(source, &restart_substrate);
    provider = c34f_memory_substrate_provider(&restart_substrate);
    CHECK(c34_file_restart(
              file_arena.bytes, sizeof(file_arena.bytes), &provider, uuid,
              &file) == C34_FILE_OK);
    media = c34_file_nand_media(file);
    physical = c34_file_txn_provider(file);
    CHECK(c34_test_init_bound(
        &environment, 0, nonce, UINT64_C(0x9b6d3e7a4c2158f1),
        &media, &physical));
    CHECK(c34_logical_state(environment.c34, 0, &entry) == C34_OK);
    *kind = entry.kind;
    return 1;
}

static int run_cut(unsigned int cut, int volatile_success)
{
    struct c34f_memory_substrate substrate;
    struct c34_file_substrate provider;
    union c34f_test_arena file_arena;
    struct c34_file_media *file;
    struct fwlab_nand_media media;
    struct c34_physical_txn_provider physical;
    struct c34_test_environment environment;
    struct c34_request write = c34_test_write(
        1, volatile_success ? FWLAB_PERSIST_DEFAULT :
                              FWLAB_PERSIST_SELF_DURABLE,
        1, 0x91, 0);
    uint32_t baseline;
    uint8_t recovered;

    c34f_memory_substrate_init(&substrate);
    provider = c34f_memory_substrate_provider(&substrate);
    CHECK(c34_file_format(
              file_arena.bytes, sizeof(file_arena.bytes), &provider, uuid,
              &file) == C34_FILE_OK);
    baseline = substrate.barriers;
    media = c34_file_nand_media(file);
    physical = c34_file_txn_provider(file);
    CHECK(c34_test_init_bound(
        &environment, volatile_success ? 1 : 0,
        UINT64_C(0x3420) + cut + (volatile_success ? 32u : 0u),
        UINT64_C(0x9b6d3e7a4c2158f1), &media, &physical));
    substrate.cut_after_barrier = baseline + cut;
    if (volatile_success) {
        struct fwlab_c31_command_handle command;
        struct fwlab_c31_completion_lease lease;
        struct fwlab_c31_completion_intent intent;
        struct c34_command_result result;

        CHECK(c34_test_submit(
                  &environment, &write, &command, &lease, &intent, &result));
        CHECK(result.witness.witness_class ==
              FWLAB_PERSIST_VOLATILE_ELIGIBLE);
        CHECK(c34_test_consume(&environment, &command, &lease));
        (void)c34_test_pump_quiescent(&environment, 4096);
    } else {
        struct fwlab_c31_command_handle command;
        struct fwlab_c31_completion_lease lease;
        struct fwlab_c31_completion_intent intent;
        struct c34_command_result result;

        CHECK(!c34_test_submit(
            &environment, &write, &command, &lease, &intent, &result));
    }
    CHECK(recover_kind(
        &substrate,
        UINT64_C(0x3460) + cut + (volatile_success ? 32u : 0u),
        &recovered));
    CHECK(recovered == (cut >= 6 ? C34_LOGICAL_VALUE : C34_LOGICAL_NONE));
    return 1;
}

static int test_durable_floor(void)
{
    struct c34f_memory_substrate substrate;
    struct c34f_memory_substrate restart_substrate;
    struct c34_file_substrate provider;
    union c34f_test_arena file_arena;
    union c34f_test_arena restart_file_arena;
    struct c34_file_media *file;
    struct c34_file_media *restart_file;
    struct fwlab_nand_media media;
    struct c34_physical_txn_provider physical;
    struct c34_test_environment environment;
    struct c34_test_environment restart_environment;
    struct c34_request write = c34_test_write(
        1, FWLAB_PERSIST_SELF_DURABLE, 1, 0xa1, 0);
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;
    struct c34_command_result result;
    struct c34_logical_entry entry;

    c34f_memory_substrate_init(&substrate);
    provider = c34f_memory_substrate_provider(&substrate);
    CHECK(c34_file_format(
              file_arena.bytes, sizeof(file_arena.bytes), &provider, uuid,
              &file) == C34_FILE_OK);
    media = c34_file_nand_media(file);
    physical = c34_file_txn_provider(file);
    CHECK(c34_test_init_bound(
        &environment, 0, UINT64_C(0x34a0),
        UINT64_C(0x9b6d3e7a4c2158f1), &media, &physical));
    CHECK(c34_test_submit(
              &environment, &write, &command, &lease, &intent, &result) &&
          result.witness.witness_class ==
              FWLAB_PERSIST_DURABLE_ELIGIBLE &&
          c34_test_consume(&environment, &command, &lease));
    c34f_memory_substrate_restart_image(&substrate, &restart_substrate);
    provider = c34f_memory_substrate_provider(&restart_substrate);
    CHECK(c34_file_restart(
              restart_file_arena.bytes, sizeof(restart_file_arena.bytes),
              &provider, uuid, &restart_file) == C34_FILE_OK);
    media = c34_file_nand_media(restart_file);
    physical = c34_file_txn_provider(restart_file);
    CHECK(c34_test_init_bound(
        &restart_environment, 0, UINT64_C(0x34a1),
        UINT64_C(0x9b6d3e7a4c2158f1), &media, &physical));
    CHECK(c34_logical_state(restart_environment.c34, 0, &entry) == C34_OK &&
          entry.kind == C34_LOGICAL_VALUE);
    return 1;
}

int main(void)
{
    unsigned int cut;

    for (cut = 1; cut <= 8; ++cut) {
        CHECK(run_cut(cut, 0));
        CHECK(run_cut(cut, 1));
    }
    CHECK(test_durable_floor());
    puts("C3.4 C3.2/file crash conformance: PASS (17 cases)");
    return 0;
}
