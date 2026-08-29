/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _POSIX_C_SOURCE 200809L

#include "c34_file_test_support.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static const uint8_t uuid[16] = {
    0x34, 0x04, 0x50, 0x4f, 0x53, 0x49, 0x58, 0x01,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
};

static int program(struct c34_file_media *media)
{
    struct fwlab_nand_media nand = c34_file_nand_media(media);
    struct c34_physical_txn_provider physical =
        c34_file_txn_provider(media);
    struct fwlab_nfc_operation_token inner;
    struct c34_physical_receipt receipt;
    struct fwlab_nand_media_result result;
    struct fwlab_nfc_ppa ppa;
    uint8_t main[C34F_MAIN_BYTES];
    uint8_t oob[C34F_OOB_BYTES];

    memset(&ppa, 0, sizeof(ppa));
    memset(main, 0x39, sizeof(main));
    memset(oob, 0x93, sizeof(oob));
    CHECK(c34f_test_bind_program(media, 1, ppa, main, oob, &inner));
    CHECK(nand.ops->program(
              nand.context, &ppa, main, sizeof(main), oob, sizeof(oob),
              sizeof(main), sizeof(oob), FWLAB_NFC_INTEGRITY_COMPLETE,
              &result) == FWLAB_NFC_API_OK);
    CHECK(physical.ops->receipt(
              physical.context, &inner, &receipt) == C34_PHYSICAL_TXN_OK &&
          receipt.committed == 1);
    return 1;
}

static int test_safe_fd_and_restart(void)
{
    char path[] = "/tmp/c34-file-posix-XXXXXX";
    union c34f_test_arena arena;
    union c34f_test_arena restart_arena;
    struct c34_file_media *media;
    struct c34_file_media *restart;
    int fd = mkstemp(path);
    int restart_fd;
    uint64_t hash;

    CHECK(fd >= 0);
    CHECK(c34_file_posix_format(
              arena.bytes, sizeof(arena.bytes), fd, uuid, &media) ==
          C34_FILE_INVALID);
    CHECK(unlink(path) == 0);
    CHECK(c34_file_posix_format(
              arena.bytes, sizeof(arena.bytes), fd, uuid, &media) ==
          C34_FILE_OK);
    CHECK(program(media));
    hash = c34_file_physical_hash(media);
    restart_fd = dup(fd);
    CHECK(restart_fd >= 0 && close(fd) == 0);
    CHECK(c34_file_posix_restart(
              restart_arena.bytes, sizeof(restart_arena.bytes), restart_fd,
              uuid, &restart) == C34_FILE_OK);
    CHECK(c34_file_physical_hash(restart) == hash);
    CHECK(close(restart_fd) == 0);
    return 1;
}

static int test_reject_wrong_target(void)
{
    char path[] = "/tmp/c34-file-size-XXXXXX";
    union c34f_test_arena arena;
    struct c34_file_media *media;
    int fd = mkstemp(path);
    int directory;

    CHECK(fd >= 0 && unlink(path) == 0 && ftruncate(fd, 127) == 0);
    CHECK(c34_file_posix_restart(
              arena.bytes, sizeof(arena.bytes), fd, uuid, &media) ==
          C34_FILE_INVALID);
    CHECK(close(fd) == 0);
    directory = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(directory >= 0);
    CHECK(c34_file_posix_format(
              arena.bytes, sizeof(arena.bytes), directory, uuid, &media) ==
          C34_FILE_INVALID);
    CHECK(close(directory) == 0);
    return 1;
}

int main(void)
{
    CHECK(test_safe_fd_and_restart());
    CHECK(test_reject_wrong_target());
    puts("C3.4 POSIX disposable-file adapter: PASS (2 cases)");
    return 0;
}
