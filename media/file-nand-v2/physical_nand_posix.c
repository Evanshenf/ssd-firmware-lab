/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include "physical_nand_internal.h"
#include "physical_nand_batch.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/magic.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <unistd.h>

struct fnv2_posix_context {
    uint64_t magic;
    int fd;
    int allow_resize;
    uint64_t device;
    uint64_t inode;
    uint64_t expected_size;
    uint8_t operation_active;
    uint8_t exclusive_owned;
    uint8_t *mapping;
    size_t mapped_length;
};

#define FNV2_POSIX_MAGIC UINT64_C(0x464e5632504f5358)
#define FNV2_MAPPED_MAX_BYTES (UINT64_C(600) * 1024u * 1024u)

_Static_assert(sizeof(struct fnv2_posix_context) <= 128,
               "POSIX context fits persistent IO storage");
_Static_assert(sizeof(off_t) >= sizeof(int64_t),
               "compact NAND requires 64-bit file offsets");

static int private_file(const struct stat *status)
{
    return S_ISREG(status->st_mode) && status->st_uid == geteuid() &&
           (status->st_mode & 07777) == 0600 && status->st_nlink == 1 &&
           status->st_size >= 0;
}

static int context_stat(const struct fnv2_posix_context *context,
                        struct stat *status)
{
    return context != NULL && context->fd >= 0 && status != NULL &&
           fstat(context->fd, status) == 0 && private_file(status) &&
           (uint64_t)status->st_dev == context->device &&
           (uint64_t)status->st_ino == context->inode &&
           (uint64_t)status->st_size == context->expected_size;
}

static int callback_valid(const struct fnv2_posix_context *context)
{
    struct stat status;
    return context != NULL && context->fd >= 0 &&
        (context->operation_active || context_stat(context, &status));
}

static int span_valid(uint64_t offset, size_t size, uint64_t file_size)
{
    return offset <= (uint64_t)INT64_MAX &&
           size <= (uint64_t)INT64_MAX - offset && offset <= file_size &&
           size <= file_size - offset;
}

static int mapped_size_valid(uint64_t size)
{
    return size != 0 && size <= FNV2_MAPPED_MAX_BYTES &&
           size <= (uint64_t)SIZE_MAX && size <= (uint64_t)PTRDIFF_MAX &&
           size <= (uint64_t)INT64_MAX;
}

static int mapped_file(int fd)
{
    struct statfs status;

    return fstatfs(fd, &status) == 0 && status.f_type == TMPFS_MAGIC;
}

static enum fwlab_nfc_api_result mapped_prepare(
    struct fnv2_posix_context *context)
{
    struct stat status;
    void *mapping;
    int result;

