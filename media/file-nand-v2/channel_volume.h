/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_NAND_CHANNEL_VOLUME_H
#define FWLAB_NAND_CHANNEL_VOLUME_H

#include <stddef.h>
#include <stdint.h>
#include "fwlab/private/nand_channel_v2.h"

#define FWLAB_NAND_CHANNEL_VOLUME_VERSION 1u
#define FWLAB_NAND_CHANNEL_VOLUME_MANIFEST "volume.nand"
#define FWLAB_NAND_CHANNEL_VOLUME_LOCK "volume.lock"
#define FWLAB_NAND_CHANNEL_VOLUME_PENDING "volume.pending"

struct fwlab_nand_channel_volume_config {
    uint16_t version, size;
    uint32_t reserved;
    struct fwlab_nfc_geometry geometry;
    uint8_t media_uuid[16];
    uint8_t child_uuid[FWLAB_NAND_CHANNEL_V2_MAX][16];
};
struct fwlab_nand_channel_volume;

size_t fwlab_nand_channel_volume_arena_size(void);
size_t fwlab_nand_channel_volume_arena_alignment(void);
/* Fixed relative names channel-0.nand ... channel-3.nand; NULL out of range. */
const char *fwlab_nand_channel_volume_shard_name(uint32_t channel);

/* Caller supplies a new empty, caller-owned private directory. Files are
 * new-only; the immutable LE/CRC manifest is published last without replacing
 * any existing name. Failure closes acquired handles but preserves all partial
 * files, including volume.pending. Physical assembly is not an FTL format.
 * The arena must be fresh/unowned, suitably aligned and live until close. */
enum fwlab_nfc_api_result fwlab_nand_channel_volume_posix_format(
    void *arena, size_t arena_bytes, int directory_fd,
    const struct fwlab_nand_channel_volume_config *config,
    struct fwlab_nand_channel_volume **volume);

/* No create, resize, fallback or guessed geometry. The manifest supplies exact
 * geometry/child UUIDs; expected global UUID must match. Runtime holders come
 * from fstat and are rechecked by each actual physical-v2 open. */
enum fwlab_nfc_api_result fwlab_nand_channel_volume_posix_restart(
    void *arena, size_t arena_bytes, int directory_fd,
    const uint8_t expected_media_uuid[16],
    struct fwlab_nand_channel_volume **volume);

struct fwlab_nand_channel_v2 fwlab_nand_channel_volume_binding(
    struct fwlab_nand_channel_volume *volume);
/* Caller first drains every child actor/callback/message/frame/retirement ACK.
 * aggregate access is serialized/quiescent only. Child and aggregate bindings
 * become invalid on close. This function does not unlink any file. */
enum fwlab_nfc_api_result fwlab_nand_channel_volume_close(
    struct fwlab_nand_channel_volume *volume);

#endif
