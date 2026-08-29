/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>

#include "c33_test_support.h"

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

static int test_program_read_erase(void)
{
    struct c33_test_environment environment;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_ppa ppa = {0, 0, 0, 0, 0, 0};
    const uint8_t input[2] = {UINT8_C(0xf0), UINT8_C(0x0f)};
    uint8_t output[2];
    uint8_t oob;
    struct fwlab_nfc_completion completion;

    CHECK(c33_test_init(&environment, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    CHECK(c33_test_program(&environment, ppa, input, UINT8_C(0x3c),
                           &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(completion.physical_outcome == FWLAB_NFC_PHYS_APPLIED);
    CHECK(completion.integrity == FWLAB_NFC_INTEGRITY_COMPLETE);
    CHECK(c33_test_read(&environment, ppa, 0, output, &oob, &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(output[0] == input[0] && output[1] == input[1]);
    CHECK(oob == UINT8_C(0x3c));

    CHECK(c33_test_program(&environment, ppa, input, UINT8_C(0x3c),
                           &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_FAILED);
    CHECK(completion.reason == FWLAB_NFC_REASON_NOT_ERASED);
    CHECK(completion.physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT);

    CHECK(c33_test_erase(&environment, ppa, &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(completion.final_erase_generation == 1);
    CHECK(c33_test_read(&environment, ppa, 0, output, &oob, &completion));
    CHECK(output[0] == UINT8_C(0xff) && output[1] == UINT8_C(0xff));
    CHECK(oob == UINT8_C(0xff));
    return 0;
}

static int test_erase_scope(void)
{
    struct c33_test_environment environment;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_ppa first = {0, 0, 0, 0, 0, 0};
    struct fwlab_nfc_ppa sibling = {0, 0, 1, 0, 0, 0};
    const uint8_t one[2] = {UINT8_C(0xaa), UINT8_C(0x55)};
    const uint8_t two[2] = {UINT8_C(0x0f), UINT8_C(0xf0)};
    uint8_t output[2];
    uint8_t oob;
    struct fwlab_nfc_completion completion;

    CHECK(c33_test_init(&environment, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    CHECK(c33_test_program(&environment, first, one, UINT8_C(0x11),
                           &completion));
    CHECK(c33_test_program(&environment, sibling, two, UINT8_C(0x22),
                           &completion));
    CHECK(c33_test_erase(&environment, first, &completion));
    CHECK(c33_test_read(&environment, sibling, 0, output, &oob, &completion));
    CHECK(output[0] == two[0] && output[1] == two[1]);
    CHECK(oob == UINT8_C(0x22));
    return 0;
}

int main(void)
{
    int line = test_program_read_erase();

    if (line != 0) {
        fprintf(stderr, "C3.3 legality program/read/erase failed at line %d\n",
                line);
        return 1;
    }
    line = test_erase_scope();
    if (line != 0) {
        fprintf(stderr, "C3.3 legality erase-scope failed at line %d\n",
                line);
        return 1;
    }
    printf("C3.3 NAND legality unit: PASS (2 cases)\n");
    return 0;
}
