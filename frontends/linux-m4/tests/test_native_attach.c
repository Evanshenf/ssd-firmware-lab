/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "../native_internal.h"
#include "../../../kernel/m4-native/m4_attach_identity.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "ATTACH_CHECK %d: %s\n", __LINE__, #x); exit(1); \
} } while (0)

static struct {
    struct fwlab_m4_attachment stored;
    unsigned calls, legacy_calls, losses, malformed, mode, pump_calls;
    int service_result;
    int unsupported, always_lost;
} endpoint;
static const uint8_t uuid[16] = { 1, 2, 3, 4 };
static const uint8_t binding[32] = { 9, 8, 7, 6 };

/* Actual HIF identity validation/pinning and actual userspace retry code.
 * Only syscall/copyout is simulated: no kernel lock, readiness or PCI claim. */
int __wrap_ioctl(int descriptor, unsigned long command, ...)
{
    void *argument;
    va_list args;
    int result;

    CHECK(descriptor == -180);
    ++endpoint.calls;
    if (endpoint.unsupported) { errno = ENOTTY; return -1; }
    va_start(args, command);
    if (command == FWLAB_M4_ATTACH_IDENTITY)
        argument = va_arg(args, struct fwlab_m4_attach_message *);
    else if (command == FWLAB_M4_ATTACH_MODE)
        argument = va_arg(args, struct fwlab_m4_attach_mode_message *);
    else if (command == FWLAB_M4_PUMP)
        argument = va_arg(args, struct fwlab_m4_pump_message *);
    else
        argument = va_arg(args, struct fwlab_m4_native_message *);
    va_end(args);
    if (command == FWLAB_M4_ATTACH_IDENTITY) {
        struct fwlab_m4_attach_message *message = argument;
        CHECK(fwlab_m4_attach_request_valid(message));
        CHECK(!memcmp(message->media_uuid, uuid, sizeof(uuid)));
        CHECK(!memcmp(message->binding_sha256, binding, sizeof(binding)));
        result = fwlab_m4_attach_pin_mode(&endpoint.stored,
            endpoint.mode ? endpoint.mode : FWLAB_M4_PRODUCER_BAR,
            FWLAB_M4_PRODUCER_BAR, message->media_format_version,
            message->media_uuid, message->binding_sha256);
        message->result = result;
        if (!result) {
            message->media_format_version = endpoint.stored.media_format_version;
            memcpy(message->media_uuid, endpoint.stored.media_uuid, 16);
            memcpy(message->binding_sha256, endpoint.stored.binding_sha256, 32);
            message->function_nonce = 9123;
            message->controller_epoch = 7;
        }
        if (endpoint.always_lost || endpoint.losses) {
            if (endpoint.losses) --endpoint.losses;
            /* Includes partial header/identity copyout. The next attempt must
             * reconstruct the original request, not reuse these output bytes. */
            memset(message, 0x5a, sizeof(*message) / 2);
            errno = EFAULT;
            return -1;
        }
        switch (endpoint.malformed) {
        case 1: ++message->version; break;
        case 2: --message->size; break;
        case 3: message->reserved[3] = 1; break;
        case 4: message->media_uuid[15] ^= 1; break;
        case 5: message->binding_sha256[31] ^= 1; break;
        case 6: ++message->media_format_version; break;
        case 7: message->function_nonce = 0; break;
        case 8: message->controller_epoch = 0; break;
        default: break;
        }
        return 0;
    }
    if (command == FWLAB_M4_ATTACH_MODE) {
        struct fwlab_m4_attach_mode_message *message = argument;
        CHECK(fwlab_m4_attach_mode_request_valid(message));
        CHECK(!memcmp(message->media_uuid, uuid, 16));
        CHECK(!memcmp(message->binding_sha256, binding, 32));
        message->result = fwlab_m4_attach_pin_mode(&endpoint.stored,
            endpoint.mode, message->producer_mode, message->media_format_version,
            message->media_uuid, message->binding_sha256);
        if (!message->result) {
            message->producer_mode = endpoint.mode;
            message->function_nonce = 9123;
            message->controller_epoch = 7;
        }
        if (endpoint.always_lost || endpoint.losses) {
            if (endpoint.losses) --endpoint.losses;
            memset(message, 0x5a, sizeof(*message) / 2);
            errno = EFAULT;
            return -1;
        }
        switch (endpoint.malformed) {
        case 1: ++message->version; break;
        case 2: --message->size; break;
        case 3: message->reserved[4] = 1; break;
        case 4: message->producer_mode ^= 3; break;
        case 5: message->binding_sha256[31] ^= 1; break;
        case 6: ++message->media_format_version; break;
        case 7: message->function_nonce = 0; break;
        case 8: message->controller_epoch = 0; break;
        default: break;
        }
        return 0;
    }
    if (command == FWLAB_M4_PUMP) {
        struct fwlab_m4_pump_message *message = argument;
        CHECK(fwlab_m4_pump_request_valid(message) && message->function_nonce == 9123);
        ++endpoint.pump_calls;
        message->result = 0;
        message->service_result = endpoint.service_result;
        /* The final observation may be zero after an earlier lost tick.
         * This is syscall/retry coverage, not a kernel SQ/CQ model. */
        message->captured = endpoint.pump_calls == 1;
        if (endpoint.always_lost || endpoint.losses) {
            if (endpoint.losses) --endpoint.losses;
            memset(message, 0x5a, sizeof(*message) / 2);
            errno = EFAULT;
            return -1;
        }
        switch (endpoint.malformed) {
        case 1: ++message->version; break;
        case 2: --message->size; break;
        case 3: message->reserved[1] = 1; break;
        case 4: message->captured = 2; break;
        case 5: message->service_result = 1; break;
        case 6: message->service_result = -4096; break;
        case 7: ++message->function_nonce; break;
        case 8: message->result = 1; break;
        default: break;
        }
        return 0;
    }
    CHECK(command == FWLAB_M4_NATIVE_EXCHANGE);
    {
        struct fwlab_m4_native_message *message = argument;
        ++endpoint.legacy_calls;
        CHECK(message->version == FWLAB_M4_NATIVE_VERSION && message->size == sizeof(*message));
        CHECK(message->operation == FWLAB_M4_NATIVE_ATTACH);
        CHECK(!message->function_nonce && !message->controller_epoch);
        result = fwlab_m4_attach_pin_mode(&endpoint.stored,
            endpoint.mode ? endpoint.mode : FWLAB_M4_PRODUCER_BAR,
            FWLAB_M4_PRODUCER_BAR, FWLAB_M4_MEDIA_LEGACY,
            message->media_uuid, message->binding_sha256);
        message->result = result;
        if (!result) { message->function_nonce = 9123; message->controller_epoch = 7; }
        if (endpoint.losses) {
            --endpoint.losses;
            memset(message, 0x3c, sizeof(*message) / 2);
            errno = EINTR;
            return -1;
        }
        return 0;
    }
}

