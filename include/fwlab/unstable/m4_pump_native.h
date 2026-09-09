/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_UNSTABLE_M4_PUMP_NATIVE_H
#define FWLAB_UNSTABLE_M4_PUMP_NATIVE_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define FWLAB_M4_PUMP_VERSION 1U

/* Scheduler-only ADVANCE+OBSERVE. A lost reply may follow a completed tick;
 * another tick is not replay. NEXT, not captured, owns command delivery.
 * result describes admission; service_result describes an accepted tick's
 * HIF fault/reset outcome. Neither describes Host completion or durability. */
struct fwlab_m4_pump_message {
    __u32 version;
    __u32 size;
    __u64 function_nonce;
    __s32 result;
    __s32 service_result;
    __u32 captured;
    __u32 reserved0;
    __u64 reserved[2];
};

#define FWLAB_M4_PUMP _IOWR('N', 0x74, struct fwlab_m4_pump_message)

#endif
