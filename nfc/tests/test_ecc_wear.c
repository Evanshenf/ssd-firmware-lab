/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>

#include "c33_test_support.h"

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

static uint64_t fault_vector_hash = UINT64_C(1469598103934665603);

static void vector_add(uint64_t value)
{
    unsigned int byte;

    for (byte = 0; byte < 8; ++byte) {
        fault_vector_hash ^= (uint8_t)(value >> (byte * 8u));
        fault_vector_hash *= UINT64_C(1099511628211);
    }
}

static int trigger_with_tag(
    struct c33_test_environment *environment,
    uint64_t tag,
    uint8_t retry,
    struct fwlab_nfc_completion *completion
)
{
    struct fwlab_nfc_request request = c33_test_request(
        environment, FWLAB_NFC_READ_TRIGGER,
        (struct fwlab_nfc_ppa){0, 0, 0, 0, 0, 0});

    request.region_mask = FWLAB_NFC_REGION_MASK;
    request.retry_step = retry;
    request.fault_tag = tag;
    return c33_test_run_event(environment, &request, completion);
}

static int test_ecc_classes_and_retry(void)
{
    struct fwlab_nfc_model_config config = c33_test_config();
    uint64_t tag;
    int found_corrected = 0;
    int found_uncorrectable = 0;
    int found_retry = 0;

    config.fault.read_error_modulus = 1;
    for (tag = 1; tag < 1024 &&
         (!found_corrected || !found_uncorrectable || !found_retry); ++tag) {
        struct c33_test_environment environment;
        struct fwlab_nfc_completion first;
        struct fwlab_nfc_completion second;

        CHECK(c33_test_init(&environment, &config, NULL, 0,
                            UINT64_C(0x1122334455667788)));
        CHECK(trigger_with_tag(&environment, tag, 0, &first));
        if (first.ecc_status == FWLAB_NFC_ECC_CORRECTED) {
            if (!found_corrected) {
                vector_add(tag);
                vector_add(first.frozen_fault_word);
                found_corrected = 1;
            }
        }
        if (first.ecc_status == FWLAB_NFC_ECC_UNCORRECTABLE) {
            if (!found_uncorrectable) {
                vector_add(tag);
                vector_add(first.frozen_fault_word);
                found_uncorrectable = 1;
            }
            CHECK(trigger_with_tag(&environment, tag, 1, &second));
            if (second.ecc_status == FWLAB_NFC_ECC_CORRECTED ||
                second.ecc_status == FWLAB_NFC_ECC_CLEAN) {
                if (!found_retry) {
                    vector_add(tag);
                    vector_add(second.frozen_fault_word);
                    found_retry = 1;
                }
            }
        }
    }
    CHECK(found_corrected);
    CHECK(found_uncorrectable);
    CHECK(found_retry);
    return 0;
}

static int test_wear_limit(void)
{
    struct c33_test_environment environment;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_ppa ppa = {0, 0, 0, 0, 0, 0};
    struct fwlab_nfc_completion completion;

    CHECK(c33_test_init(&environment, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    CHECK(c33_test_erase(&environment, ppa, &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(c33_test_erase(&environment, ppa, &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(c33_test_erase(&environment, ppa, &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_FAILED);
    CHECK(completion.reason == FWLAB_NFC_REASON_WEAR_OUT);
    CHECK(completion.physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT);
    CHECK(completion.block_health == FWLAB_NFC_BLOCK_RUNTIME_BAD);
    return 0;
}

static int test_seed_replay(void)
{
    struct c33_test_environment first;
    struct c33_test_environment second;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_completion one;
    struct fwlab_nfc_completion two;

    config.fault.read_error_modulus = 1;
    CHECK(c33_test_init(&first, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    CHECK(c33_test_init(&second, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    CHECK(trigger_with_tag(&first, 77, 0, &one));
    CHECK(trigger_with_tag(&second, 77, 0, &two));
    CHECK(one.frozen_fault_word == two.frozen_fault_word);
    CHECK(one.ecc_status == two.ecc_status);
    CHECK(one.corrected_main_bits == two.corrected_main_bits);
    CHECK(one.corrected_oob_bits == two.corrected_oob_bits);
    CHECK(one.status_tick == two.status_tick);
    return 0;
}

int main(void)
{
    int line = test_ecc_classes_and_retry();

    if (line == 0) {
        line = test_wear_limit();
    }
    if (line == 0) {
        line = test_seed_replay();
    }
    if (line != 0) {
        fprintf(stderr, "C3.3 ECC/wear unit failed at line %d\n", line);
        return 1;
    }
    printf("C3.3 ECC/retry/wear unit: PASS (3 cases, "
           "seed=9b6d3e7a4c2158f1, vector-hash=%016llx)\n",
           (unsigned long long)fault_vector_hash);
    return 0;
}