static void pin_and_header_checks(void)
{
    struct fwlab_m4_attachment stored = { 0 }, saved;
    struct fwlab_m4_attach_message request = { 0 }, bad;
    uint8_t changed_uuid[16], changed_binding[32], zero[32] = { 0 };

    CHECK(sizeof(request) == 112);
    CHECK(FWLAB_M4_NATIVE_VERSION == 1 && FWLAB_M4_NATIVE_MAX_BYTES == 8192);
    CHECK(fwlab_m4_attach_pin(&stored, 0, uuid, binding) == -EINVAL);
    CHECK(fwlab_m4_attach_pin(&stored, 3, uuid, binding) == -EINVAL);
    CHECK(fwlab_m4_attach_pin(&stored, 2, zero, binding) == -EINVAL);
    CHECK(fwlab_m4_attach_pin(&stored, 2, uuid, zero) == -EINVAL);
    CHECK(!stored.media_format_version);
    CHECK(fwlab_m4_attach_pin(&stored, 2, uuid, binding) == 0);
    saved = stored;
    CHECK(fwlab_m4_attach_pin(&stored, 2, uuid, binding) == 0);
    CHECK(fwlab_m4_attach_pin(&stored, 1, uuid, binding) == -EBUSY);
    memcpy(changed_uuid, uuid, 16); changed_uuid[15] ^= 1;
    memcpy(changed_binding, binding, 32); changed_binding[31] ^= 1;
    CHECK(fwlab_m4_attach_pin(&stored, 2, changed_uuid, binding) == -EBUSY);
    CHECK(fwlab_m4_attach_pin(&stored, 2, uuid, changed_binding) == -EBUSY);
    CHECK(!memcmp(&stored, &saved, sizeof(saved)));
    request.version = FWLAB_M4_ATTACH_VERSION;
    request.size = sizeof(request);
    CHECK(fwlab_m4_attach_request_valid(&request));
    bad = request; ++bad.version; CHECK(!fwlab_m4_attach_request_valid(&bad));
    bad = request; --bad.size; CHECK(!fwlab_m4_attach_request_valid(&bad));
    bad = request; bad.reserved0 = 1; CHECK(!fwlab_m4_attach_request_valid(&bad));
    bad = request; bad.reserved[3] = 1; CHECK(!fwlab_m4_attach_request_valid(&bad));
    bad = request; bad.function_nonce = 1; CHECK(!fwlab_m4_attach_request_valid(&bad));
    bad = request; bad.controller_epoch = 1; CHECK(!fwlab_m4_attach_request_valid(&bad));
}

