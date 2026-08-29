/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>

#include "../tests/c33_test_support.h"

int main(void)
{
    struct c33_test_environment environment;
    struct fwlab_nfc_model_config config = c33_test_config();

    if (!c33_test_init(&environment, &config, NULL, 0,
                       UINT64_C(0x1122334455667788)) ||
        environment.provider.ops == NULL ||
        environment.provider.ops->try_submit == NULL ||
        environment.provider.ops->step == NULL ||
        environment.provider.ops->poll == NULL) {
        return 1;
    }
    printf("C3.3 NFC layer fake link: PASS\n");
    return 0;
}
