/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CONTRACTS_NFC_PROVIDER_H
#define FWLAB_CONTRACTS_NFC_PROVIDER_H

#include <stdbool.h>
#include <stdint.h>

#include "fwlab/portable/nfc_types.h"

typedef enum fwlab_nfc_api_result
(*fwlab_nfc_buffer_read_fn)(
    void *context,
    const struct fwlab_nfc_buffer_ref *source,
    uint8_t *destination,
    uint32_t length
);

typedef enum fwlab_nfc_api_result
(*fwlab_nfc_buffer_write_fn)(
    void *context,
    const struct fwlab_nfc_buffer_ref *destination,
    const uint8_t *source,
    uint32_t length
);

struct fwlab_nfc_buffer_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    fwlab_nfc_buffer_read_fn read;
    fwlab_nfc_buffer_write_fn write;
};

struct fwlab_nfc_buffer_provider {
    const struct fwlab_nfc_buffer_ops *ops;
    void *context;
};

typedef struct fwlab_nfc_submit_result
(*fwlab_nfc_provider_try_submit_fn)(
    void *context,
    const struct fwlab_nfc_request *request
);

typedef enum fwlab_nfc_api_result
(*fwlab_nfc_provider_cancel_fn)(
    void *context,
    const struct fwlab_nfc_operation_token *operation
);

typedef enum fwlab_nfc_api_result
(*fwlab_nfc_provider_step_fn)(
    void *context,
    uint32_t budget,
    struct fwlab_nfc_step_result *result
);

typedef enum fwlab_nfc_api_result
(*fwlab_nfc_provider_poll_fn)(
    void *context,
    uint32_t budget,
    struct fwlab_nfc_completion *events,
    uint32_t event_capacity,
    uint32_t *event_count
);

typedef enum fwlab_nfc_api_result
(*fwlab_nfc_provider_reset_begin_fn)(
    void *context,
    uint64_t instance_nonce,
    uint32_t old_epoch
);

typedef enum fwlab_nfc_api_result
(*fwlab_nfc_provider_quiescent_fn)(
    void *context,
    uint64_t instance_nonce,
    uint32_t old_epoch,
    bool *quiescent
);

struct fwlab_nfc_provider_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    fwlab_nfc_provider_try_submit_fn try_submit;
    fwlab_nfc_provider_cancel_fn cancel;
    fwlab_nfc_provider_step_fn step;
    fwlab_nfc_provider_poll_fn poll;
    fwlab_nfc_provider_reset_begin_fn reset_begin;
    fwlab_nfc_provider_quiescent_fn quiescent;
};

struct fwlab_nfc_provider {
    const struct fwlab_nfc_provider_ops *ops;
    void *context;
};

#endif
