/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>

#include "c33_test_support.h"

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

static int test_config_validation(void)
{
    struct fwlab_nfc_model_config config = c33_test_config();

    CHECK(fwlab_nfc_model_config_validate(&config) == FWLAB_NFC_API_OK);
    CHECK(fwlab_nfc_model_arena_size(&config) != 0);
    config.geometry.channels = 0;
    CHECK(fwlab_nfc_model_config_validate(&config) ==
          FWLAB_NFC_API_INVALID_CONTRACT);
    config = c33_test_config();
    config.geometry.max_programs_per_erase = 2;
    CHECK(fwlab_nfc_model_config_validate(&config) ==
          FWLAB_NFC_API_INVALID_CONTRACT);
    config = c33_test_config();
    config.geometry.blocks_per_plane = UINT16_MAX;
    CHECK(fwlab_nfc_model_config_validate(&config) ==
          FWLAB_NFC_API_INVALID_CONTRACT);
    config = c33_test_config();
    config.timing.read_array_ticks = 0;
    CHECK(fwlab_nfc_model_config_validate(&config) ==
          FWLAB_NFC_API_INVALID_CONTRACT);
    return 0;
}

static int test_rejection_has_no_effect(void)
{
    struct c33_test_environment environment;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_ppa ppa = {0, 0, 0, 0, 0, 0};
    struct fwlab_nfc_request request;
    struct fwlab_nfc_submit_result submit;
    uint64_t state_hash;
    uint64_t media_hash;

    CHECK(c33_test_init(&environment, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    state_hash = fwlab_nfc_model_state_hash(environment.model);
    media_hash = fwlab_nfc_model_media_hash(environment.model);
    request = c33_test_request(&environment, FWLAB_NFC_ERASE, ppa);
    request.ppa.page = config.geometry.pages_per_block;
    submit = environment.provider.ops->try_submit(
        environment.provider.context, &request);
    CHECK(submit.disposition == FWLAB_NFC_REJECTED);
    CHECK(fwlab_nfc_model_state_hash(environment.model) == state_hash);
    CHECK(fwlab_nfc_model_media_hash(environment.model) == media_hash);
    return 0;
}

static int test_factory_bad(void)
{
    struct c33_test_environment environment;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_factory_bad bad;
    struct fwlab_nfc_completion completion;

    memset(&bad, 0, sizeof(bad));
    bad.version = FWLAB_NFC_CONTRACT_VERSION;
    bad.size = (uint16_t)sizeof(bad);
    bad.block = (struct fwlab_nfc_ppa){0, 0, 1, 0, 0, 0};
    CHECK(c33_test_init(&environment, &config, &bad, 1,
                        UINT64_C(0x1122334455667788)));
    CHECK(c33_test_erase(&environment, bad.block, &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_FAILED);
    CHECK(completion.reason == FWLAB_NFC_REASON_BAD_BLOCK);
    CHECK(completion.physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT);
    CHECK(completion.block_health == FWLAB_NFC_BLOCK_FACTORY_BAD);
    return 0;
}

static int test_completed_identity_is_stale(void)
{
    struct c33_test_environment environment;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_request request;
    struct fwlab_nfc_completion completion;
    struct fwlab_nfc_submit_result submit;

    CHECK(c33_test_init(&environment, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    request = c33_test_request(
        &environment, FWLAB_NFC_STATUS,
        (struct fwlab_nfc_ppa){0, 0, 0, 0, 0, 0});
    CHECK(c33_test_run_event(&environment, &request, &completion));
    submit = environment.provider.ops->try_submit(
        environment.provider.context, &request);
    CHECK(submit.disposition == FWLAB_NFC_REJECTED);
    CHECK(submit.reason == FWLAB_NFC_REASON_STALE);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        {"config_validation", test_config_validation},
        {"rejection_has_no_effect", test_rejection_has_no_effect},
        {"factory_bad", test_factory_bad},
        {"completed_identity_is_stale", test_completed_identity_is_stale},
    };
    unsigned int index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        int line = tests[index].run();

        if (line != 0) {
            fprintf(stderr, "C3.3 contract test %s failed at line %d\n",
                    tests[index].name, line);
            return 1;
        }
    }
    printf("C3.3 contract unit: PASS (%u cases)\n",
           (unsigned int)(sizeof(tests) / sizeof(tests[0])));
    return 0;
}
