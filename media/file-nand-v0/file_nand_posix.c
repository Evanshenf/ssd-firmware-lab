/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _GNU_SOURCE

#include "file_nand_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct file_nand_posix_context {
    int fd;
    int locked;
    uint64_t device;
    uint64_t inode;
};

_Static_assert(sizeof(struct file_nand_posix_context) <= 128,
               "POSIX binding fits opaque substrate storage");

static enum fwlab_nfc_api_result posix_size(void *opaque, uint64_t *size)
{
    struct file_nand_posix_context *context = opaque;
    struct stat status;

    if (context == NULL || size == NULL || context->fd < 0 ||
        fstat(context->fd, &status) != 0 || status.st_size < 0) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *size = (uint64_t)status.st_size;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result posix_resize(void *opaque, uint64_t size)
{
    struct file_nand_posix_context *context = opaque;

    if (context == NULL || context->fd < 0 || size > (uint64_t)INT64_MAX ||
        ftruncate(context->fd, (off_t)size) != 0) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result posix_read(void *opaque, uint64_t offset,
                                             void *buffer, size_t size)
{
    struct file_nand_posix_context *context = opaque;
    uint8_t *bytes = buffer;
    size_t completed = 0;

    if (context == NULL || context->fd < 0 || buffer == NULL ||
        offset > (uint64_t)INT64_MAX || size > (uint64_t)INT64_MAX - offset) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    while (completed < size) {
        ssize_t count = pread(context->fd, &bytes[completed], size - completed,
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

static enum fwlab_nfc_api_result posix_write(void *opaque, uint64_t offset,
                                              const void *buffer, size_t size)
{
    struct file_nand_posix_context *context = opaque;
    const uint8_t *bytes = buffer;
    size_t completed = 0;

    if (context == NULL || context->fd < 0 || buffer == NULL ||
        offset > (uint64_t)INT64_MAX || size > (uint64_t)INT64_MAX - offset) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    while (completed < size) {
        ssize_t count = pwrite(context->fd, &bytes[completed], size - completed,
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

static enum fwlab_nfc_api_result posix_barrier(void *opaque)
{
    struct file_nand_posix_context *context = opaque;

    return context != NULL && context->fd >= 0 && fdatasync(context->fd) == 0 ?
        FWLAB_NFC_API_OK : FWLAB_NFC_API_INVARIANT_FAILURE;
}

static enum fwlab_nfc_api_result posix_identity(
    void *opaque, struct file_nand_identity *identity)
{
    struct file_nand_posix_context *context = opaque;
    struct stat status;

    if (context == NULL || identity == NULL || context->fd < 0 ||
        fstat(context->fd, &status) != 0 || !S_ISREG(status.st_mode) ||
        (uint64_t)status.st_dev != context->device ||
        (uint64_t)status.st_ino != context->inode) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    identity->device = (uint64_t)status.st_dev;
    identity->inode = (uint64_t)status.st_ino;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result posix_close(void *opaque)
{
    struct file_nand_posix_context *context = opaque;
    struct flock lock;
    int failed = 0;

    if (context == NULL || context->fd < 0) {
        return FWLAB_NFC_API_WRONG_STATE;
    }
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    if (context->locked && fcntl(context->fd, F_SETLK, &lock) != 0) {
        failed = 1;
    }
    if (close(context->fd) != 0) {
        failed = 1;
    }
    context->fd = -1;
    context->locked = 0;
    return failed ? FWLAB_NFC_API_INVARIANT_FAILURE : FWLAB_NFC_API_OK;
}

static const struct file_nand_substrate_ops posix_ops = {
    .version = FWLAB_FILE_NAND_V0_VERSION,
    .size = sizeof(struct file_nand_substrate_ops),
    .reserved = 0,
    .size_get = posix_size,
    .resize = posix_resize,
    .read = posix_read,
    .write = posix_write,
    .barrier = posix_barrier,
    .identity = posix_identity,
    .close = posix_close,
};

static int safe_name(const char *name)
{
    size_t index;

    if (name == NULL || name[0] == '\0' ||
        (name[0] == '.' && (name[1] == '\0' ||
         (name[1] == '.' && name[2] == '\0')))) {
        return 0;
    }
    for (index = 0; name[index] != '\0'; ++index) {
        if (name[index] == '/' || index >= 255) {
            return 0;
        }
    }
    return 1;
}

static int private_directory_valid(int directory_fd)
{
    struct stat status;

    return directory_fd >= 0 && fstat(directory_fd, &status) == 0 &&
           S_ISDIR(status.st_mode) && status.st_uid == geteuid() &&
           (status.st_mode & 07777) == 0700;
}

static int media_file_valid(int fd, uint64_t expected_size,
                            const struct fwlab_file_nand_holder_v0 *holder,
                            struct stat *status)
{
    return fd >= 0 && status != NULL && fstat(fd, status) == 0 &&
           S_ISREG(status->st_mode) && status->st_uid == geteuid() &&
           (status->st_mode & 07777) == 0600 && status->st_nlink == 1 &&
           status->st_size >= 0 && (uint64_t)status->st_size == expected_size &&
           (holder == NULL ||
            ((uint64_t)status->st_dev == holder->device &&
             (uint64_t)status->st_ino == holder->inode));
}

static int lock_file(int fd)
{
    struct flock lock;

    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    return fcntl(fd, F_SETLK, &lock) == 0;
}

static void bind_persistent_context(
    struct fwlab_file_nand_v0 *media,
    const struct file_nand_posix_context *temporary)
{
    struct file_nand_posix_context *stored =
        (struct file_nand_posix_context *)media->substrate_storage;

    *stored = *temporary;
    media->substrate.ops = &posix_ops;
    media->substrate.context = stored;
}

enum fwlab_nfc_api_result fwlab_file_nand_v0_posix_format(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const uint8_t media_uuid[16], struct fwlab_file_nand_v0 **media_out,
    struct fwlab_file_nand_holder_v0 *holder)
{
    struct file_nand_posix_context context;
    struct file_nand_substrate substrate;
    struct fwlab_file_nand_v0 *media;
    struct stat status;
    enum fwlab_nfc_api_result result;
    int fd;

    if (!private_directory_valid(directory_fd) || !safe_name(name) ||
        media_uuid == NULL || media_out == NULL || holder == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *media_out = NULL;
    memset(holder, 0, sizeof(*holder));
    fd = openat(directory_fd, name,
                O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0 || !media_file_valid(fd, 0, NULL, &status) || !lock_file(fd)) {
        if (fd >= 0) {
            (void)close(fd);
            (void)unlinkat(directory_fd, name, 0);
        }
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(&context, 0, sizeof(context));
    context.fd = fd;
    context.locked = 1;
    context.device = (uint64_t)status.st_dev;
    context.inode = (uint64_t)status.st_ino;
    substrate.ops = &posix_ops;
    substrate.context = &context;
    result = file_nand_engine_format(arena, arena_size, &substrate,
                                     media_uuid, &media);
    if (result != FWLAB_NFC_API_OK || fsync(directory_fd) != 0) {
        (void)posix_close(&context);
        (void)unlinkat(directory_fd, name, 0);
        return result == FWLAB_NFC_API_OK ?
                   FWLAB_NFC_API_INVARIANT_FAILURE : result;
    }
    bind_persistent_context(media, &context);
    holder->device = context.device;
    holder->inode = context.inode;
    memcpy(holder->media_uuid, media_uuid, 16);
    *media_out = media;
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_file_nand_v0_posix_restart(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_holder_v0 *holder,
    struct fwlab_file_nand_v0 **media_out)
{
    struct file_nand_posix_context context;
    struct file_nand_substrate substrate;
    struct fwlab_file_nand_v0 *media;
    struct stat status;
    enum fwlab_nfc_api_result result;
    int fd;

    if (!private_directory_valid(directory_fd) || !safe_name(name) ||
        holder == NULL || holder->device == 0 || holder->inode == 0 ||
        file_nand_bytes_zero(holder->media_uuid, 16) || media_out == NULL) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *media_out = NULL;
    fd = openat(directory_fd, name, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0 || !media_file_valid(fd, FWLAB_FILE_NAND_V0_IMAGE_BYTES,
                                    holder, &status) || !lock_file(fd)) {
        if (fd >= 0) {
            (void)close(fd);
        }
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(&context, 0, sizeof(context));
    context.fd = fd;
    context.locked = 1;
    context.device = (uint64_t)status.st_dev;
    context.inode = (uint64_t)status.st_ino;
    substrate.ops = &posix_ops;
    substrate.context = &context;
    result = file_nand_engine_restart(arena, arena_size, &substrate, holder,
                                      &media);
    if (result != FWLAB_NFC_API_OK) {
        (void)posix_close(&context);
        return result;
    }
    bind_persistent_context(media, &context);
    *media_out = media;
    return FWLAB_NFC_API_OK;
}
