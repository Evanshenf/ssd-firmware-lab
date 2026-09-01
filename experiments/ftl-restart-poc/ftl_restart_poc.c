/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _POSIX_C_SOURCE 200809L

#include "c34_file_media.h"
#include "c34_file_test_support.h"
#include "c34_test_support.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define REQUIRE(condition, message)                                         \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "FTL POC FAIL: %s\n", message);               \
            return 0;                                                       \
        }                                                                   \
    } while (0)

static const uint8_t image_uuid[16] = {
    0x46, 0x54, 0x4c, 0x50, 0x4f, 0x43, 0x00, 0x01,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
};

static int execute(
    struct c34_test_environment *environment,
    const struct c34_request *request,
    struct c34_command_result *result)
{
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;

    REQUIRE(c34_test_submit(
                environment, request, &command, &lease, &intent, result),
            "command did not reach completion-ready");
    REQUIRE(intent.result == FWLAB_C31_COMPLETION_SUCCESS,
            "completion intent was not success");
    REQUIRE(result->status == C34_COMMAND_SUCCESS,
            "C34 command result was not success");
    REQUIRE(c34_test_consume(environment, &command, &lease),
            "completion/result consume failed");
    return 1;
}

static int check_payload(
    struct c34_test_environment *environment,
    uint8_t atom,
    uint8_t present,
    uint8_t expected)
{
    struct c34_request request = c34_test_read(atom);
    struct c34_command_result result;

    REQUIRE(execute(environment, &request, &result), "read failed");
    REQUIRE(((result.present_mask >> atom) & 1u) == present,
            "read presence differs");
    if (present != 0) {
        unsigned int index;

        for (index = 0; index < C34_ATOM_BYTES; ++index) {
            REQUIRE(result.payload[atom][index] == expected,
                    "read payload differs");
        }
    }
    return 1;
}

static int run_poc(void)
{
    char path[] = "/tmp/fwlab-ftl-poc-XXXXXX";
    union c34f_test_arena first_file_arena;
    union c34f_test_arena restart_file_arena;
    struct c34_file_media *first_file;
    struct c34_file_media *restart_file;
    struct fwlab_nand_media media;
    struct c34_physical_txn_provider physical;
    struct c34_test_environment first;
    struct c34_test_environment restarted;
    struct c34_command_result result;
    struct c34_logical_entry atom0;
    struct c34_logical_entry atom1;
    struct c34_request write_both = c34_test_write(
        3, FWLAB_PERSIST_SELF_DURABLE, 1, 0xa1, 0xb2);
    struct c34_request trim0 = c34_test_trim(
        1, FWLAB_PERSIST_SELF_DURABLE, 2);
    struct c34_request overwrite1 = c34_test_write(
        2, FWLAB_PERSIST_SELF_DURABLE, 3, 0, 0xc3);
    int fd = mkstemp(path);
    int restart_fd;
    uint64_t image_hash_before;
    uint64_t physical_hash_before;

    REQUIRE(fd >= 0, "mkstemp failed");
    REQUIRE(unlink(path) == 0, "temporary image unlink failed");
    REQUIRE(c34_file_posix_format(
                first_file_arena.bytes, sizeof(first_file_arena.bytes), fd,
                image_uuid, &first_file) == C34_FILE_OK,
            "file-media format failed");
    media = c34_file_nand_media(first_file);
    physical = c34_file_txn_provider(first_file);
    REQUIRE(c34_test_init_bound(
                &first, 0, UINT64_C(0x46544c500001),
                UINT64_C(0x9b6d3e7a4c2158f1), &media, &physical),
            "initial firmware stack init failed");

    REQUIRE(execute(&first, &write_both, &result), "two-atom write failed");
    REQUIRE(check_payload(&first, 0, 1, 0xa1), "atom 0 readback failed");
    REQUIRE(check_payload(&first, 1, 1, 0xb2), "atom 1 readback failed");
    REQUIRE(c34_checkpoint_start(first.c34) == C34_OK,
            "checkpoint start failed");
    REQUIRE(c34_test_pump_quiescent(&first, 4096),
            "checkpoint did not quiesce");
    REQUIRE(execute(&first, &trim0, &result), "atom 0 trim failed");
    REQUIRE(execute(&first, &overwrite1, &result),
            "atom 1 overwrite failed");
    REQUIRE(check_payload(&first, 0, 0, 0),
            "trimmed atom remained visible");
    REQUIRE(check_payload(&first, 1, 1, 0xc3),
            "overwritten atom readback failed");

    image_hash_before = c34_file_image_hash(first_file);
    physical_hash_before = c34_file_physical_hash(first_file);
    restart_fd = dup(fd);
    REQUIRE(restart_fd >= 0, "image fd duplication failed");
    REQUIRE(close(fd) == 0, "initial image fd close failed");

    REQUIRE(c34_file_posix_restart(
                restart_file_arena.bytes, sizeof(restart_file_arena.bytes),
                restart_fd, image_uuid, &restart_file) == C34_FILE_OK,
            "file-media restart failed");
    REQUIRE(c34_file_image_hash(restart_file) == image_hash_before,
            "image hash changed across restart");
    REQUIRE(c34_file_physical_hash(restart_file) == physical_hash_before,
            "physical hash changed across restart");
    media = c34_file_nand_media(restart_file);
    physical = c34_file_txn_provider(restart_file);
    REQUIRE(c34_test_init_bound(
                &restarted, 0, UINT64_C(0x46544c500002),
                UINT64_C(0x9b6d3e7a4c2158f1), &media, &physical),
            "restarted firmware stack init failed");
    REQUIRE(check_payload(&restarted, 0, 0, 0),
            "trim tombstone resurrected after restart");
    REQUIRE(check_payload(&restarted, 1, 1, 0xc3),
            "atom 1 payload was not recovered");
    REQUIRE(c34_logical_state(restarted.c34, 0, &atom0) == C34_OK &&
                atom0.kind == C34_LOGICAL_TOMBSTONE,
            "atom 0 did not recover as tombstone");
    REQUIRE(c34_logical_state(restarted.c34, 1, &atom1) == C34_OK &&
                atom1.kind == C34_LOGICAL_VALUE,
            "atom 1 did not recover as value");

    printf("FTL POC: image-bytes=%u image-hash=%016" PRIx64
           " physical-hash=%016" PRIx64 "\n",
           C34_FILE_IMAGE_BYTES, image_hash_before, physical_hash_before);
    printf("FTL POC: atom0=tombstone(v%u) atom1=value(v%u,block=%u,page=%u)\n",
           atom0.version, atom1.version, atom1.data_ppa.block,
           atom1.data_ppa.page);
    REQUIRE(close(restart_fd) == 0, "restart image fd close failed");
    puts("FTL file-backed write/trim/restart/recovery POC: PASS");
    return 1;
}

int main(void)
{
    return run_poc() ? 0 : 1;
}
