/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C34_FILE_MEDIA_H
#define FWLAB_C34_FILE_MEDIA_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/contracts/nand_media.h"
#include "fwlab/private/c34_physical_txn.h"

#define C34_FILE_FORMAT_VERSION 1u
#define C34_FILE_IMAGE_BYTES UINT32_C(65536)

enum c34_file_result {
    C34_FILE_OK = 0,
    C34_FILE_INVALID = 1,
    C34_FILE_IO = 2,
    C34_FILE_CORRUPT = 3,
    C34_FILE_CUT = 4,
    C34_FILE_NO_CAPACITY = 5,
    C34_FILE_BUSY = 6
};

enum c34_file_io_result {
    C34_FILE_IO_OK = 0,
    C34_FILE_IO_FAILURE = 1,
    C34_FILE_IO_CUT = 2
};

typedef enum c34_file_io_result
(*c34_file_size_fn)(void *context, uint64_t *size);
typedef enum c34_file_io_result
(*c34_file_resize_fn)(void *context, uint64_t size);
typedef enum c34_file_io_result
(*c34_file_read_fn)(
    void *context,
    uint64_t offset,
    uint8_t *bytes,
    size_t length
);
typedef enum c34_file_io_result
(*c34_file_write_fn)(
    void *context,
    uint64_t offset,
    const uint8_t *bytes,
    size_t length
);
typedef enum c34_file_io_result
(*c34_file_barrier_fn)(void *context);

struct c34_file_substrate_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    c34_file_size_fn size_bytes;
    c34_file_resize_fn resize;
    c34_file_read_fn read;
    c34_file_write_fn write;
    c34_file_barrier_fn barrier;
};

struct c34_file_substrate {
    const struct c34_file_substrate_ops *ops;
    void *context;
};

struct c34_file_media;

size_t c34_file_media_arena_alignment(void);
size_t c34_file_media_arena_size(void);

enum c34_file_result c34_file_format(
    void *arena,
    size_t arena_size,
    const struct c34_file_substrate *substrate,
    const uint8_t image_uuid[16],
    struct c34_file_media **media
);

enum c34_file_result c34_file_restart(
    void *arena,
    size_t arena_size,
    const struct c34_file_substrate *substrate,
    const uint8_t expected_uuid[16],
    struct c34_file_media **media
);

enum c34_file_result c34_file_physical_checkpoint(
    struct c34_file_media *media
);

struct fwlab_nand_media c34_file_nand_media(
    struct c34_file_media *media
);
struct c34_physical_txn_provider c34_file_txn_provider(
    struct c34_file_media *media
);

uint64_t c34_file_image_hash(struct c34_file_media *media);
uint64_t c34_file_physical_hash(const struct c34_file_media *media);

enum c34_file_result c34_file_posix_format(
    void *arena,
    size_t arena_size,
    int fd,
    const uint8_t image_uuid[16],
    struct c34_file_media **media
);

enum c34_file_result c34_file_posix_restart(
    void *arena,
    size_t arena_size,
    int fd,
    const uint8_t expected_uuid[16],
    struct c34_file_media **media
);

#endif