    if (!context_stat(context, &status) || context->allow_resize ||
        context->mapped_length != 0 ||
        !mapped_size_valid(context->expected_size)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    do {
        result = fallocate(context->fd, FALLOC_FL_KEEP_SIZE, 0,
                           (off_t)context->expected_size);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    mapping = mmap(NULL, (size_t)context->expected_size,
                   PROT_READ | PROT_WRITE, MAP_SHARED, context->fd, 0);
    if (mapping == MAP_FAILED) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    /* Own the mapping before any later preparation can fail. The caller's
     * shared close path unmaps even a partially populated admission. */
    context->mapping = mapping;
    context->mapped_length = (size_t)context->expected_size;
    if (mapping == NULL) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    do {
        result = madvise(mapping, context->mapped_length, MADV_POPULATE_WRITE);
    } while (result != 0 && errno == EINTR);
    return result == 0 && context_stat(context, &status) ? FWLAB_NFC_API_OK :
                                          FWLAB_NFC_API_INVARIANT_FAILURE;
}

static enum fwlab_nfc_api_result mapped_read(
    void *opaque, uint64_t offset, void *buffer, size_t size)
{
    struct fnv2_posix_context *context = opaque;

    if (buffer == NULL || !callback_valid(context) ||
        context->allow_resize || context->mapped_length == 0 ||
        context->expected_size != context->mapped_length ||
        offset > context->mapped_length ||
        size > context->mapped_length - offset) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    if (size != 0) {
        memcpy(buffer, context->mapping + (size_t)offset, size);
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result mapped_write(
    void *opaque, uint64_t offset, const void *buffer, size_t size)
{
    struct fnv2_posix_context *context = opaque;

    if (buffer == NULL || !callback_valid(context) ||
        context->allow_resize || context->mapped_length == 0 ||
        context->expected_size != context->mapped_length ||
        offset > context->mapped_length ||
        size > context->mapped_length - offset) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    if (size != 0) {
        memcpy(context->mapping + (size_t)offset, buffer, size);
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result posix_read(
    void *opaque, uint64_t offset, void *buffer, size_t size)
{
    struct fnv2_posix_context *context = opaque;
    uint8_t *bytes = buffer;
    size_t completed = 0;

    if (buffer == NULL || !callback_valid(context) ||
        !span_valid(offset, size, context->expected_size)) {
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
    struct fnv2_posix_context *context = opaque;
    const uint8_t *bytes = buffer;
    size_t completed = 0;

    if (buffer == NULL || !callback_valid(context) ||
        !span_valid(offset, size, context->expected_size)) {
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
    struct fnv2_posix_context *context = opaque;
    int result;

    if (!callback_valid(context)) {
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    do {
        result = fdatasync(context->fd);
    } while (result != 0 && errno == EINTR);
    return result == 0 ? FWLAB_NFC_API_OK : FWLAB_NFC_API_INVARIANT_FAILURE;
}

static enum fwlab_nfc_api_result posix_resize(void *opaque, uint64_t size)
{
    struct fnv2_posix_context *context = opaque;
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
    struct fnv2_posix_context *context = opaque;
    struct stat status;

    if (size == NULL || !context_stat(context, &status)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    *size = (uint64_t)status.st_size;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result posix_close(void *opaque)
{
    struct fnv2_posix_context *context = opaque;
    int fd, properties_ok = 1, close_result;

    if (context == NULL || context->fd < 0 || context->operation_active) {
        return FWLAB_NFC_API_WRONG_STATE;
    }
    if (context->exclusive_owned) {
        struct stat status;
        /* Audit the admitted ownership epoch, but never leak its mapping or
         * OFD on a late property error. This does not revoke prior results. */
        properties_ok = context_stat(context, &status);
    }
    if (context->mapped_length != 0) {
        if (munmap(context->mapping, context->mapped_length) != 0) {
            static const char message[] =
                "file-NAND v2 mapped backend: munmap failed; terminating\n";
            ssize_t written = write(STDERR_FILENO, message, sizeof(message) - 1u);

            (void)written;
            /* Generic media close consumes its instance even on error.
             * Never return with a live mapping and retained OFD behind it. */
            _exit(1);
        }
        context->mapping = NULL;
        context->mapped_length = 0;
    }
    fd = context->fd;
    context->fd = -1;
    context->allow_resize = 0;
    context->exclusive_owned = 0;
    /* Linux closes the descriptor even when close reports EINTR. Retrying
     * could close a reused descriptor. The OFD lock is released by close. */
    close_result = close(fd);
    return close_result == 0 && properties_ok ? FWLAB_NFC_API_OK :
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
    const struct fwlab_file_nand_v2_config *config,
    const struct fwlab_file_nand_holder_v2 *expected_holder,
    struct fwlab_file_nand_v2 **media_out,
    struct fwlab_file_nand_holder_v2 *new_holder, int format, int mapped)
{
    struct fnv2_posix_context context;
    struct fnv2_posix_context *stored;
    struct fnv2_io io;
    struct fwlab_file_nand_v2 *media = NULL;
    struct stat status;
    enum fwlab_nfc_api_result result;
    uint64_t image_bytes;
    size_t alignment = fwlab_file_nand_v2_arena_alignment();
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
        arena_size < fwlab_file_nand_v2_arena_size() || config == NULL ||
        !private_directory(directory_fd) || !safe_name(name) ||
        (format && new_holder == NULL) ||
        (!format && (expected_holder == NULL ||
         memcmp(config->media_uuid, expected_holder->media_uuid, 16) != 0))) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    image_bytes = fwlab_file_nand_v2_image_bytes(config);
    if (image_bytes == 0 || image_bytes > (uint64_t)INT64_MAX ||
        (mapped && !mapped_size_valid(image_bytes))) {
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
    context.magic = FNV2_POSIX_MAGIC;
    context.fd = openat(directory_fd, name, flags, 0600);
    if (context.fd < 0) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    result = FWLAB_NFC_API_INVALID_CONTRACT;
    if (fstat(context.fd, &status) != 0 || !private_file(&status) ||
        (uint64_t)status.st_size != (format ? 0 : image_bytes) ||
        (!format && ((uint64_t)status.st_dev != expected_holder->device ||
                     (uint64_t)status.st_ino != expected_holder->inode)) ||
        !exclusive_lock(context.fd) || (mapped && !mapped_file(context.fd))) {
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
    result = fnv2_engine_open(arena, arena_size, config, &io, format, &media);
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
    if (mapped) {
        result = mapped_prepare(&context);
        if (result != FWLAB_NFC_API_OK) {
            goto failed;
        }
        media->io.read = mapped_read;
        media->io.write = mapped_write;
        media->page_copy_crc = 1;
        context.exclusive_owned = FWLAB_MEDIA_EXCLUSIVE;
    }
    stored = (struct fnv2_posix_context *)media->io_storage;
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

enum fwlab_nfc_api_result fwlab_file_nand_v2_posix_format(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_v2_config *config,
    struct fwlab_file_nand_v2 **media,
    struct fwlab_file_nand_holder_v2 *holder)
{
    return open_media(arena, arena_size, directory_fd, name, config, NULL,
                      media, holder, 1, 0);
}

enum fwlab_nfc_api_result fwlab_file_nand_v2_posix_restart(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_v2_config *config,
    const struct fwlab_file_nand_holder_v2 *holder,
    struct fwlab_file_nand_v2 **media)
{
    return open_media(arena, arena_size, directory_fd, name, config, holder,
                      media, NULL, 0, 0);
}

enum fwlab_nfc_api_result fwlab_file_nand_v2_posix_mapped_format(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_v2_config *config,
    struct fwlab_file_nand_v2 **media,
    struct fwlab_file_nand_holder_v2 *holder)
{
    return open_media(arena, arena_size, directory_fd, name, config, NULL,
                      media, holder, 1, 1);
}

enum fwlab_nfc_api_result fwlab_file_nand_v2_posix_mapped_restart(
    void *arena, size_t arena_size, int directory_fd, const char *name,
    const struct fwlab_file_nand_v2_config *config,
    const struct fwlab_file_nand_holder_v2 *holder,
    struct fwlab_file_nand_v2 **media)
{
    return open_media(arena, arena_size, directory_fd, name, config, holder,
                      media, NULL, 0, 1);
}

/* These concrete POSIX wrappers alone mint/retire the short reuse scope.
 * Generic NAND entrypoints and fnv2_io retain their existing contracts. */
static struct fnv2_posix_context *operation_context(struct fwlab_file_nand_v2 *m)
{
    struct fnv2_posix_context *c;
    if (!m) return NULL;
    c = (struct fnv2_posix_context *)m->io_storage;
    return c->magic == FNV2_POSIX_MAGIC && c->fd >= 0 && !c->allow_resize ? c : NULL;
}

static int operation_file_valid(const struct fnv2_posix_context *c)
{
    struct stat status;

    if (c != NULL && c->exclusive_owned) {
        /* Admission/close own file-property queries in the opt-in build.
         * Every operation still requires its live, fixed mapped extent. */
        return c->magic == FNV2_POSIX_MAGIC && c->fd >= 0 && !c->allow_resize &&
            c->mapping != NULL && c->mapped_length != 0 &&
            c->expected_size == c->mapped_length;
    }
    return context_stat(c, &status);
}

static enum fwlab_nfc_api_result operation_begin(struct fwlab_file_nand_v2 *m,
                                                  struct fwlab_nand_media *base)
{
    struct fnv2_posix_context *c;
    *base = fwlab_file_nand_v2_media(m);
    if (!base->ops) return FWLAB_NFC_API_WRONG_STATE;
    c = operation_context(m);
    if (!c) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (c->operation_active) return FWLAB_NFC_API_WRONG_STATE;
    if (!operation_file_valid(c)) {
        m->quarantined = 1;
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    c->operation_active = 1;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result operation_end(struct fwlab_file_nand_v2 *m,
                                                enum fwlab_nfc_api_result result)
{
    struct fnv2_posix_context *c = (struct fnv2_posix_context *)m->io_storage;
    /* The synchronous wrapper owns this flag even after engine quarantine. */
    c->operation_active = 0;
    if (c->magic != FNV2_POSIX_MAGIC || c->allow_resize || !operation_file_valid(c)) {
        m->quarantined = 1;
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    return result;
}

static enum fwlab_nfc_api_result operation_read_pages(void *opaque,
    const struct fwlab_nfc_ppa *first, uint32_t count,
    uint8_t *main, size_t main_bytes, uint8_t *oob, size_t oob_bytes,
    struct fwlab_nand_page_info *pages, size_t capacity, struct fwlab_nand_block_info *block)
{
    struct fwlab_file_nand_v2 *m = opaque; struct fwlab_nand_media base;
    enum fwlab_nfc_api_result r = operation_begin(m, &base);
    if (r != FWLAB_NFC_API_OK) return r;
    r = fwlab_file_nand_v2_read_pages(m, first, count, main, main_bytes,
                                     oob, oob_bytes, pages, capacity, block);
    return operation_end(m, r);
}
static enum fwlab_nfc_api_result operation_program_pages(void *opaque,
    const struct fwlab_nfc_ppa *first, uint32_t count,
    const uint8_t *main, size_t main_bytes, const uint8_t *oob, size_t oob_bytes,
    struct fwlab_nand_media_result *results, size_t capacity)
{
    struct fwlab_file_nand_v2 *m = opaque; struct fwlab_nand_media base;
    enum fwlab_nfc_api_result r = operation_begin(m, &base);
    if (r != FWLAB_NFC_API_OK) return r;
    r = fwlab_file_nand_v2_program_pages(m, first, count, main, main_bytes,
                                        oob, oob_bytes, results, capacity);
    return operation_end(m, r);
}
static enum fwlab_nfc_api_result operation_read(void *opaque,
    const struct fwlab_nfc_ppa *ppa, uint8_t *main, uint32_t main_bytes,
    uint8_t *oob, uint32_t oob_bytes, struct fwlab_nand_page_info *page,
    struct fwlab_nand_block_info *block)
{
    struct fwlab_file_nand_v2 *m = opaque; struct fwlab_nand_media base;
    enum fwlab_nfc_api_result r = operation_begin(m, &base);
    if (r != FWLAB_NFC_API_OK) return r;
    r = base.ops->read_page(m, ppa, main, main_bytes, oob, oob_bytes, page, block);
    return operation_end(m, r);
}
static enum fwlab_nfc_api_result operation_program(void *opaque,
    const struct fwlab_nfc_ppa *ppa, const uint8_t *main, uint32_t main_bytes,
    const uint8_t *oob, uint32_t oob_bytes, uint32_t applied_main, uint32_t applied_oob,
    uint8_t integrity, struct fwlab_nand_media_result *out)
{
    struct fwlab_file_nand_v2 *m = opaque; struct fwlab_nand_media base;
    enum fwlab_nfc_api_result r = operation_begin(m, &base);
    if (r != FWLAB_NFC_API_OK) return r;
    r = base.ops->program(m, ppa, main, main_bytes, oob, oob_bytes,
                          applied_main, applied_oob, integrity, out);
    return operation_end(m, r);
}
static enum fwlab_nfc_api_result operation_erase(void *opaque,
    const struct fwlab_nfc_ppa *ppa, uint32_t applied, uint8_t integrity,
    struct fwlab_nand_media_result *out)
{
    struct fwlab_file_nand_v2 *m = opaque; struct fwlab_nand_media base;
    enum fwlab_nfc_api_result r = operation_begin(m, &base);
    if (r != FWLAB_NFC_API_OK) return r;
    r = base.ops->erase(m, ppa, applied, integrity, out);
    return operation_end(m, r);
}
static enum fwlab_nfc_api_result operation_bad(void *opaque, const struct fwlab_nfc_ppa *ppa)
{
    struct fwlab_file_nand_v2 *m = opaque; struct fwlab_nand_media base;
    enum fwlab_nfc_api_result r = operation_begin(m, &base);
    if (r != FWLAB_NFC_API_OK) return r;
    r = base.ops->mark_runtime_bad(m, ppa);
    return operation_end(m, r);
}
static uint64_t strict_hash(void *opaque)
{
    struct fwlab_file_nand_v2 *m = opaque;
    struct fwlab_nand_media base = fwlab_file_nand_v2_media(m);
    struct fnv2_posix_context *c = operation_context(m);
    return base.ops && c && !c->operation_active ? base.ops->hash(m) : 0;
}
static const struct fwlab_nand_media_ops operation_scalar_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(operation_scalar_ops),
    .read_page = operation_read, .program = operation_program,
    .erase = operation_erase, .mark_runtime_bad = operation_bad, .hash = strict_hash
};
static const struct fwlab_nand_batch_v2_ops operation_batch_ops = {
    .version = FWLAB_NAND_BATCH_V2_VERSION, .size = sizeof(operation_batch_ops),
    .read_pages = operation_read_pages, .program_pages = operation_program_pages
};
struct fwlab_nand_batch_v2 fwlab_file_nand_v2_posix_operation_batch(struct fwlab_file_nand_v2 *m)
{
    struct fwlab_nand_batch_v2 out = fwlab_file_nand_v2_batch(m);
    struct fnv2_posix_context *c = out.ops ? operation_context(m) : NULL;
    if (!c || c->operation_active) {
        memset(&out, 0, sizeof(out));
        return out;
    }
    out.ops = &operation_batch_ops;
    out.scalar.ops = &operation_scalar_ops;
    return out;
}
