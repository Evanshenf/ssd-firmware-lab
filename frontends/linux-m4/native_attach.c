/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "native_internal.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/ioctl.h>

static int empty_identity(const uint8_t *bytes, size_t count)
{
    while (count--)
        if (*bytes++) return 0;
    return 1;
}

static int retryable(int error)
{
    return error == EFAULT || error == EINTR;
}

static int remember_attachment(struct native_context *context,
    const struct fwlab_m4_attach_message *reply, uint32_t producer)
{
    if (!reply->function_nonce || !reply->controller_epoch ||
        (context->function_nonce && context->function_nonce != reply->function_nonce))
        return -EPROTO;
    if (context->producer_mode && context->producer_mode != producer)
        return -EPROTO;
    if (context->attachment.media_format_version &&
        (context->attachment.media_format_version != reply->media_format_version ||
         memcmp(context->attachment.media_uuid, reply->media_uuid, 16) ||
         memcmp(context->attachment.binding_sha256, reply->binding_sha256, 32)))
        return -EPROTO;
    context->attachment = *reply;
    context->producer_mode = producer;
    context->function_nonce = reply->function_nonce;
    context->epoch = reply->controller_epoch;
    return 0;
}

int native_attach_explicit(struct native_context *context, uint32_t format,
    const uint8_t uuid[16], const uint8_t binding[32])
{
    struct fwlab_m4_attach_message request = { 0 };
    unsigned attempt;

    if (!context || !uuid || !binding || context->runtime ||
        (format != FWLAB_M4_MEDIA_LEGACY && format != FWLAB_M4_MEDIA_SCALED) ||
        empty_identity(uuid, 16) || empty_identity(binding, 32))
        return -EINVAL;
    request.version = FWLAB_M4_ATTACH_VERSION;
    request.size = sizeof(request);
    request.media_format_version = format;
    request.result = INT32_MIN;
    memcpy(request.media_uuid, uuid, 16);
    memcpy(request.binding_sha256, binding, 32);
    for (attempt = 0; attempt < 3; ++attempt) {
        struct fwlab_m4_attach_message reply = request, expected = request;
        int result = ioctl(context->descriptor, FWLAB_M4_ATTACH_IDENTITY, &reply);
        if (result < 0) {
            int error = errno;
            if (retryable(error) && attempt + 1 < 3) continue;
            return -error;
        }
        if (result || reply.result == INT32_MIN || reply.result > 0)
            return -EPROTO;
        if (reply.result) return reply.result;
        expected.result = 0;
        expected.function_nonce = reply.function_nonce;
        expected.controller_epoch = reply.controller_epoch;
        if (memcmp(&reply, &expected, sizeof(reply))) return -EPROTO;
        return remember_attachment(context, &reply, FWLAB_M4_PRODUCER_BAR);
    }
    return -EIO;
}

int native_attach_legacy(struct native_context *context,
    const uint8_t uuid[16], const uint8_t binding[32])
{
    struct fwlab_m4_native_message request = { 0 };
    struct fwlab_m4_attach_message accepted = { 0 };
    unsigned attempt;

    if (!context || !uuid || !binding || context->runtime ||
        empty_identity(uuid, 16) || empty_identity(binding, 32))
        return -EINVAL;
    request.version = FWLAB_M4_NATIVE_VERSION;
    request.size = sizeof(request);
    request.operation = FWLAB_M4_NATIVE_ATTACH;
    request.result = INT32_MIN;
    memcpy(request.media_uuid, uuid, 16);
    memcpy(request.binding_sha256, binding, 32);
    for (attempt = 0; attempt < 3; ++attempt) {
        struct fwlab_m4_native_message reply = request, expected = request;
        int result = ioctl(context->descriptor, FWLAB_M4_NATIVE_EXCHANGE, &reply);
        if (result < 0) {
            int error = errno;
            if (retryable(error) && attempt + 1 < 3) continue;
            return -error;
        }
        if (result || reply.result == INT32_MIN || reply.result > 0)
            return -EPROTO;
        if (reply.result) return reply.result;
        expected.result = 0;
        expected.function_nonce = reply.function_nonce;
        expected.controller_epoch = reply.controller_epoch;
        if (memcmp(&reply, &expected, sizeof(reply))) return -EPROTO;
        accepted.version = FWLAB_M4_ATTACH_VERSION;
        accepted.size = sizeof(accepted);
        accepted.media_format_version = FWLAB_M4_MEDIA_LEGACY;
        accepted.function_nonce = reply.function_nonce;
        accepted.controller_epoch = reply.controller_epoch;
        memcpy(accepted.media_uuid, uuid, 16);
        memcpy(accepted.binding_sha256, binding, 32);
        return remember_attachment(context, &accepted, FWLAB_M4_PRODUCER_BAR);
    }
    return -EIO;
}

int native_attach_mode(struct native_context *context, uint32_t producer, uint32_t format,
    const uint8_t uuid[16], const uint8_t binding[32])
{
    struct fwlab_m4_attach_mode_message request = { 0 };
    unsigned attempt;

    if (!context || !uuid || !binding || context->runtime ||
        (producer != FWLAB_M4_PRODUCER_BAR && producer != FWLAB_M4_PRODUCER_PUMP) ||
        (format != FWLAB_M4_MEDIA_LEGACY && format != FWLAB_M4_MEDIA_SCALED) ||
        empty_identity(uuid, 16) || empty_identity(binding, 32))
        return -EINVAL;
    request.version = FWLAB_M4_ATTACH_MODE_VERSION;
    request.size = sizeof(request);
    request.producer_mode = producer;
    request.media_format_version = format;
    request.result = INT32_MIN;
    memcpy(request.media_uuid, uuid, 16);
    memcpy(request.binding_sha256, binding, 32);
    for (attempt = 0; attempt < 3; ++attempt) {
        struct fwlab_m4_attach_mode_message reply = request, expected = request;
        struct fwlab_m4_attach_message identity = { 0 };
        int result = ioctl(context->descriptor, FWLAB_M4_ATTACH_MODE, &reply);
        if (result < 0) {
            int error = errno;
            if (retryable(error) && attempt + 1 < 3) continue;
            return -error;
        }
        if (result || reply.result == INT32_MIN || reply.result > 0)
            return -EPROTO;
        if (reply.result) return reply.result;
        expected.result = 0;
        expected.function_nonce = reply.function_nonce;
        expected.controller_epoch = reply.controller_epoch;
        if (memcmp(&reply, &expected, sizeof(reply))) return -EPROTO;
        /* Store the same private identity representation used by BAR clients;
         * wire v2 has already been validated in full, without a v1 cast. */
        identity.version = FWLAB_M4_ATTACH_VERSION;
        identity.size = sizeof(identity);
        identity.media_format_version = reply.media_format_version;
        identity.function_nonce = reply.function_nonce;
        identity.controller_epoch = reply.controller_epoch;
        memcpy(identity.media_uuid, reply.media_uuid, 16);
        memcpy(identity.binding_sha256, reply.binding_sha256, 32);
        return remember_attachment(context, &identity, producer);
    }
    return -EIO;
}
