/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CONTRACTS_CONTROLLER_BUFFER_V0_H
#define FWLAB_CONTRACTS_CONTROLLER_BUFFER_V0_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/portable/nvme_types.h"

#define FWLAB_CONTROLLER_BUFFER_V0_VERSION 1u
#define FWLAB_CONTROLLER_BUFFER_LEASE_V0_TAG UINT32_C(0x43424c53)
#define FWLAB_CONTROLLER_BUFFER_RIGHT_V0_ALL UINT32_C(0x00000003)

enum fwlab_controller_buffer_result_v0 {
    FWLAB_CONTROLLER_BUFFER_V0_OK = 0,
    FWLAB_CONTROLLER_BUFFER_V0_INVALID = 1,
    FWLAB_CONTROLLER_BUFFER_V0_WRONG_STATE = 2,
    FWLAB_CONTROLLER_BUFFER_V0_STALE = 3,
    FWLAB_CONTROLLER_BUFFER_V0_NO_CAPACITY = 4,
    FWLAB_CONTROLLER_BUFFER_V0_IN_PROGRESS = 5,
    FWLAB_CONTROLLER_BUFFER_V0_POISONED = 6,
    FWLAB_CONTROLLER_BUFFER_V0_COUNTER_EXHAUSTED = 7
};

enum fwlab_controller_buffer_right_v0 {
    FWLAB_CONTROLLER_BUFFER_V0_READ = 1u << 0,
    FWLAB_CONTROLLER_BUFFER_V0_WRITE = 1u << 1
};

struct fwlab_controller_buffer_lease_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t type_tag;
    uint64_t issuer_nonce;
    uint64_t buffer_uid;
    uint64_t lease_uid;
    uint32_t generation;
    uint32_t capacity_bytes;
    uint32_t rights;
    uint32_t reserved0;
    uint32_t reserved1[4];
};

struct fwlab_controller_buffer_span_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint32_t offset;
    uint32_t length;
    uint32_t reserved1[2];
};

struct fwlab_controller_buffer_acquire_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nvme_command_handle command;
    struct fwlab_nvme_origin_token origin;
    uint64_t client_uid;
    uint32_t execution_epoch;
    uint32_t capacity_bytes;
    uint32_t rights;
    uint32_t reserved1[4];
};

typedef enum fwlab_controller_buffer_result_v0
(*fwlab_controller_buffer_acquire_fn_v0)(
    void *context,
    const struct fwlab_controller_buffer_acquire_v0 *request,
    struct fwlab_controller_buffer_lease_v0 *lease
);

typedef enum fwlab_controller_buffer_result_v0
(*fwlab_controller_buffer_read_fn_v0)(
    void *context,
    const struct fwlab_controller_buffer_lease_v0 *lease,
    const struct fwlab_controller_buffer_span_v0 *span,
    void *output,
    size_t output_size
);

typedef enum fwlab_controller_buffer_result_v0
(*fwlab_controller_buffer_write_fn_v0)(
    void *context,
    const struct fwlab_controller_buffer_lease_v0 *lease,
    const struct fwlab_controller_buffer_span_v0 *span,
    const void *input,
    size_t input_size
);

typedef enum fwlab_controller_buffer_result_v0
(*fwlab_controller_buffer_copy_fn_v0)(
    void *context,
    const struct fwlab_controller_buffer_lease_v0 *destination,
    const struct fwlab_controller_buffer_span_v0 *destination_span,
    const struct fwlab_controller_buffer_lease_v0 *source,
    const struct fwlab_controller_buffer_span_v0 *source_span
);

typedef enum fwlab_controller_buffer_result_v0
(*fwlab_controller_buffer_release_fn_v0)(
    void *context,
    const struct fwlab_controller_buffer_lease_v0 *lease
);

typedef enum fwlab_controller_buffer_result_v0
(*fwlab_controller_buffer_epoch_fn_v0)(
    void *context,
    uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch
);

typedef enum fwlab_controller_buffer_result_v0
(*fwlab_controller_buffer_quiescent_fn_v0)(
    void *context,
    uint64_t lifecycle_instance_nonce,
    uint32_t old_execution_epoch,
    uint32_t *active_leases,
    uint8_t *quiescent
);

struct fwlab_controller_buffer_ops_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    fwlab_controller_buffer_acquire_fn_v0 acquire;
    fwlab_controller_buffer_read_fn_v0 read;
    fwlab_controller_buffer_write_fn_v0 write;
    fwlab_controller_buffer_copy_fn_v0 copy;
    fwlab_controller_buffer_release_fn_v0 release;
    fwlab_controller_buffer_epoch_fn_v0 epoch_close;
    fwlab_controller_buffer_quiescent_fn_v0 epoch_quiescent;
    uint32_t reserved1[4];
};

struct fwlab_controller_buffer_port_v0 {
    const struct fwlab_controller_buffer_ops_v0 *ops;
    void *context;
    uint64_t issuer_nonce;
    uint32_t generation;
    uint32_t reserved[3];
};

int fwlab_controller_buffer_lease_v0_valid(
    const struct fwlab_controller_buffer_lease_v0 *lease
);
int fwlab_controller_buffer_span_v0_valid(
    const struct fwlab_controller_buffer_span_v0 *span
);
int fwlab_controller_buffer_span_v0_valid_for_lease(
    const struct fwlab_controller_buffer_span_v0 *span,
    const struct fwlab_controller_buffer_lease_v0 *lease,
    uint32_t required_rights
);
int fwlab_controller_buffer_acquire_v0_valid(
    const struct fwlab_controller_buffer_acquire_v0 *request
);
int fwlab_controller_buffer_ops_v0_valid(
    const struct fwlab_controller_buffer_ops_v0 *ops
);
int fwlab_controller_buffer_port_v0_valid(
    const struct fwlab_controller_buffer_port_v0 *port
);

#endif /* FWLAB_CONTRACTS_CONTROLLER_BUFFER_V0_H */
