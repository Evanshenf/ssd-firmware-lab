/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C41_WIRE_H
#define FWLAB_C41_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/portable/nvme_types.h"

#define C41_SQE_BYTES 64u
#define C41_CQE_BYTES 16u

enum c41_wire_result {
    C41_WIRE_OK = 0,
    C41_WIRE_INVALID_ARGUMENT = 1,
    C41_WIRE_INVALID_VALUE = 2,
    C41_WIRE_BAD_ENCODING = 3,
    C41_WIRE_IDENTITY_MISMATCH = 4
};

struct c41_raw_command {
    uint8_t opcode;
    uint8_t fuse;
    uint8_t data_pointer_format;
    uint8_t command_flags_reserved;
    uint16_t command_id;
    uint16_t reserved0;
    uint32_t namespace_id;
    uint32_t command_dword2;
    uint32_t command_dword3;
    uint64_t metadata_pointer;
    uint64_t data_pointer1;
    uint64_t data_pointer2;
    uint32_t command_dword10_15[6];
};

struct c41_raw_completion {
    uint32_t result_dword0;
    uint16_t submission_queue_head;
    uint16_t submission_queue_id;
    uint16_t command_id;
    uint8_t phase;
    uint8_t status_code;
    uint8_t status_code_type;
    uint8_t command_retry_delay;
    uint8_t more;
    uint8_t do_not_retry;
};

struct c41_capture_context {
    struct fwlab_nvme_command_handle handle;
    struct fwlab_nvme_origin_token origin;
    uint64_t trace_cookie;
    uint32_t safety_generation;
    uint32_t transport_fault;
    uint8_t queue_class;
    uint8_t reserved[3];
};

struct c41_publication_context {
    struct fwlab_nvme_command_handle handle;
    struct fwlab_nvme_origin_token origin;
    uint16_t submission_queue_head;
    uint16_t submission_queue_id;
    uint16_t command_id;
    uint8_t phase;
    uint8_t reserved;
};

enum c41_wire_result c41_sqe_decode(
    const uint8_t *input,
    size_t input_size,
    struct c41_raw_command *command
);
enum c41_wire_result c41_cqe_encode(
    const struct c41_raw_completion *completion,
    uint8_t *output,
    size_t output_size
);
enum c41_wire_result c41_cqe_decode(
    const uint8_t *input,
    size_t input_size,
    struct c41_raw_completion *completion
);
enum c41_wire_result c41_capture_command(
    const struct c41_raw_command *raw,
    const struct c41_capture_context *context,
    struct fwlab_nvme_command *command
);
enum c41_wire_result c41_completion_publish(
    const struct fwlab_nvme_completion_intent *intent,
    const struct c41_publication_context *context,
    uint8_t *output,
    size_t output_size
);

#endif
