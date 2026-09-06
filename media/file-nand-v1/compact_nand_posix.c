/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include "compact_nand_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct fnv1_posix_context {
    int fd;
    int allow_resize;
    uint64_t device;
    uint64_t inode;
    uint64_t expected_size;
};

_Static_assert(sizeof(struct fnv1_posix_context) <= 128,
               "POSIX context fits persistent IO storage");
_Static_assert(sizeof(off_t) >= sizeof(int64_t),
               "compact NAND requires 64-bit file offsets");

static int private_file(const struct stat *status)
{
    return S_ISREG(status->st_mode) && status->st_uid == geteuid() &&
           (status->st_mode & 07777) == 0600 && status->st_nlink == 1 &&
           status->st_size >= 0;
}

static int context_stat(const struct fnv1_posix_context *context,
                        struct stat *status)
{
    return context != NULL && context->fd >= 0 && status != NULL &&
           fstat(context->fd, status) == 0 && private_file(status) &&
           (uint64_t)status->st_dev == context->device &&
           (uint64_t)status->st_ino == context->inode &&
           (uint64_t)status->st_size == context->expected_size;
}

static int span_valid(uint64_t offset, size_t size, uint64_t file_size)
{
    return offset <= (uint64_t)INT64_MAX &&
           size <= (uint64_t)INT64_MAX - offset && offset <= file_size &&
           size <= file_size - offset;
}

