/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _POSIX_C_SOURCE 200809L

#include "c34_test_support.h"
#include "c34_file_media.h"
#include "c34_file_test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static int test_full_posix_restart(void)
{
    static const uint8_t uuid[16] = {
        0x34, 0x04, 0x46, 0x55, 0x4c, 0x4c, 0x50, 0x58,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    };
    char path[] = "/tmp/c34-full-posix-XXXXXX";
    union c34f_test_arena file_arena;
    union c34f_test_arena restart_file_arena;
    struct c34_file_media *file;
    struct c34_file_media *restart_file;
    struct fwlab_nand_media media;
    struct c34_physical_txn_provider physical;
    struct c34_test_environment environment;
    struct c34_test_environment restart_environment;
    struct c34_request write = c34_test_write(
        1, FWLAB_PERSIST_SELF_DURABLE, 1, 0xb4, 0);
    struct c34_request read = c34_test_read(0);
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;
    struct c34_command_result result;
    int fd = mkstemp(path);
    int restart_fd;

    CHECK(fd >= 0 && unlink(path) == 0);
    CHECK(c34_file_posix_format(
              file_arena.bytes, sizeof(file_arena.bytes), fd, uuid, &file) ==
          C34_FILE_OK);
    media = c34_file_nand_media(file);
    physical = c34_file_txn_provider(file);
    CHECK(c34_test_init_bound(
        &environment, 0, UINT64_C(0x34b0),
        UINT64_C(0x9b6d3e7a4c2158f1), &media, &physical));
    CHECK(c34_test_submit(
              &environment, &write, &command, &lease, &intent, &result) &&
          result.witness.witness_class ==
              FWLAB_PERSIST_DURABLE_ELIGIBLE &&
          c34_test_consume(&environment, &command, &lease));
    restart_fd = dup(fd);
    CHECK(restart_fd >= 0 && close(fd) == 0);
    CHECK(c34_file_posix_restart(
              restart_file_arena.bytes, sizeof(restart_file_arena.bytes),
              restart_fd, uuid, &restart_file) == C34_FILE_OK);
    media = c34_file_nand_media(restart_file);
    physical = c34_file_txn_provider(restart_file);
    CHECK(c34_test_init_bound(
        &restart_environment, 0, UINT64_C(0x34b1),
        UINT64_C(0x9b6d3e7a4c2158f1), &media, &physical));
    CHECK(c34_test_submit(
              &restart_environment, &read, &command, &lease, &intent,
              &result) &&
          result.present_mask == 1 && result.payload[0][0] == 0xb4 &&
          result.payload[0][15] == 0xb4 &&
          c34_test_consume(&restart_environment, &command, &lease));
    CHECK(close(restart_fd) == 0);
    return 1;
}

int main(void)
{
    CHECK(test_full_posix_restart());
    puts("C3.4 full POSIX file restart integration: PASS");
    return 0;
}
