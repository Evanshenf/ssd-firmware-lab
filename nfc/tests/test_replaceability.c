/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>

#include "../adapters/nfc_c31_adapter.h"
#include "../fakes/nfc_scripted.h"
#include "c33_test_support.h"

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

static struct fwlab_c31_provider_request outer_request(
    uint64_t cookie,
    struct fwlab_c31_request_token token
)
{
    struct fwlab_c31_provider_request request;

    memset(&request, 0, sizeof(request));
    request.version = FWLAB_C31_PROVIDER_CONTRACT_VERSION;
    request.size = (uint16_t)sizeof(request);
    request.operation.command.instance_nonce =
        UINT64_C(0x1122334455667788);
    request.operation.command.command_uid = cookie;
    request.operation.command.controller_epoch = 1;
    request.operation.command.slot = 1;
    request.operation.command.slot_generation = 1;
    request.operation.cookie = cookie;
    request.operation.operation_generation = (uint32_t)cookie;
    request.request = token;
    request.provider_kind = FWLAB_C31_PROVIDER_NFC;
    return request;
}

static struct fwlab_nfc_request inner_status(uint64_t cookie)
{
    struct fwlab_nfc_request request;

    memset(&request, 0, sizeof(request));
    request.version = FWLAB_NFC_CONTRACT_VERSION;
    request.size = (uint16_t)sizeof(request);
    request.kind = FWLAB_NFC_STATUS;
    request.cookie = cookie;
    return request;
}

static int run_adapter(
    struct c33_c31_adapter *adapter,
    struct fwlab_c31_provider_request *request,
    struct fwlab_nfc_completion *sidecar
)
{
    struct fwlab_c31_provider provider =
        c33_c31_adapter_provider(adapter);
    struct fwlab_c31_provider_submit_result submit =
        provider.ops->try_submit(provider.context, request);
    unsigned int iteration;

    CHECK(submit.disposition == FWLAB_C31_PROVIDER_ACCEPTED);
    for (iteration = 0; iteration < 128; ++iteration) {
        struct fwlab_c31_provider_event event;
        uint32_t count = 0;

        CHECK(provider.ops->poll(provider.context, 1, &event, 1, &count) ==
              FWLAB_C31_API_OK);
        if (count == 1) {
            CHECK(event.terminal == FWLAB_C31_PROVIDER_SUCCESS);
            CHECK(event.operation.cookie == request->operation.cookie);
            CHECK(c33_c31_adapter_result_read(
                      adapter, &request->operation, sidecar) ==
                  FWLAB_NFC_API_OK);
            CHECK(c33_c31_adapter_result_ack(
                      adapter, &request->operation) == FWLAB_NFC_API_OK);
            return 0;
        }
    }
    return __LINE__;
}

static int test_scripted_and_model(void)
{
    struct c33_scripted_nfc fake;
    struct c33_scripted_scenario scenario;
    struct c33_c31_adapter fake_adapter;
    struct c33_c31_adapter model_adapter;
    struct c33_test_environment environment;
    struct fwlab_nfc_model_config config = c33_test_config();
    struct fwlab_nfc_provider provider;
    struct fwlab_c31_request_token fake_token = {
        {UINT64_C(0x1001), UINT64_C(0x2001)}};
    struct fwlab_c31_request_token model_token = {
        {UINT64_C(0x1002), UINT64_C(0x2002)}};
    struct fwlab_nfc_request fake_inner = inner_status(1);
    struct fwlab_nfc_request model_inner = inner_status(2);
    struct fwlab_c31_provider_request fake_outer =
        outer_request(1, fake_token);
    struct fwlab_c31_provider_request model_outer =
        outer_request(2, model_token);
    struct fwlab_nfc_completion fake_result;
    struct fwlab_nfc_completion model_result;
    int line;

    c33_scripted_init(&fake);
    memset(&scenario, 0, sizeof(scenario));
    scenario.request_cookie = 1;
    scenario.completion.version = FWLAB_NFC_CONTRACT_VERSION;
    scenario.completion.size = (uint16_t)sizeof(scenario.completion);
    scenario.completion.terminal = FWLAB_NFC_TERMINAL_SUCCESS;
    scenario.completion.physical_outcome = FWLAB_NFC_PHYS_NO_EFFECT;
    scenario.completion.integrity = FWLAB_NFC_INTEGRITY_NOT_APPLICABLE;
    scenario.completion.ecc_status = FWLAB_NFC_ECC_NOT_APPLICABLE;
    scenario.completion.block_health = FWLAB_NFC_BLOCK_GOOD;
    CHECK(c33_scripted_add(&fake, &scenario));
    provider = c33_scripted_provider(&fake);
    CHECK(c33_c31_adapter_init(&fake_adapter, &provider) ==
          FWLAB_NFC_API_OK);
    CHECK(c33_c31_adapter_register(&fake_adapter, &fake_token, &fake_inner) ==
          FWLAB_NFC_API_OK);
    line = run_adapter(&fake_adapter, &fake_outer, &fake_result);
    CHECK(line == 0);

    CHECK(c33_test_init(&environment, &config, NULL, 0,
                        UINT64_C(0x1122334455667788)));
    CHECK(c33_c31_adapter_init(&model_adapter, &environment.provider) ==
          FWLAB_NFC_API_OK);
    CHECK(c33_c31_adapter_register(
              &model_adapter, &model_token, &model_inner) ==
          FWLAB_NFC_API_OK);
    line = run_adapter(&model_adapter, &model_outer, &model_result);
    CHECK(line == 0);
    CHECK(fake_result.terminal == model_result.terminal);
    CHECK(fake_result.physical_outcome == model_result.physical_outcome);
    CHECK(fake_result.integrity == model_result.integrity);
    CHECK(fake_result.reason == model_result.reason);
    CHECK(fake_result.operation_kind == model_result.operation_kind);
    return 0;
}

int main(void)
{
    int line = test_scripted_and_model();

    if (line != 0) {
        fprintf(stderr, "C3.3 provider replaceability failed at line %d\n",
                line);
        return 1;
    }
    printf("C3.3 fake/model provider replaceability: PASS\n");
    return 0;
}
