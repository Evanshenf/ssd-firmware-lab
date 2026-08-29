/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _POSIX_C_SOURCE 200809L

#include "c34_file_internal.h"

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static enum c34_file_io_result posix_size(void *context, uint64_t *size)
{
    int *fd = context;
    struct stat status;

    if (fd == NULL || size == NULL || fstat(*fd, &status) != 0 ||
        status.st_size < 0) {
        return C34_FILE_IO_FAILURE;
    }
    *size = (uint64_t)status.st_size;
    return C34_FILE_IO_OK;
}

static enum c34_file_io_result posix_resize(void *context, uint64_t size)
{
    int *fd = context;

    if (fd == NULL || size > (uint64_t)INT64_MAX ||
        ftruncate(*fd, (off_t)size) != 0) {
        return C34_FILE_IO_FAILURE;
    }
    return C34_FILE_IO_OK;
}

static enum c34_file_io_result posix_read_all(
    void *context,
    uint64_t offset,
    uint8_t *bytes,
    size_t length
)
{
    int *fd = context;
    size_t done = 0;

    if (fd == NULL || bytes == NULL || offset > (uint64_t)INT64_MAX) {
        return C34_FILE_IO_FAILURE;
    }
    while (done < length) {
        ssize_t count = pread(*fd, &bytes[done], length - done,
                              (off_t)(offset + done));

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return C34_FILE_IO_FAILURE;
        }
        done += (size_t)count;
    }
    return C34_FILE_IO_OK;
}

static enum c34_file_io_result posix_write_all(
    void *context,
    uint64_t offset,
    const uint8_t *bytes,
    size_t length
)
{
    int *fd = context;
    size_t done = 0;

    if (fd == NULL || bytes == NULL || offset > (uint64_t)INT64_MAX) {
        return C34_FILE_IO_FAILURE;
    }
    while (done < length) {
        ssize_t count = pwrite(*fd, &bytes[done], length - done,
                               (off_t)(offset + done));

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return C34_FILE_IO_FAILURE;
        }
        done += (size_t)count;
    }
    return C34_FILE_IO_OK;
}

static enum c34_file_io_result posix_barrier(void *context)
{
    int *fd = context;

    return fd != NULL && fdatasync(*fd) == 0 ?
        C34_FILE_IO_OK : C34_FILE_IO_FAILURE;
}

static const struct c34_file_substrate_ops posix_ops = {
    .version = C34_FILE_FORMAT_VERSION,
    .size = sizeof(struct c34_file_substrate_ops),
    .reserved = 0,
    .size_bytes = posix_size,
    .resize = posix_resize,
    .read = posix_read_all,
    .write = posix_write_all,
    .barrier = posix_barrier,
};

static int fd_is_regular(int fd, int require_empty, int require_unlinked)
{
    struct stat status;

    return fd >= 0 && fstat(fd, &status) == 0 && S_ISREG(status.st_mode) &&
           (!require_empty || status.st_size == 0) &&
           (!require_unlinked || status.st_nlink == 0);
}

static void retain_context(struct c34_file_media *media, int fd)
{
    media->platform_fd = fd;
    media->substrate.ops = &posix_ops;
    media->substrate.context = &media->platform_fd;
}

enum c34_file_result c34_file_posix_format(
    void *arena,
    size_t arena_size,
    int fd,
    const uint8_t image_uuid[16],
    struct c34_file_media **media
)
{
    int context;
    struct c34_file_substrate substrate;
    enum c34_file_result result;

    if (!fd_is_regular(fd, 1, 1)) {
        return C34_FILE_INVALID;
    }
    context = fd;
    substrate.ops = &posix_ops;
    substrate.context = &context;
    result = c34_file_format(
        arena, arena_size, &substrate, image_uuid, media);
    if (result == C34_FILE_OK) {
        retain_context(*media, fd);
    }
    return result;
}

enum c34_file_result c34_file_posix_restart(
    void *arena,
    size_t arena_size,
    int fd,
    const uint8_t expected_uuid[16],
    struct c34_file_media **media
)
{
    int context;
    struct c34_file_substrate substrate;
    struct stat status;
    enum c34_file_result result;

    if (!fd_is_regular(fd, 0, 0) || fstat(fd, &status) != 0 ||
        status.st_size != C34_FILE_IMAGE_BYTES) {
        return C34_FILE_INVALID;
    }
    context = fd;
    substrate.ops = &posix_ops;
    substrate.context = &context;
    result = c34_file_restart(
        arena, arena_size, &substrate, expected_uuid, media);
    if (result == C34_FILE_OK) {
        retain_context(*media, fd);
    }
    return result;
}
