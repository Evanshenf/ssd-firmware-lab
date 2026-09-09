/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef FWLAB_M4_PRP_GRAPH_H
#define FWLAB_M4_PRP_GRAPH_H

#include "fwlab/unstable/m4_profile_native.h"
#include <linux/errno.h>

/* Bounded HIF-only parser. read_list reads metadata with Host-read permission;
 * capture records direction-specific mappings, never performs data DMA. The
 * caller owns scratch and publishes authority only after this whole walk. */
struct fwlab_m4_prp_walk {
    void *context;
    int (*read_list)(void *, __u64, __u32, void *);
    int (*capture)(void *, __u64, __u32);
    unsigned char *scratch;
    __u32 scratch_bytes;
    __u32 data_pages;
    __u32 list_pages;
};

static inline __u64 fwlab_m4_prp_le64(const unsigned char *p)
{
    __u64 value = 0;
    unsigned i;
    for (i = 0; i < 8; ++i) value |= (__u64)p[i] << (i * 8);
    return value;
}

static inline int fwlab_m4_prp_capture(struct fwlab_m4_prp_walk *walk,
    const struct fwlab_m4_host_limits *limits, __u64 address, __u32 bytes)
{
    int result;
    if (!address || !bytes || bytes > limits->controller_page_bytes ||
        address > ~(__u64)0 - (bytes - 1) || walk->data_pages >= limits->max_data_pages)
        return -EINVAL;
    result = walk->capture(walk->context, address, bytes);
    if (!result) ++walk->data_pages;
    return result;
}

static inline int fwlab_m4_prp_build(struct fwlab_m4_prp_walk *walk,
    const struct fwlab_m4_host_limits *limits, __u64 prp1, __u64 prp2, __u32 bytes)
{
    __u32 page, first, remaining, offset, available, needed, slots, data, index;
    __u64 list;
    int result, chain;

    if (!walk || !limits || !walk->read_list || !walk->capture || !bytes ||
        bytes > limits->max_io_bytes || limits->max_io_bytes > FWLAB_M4_LARGE_IO_BYTES ||
        limits->max_data_pages > 257 ||
        limits->controller_page_bytes != FWLAB_M4_CONTROL_PAGE_BYTES ||
        !limits->max_list_pages || limits->max_list_pages > 2 ||
        !prp1 || (prp1 & 3))
        return -EINVAL;
    page = limits->controller_page_bytes;
    offset = (__u32)(prp1 & (page - 1));
    if (((__u64)offset + bytes + page - 1) / page > limits->max_data_pages)
        return -E2BIG;
    walk->data_pages = walk->list_pages = 0;
    first = page - offset;
    if (first > bytes) first = bytes;
    result = fwlab_m4_prp_capture(walk, limits, prp1, first);
    if (result) return result;
    remaining = bytes - first;
    if (!remaining) return 0;
    if (!prp2) return -EINVAL;
    if (remaining <= page) {
        if (prp2 & (page - 1)) return -EINVAL;
        return fwlab_m4_prp_capture(walk, limits, prp2, remaining);
    }
    if (!walk->scratch || walk->scratch_bytes < page || (prp2 & 7))
        return -EINVAL;
    list = prp2;
    while (remaining) {
        if (walk->list_pages >= limits->max_list_pages || !list ||
            (walk->list_pages && (list & (page - 1))))
            return -EINVAL;
        offset = (__u32)(list & (page - 1));
        available = (page - offset) / 8;
        needed = (remaining + page - 1) / page;
        chain = needed > available;
        slots = chain ? available : needed;
        data = chain ? slots - 1 : slots;
        if (!slots || list > ~(__u64)0 - (slots * 8 - 1)) return -EINVAL;
        result = walk->read_list(walk->context, list, slots * 8, walk->scratch);
        if (result) return result;
        ++walk->list_pages;
        for (index = 0; index < data; ++index) {
            __u64 address = fwlab_m4_prp_le64(walk->scratch + index * 8);
            __u32 count = remaining > page ? page : remaining;
            if (!address || (address & (page - 1))) return -EINVAL;
            result = fwlab_m4_prp_capture(walk, limits, address, count);
            if (result) return result;
            remaining -= count;
        }
        if (chain) {
            list = fwlab_m4_prp_le64(walk->scratch + data * 8);
            if (!list || (list & (page - 1))) return -EINVAL;
        }
    }
    return 0;
}

#endif
