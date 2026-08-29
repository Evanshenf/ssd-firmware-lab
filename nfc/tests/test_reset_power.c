/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>

#include "../nfc_internal.h"
#include "c33_test_support.h"

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

static uint64_t effect_vector_hash = UINT64_C(1469598103934665603);

static void effect_add(uint64_t value)
{
    unsigned int byte;

    for (byte = 0; byte < 8; ++byte) {
        effect_vector_hash ^= (uint8_t)(value >> (byte * 8u));
        effect_vector_hash *= UINT64_C(1099511628211);
    }
}

static int test_reset_cancels_before_begin(void)
{
    struct c33_test_environment environment;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_request request;
    struct fwlab_nfc_submit_result submit;
    struct fwlab_nfc_completion completion;
    uint32_t count = 0;
    bool quiescent = false;
    unsigned int iteration;

    config.timing.command_ticks = 10;
    CHECK(c33_test_init(&environment, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    request = c33_test_request(
        &environment, FWLAB_NFC_STATUS,
        (struct fwlab_nfc_ppa){0, 0, 0, 0, 0, 0});
    submit = environment.provider.ops->try_submit(
        environment.provider.context, &request);
    CHECK(submit.disposition == FWLAB_NFC_ACCEPTED);
    CHECK(environment.provider.ops->reset_begin(
              environment.provider.context,
              UINT64_C(0x1122334455667788), 1) == FWLAB_NFC_API_OK);
    for (iteration = 0; iteration < 128 && count == 0; ++iteration) {
        struct fwlab_nfc_step_result step;

        CHECK(environment.provider.ops->step(
                  environment.provider.context, 1, &step) ==
              FWLAB_NFC_API_OK);
        CHECK(environment.provider.ops->poll(
                  environment.provider.context, 1, &completion, 1, &count) ==
              FWLAB_NFC_API_OK);
    }
    CHECK(count == 1);
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_FAILED);
    CHECK(completion.reason == FWLAB_NFC_REASON_RESET);
    CHECK(environment.provider.ops->quiescent(
              environment.provider.context,
              UINT64_C(0x1122334455667788), 1, &quiescent) ==
          FWLAB_NFC_API_OK);
    CHECK(quiescent);
    return 0;
}

static int test_power_keeps_torn_prefix(void)
{
    struct c33_test_environment environment;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_ppa ppa = {0, 0, 0, 0, 0, 0};
    struct fwlab_nfc_request transfer;
    struct fwlab_nfc_request execute;
    struct fwlab_nfc_completion completion;
    const uint8_t input[2] = {UINT8_C(0x00), UINT8_C(0x55)};
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
    struct fwlab_nand_media media;
    uint8_t main[2];
    uint8_t oob;
    unsigned int iteration;

    CHECK(c33_test_init(&environment, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    memcpy(&environment.buffer.bytes[0], input, 2);
    environment.buffer.bytes[16] = UINT8_C(0x0f);
    transfer = c33_test_request(&environment, FWLAB_NFC_PROGRAM_TRANSFER,
                                ppa);
    transfer.region_mask = FWLAB_NFC_REGION_MASK;
    transfer.main = (struct fwlab_nfc_buffer_ref){1, 0, 2, 0};
    transfer.oob = (struct fwlab_nfc_buffer_ref){1, 16, 1, 0};
    CHECK(c33_test_run_event(&environment, &transfer, &completion));
    execute = c33_test_request(&environment, FWLAB_NFC_PROGRAM_EXECUTE, ppa);
    execute.region_mask = FWLAB_NFC_REGION_MASK;
    execute.cache = completion.cache;
    CHECK(environment.provider.ops->try_submit(
              environment.provider.context, &execute).disposition ==
          FWLAB_NFC_ACCEPTED);
    for (iteration = 0; iteration < 4; ++iteration) {
        struct fwlab_nfc_step_result step;

        CHECK(environment.provider.ops->step(
                  environment.provider.context, 1, &step) ==
              FWLAB_NFC_API_OK);
    }
    CHECK(fwlab_nfc_model_inject_cut(
              environment.model, FWLAB_NFC_CUT_SSD_POWER_LOSS) ==
          FWLAB_NFC_API_OK);
    media = c33_memory_media_provider(environment.memory);
    CHECK(media.ops->read_page(media.context, &ppa, main, 2, &oob, 1,
                               &page, &block) == FWLAB_NFC_API_OK);
    CHECK(page.state == FWLAB_NAND_PAGE_TORN);
    CHECK(main[0] == input[0] && main[1] == input[1]);
    CHECK(oob == UINT8_C(0xff));
    return 0;
}

static int test_seeded_torn_faults(void)
{
    struct c33_test_environment program_environment;
    struct c33_test_environment erase_environment;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_ppa ppa = {0, 0, 0, 0, 0, 0};
    const uint8_t input[2] = {UINT8_C(0x00), UINT8_C(0x55)};
    struct fwlab_nfc_completion completion;
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
    struct fwlab_nand_media media;
    uint8_t main[2];
    uint8_t oob;

    config.fault.program_torn_modulus = 1;
    CHECK(c33_test_init(&program_environment, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    CHECK(c33_test_program(&program_environment, ppa, input, UINT8_C(0x0f),
                           &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_FAILED);
    CHECK(completion.physical_outcome == FWLAB_NFC_PHYS_APPLIED);
    CHECK(completion.integrity == FWLAB_NFC_INTEGRITY_TORN);
    effect_add(completion.frozen_fault_word);
    media = c33_memory_media_provider(program_environment.memory);
    CHECK(media.ops->read_page(media.context, &ppa, main, 2, &oob, 1,
                               &page, &block) == FWLAB_NFC_API_OK);
    CHECK(page.state == FWLAB_NAND_PAGE_TORN);
    CHECK(main[0] == input[0] && main[1] == UINT8_C(0xff));
    CHECK(oob == UINT8_C(0xff));

    config = c33_test_config();
    config.fault.erase_torn_modulus = 1;
    CHECK(c33_test_init(&erase_environment, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    CHECK(c33_test_erase(&erase_environment, ppa, &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_FAILED);
    CHECK(completion.physical_outcome == FWLAB_NFC_PHYS_APPLIED);
    CHECK(completion.integrity == FWLAB_NFC_INTEGRITY_TORN);
    effect_add(completion.frozen_fault_word);
    media = c33_memory_media_provider(erase_environment.memory);
    CHECK(media.ops->read_page(media.context, &ppa, main, 2, &oob, 1,
                               &page, &block) == FWLAB_NFC_API_OK);
    CHECK(block.erase_state == FWLAB_NAND_ERASE_TORN);
    return 0;
}

int main(void)
{
    int line = test_reset_cancels_before_begin();

    if (line == 0) {
        line = test_power_keeps_torn_prefix();
    }
    if (line == 0) {
        line = test_seeded_torn_faults();
    }
    if (line != 0) {
        fprintf(stderr, "C3.3 reset/power unit failed at line %d\n", line);
        return 1;
    }
    printf("C3.3 reset/power unit: PASS (3 cases, "
           "effect-vector-hash=%016llx)\n",
           (unsigned long long)effect_vector_hash);
    return 0;
}
