/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_UNSTABLE_M4_NATIVE_H
#define FWLAB_UNSTABLE_M4_NATIVE_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define FWLAB_M4_NATIVE_VERSION 1U
#define FWLAB_M4_NATIVE_MAX_BYTES 8192U

enum fwlab_m4_native_operation {
    FWLAB_M4_NATIVE_ATTACH = 1,
    FWLAB_M4_NATIVE_STATUS = 2,
    FWLAB_M4_NATIVE_NEXT = 3,
    FWLAB_M4_NATIVE_SHAPE = 4,
    FWLAB_M4_NATIVE_DMA = 5,
    FWLAB_M4_NATIVE_DMA_QUERY = 6,
    FWLAB_M4_NATIVE_QUEUE = 7,
    FWLAB_M4_NATIVE_PUBLISH = 8,
    FWLAB_M4_NATIVE_PUBLISH_QUERY = 9,
    FWLAB_M4_NATIVE_RETIRE = 10,
    FWLAB_M4_NATIVE_RESET_ACK = 11,
    FWLAB_M4_NATIVE_DMA_CANCEL = 12,
    FWLAB_M4_NATIVE_DMA_RETIRE = 13,
    FWLAB_M4_NATIVE_AUTHORITY_RELEASE = 14,
    FWLAB_M4_NATIVE_REVOKE = 15
};

enum fwlab_m4_native_event {
    FWLAB_M4_NATIVE_IDLE = 0,
    FWLAB_M4_NATIVE_COMMAND = 1,
    FWLAB_M4_NATIVE_RESET = 2
};

enum fwlab_m4_native_dma_state {
    FWLAB_M4_NATIVE_DMA_RESERVED = 1,
    FWLAB_M4_NATIVE_DMA_DONE = 2,
    FWLAB_M4_NATIVE_DMA_CANCELLED = 3,
    FWLAB_M4_NATIVE_DMA_FAILED = 4
};

enum fwlab_m4_native_publication {
    FWLAB_M4_NATIVE_UNPUBLISHED = 0,
    FWLAB_M4_NATIVE_COMMITTED = 1,
    FWLAB_M4_NATIVE_DISCARDED = 2
};

enum fwlab_m4_native_queue_effect {
    FWLAB_M4_NATIVE_NUMBER_OF_QUEUES = 10,
    FWLAB_M4_NATIVE_CREATE_CQ = 11,
    FWLAB_M4_NATIVE_CREATE_SQ = 12,
    FWLAB_M4_NATIVE_DELETE_CQ = 13,
    FWLAB_M4_NATIVE_DELETE_SQ = 14
};

/* Private HIF control ABI, not a portable firmware header. The data pointer
 * names the attached firmware process's transfer buffer, never a Host IOVA.
 * Host addresses remain in the captured SQE and the kernel mapping records. */
struct fwlab_m4_native_message {
    __u32 version;
    __u32 size;
    __u32 operation;
    __u32 event;
    __u64 function_nonce;
    __u64 origin_uid;
    __u32 controller_epoch;
    __s32 result;
    __u32 queue_id;
    __u32 command_id;
    __u32 direction;
    __u32 bytes;
    __u64 authority_uid;
    __u64 dma_uid;
    __u64 completion_uid;
    __aligned_u64 data_pointer;
    __u32 dma_state;
    __u32 bytes_done;
    __u32 publication;
    __u32 queue_effect;
    __u32 queue_entries;
    __u32 associated_queue;
    __u32 interrupt_vector;
    __u32 result_dword0;
    __u32 status_code;
    __u32 status_code_type;
    __u32 do_not_retry;
    __u32 more;
    __u32 retry_delay;
    __u8 sqe[64];
    __u8 media_uuid[16];
    __u8 binding_sha256[32];
    __u32 reserved0;
    __u64 reserved[4];
};

#define FWLAB_M4_NATIVE_EXCHANGE \
    _IOWR('N', 0x70, struct fwlab_m4_native_message)

#endif /* FWLAB_UNSTABLE_M4_NATIVE_H */
