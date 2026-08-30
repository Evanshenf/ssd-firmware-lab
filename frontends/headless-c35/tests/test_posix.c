/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _POSIX_C_SOURCE 200809L

#include "c35_test_support.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static int test_dup_restart(void)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *before = calloc(1, sizeof(*before));
    struct c35_runtime *after = calloc(1, sizeof(*after));
    const uint8_t uuid[16] = {
        0x35, 0x50, 0x4f, 0x53, 0x49, 0x58, 0x20, 0x26,
        0x08, 0x29, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
    };
    uint8_t value[C35_ATOM_BYTES];
    uint8_t raw_before[C35_RAW_PROJECTION_BYTES];
    uint8_t raw_after[C35_RAW_PROJECTION_BYTES];
    uint8_t container_before[C34_FILE_IMAGE_BYTES];
    uint8_t container_after[C34_FILE_IMAGE_BYTES];
    struct c35_request request;
    struct c35_semantic_result result;
    int original;
    int retained;
    int closed_fd;

    CHECK(storage != NULL && before != NULL && after != NULL);
    memset(value, 0x6d, sizeof(value));
    CHECK(c35_storage_init(storage, C35_LANE_POSIX, uuid));
    original = storage->fd;
    CHECK(original >= 0);
    CHECK(c35_runtime_init(
        before, storage, C35_LANE_POSIX, UINT64_C(0x35500001),
        UINT64_C(0x1020304050607080), 0, 0, 0x3550));
    request = c35_request_write(
        1, FWLAB_PERSIST_SELF_DURABLE, 1, value);
    CHECK(c35_run_command(before, &request, &result));
    CHECK(c35_runtime_projection(before, raw_before));
    CHECK(c35_storage_container(storage, container_before));
    CHECK(c35_runtime_teardown(before));

    retained = fcntl(original, F_DUPFD_CLOEXEC, 3);
    CHECK(retained >= 0 && retained != original);
    CHECK(close(original) == 0);
    storage->fd = retained;
    CHECK(c35_storage_restart(storage));
    CHECK(c35_runtime_init(
        after, storage, C35_LANE_POSIX, UINT64_C(0x35500002),
        UINT64_C(0x8877665544332211), 0, 0, 0x3550));
    request = c35_request_read(1);
    CHECK(c35_run_command(after, &request, &result));
    CHECK(result.present_mask == 2u &&
          memcmp(result.payload[1], value, sizeof(value)) == 0);
    CHECK(c35_runtime_projection(after, raw_after));
    CHECK(memcmp(raw_before, raw_after, sizeof(raw_before)) == 0);
    CHECK(c35_storage_container(storage, container_after));
    CHECK(memcmp(container_before, container_after,
                 sizeof(container_before)) == 0);
    CHECK(c35_runtime_teardown(after));
    closed_fd = storage->fd;
    CHECK(c35_storage_close(storage));
    errno = 0;
    CHECK(fcntl(closed_fd, F_GETFD) == -1 && errno == EBADF);
    free(after);
    free(before);
    free(storage);
    return 1;
}

static int test_failed_profile_cleanup(void)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct fwlab_nfc_model_config config =
        c35_test_nfc_config(UINT64_C(0xaabbccddeeff0011));
    const uint8_t uuid[16] = {
        0x35, 0x50, 0x46, 0x41, 0x49, 0x4c, 0x20, 0x26,
        0x08, 0x29, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    };
    uint8_t before[C34_FILE_IMAGE_BYTES];
    uint8_t after[C34_FILE_IMAGE_BYTES];
    int owned_fd;

    CHECK(storage != NULL && runtime != NULL);
    CHECK(c35_storage_init(storage, C35_LANE_POSIX, uuid));
    owned_fd = storage->fd;
    CHECK(c35_storage_container(storage, before));
    config.geometry.pages_per_block = C34_PAGES_PER_BLOCK + 1u;
    CHECK(!c35_runtime_init_profile(
        runtime, storage, C35_LANE_POSIX, UINT64_C(0x3550f001),
        config.fault.seed, 0, 0, 0x355f, &config));
    CHECK(!storage->bundle.claimed);
    CHECK(c35_storage_container(storage, after));
    CHECK(memcmp(before, after, sizeof(before)) == 0);
    CHECK(c35_storage_close(storage));
    errno = 0;
    CHECK(fcntl(owned_fd, F_GETFD) == -1 && errno == EBADF);
    free(runtime);
    free(storage);
    return 1;
}

int main(void)
{
    CHECK(test_dup_restart());
    CHECK(test_failed_profile_cleanup());
    puts("C3.5 POSIX restart: PASS (dup/close-original + exact-fd cleanup)");
    return 0;
}
