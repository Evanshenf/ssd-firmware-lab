/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_UNSTABLE_M4_ATTACH_NATIVE_H
#define FWLAB_UNSTABLE_M4_ATTACH_NATIVE_H

#include <linux/ioctl.h>
#include <linux/types.h>

/* This schema is independent of ordinary native EXCHANGE v1 and media format.
 * Legacy ATTACH implicitly selects LEGACY. Explicit attachment never falls
 * back to it. These identify complete construction-selected media families. */
#define FWLAB_M4_ATTACH_VERSION 1U
#define FWLAB_M4_MEDIA_LEGACY 1U
#define FWLAB_M4_MEDIA_SCALED 2U
#define FWLAB_M4_ATTACH_MODE_VERSION 2U
#define FWLAB_M4_PRODUCER_BAR 1U
#define FWLAB_M4_PRODUCER_PUMP 2U

struct fwlab_m4_attach_message {
    __u32 version;
    __u32 size;
    __u32 media_format_version;
    __s32 result;
    __u64 function_nonce; /* output only; zero in every request/retry */
    __u32 controller_epoch; /* current observation, not a readiness grant */
    __u32 reserved0;
    __u8 media_uuid[16];
    __u8 binding_sha256[32];
    __u64 reserved[4];
};

#define FWLAB_M4_ATTACH_IDENTITY \
    _IOWR('N', 0x73, struct fwlab_m4_attach_message)

/* A distinct 128-byte command, not a reinterpretation of v1 reserved bytes.
 * Producer mode is construction identity, independent of media format. */
struct fwlab_m4_attach_mode_message {
    __u32 version;
    __u32 size;
    __u32 media_format_version;
    __s32 result;
    __u64 function_nonce;
    __u32 controller_epoch;
    __u32 reserved0;
    __u8 media_uuid[16];
    __u8 binding_sha256[32];
    __u32 producer_mode;
    __u32 reserved1;
    __u64 reserved[5];
};

#define FWLAB_M4_ATTACH_MODE \
    _IOWR('N', 0x73, struct fwlab_m4_attach_mode_message)

#endif
