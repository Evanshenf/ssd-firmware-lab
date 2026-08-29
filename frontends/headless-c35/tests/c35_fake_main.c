/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_test_support.h"

#include <stdio.h>

int main(void)
{
    static const uint8_t uuid[16] = {0x35, 0x05, 0x01};
    struct c35_storage storage;
    struct c35_runtime runtime;
    struct c35_request request = c35_request_read(0);
    struct c35_semantic_result result;

    if (!c35_storage_init(&storage, C35_LANE_MEMORY, uuid) ||
        !c35_runtime_init(
            &runtime, &storage, C35_LANE_MEMORY, UINT64_C(0x35f1),
            UINT64_C(0x1234), 0, 0, 1) ||
        !c35_run_command(&runtime, &request, &result) ||
        !c35_runtime_teardown(&runtime) || !c35_storage_close(&storage)) {
        return 1;
    }
    puts("C3.5 headless fake link: PASS");
    return 0;
}
