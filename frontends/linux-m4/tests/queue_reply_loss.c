/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Narrow real-ioctl fault fixture, linked only into an explicitly named test
 * worker. Observe a literal Host's unique Admin CID, lose one accepted Delete
 * reply, then require the exact same request on retry. No media/IRQ shortcuts. */
#include "fwlab/unstable/m4_native.h"
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DELETE_TEST_CID 0xd195u
static uint64_t target_function, target_origin;
static uint32_t target_epoch;
static int injected, retried, target_fd;
static struct fwlab_m4_native_message original;

int __real_ioctl(int descriptor, unsigned long operation, ...);
int __wrap_ioctl(int descriptor, unsigned long operation, ...)
{
    va_list args;
    void *argument;
    struct fwlab_m4_native_message before;
    struct fwlab_m4_native_message *message = NULL;
    int target = 0, result;
    va_start(args, operation); argument = va_arg(args, void *); va_end(args);
    if (operation == FWLAB_M4_NATIVE_EXCHANGE) {
        message = argument;
        before = *message;
        target = target_origin && descriptor == target_fd &&
            before.function_nonce == target_function && before.origin_uid == target_origin &&
            before.controller_epoch == target_epoch && before.operation == FWLAB_M4_NATIVE_QUEUE &&
            before.queue_effect == FWLAB_M4_NATIVE_DELETE_SQ && before.queue_id == 1;
        if (target && injected && !retried) {
            if (memcmp(&before, &original, sizeof(before))) {
                fputs("QUEUE_REPLY_RETRY_MISMATCH\n", stderr);
                errno = EPROTO; return -1;
            }
            retried = 1;
            fprintf(stderr, "QUEUE_REPLY_EXACT_RETRY uid=%llu epoch=%u\n",
                    (unsigned long long)target_origin, target_epoch);
        }
    }
    result = __real_ioctl(descriptor, operation, argument);
    if (!message || result) return result;
    if (before.operation == FWLAB_M4_NATIVE_NEXT && !message->result &&
        message->event == FWLAB_M4_NATIVE_COMMAND && !message->queue_id &&
        message->command_id == DELETE_TEST_CID && message->sqe[0] == 0) {
        target_function = message->function_nonce; target_origin = message->origin_uid;
        target_epoch = message->controller_epoch; target_fd = descriptor;
    }
    if (target && !injected && message->result == -EINPROGRESS) {
        original = before; injected = 1;
        fprintf(stderr, "QUEUE_DRAINING_REPLY_LOST uid=%llu epoch=%u cid=%u\n",
                (unsigned long long)target_origin, target_epoch, DELETE_TEST_CID);
        memset(message, 0x5a, sizeof(*message) / 2);
        errno = EFAULT; return -1;
    }
    return result;
}