static void mode_and_pump_checks(struct native_context *context)
{
    struct fwlab_m4_attach_mode_message wire = { 0 }, bad;
    struct fwlab_m4_pump_message pump = { 0 }, bad_pump;
    int service;

    CHECK(sizeof(wire) == 128 && sizeof(pump) == 48);
    CHECK(FWLAB_M4_ATTACH_MODE != FWLAB_M4_ATTACH_IDENTITY);
    wire.version = FWLAB_M4_ATTACH_MODE_VERSION;
    wire.size = sizeof(wire);
    wire.producer_mode = FWLAB_M4_PRODUCER_PUMP;
    CHECK(fwlab_m4_attach_mode_request_valid(&wire));
    bad = wire; bad.version = 1; CHECK(!fwlab_m4_attach_mode_request_valid(&bad));
    bad = wire; bad.size = 112; CHECK(!fwlab_m4_attach_mode_request_valid(&bad));
    bad = wire; bad.reserved1 = 1; CHECK(!fwlab_m4_attach_mode_request_valid(&bad));
    bad = wire; bad.reserved[4] = 1; CHECK(!fwlab_m4_attach_mode_request_valid(&bad));
    bad = wire; bad.producer_mode = 3; CHECK(!fwlab_m4_attach_mode_request_valid(&bad));
    pump.version = FWLAB_M4_PUMP_VERSION; pump.size = sizeof(pump); pump.function_nonce = 9123;
    CHECK(fwlab_m4_pump_request_valid(&pump));
    bad_pump = pump; --bad_pump.size; CHECK(!fwlab_m4_pump_request_valid(&bad_pump));
    bad_pump = pump; bad_pump.reserved0 = 1; CHECK(!fwlab_m4_pump_request_valid(&bad_pump));
    bad_pump = pump; bad_pump.captured = 1; CHECK(!fwlab_m4_pump_request_valid(&bad_pump));
    bad_pump = pump; bad_pump.service_result = -EIO; CHECK(!fwlab_m4_pump_request_valid(&bad_pump));
    for (unsigned mode = 1; mode <= 2; ++mode) {
        memset(context, 0, sizeof(*context)); context->descriptor = -180;
        memset(&endpoint, 0, sizeof(endpoint)); endpoint.mode = mode;
        if (mode == 2) {
            CHECK(native_attach_explicit(context, 2, uuid, binding) == -EOPNOTSUPP);
            CHECK(native_attach_legacy(context, uuid, binding) == -EOPNOTSUPP);
            CHECK(!endpoint.stored.media_format_version && !context->function_nonce);
        }
        CHECK(native_attach_mode(context, mode ^ 3, 2, uuid, binding) == -EOPNOTSUPP);
        CHECK(!endpoint.stored.media_format_version && !context->function_nonce);
        endpoint.losses = 1;
        CHECK(native_attach_mode(context, mode, 2, uuid, binding) == 0);
        CHECK(context->producer_mode == mode && context->attachment.media_format_version == 2);
        CHECK(native_attach_mode(context, mode, 2, uuid, binding) == 0);
    }
    for (unsigned malformed = 1; malformed <= 8; ++malformed) {
        memset(context, 0, sizeof(*context)); context->descriptor = -180;
        memset(&endpoint, 0, sizeof(endpoint)); endpoint.mode = 2; endpoint.malformed = malformed;
        CHECK(native_attach_mode(context, 2, 2, uuid, binding) == -EPROTO);
        CHECK(endpoint.calls == 1 && !context->producer_mode && !context->function_nonce);
    }
    memset(context, 0, sizeof(*context)); context->descriptor = -180;
    memset(&endpoint, 0, sizeof(endpoint)); endpoint.unsupported = 1;
    CHECK(native_attach_mode(context, 2, 2, uuid, binding) == -ENOTTY);
    CHECK(endpoint.calls == 1 && !endpoint.legacy_calls);
    memset(&endpoint, 0, sizeof(endpoint)); endpoint.mode = 2; endpoint.always_lost = 1;
    CHECK(native_attach_mode(context, 2, 2, uuid, binding) == -EFAULT);
    CHECK(endpoint.calls == 3 && endpoint.stored.media_format_version == 2 && !context->producer_mode);
    memset(&endpoint, 0, sizeof(endpoint)); endpoint.mode = 2;
    CHECK(native_attach_mode(context, 2, 2, uuid, binding) == 0);
    endpoint.losses = 1;
    CHECK(native_pump(context, &service) == 0 && service == 0 && endpoint.pump_calls == 2);
    endpoint.service_result = -EIO;
    CHECK(native_pump(context, &service) == 0 && service == -EIO);
    for (unsigned malformed = 1; malformed <= 8; ++malformed) {
        endpoint.malformed = malformed;
        service = 99;
        CHECK(native_pump(context, &service) == -EPROTO && service == 99);
    }
    endpoint.malformed = 0; endpoint.always_lost = 1; endpoint.pump_calls = 0;
    CHECK(native_pump(context, &service) == -EFAULT && endpoint.pump_calls == 3);
    endpoint.always_lost = 0; endpoint.unsupported = 1;
    CHECK(native_pump(context, &service) == -ENOTTY);
    puts("NATIVE_MODE_PUMP_PASS|actual_mode_pin_and_retry=1|legacy_wire_unchanged=1|service_fault_distinct=1|not_kernel_progress_proof=1");
}

