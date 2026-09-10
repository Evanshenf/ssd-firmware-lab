/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_LINUX_PROFILE_LIMITS_H
#define FWLAB_LINUX_PROFILE_LIMITS_H

#include <stdint.h>

/* Private construction values. No Linux wire, Host address or media geometry. */
struct fwlab_linux_profile_limits {
    uint32_t max_io_bytes;
    uint32_t max_admin_bytes;
    uint32_t controller_page_bytes;
    uint32_t io_queue_pairs;
    uint32_t queue_depth;
    uint32_t vectors;
};

static inline struct fwlab_linux_profile_limits fwlab_linux_profile_small_limits(void)
{
    const struct fwlab_linux_profile_limits limits = {8192, 8192, 4096, 1, 32, 1};
    return limits;
}

static inline struct fwlab_linux_profile_limits fwlab_linux_profile_large_limits(void)
{
    const struct fwlab_linux_profile_limits limits = {1048576, 4096, 4096, 1, 32, 1};
    return limits;
}

static inline int fwlab_linux_profile_limits_valid(const struct fwlab_linux_profile_limits *p)
{
    int small, large;
    if (!p || p->controller_page_bytes != 4096 || p->queue_depth != 32)
        return 0;
    small = p->max_io_bytes == 8192 && p->max_admin_bytes == 8192;
    large = p->max_io_bytes == 1048576 && p->max_admin_bytes == 4096;
    return ((small || large) && p->io_queue_pairs == 1 && p->vectors == 1) ||
           (large && p->io_queue_pairs == 2 && p->vectors == 3);
}

static inline struct fwlab_linux_profile_limits fwlab_linux_profile_mq2_limits(void)
{
    const struct fwlab_linux_profile_limits limits = {1048576, 4096, 4096, 2, 32, 3};
    return limits;
}

#endif