static enum fwlab_nfc_api_result posix_read(
    void *opaque, uint64_t offset, void *buffer, size_t size)
{
    struct fnv1_posix_context *context = opaque;
    struct stat status;
    uint8_t *bytes = buffer;
    size_t completed = 0;

    if (buffer == NULL || !context_stat(context, &status) ||
        !span_valid(offset, size, (uint64_t)status.st_size)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    while (completed < size) {
        size_t chunk = size - completed;
        ssize_t count;

        if (chunk > (size_t)SSIZE_MAX) {
            chunk = (size_t)SSIZE_MAX;
        }
        count = pread(context->fd, &bytes[completed], chunk,
                      (off_t)(offset + completed));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
        completed += (size_t)count;
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result posix_write(
    void *opaque, uint64_t offset, const void *buffer, size_t size)
{
    struct fnv1_posix_context *context = opaque;
    struct stat status;
    const uint8_t *bytes = buffer;
    size_t completed = 0;

    if (buffer == NULL || !context_stat(context, &status) ||
        !span_valid(offset, size, (uint64_t)status.st_size)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    while (completed < size) {
        size_t chunk = size - completed;
        ssize_t count;

        if (chunk > (size_t)SSIZE_MAX) {
            chunk = (size_t)SSIZE_MAX;
        }
        count = pwrite(context->fd, &bytes[completed], chunk,
                       (off_t)(offset + completed));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
        completed += (size_t)count;
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result posix_sync(void *opaque)
{
    struct fnv1_posix_context *context = opaque;
    struct stat status;
    int result;

    if (!context_stat(context, &status)) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    do {
        result = fdatasync(context->fd);
    } while (result != 0 && errno == EINTR);
    return result == 0 ? FWLAB_NFC_API_OK : FWLAB_NFC_API_INVARIANT_FAILURE;
}

static enum fwlab_nfc_api_result posix_resize(void *opaque, uint64_t size)
{
    struct fnv1_posix_context *context = opaque;
    struct stat status;
    int result;

    if (!context_stat(context, &status) || !context->allow_resize ||
        size > (uint64_t)INT64_MAX) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    do {
        result = ftruncate(context->fd, (off_t)size);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    context->expected_size = size;
    return context_stat(context, &status) ? FWLAB_NFC_API_OK :
                                          FWLAB_NFC_API_INVARIANT_FAILURE;
}

static enum fwlab_nfc_api_result posix_size(void *opaque, uint64_t *size)
{
    struct fnv1_posix_context *context = opaque;
    struct stat status;

    if (size == NULL || !context_stat(context, &status)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *size = (uint64_t)status.st_size;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result posix_close(void *opaque)
{
    struct fnv1_posix_context *context = opaque;
    int fd;

    if (context == NULL || context->fd < 0) {
        return FWLAB_NFC_API_WRONG_STATE;
    }
    fd = context->fd;
    context->fd = -1;
    context->allow_resize = 0;
    /* Linux closes the descriptor even when close reports EINTR. Retrying
     * could close a reused descriptor. The OFD lock is released by close. */
    return close(fd) == 0 ? FWLAB_NFC_API_OK :
                            FWLAB_NFC_API_INVARIANT_FAILURE;
}

static int private_directory(int directory_fd)
{
    struct stat status;

    return directory_fd >= 0 && fstat(directory_fd, &status) == 0 &&
           S_ISDIR(status.st_mode) && status.st_uid == geteuid() &&
           (status.st_mode & 07777) == 0700;
}

static int safe_name(const char *name)
{
    size_t index;

    if (name == NULL || name[0] == '\0' ||
        (name[0] == '.' && (name[1] == '\0' ||
         (name[1] == '.' && name[2] == '\0')))) {
        return 0;
    }
    for (index = 0; name[index] != '\0'; ++index) {
        if (index >= 255 || name[index] == '/') {
            return 0;
        }
    }
    return 1;
}

static int exclusive_lock(int fd)
{
    struct flock lock;
    int result;

    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    do {
        result = fcntl(fd, F_OFD_SETLK, &lock);
    } while (result != 0 && errno == EINTR);
    /* No process-lock fallback: separate opens in this process must conflict. */
    return result == 0;
}

static enum fwlab_nfc_api_result open_media(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_v1_config *config,
    const struct fwlab_file_nand_holder_v1 *expected_holder,
    struct fwlab_file_nand_v1 **media_out,
    struct fwlab_file_nand_holder_v1 *new_holder, int format)
{
    struct fnv1_posix_context context;
    struct fnv1_posix_context *stored;
    struct fnv1_io io;
    struct fwlab_file_nand_v1 *media = NULL;
    struct stat status;
    enum fwlab_nfc_api_result result;
    uint64_t image_bytes;
    size_t alignment = fwlab_file_nand_v1_arena_alignment();
    int flags = O_RDWR | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK;
    int sync_result;

    if (media_out == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *media_out = NULL;
    if (new_holder != NULL) {
        memset(new_holder, 0, sizeof(*new_holder));
    }
    if (arena == NULL || alignment == 0 ||
        (uintptr_t)arena % alignment != 0 ||
        arena_size < fwlab_file_nand_v1_arena_size() || config == NULL ||
        !private_directory(directory_fd) || !safe_name(name) ||
        (format && new_holder == NULL) ||
        (!format && (expected_holder == NULL ||
         memcmp(config->media_uuid, expected_holder->media_uuid, 16) != 0))) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    image_bytes = fwlab_file_nand_v1_image_bytes(config);
    if (image_bytes == 0 || image_bytes > (uint64_t)INT64_MAX) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    if (format) {
        flags |= O_CREAT | O_EXCL;
    } else if (fstatat(directory_fd, name, &status,
                       AT_SYMLINK_NOFOLLOW) != 0 ||
               !private_file(&status) ||
               (uint64_t)status.st_dev != expected_holder->device ||
               (uint64_t)status.st_ino != expected_holder->inode ||
               (uint64_t)status.st_size != image_bytes) {
        /* Check type before opening; the fd identity check below binds the
         * opened object too. O_NONBLOCK avoids waiting on an exchanged FIFO. */
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(&context, 0, sizeof(context));
    context.fd = openat(directory_fd, name, flags, 0600);
    if (context.fd < 0) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    result = FWLAB_NFC_API_INVALID_CONTRACT;
    if (fstat(context.fd, &status) != 0 || !private_file(&status) ||
        (uint64_t)status.st_size != (format ? 0 : image_bytes) ||
        (!format && ((uint64_t)status.st_dev != expected_holder->device ||
                     (uint64_t)status.st_ino != expected_holder->inode)) ||
        !exclusive_lock(context.fd)) {
        goto failed;
    }
    context.device = (uint64_t)status.st_dev;
    context.inode = (uint64_t)status.st_ino;
    context.expected_size = (uint64_t)status.st_size;
    context.allow_resize = format;
    if (!context_stat(&context, &status)) {
        goto failed;
    }
    io.context = &context;
    io.read = posix_read;
    io.write = posix_write;
    io.sync = posix_sync;
    io.resize = posix_resize;
    io.size = posix_size;
    io.close = posix_close;
    result = fnv1_engine_open(arena, arena_size, config, &io, format, &media);
    if (result != FWLAB_NFC_API_OK) {
        goto failed;
    }
    if (media == NULL || !context_stat(&context, &status) ||
        context.expected_size != image_bytes) {
        result = FWLAB_NFC_API_INVARIANT_FAILURE;
        goto failed;
    }
    if (format) {
        do {
            sync_result = fsync(directory_fd);
        } while (sync_result != 0 && errno == EINTR);
        if (sync_result != 0) {
            result = FWLAB_NFC_API_INVARIANT_FAILURE;
            goto failed;
        }
    }
    context.allow_resize = 0;
    stored = (struct fnv1_posix_context *)media->io_storage;
    *stored = context;
    media->io.context = stored;
    if (new_holder != NULL) {
        new_holder->device = context.device;
        new_holder->inode = context.inode;
        memcpy(new_holder->media_uuid, config->media_uuid, 16);
    }
    *media_out = media;
    return FWLAB_NFC_API_OK;

failed:
    if (media != NULL) {
        media->quarantined = 1;
        media->closed = 1;
        media->io.context = NULL;
    }
    (void)posix_close(&context);
    /* Keep even a newly created partial image available for diagnosis. */
    return result;
}

enum fwlab_nfc_api_result fwlab_file_nand_v1_posix_format(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_v1_config *config,
    struct fwlab_file_nand_v1 **media,
    struct fwlab_file_nand_holder_v1 *holder)
{
    return open_media(arena, arena_size, directory_fd, name, config, NULL,
                      media, holder, 1);
}

enum fwlab_nfc_api_result fwlab_file_nand_v1_posix_restart(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_v1_config *config,
    const struct fwlab_file_nand_holder_v1 *holder,
    struct fwlab_file_nand_v1 **media)
{
    return open_media(arena, arena_size, directory_fd, name, config, holder,
                      media, NULL, 0);
}
