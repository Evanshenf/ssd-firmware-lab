/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef FWLAB_M4_ATTACH_IDENTITY_H
#define FWLAB_M4_ATTACH_IDENTITY_H

#include "fwlab/unstable/m4_attach_native.h"
#include "fwlab/unstable/m4_pump_native.h"
#include <linux/errno.h>
#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

/* HIF-private immutable identity, not a media implementation or a registry.
 * Caller holds the HIF mutex and checks stopped/quarantined state. Zero format
 * denotes an unbound identity. No reset or owner transition clears it. */
struct fwlab_m4_attachment {
    __u32 media_format_version;
    __u8 media_uuid[16];
    __u8 binding_sha256[32];
};

static inline int fwlab_m4_attach_zero(const void *bytes, unsigned long size)
{
    const unsigned char *p = bytes;
    while (size--)
        if (*p++) return 0;
    return 1;
}

static inline int fwlab_m4_attach_request_valid(
    const struct fwlab_m4_attach_message *message)
{
    return message->version == FWLAB_M4_ATTACH_VERSION &&
        message->size == sizeof(*message) && !message->function_nonce &&
        !message->controller_epoch && !message->reserved0 &&
        fwlab_m4_attach_zero(message->reserved, sizeof(message->reserved));
}

static inline int fwlab_m4_attach_pin(struct fwlab_m4_attachment *stored,
    __u32 format, const __u8 uuid[16], const __u8 binding[32])
{
    if ((format != FWLAB_M4_MEDIA_LEGACY && format != FWLAB_M4_MEDIA_SCALED) ||
        fwlab_m4_attach_zero(uuid, 16) || fwlab_m4_attach_zero(binding, 32))
        return -EINVAL;
    if (stored->media_format_version) {
        return stored->media_format_version == format &&
            !memcmp(stored->media_uuid, uuid, 16) &&
            !memcmp(stored->binding_sha256, binding, 32) ? 0 : -EBUSY;
    }
    memcpy(stored->media_uuid, uuid, 16);
    memcpy(stored->binding_sha256, binding, 32);
    stored->media_format_version = format;
    return 0;
}

static inline int fwlab_m4_producer_valid(__u32 mode)
{
    return mode == FWLAB_M4_PRODUCER_BAR || mode == FWLAB_M4_PRODUCER_PUMP;
}

static inline int fwlab_m4_attach_mode_request_valid(
    const struct fwlab_m4_attach_mode_message *message)
{
    return message->version == FWLAB_M4_ATTACH_MODE_VERSION &&
        message->size == sizeof(*message) && !message->function_nonce &&
        !message->controller_epoch && !message->reserved0 && !message->reserved1 &&
        fwlab_m4_producer_valid(message->producer_mode) &&
        fwlab_m4_attach_zero(message->reserved, sizeof(message->reserved));
}

/* Mode rejection precedes any identity mutation. The kernel supplies its
 * immutable build mode, never a module parameter or an attached client's mode. */
static inline int fwlab_m4_attach_pin_mode(struct fwlab_m4_attachment *stored,
    __u32 constructed, __u32 requested, __u32 format,
    const __u8 uuid[16], const __u8 binding[32])
{
    if (!fwlab_m4_producer_valid(constructed) || !fwlab_m4_producer_valid(requested))
        return -EINVAL;
    if (constructed != requested)
        return -EOPNOTSUPP;
    return fwlab_m4_attach_pin(stored, format, uuid, binding);
}

static inline int fwlab_m4_pump_request_valid(const struct fwlab_m4_pump_message *message)
{
    return message->version == FWLAB_M4_PUMP_VERSION &&
        message->size == sizeof(*message) && message->function_nonce &&
        !message->service_result && !message->captured && !message->reserved0 &&
        fwlab_m4_attach_zero(message->reserved, sizeof(message->reserved));
}

#endif
