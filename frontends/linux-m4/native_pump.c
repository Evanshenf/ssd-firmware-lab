/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "native_internal.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/ioctl.h>

int native_pump(struct native_context *context, int *service_result)
{
    struct fwlab_m4_pump_message request = { 0 };
    unsigned attempt;

    if (!context || !service_result || !context->function_nonce ||
        context->producer_mode != FWLAB_M4_PRODUCER_PUMP)
        return -EINVAL;
    request.version = FWLAB_M4_PUMP_VERSION;
    request.size = sizeof(request);
    request.function_nonce = context->function_nonce;
    request.result = INT32_MIN;
    for (attempt = 0; attempt < 3; ++attempt) {
        struct fwlab_m4_pump_message reply = request, expected = request;
        int result = ioctl(context->descriptor, FWLAB_M4_PUMP, &reply);
        if (result < 0) {
            int error = errno;
            if ((error == EFAULT || error == EINTR) && attempt + 1 < 3) continue;
            return -error;
        }
        if (result || reply.result == INT32_MIN || reply.result > 0)
            return -EPROTO;
        if (reply.result) return reply.result;
        if (reply.captured > 1 || reply.service_result > 0 || reply.service_result < -4095)
            return -EPROTO;
        expected.result = 0;
        expected.service_result = reply.service_result;
        expected.captured = reply.captured;
        if (memcmp(&reply, &expected, sizeof(reply))) return -EPROTO;
        /* A valid service fault is not a failed exchange. The caller still
         * visits STATUS/owner/reset and must not admit business this turn.
         * Never use captured==0 as evidence that NEXT has nothing retained. */
        *service_result = reply.service_result;
        if (reply.captured) context->progressed = 1;
        return 0;
    }
    return -EIO;
}
