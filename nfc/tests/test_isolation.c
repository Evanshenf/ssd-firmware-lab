/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>

#include "c33_test_support.h"

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

static int test_two_instances(void)
{
    struct c33_test_environment first;
    struct c33_test_environment second;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_ppa ppa = {0, 0, 0, 0, 0, 0};
    const uint8_t input[2] = {UINT8_C(0x12), UINT8_C(0x34)};
    struct fwlab_nfc_completion completion;
    uint64_t second_state;
    uint64_t second_media;
    uint8_t output[2];
    uint8_t oob;

    CHECK(c33_test_init(&first, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    config.fault.seed ^= UINT64_C(0xffff);
    CHECK(c33_test_init(&second, &config, NULL, 0,
                        UINT64_C(0x8877665544332211)));
    second_state = fwlab_nfc_model_state_hash(second.model);
    second_media = fwlab_nfc_model_media_hash(second.model);
    CHECK(c33_test_program(&first, ppa, input, UINT8_C(0x56), &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(fwlab_nfc_model_state_hash(second.model) == second_state);
    CHECK(fwlab_nfc_model_media_hash(second.model) == second_media);
    CHECK(c33_test_read(&second, ppa, 0, output, &oob, &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(output[0] == UINT8_C(0xff) && output[1] == UINT8_C(0xff));
    CHECK(oob == UINT8_C(0xff));
    return 0;
}

int main(void)
{
    int line = test_two_instances();

    if (line != 0) {
        fprintf(stderr, "C3.3 two-instance isolation failed at line %d\n",
                line);
        return 1;
    }
    printf("C3.3 two-instance isolation: PASS\n");
    return 0;
}
