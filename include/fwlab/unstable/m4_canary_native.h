/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_UNSTABLE_M4_CANARY_NATIVE_H
#define FWLAB_UNSTABLE_M4_CANARY_NATIVE_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define FWLAB_M4_CANARY_VERSION 1U
enum fwlab_m4_canary_operation {
    FWLAB_M4_CANARY_ARM = 1, FWLAB_M4_CANARY_HOLD = 2,
    FWLAB_M4_CANARY_PROBE = 3, FWLAB_M4_CANARY_RELEASE = 4,
    FWLAB_M4_CANARY_QUERY = 5, FWLAB_M4_CANARY_DISARM = 6,
    FWLAB_M4_CANARY_HELD = 7
};

/* Fixed J3 observation/control record on the already exclusive firmware FD.
 * Inert snapshots do not retain a mapping, pin, command or publication lease. */
struct fwlab_m4_canary_message {
    __u32 version, size, operation, flags;
    __u64 function_nonce;
    __s32 result, dma_result, mapping_result, publication_result, irq_result;
    __s32 firmware_lease_result;
    __u64 origin_uid;
    __u32 controller_epoch, held;
    __aligned_u64 data_pointer;
    __u64 old_origin, new_origin;
    __u64 old_owner_epoch, new_owner_epoch;
    __u64 old_domain, new_domain;
    __u64 old_data_iova, new_data_iova;
    __u64 old_cq_iova, new_cq_iova;
    __u64 old_completion_uid, new_completion_uid;
    __u64 old_route_generation, new_route_generation;
    __u32 old_controller, new_controller;
    __u32 old_virq, new_virq;
    __u64 reserved[4];
};

#define FWLAB_M4_CANARY_EXCHANGE _IOWR('N', 0x72, struct fwlab_m4_canary_message)
#endif /* FWLAB_UNSTABLE_M4_CANARY_NATIVE_H */
