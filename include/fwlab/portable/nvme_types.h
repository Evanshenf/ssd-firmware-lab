/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PORTABLE_NVME_TYPES_H
#define FWLAB_PORTABLE_NVME_TYPES_H

#include <stdint.h>

#define FWLAB_NVME_COMMAND_VERSION 1u
#define FWLAB_NVME_COMPLETION_VERSION 1u
#define FWLAB_NVME_PROFILE_VERSION 1u

#define FWLAB_NVME_COMMAND_WIRE_BYTES 128u
#define FWLAB_NVME_COMPLETION_WIRE_BYTES 64u
#define FWLAB_NVME_PROFILE_WIRE_BYTES 64u

enum fwlab_nvme_queue_class {
    FWLAB_NVME_QUEUE_ADMIN = 1,
    FWLAB_NVME_QUEUE_IO = 2
};

enum fwlab_nvme_fuse {
    FWLAB_NVME_FUSE_NONE = 0,
    FWLAB_NVME_FUSE_FIRST = 1,
    FWLAB_NVME_FUSE_SECOND = 2,
    FWLAB_NVME_FUSE_RESERVED = 3
};

enum fwlab_nvme_data_pointer_format {
    FWLAB_NVME_DATA_POINTER_PRP = 0,
    FWLAB_NVME_DATA_POINTER_UNSUPPORTED_1 = 1,
    FWLAB_NVME_DATA_POINTER_UNSUPPORTED_2 = 2,
    FWLAB_NVME_DATA_POINTER_RESERVED = 3
};

enum fwlab_nvme_transport_fault {
    FWLAB_NVME_TRANSPORT_NONE = 0,
    FWLAB_NVME_TRANSPORT_UNSAFE_GRAPH = 1,
    FWLAB_NVME_TRANSPORT_UNSUPPORTED_FORMAT = 2,
    FWLAB_NVME_TRANSPORT_QUEUE_MEMORY = 3,
    FWLAB_NVME_TRANSPORT_STALE_GENERATION = 4
};

enum fwlab_nvme_effect_class {
    FWLAB_NVME_EFFECT_NONE = 0,
    FWLAB_NVME_EFFECT_FULL = 1,
    FWLAB_NVME_EFFECT_EXACT_PREFIX = 2,
    FWLAB_NVME_EFFECT_UNKNOWN_PREFIX = 3
};

enum fwlab_nvme_profile_feature {
    FWLAB_NVME_PROFILE_READ = 1u << 0,
    FWLAB_NVME_PROFILE_WRITE = 1u << 1,
    FWLAB_NVME_PROFILE_FLUSH = 1u << 2,
    FWLAB_NVME_PROFILE_FUA = 1u << 3,
    FWLAB_NVME_PROFILE_VOLATILE_WRITE_CACHE = 1u << 4,
    FWLAB_NVME_PROFILE_PRP_DIRECT = 1u << 5
};

struct fwlab_nvme_origin_token {
    uint64_t word[2];
};

struct fwlab_nvme_command_handle {
    uint64_t instance_nonce;
    uint64_t command_uid;
    uint32_t controller_epoch;
    uint32_t generation;
};

/*
 * This native value is address-free. It is never a packed command mirror and
 * must cross a byte boundary only through fwlab_nvme_command_encode().
 */
struct fwlab_nvme_command {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nvme_command_handle handle;
    struct fwlab_nvme_origin_token origin;
    uint64_t trace_cookie;
    uint32_t safety_generation;
    uint32_t namespace_id;
    uint32_t command_dword2;
    uint32_t command_dword3;
    uint32_t command_dword10_15[6];
    uint32_t transport_fault;
    uint8_t opcode;
    uint8_t queue_class;
    uint8_t fuse;
    uint8_t data_pointer_format;
    uint8_t data_address_present;
    uint8_t metadata_address_present;
    uint8_t command_flags_reserved;
    uint8_t reserved1;
    uint32_t reserved2[5];
};

/* Protocol result only. Physical queue identity and phase remain HIF-private. */
struct fwlab_nvme_completion_intent {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nvme_command_handle handle;
    struct fwlab_nvme_origin_token origin;
    uint32_t result_dword0;
    uint32_t actual_length;
    uint16_t status_code;
    uint8_t status_code_type;
    uint8_t command_retry_delay;
    uint8_t more;
    uint8_t do_not_retry;
    uint8_t effect_class;
    uint8_t reserved1;
};

/* Project test-oracle profile, not a PCI register image or certification. */
struct fwlab_nvme_profile {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint32_t namespace_count;
    uint32_t lba_bytes;
    uint32_t lba_count;
    uint32_t memory_page_bytes;
    uint32_t maximum_transfer_bytes;
    uint16_t maximum_io_queue_pairs;
    uint16_t integration_queue_depth;
    uint16_t queue_depth_hard_maximum;
    uint16_t data_segments_hard_maximum;
    uint32_t feature_flags;
    uint32_t reserved1[6];
};

#endif
