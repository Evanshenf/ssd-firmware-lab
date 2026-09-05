/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_UNSTABLE_M4_OWNER_NATIVE_H
#define FWLAB_UNSTABLE_M4_OWNER_NATIVE_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* Kernel HIF wire facts. The userspace binding implements owner_control_v0;
 * this private record is not that contract and contains no function pointers. */
#define FWLAB_M4_OWNER_VERSION 1U
#define FWLAB_M4_OWNER_PROOFS 0x3fU

enum fwlab_m4_owner_operation {
    FWLAB_M4_OWNER_OBSERVE = 1,
    FWLAB_M4_OWNER_REVOKE = 2,
    FWLAB_M4_OWNER_REVOKE_QUERY = 3,
    FWLAB_M4_OWNER_CERTIFY = 4,
    FWLAB_M4_OWNER_GRANT = 5,
    FWLAB_M4_OWNER_GRANT_QUERY = 6,
    FWLAB_M4_OWNER_QUARANTINE = 7
};

enum fwlab_m4_owner_phase {
    FWLAB_M4_OWNER_OWNED = 1,
    FWLAB_M4_OWNER_DRAINING = 3,
    FWLAB_M4_OWNER_NONE = 4,
    FWLAB_M4_OWNER_QUARANTINED = 5
};

struct fwlab_m4_owner_message {
    __u32 version;
    __u32 size;
    __u32 operation;
    __s32 result;
    __u64 function_nonce;
    __u64 client_uid;
    __u64 owner_epoch;
    __u64 transition_uid;
    __u64 certificate_uid;
    __u32 controller_epoch;
    __u32 execution_epoch;
    __u32 owner_kind;
    __u32 phase;
    __u32 target_owner;
    __u32 policy;
    __u32 old_controller_epoch;
    __u32 old_execution_epoch;
    __u64 old_owner_epoch;
    __u32 old_owner_kind;
    __u32 proof_flags;
    __u32 host_dma_authorities;
    __u32 mapping_refs;
    __u32 pin_refs;
    __u32 dma_operations;
    __u32 controller_buffer_leases;
    __u32 lifecycle_commands;
    __u32 aggregate_block_operations;
    __u32 completion_leases;
    __u32 cqe_workers;
    __u32 irq_workers;
    __u32 pba_pending_vectors;
    __u32 reserved0;
    __u64 ftl_epoch_proof[2];
    __u64 nfc_epoch_proof[2];
    __u8 media_uuid[16];
    __u32 media_format_version;
    __u32 generation;
    __u8 binding_sha256[32];
    __u64 reserved[4];
};

#define FWLAB_M4_OWNER_EXCHANGE _IOWR('N', 0x71, struct fwlab_m4_owner_message)

#endif /* FWLAB_UNSTABLE_M4_OWNER_NATIVE_H */
