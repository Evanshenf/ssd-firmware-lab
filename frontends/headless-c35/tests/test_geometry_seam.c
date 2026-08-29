/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_test_support.h"
#include "c33_test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static int reset_environment(struct c33_test_environment *environment)
{
    uint32_t old_epoch = environment->current_epoch;
    unsigned int iteration;

    if (environment->provider.ops->reset_begin(
            environment->provider.context, environment->instance_nonce,
            old_epoch) != FWLAB_NFC_API_OK) {
        return 0;
    }
    for (iteration = 0; iteration < 256; ++iteration) {
        struct fwlab_nfc_step_result step;
        bool quiescent = false;

        if (environment->provider.ops->step(
                environment->provider.context, 1, &step) !=
                FWLAB_NFC_API_OK ||
            environment->provider.ops->quiescent(
                environment->provider.context,
                environment->instance_nonce, old_epoch, &quiescent) !=
                FWLAB_NFC_API_OK) {
            return 0;
        }
        if (quiescent) {
            ++environment->current_epoch;
            return 1;
        }
    }
    return 0;
}

static int test_true_dual_geometry(void)
{
    struct c33_test_environment *first = calloc(1, sizeof(*first));
    struct c33_test_environment *second = calloc(1, sizeof(*second));
    struct fwlab_nfc_model_config config_a = c33_test_config();
    struct fwlab_nfc_model_config config_b = c33_test_config();
    struct fwlab_nfc_ppa ppa_a = {0, 0, 1, 1, 0, 0};
    struct fwlab_nfc_ppa ppa_b = {1, 0, 0, 1, 0, 0};
    struct fwlab_nfc_ppa sentinel_a = {0, 0, 0, 0, 0, 0};
    struct fwlab_nfc_ppa sentinel_b = {0, 0, 0, 0, 0, 0};
    const uint8_t value_a[2] = {0x12, 0x34};
    const uint8_t value_b[2] = {0x56, 0x78};
    struct fwlab_nfc_completion completion;
    uint8_t output[2];
    uint8_t oob;
    uint64_t second_state;
    uint64_t second_media;

    CHECK(first != NULL && second != NULL);
    config_a.fault.seed = UINT64_C(0x1111222233334444);
    config_b.geometry.channels = 2;
    config_b.geometry.planes_per_lun = 1;
    config_b.fault.seed = UINT64_C(0xaaaabbbbccccdddd);
    CHECK(config_a.geometry.channels != config_b.geometry.channels &&
          config_a.geometry.planes_per_lun !=
              config_b.geometry.planes_per_lun);
    CHECK(c33_test_init(
        first, &config_a, NULL, 0, UINT64_C(0x35330001)));
    CHECK(c33_test_init(
        second, &config_b, NULL, 0, UINT64_C(0x35330002)));

    second_state = fwlab_nfc_model_state_hash(second->model);
    second_media = fwlab_nfc_model_media_hash(second->model);
    CHECK(c33_test_program(first, ppa_a, value_a, 0x9a, &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(fwlab_nfc_model_state_hash(second->model) == second_state);
    CHECK(fwlab_nfc_model_media_hash(second->model) == second_media);
    CHECK(c33_test_read(
        second, sentinel_b, 0, output, &oob, &completion));
    CHECK(output[0] == 0xff && output[1] == 0xff && oob == 0xff);

    CHECK(c33_test_program(second, ppa_b, value_b, 0x5b, &completion));
    CHECK(completion.terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    second_state = fwlab_nfc_model_state_hash(second->model);
    second_media = fwlab_nfc_model_media_hash(second->model);
    CHECK(c33_test_read(first, sentinel_a, 0, output, &oob, &completion));
    CHECK(output[0] == 0xff && output[1] == 0xff && oob == 0xff);

    CHECK(reset_environment(first));
    CHECK(fwlab_nfc_model_state_hash(second->model) == second_state);
    CHECK(fwlab_nfc_model_media_hash(second->model) == second_media);
    CHECK(c33_test_read(first, ppa_a, 0, output, &oob, &completion));
    CHECK(memcmp(output, value_a, sizeof(value_a)) == 0 && oob == 0x9a);
    CHECK(c33_test_read(second, ppa_b, 0, output, &oob, &completion));
    CHECK(memcmp(output, value_b, sizeof(value_b)) == 0 && oob == 0x5b);
    free(second);
    free(first);
    return 1;
}

static int test_full_stack_profile_rejection(void)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct fwlab_nfc_model_config config =
        c35_test_nfc_config(UINT64_C(0x1020304050607080));
    const uint8_t uuid[16] = {
        0x35, 0x35, 0x03, 0x05, 0x20, 0x26, 0x08, 0x29,
        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    };
    int ok;

    CHECK(storage != NULL && runtime != NULL);
    CHECK(c35_storage_init(storage, C35_LANE_MEMORY, uuid));
    config.geometry.blocks_per_plane = C34_BLOCKS - 1u;
    CHECK(!c35_runtime_init_profile(
        runtime, storage, C35_LANE_MEMORY, UINT64_C(0x35350001),
        config.fault.seed, 0, 0, 0x3513, &config));
    CHECK(!storage->bundle.claimed);
    ok = c35_storage_close(storage);
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

int main(void)
{
    CHECK(test_true_dual_geometry());
    CHECK(test_full_stack_profile_rejection());
    puts("C3.5 geometry seam: PASS (2 standalone C3.3 geometries; "
         "non-profile full C34 rejected)");
    return 0;
}