int main(void)
{
    struct native_context *context = calloc(1, sizeof(*context));
    CHECK(context);
    pin_and_header_checks();
    for (unsigned format = 1; format <= 2; ++format) {
        memset(context, 0, sizeof(*context)); context->descriptor = -180;
        memset(&endpoint, 0, sizeof(endpoint)); endpoint.losses = 1;
        CHECK(native_attach_explicit(context, format, uuid, binding) == 0);
        CHECK(endpoint.calls == 2 && endpoint.legacy_calls == 0);
        CHECK(context->function_nonce == 9123 && context->epoch == 7 &&
              context->attachment.media_format_version == format);
        CHECK(native_attach_explicit(context, format, uuid, binding) == 0);
        CHECK(endpoint.calls == 3);
        if (format == 2)
            CHECK(native_attach_legacy(context, uuid, binding) == -EBUSY);
    }
    memset(context, 0, sizeof(*context)); context->descriptor = -180;
    memset(&endpoint, 0, sizeof(endpoint)); endpoint.losses = 1;
    CHECK(native_attach_legacy(context, uuid, binding) == 0);
    CHECK(endpoint.calls == 2 && endpoint.legacy_calls == 2 &&
          context->attachment.media_format_version == 1);
    for (unsigned malformed = 1; malformed <= 8; ++malformed) {
        memset(context, 0, sizeof(*context)); context->descriptor = -180;
        memset(&endpoint, 0, sizeof(endpoint)); endpoint.malformed = malformed;
        CHECK(native_attach_explicit(context, 2, uuid, binding) == -EPROTO);
        CHECK(endpoint.calls == 1 && !context->attachment.media_format_version);
    }
    memset(context, 0, sizeof(*context)); context->descriptor = -180;
    memset(&endpoint, 0, sizeof(endpoint)); endpoint.unsupported = 1;
    CHECK(native_attach_explicit(context, 2, uuid, binding) == -ENOTTY);
    CHECK(endpoint.calls == 1 && endpoint.legacy_calls == 0);
    memset(&endpoint, 0, sizeof(endpoint)); endpoint.always_lost = 1;
    CHECK(native_attach_explicit(context, 2, uuid, binding) == -EFAULT);
    CHECK(endpoint.calls == 3 && endpoint.stored.media_format_version == 2 &&
          !context->function_nonce && !context->attachment.media_format_version);
    mode_and_pump_checks(context);
    free(context);
    puts("NATIVE_ATTACH_PASS|actual_pin_and_retry=1|legacy_explicit_identity=1|finite_copyout=1|no_kernel_execution_claim=1");
    return 0;
}
