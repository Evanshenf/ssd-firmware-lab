/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>

#include "c33_test_support.h"

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

static int collect_two(
    struct c33_test_environment *environment,
    const struct fwlab_nfc_request *first,
    const struct fwlab_nfc_request *second,
    struct fwlab_nfc_completion completion[2]
)
{
    struct fwlab_nfc_submit_result one =
        environment->provider.ops->try_submit(environment->provider.context,
                                               first);
    struct fwlab_nfc_submit_result two =
        environment->provider.ops->try_submit(environment->provider.context,
                                               second);
    unsigned int iteration;
    unsigned int count = 0;

    CHECK(one.disposition == FWLAB_NFC_ACCEPTED);
    CHECK(two.disposition == FWLAB_NFC_ACCEPTED);
    for (iteration = 0; iteration < 512 && count < 2; ++iteration) {
        struct fwlab_nfc_step_result step;
        uint32_t emitted = 0;

        CHECK(environment->provider.ops->step(
                  environment->provider.context, 1, &step) ==
              FWLAB_NFC_API_OK);
        CHECK(environment->provider.ops->poll(
                  environment->provider.context, 1, &completion[count], 1,
                  &emitted) == FWLAB_NFC_API_OK);
        count += emitted;
    }
    return count == 2 ? 0 : __LINE__;
}

static int test_same_lun_serial_and_parallel(void)
{
    struct c33_test_environment serial;
    struct c33_test_environment parallel;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_request first;
    struct fwlab_nfc_request second;
    struct fwlab_nfc_completion completion[2];

    CHECK(c33_test_init(&serial, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    first = c33_test_request(
        &serial, FWLAB_NFC_ERASE, (struct fwlab_nfc_ppa){0, 0, 0, 0, 0, 0});
    second = c33_test_request(
        &serial, FWLAB_NFC_ERASE, (struct fwlab_nfc_ppa){0, 0, 1, 0, 0, 0});
    CHECK(collect_two(&serial, &first, &second, completion) == 0);
    CHECK(completion[0].begin_tick < completion[0].status_tick);
    CHECK(completion[1].begin_tick >= completion[0].status_tick);

    config.geometry.plane_parallelism_per_lun = 2;
    CHECK(c33_test_init(&parallel, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    first = c33_test_request(
        &parallel, FWLAB_NFC_ERASE,
        (struct fwlab_nfc_ppa){0, 0, 0, 0, 0, 0});
    second = c33_test_request(
        &parallel, FWLAB_NFC_ERASE,
        (struct fwlab_nfc_ppa){0, 0, 1, 0, 0, 0});
    CHECK(collect_two(&parallel, &first, &second, completion) == 0);
    CHECK(completion[0].begin_tick == completion[1].begin_tick);
    CHECK(completion[0].status_tick == completion[1].status_tick);
    return 0;
}

static int test_cross_channel_overlap(void)
{
    struct c33_test_environment environment;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_request first;
    struct fwlab_nfc_request second;
    struct fwlab_nfc_completion completion[2];

    config.geometry.channels = 2;
    config.geometry.luns_per_channel = 2;
    config.geometry.planes_per_lun = 1;
    config.geometry.blocks_per_plane = 1;
    config.geometry.pages_per_block = 1;
    config.geometry.main_bytes_per_page = 1;
    config.geometry.oob_bytes_per_page = 1;
    config.ecc.main_covered_bytes = 1;
    config.capacity.scratch_main_bytes = 1;
    CHECK(c33_test_init(&environment, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    first = c33_test_request(
        &environment, FWLAB_NFC_ERASE,
        (struct fwlab_nfc_ppa){0, 0, 0, 0, 0, 0});
    second = c33_test_request(
        &environment, FWLAB_NFC_ERASE,
        (struct fwlab_nfc_ppa){1, 1, 0, 0, 0, 0});
    CHECK(collect_two(&environment, &first, &second, completion) == 0);
    CHECK(completion[0].begin_tick == completion[1].begin_tick);
    CHECK(completion[0].status_tick == completion[1].status_tick);
    return 0;
}

static int test_priority_before_submit_order(void)
{
    struct c33_test_environment environment;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_request first;
    struct fwlab_nfc_request second;
    struct fwlab_nfc_completion completion[2];
    uint64_t second_uid;

    CHECK(c33_test_init(&environment, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    first = c33_test_request(
        &environment, FWLAB_NFC_ERASE,
        (struct fwlab_nfc_ppa){0, 0, 0, 0, 0, 0});
    second = c33_test_request(
        &environment, FWLAB_NFC_ERASE,
        (struct fwlab_nfc_ppa){0, 0, 1, 0, 0, 0});
    first.priority = 10;
    second.priority = 0;
    second_uid = second.operation.operation_uid;
    CHECK(collect_two(&environment, &first, &second, completion) == 0);
    CHECK(completion[0].operation.operation_uid == second_uid);
    CHECK(completion[0].begin_tick == 0);
    CHECK(completion[1].begin_tick >= completion[0].status_tick);
    return 0;
}

int main(void)
{
    int line = test_same_lun_serial_and_parallel();

    if (line == 0) {
        line = test_cross_channel_overlap();
    }
    if (line == 0) {
        line = test_priority_before_submit_order();
    }
    if (line != 0) {
        fprintf(stderr, "C3.3 scheduler unit failed at line %d\n", line);
        return 1;
    }
    printf("C3.3 virtual scheduler unit: PASS (3 cases)\n");
    return 0;
}
