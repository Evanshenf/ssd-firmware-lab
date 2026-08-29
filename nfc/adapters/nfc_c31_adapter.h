/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_NFC_ADAPTERS_C31_H
#define FWLAB_NFC_ADAPTERS_C31_H

#include <stdint.h>

#include "fwlab/contracts/c31_provider.h"
#include "fwlab/contracts/nfc_provider.h"

#define C33_C31_ADAPTER_SLOTS 16u

struct c33_c31_registry_entry {
    uint8_t used;
    uint8_t reserved[3];
    struct fwlab_c31_request_token token;
    struct fwlab_nfc_request request;
};

struct c33_c31_active {
    uint8_t used;
    uint8_t reserved[3];
    struct fwlab_c31_operation_token outer;
    struct fwlab_nfc_operation_token inner;
};

struct c33_c31_sidecar {
    uint8_t used;
    uint8_t reserved[3];
    struct fwlab_c31_operation_token outer;
    struct fwlab_nfc_completion completion;
};

struct c33_c31_adapter {
    struct fwlab_nfc_provider provider;
    struct c33_c31_registry_entry registry[C33_C31_ADAPTER_SLOTS];
    struct c33_c31_active active[C33_C31_ADAPTER_SLOTS];
    struct c33_c31_sidecar sidecar[C33_C31_ADAPTER_SLOTS];
};

enum fwlab_nfc_api_result c33_c31_adapter_init(
    struct c33_c31_adapter *adapter,
    const struct fwlab_nfc_provider *provider
);

enum fwlab_nfc_api_result c33_c31_adapter_register(
    struct c33_c31_adapter *adapter,
    const struct fwlab_c31_request_token *token,
    const struct fwlab_nfc_request *request
);

struct fwlab_c31_provider c33_c31_adapter_provider(
    struct c33_c31_adapter *adapter
);

enum fwlab_nfc_api_result c33_c31_adapter_result_read(
    const struct c33_c31_adapter *adapter,
    const struct fwlab_c31_operation_token *operation,
    struct fwlab_nfc_completion *completion
);

enum fwlab_nfc_api_result c33_c31_adapter_result_ack(
    struct c33_c31_adapter *adapter,
    const struct fwlab_c31_operation_token *operation
);

#endif
