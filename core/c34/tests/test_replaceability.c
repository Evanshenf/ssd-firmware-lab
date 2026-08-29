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

static int submit_consume(
    struct c34_test_environment *environment,
    const struct c34_request *request,
    struct c34_command_result *result
)
{
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;

    return c34_test_submit(environment, request, &command, &lease, &intent,
                           result) &&
           intent.result == FWLAB_C31_COMPLETION_SUCCESS &&
           c34_test_consume(environment, &command, &lease);
}

static int test_memory_file_restart_equivalence(void)
{
    static const uint8_t uuid[16] = {
        0x34, 0x04, 0x52, 0x45, 0x50, 0x4c, 0x41, 0x43,
        0x45, 0x01, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    };
    struct c34f_memory_substrate substrate;
    struct c34f_memory_substrate restart_substrate;
    struct c34_file_substrate file_io;
    union c34f_test_arena file_arena;
    union c34f_test_arena restart_file_arena;
    struct c34_file_media *file;
    struct c34_file_media *restart_file;
    struct fwlab_nand_media file_media;
    struct c34_physical_txn_provider file_txn;
    struct fwlab_nand_media restart_media;
    struct c34_physical_txn_provider restart_txn;
    struct c34_test_environment memory_environment;
    struct c34_test_environment file_environment;
    struct c34_test_environment restart_environment;
    struct c34_request write = c34_test_write(
        1, FWLAB_PERSIST_SELF_DURABLE, 1, 0x84, 0);
    struct c34_request read = c34_test_read(0);
    struct c34_command_result memory_result;
    struct c34_command_result file_result;
    struct c34_command_result read_result;
    struct c34_logical_entry file_entry;
    struct c34_logical_entry restart_entry;

    CHECK(c34_test_init(
        &memory_environment, 0, UINT64_C(0x3410),
        UINT64_C(0x9b6d3e7a4c2158f1)));
    CHECK(submit_consume(&memory_environment, &write, &memory_result));

    c34f_memory_substrate_init(&substrate);
    file_io = c34f_memory_substrate_provider(&substrate);
    CHECK(c34_file_format(
              file_arena.bytes, sizeof(file_arena.bytes), &file_io, uuid,
              &file) == C34_FILE_OK);
    file_media = c34_file_nand_media(file);
    file_txn = c34_file_txn_provider(file);
    CHECK(c34_test_init_bound(
        &file_environment, 0, UINT64_C(0x3411),
        UINT64_C(0x9b6d3e7a4c2158f1), &file_media, &file_txn));
    CHECK(submit_consume(&file_environment, &write, &file_result));
    CHECK(memory_result.witness.witness_class ==
              file_result.witness.witness_class &&
          c34_state_hash(memory_environment.c34) ==
              c34_state_hash(file_environment.c34));
    CHECK(c34_logical_state(file_environment.c34, 0, &file_entry) == C34_OK);

    c34f_memory_substrate_restart_image(&substrate, &restart_substrate);
    file_io = c34f_memory_substrate_provider(&restart_substrate);
    CHECK(c34_file_restart(
              restart_file_arena.bytes, sizeof(restart_file_arena.bytes),
              &file_io, uuid, &restart_file) == C34_FILE_OK);
    restart_media = c34_file_nand_media(restart_file);
    restart_txn = c34_file_txn_provider(restart_file);
    CHECK(c34_test_init_bound(
        &restart_environment, 0, UINT64_C(0x3412),
        UINT64_C(0x9b6d3e7a4c2158f1), &restart_media, &restart_txn));
    CHECK(c34_logical_state(
              restart_environment.c34, 0, &restart_entry) == C34_OK &&
          restart_entry.kind == C34_LOGICAL_VALUE &&
          restart_entry.version == file_entry.version &&
          restart_entry.value_crc32c == file_entry.value_crc32c);
    CHECK(submit_consume(&restart_environment, &read, &read_result));
    CHECK(read_result.present_mask == 1 &&
          read_result.payload[0][0] == 0x84 &&
          read_result.payload[0][15] == 0x84);
    return 1;
}

int main(void)
{
    CHECK(test_memory_file_restart_equivalence());
    puts("C3.4 memory/file replaceability: PASS");
    return 0;
}
