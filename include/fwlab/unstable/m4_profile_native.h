/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_UNSTABLE_M4_PROFILE_NATIVE_H
#define FWLAB_UNSTABLE_M4_PROFILE_NATIVE_H

#include "m4_attach_native.h"

#define FWLAB_M4_ATTACH_PROFILE_VERSION 3U
#define FWLAB_M4_HOST_PROFILE_SMALL 1U
#define FWLAB_M4_HOST_PROFILE_LARGE_SERIAL 2U
#define FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL 3U
#define FWLAB_M4_LARGE_IO_BYTES 1048576U
#define FWLAB_M4_CONTROL_PAGE_BYTES 4096U

/* Private construction values, never a portable profile or media ABI. The
 * identity names a complete fixed contract, not arbitrary combinations. */
struct fwlab_m4_host_limits {
    __u32 max_io_bytes;
    __u32 max_admin_bytes;
    __u32 controller_page_bytes;
    __u32 io_queue_pairs;
    __u32 queue_depth;
    __u32 vectors;
    __u32 max_data_pages;
    __u32 max_list_pages;
    __u32 io_ingress;
    __u32 admin_ingress;
};

static inline struct fwlab_m4_host_limits fwlab_m4_host_limits_for(__u32 profile)
{
    struct fwlab_m4_host_limits limits = { 0 };

    if (profile == FWLAB_M4_HOST_PROFILE_SMALL) {
        limits.max_io_bytes = 8192;
        limits.max_admin_bytes = 8192;
        limits.max_data_pages = 3;
        limits.max_list_pages = 1;
        /* These ceilings share the existing 32 metadata slots; not 64. */
        limits.io_ingress = 32;
        limits.admin_ingress = 32;
    } else if (profile == FWLAB_M4_HOST_PROFILE_LARGE_SERIAL ||
               profile == FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL) {
        limits.max_io_bytes = FWLAB_M4_LARGE_IO_BYTES;
        limits.max_admin_bytes = FWLAB_M4_CONTROL_PAGE_BYTES;
        limits.max_data_pages = 257;
        limits.max_list_pages = 2;
        limits.io_ingress = 1;
        limits.admin_ingress = 1;
    } else {
        return limits;
    }
    limits.controller_page_bytes = FWLAB_M4_CONTROL_PAGE_BYTES;
    limits.io_queue_pairs = profile == FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL ? 2 : 1;
    limits.queue_depth = 32;
    limits.vectors = profile == FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL ? 3 : 1;
    return limits;
}

struct fwlab_m4_attach_profile_message {
    __u32 version;
    __u32 size;
    __s32 result;
    __u32 producer_mode;
    __u32 host_profile_id;
    __u32 media_format_version;
    __u64 function_nonce;
    __u32 controller_epoch;
    __u32 reserved0;
    __u8 media_uuid[16];
    __u8 binding_sha256[32];
    struct fwlab_m4_host_limits limits;
    __u64 reserved[4];
};

/* Complete command differs from both 112-byte/v1 and 128-byte/v2. */
#define FWLAB_M4_ATTACH_PROFILE \
    _IOWR('N', 0x73, struct fwlab_m4_attach_profile_message)

#endif
