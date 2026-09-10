/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_NATIVE_SCALED_MEDIA_H
#define FWLAB_NATIVE_SCALED_MEDIA_H

#include "native_internal.h"
#include "../headless-scale/scale_storage.h"
#include "physical_nand.h"

#define NATIVE_SCALED_DEFAULT_MIB 64u

/* Initialize this process-lived owner to zero before first use; do not move it
 * while open. Its context outlives it. Keep it across reset and NO_OWNER even
 * when runtime is NULL: owner grant will reuse it. Final close requires the
 * owner server stopped and runtime finished. Factory release owns FTL/NFC
 * allocations, never this holder. This type is not a kernel readiness proof. */
struct native_scaled_media {
    struct native_media native;
    struct native_context *owner;
    struct fwlab_file_nand_v2 *physical;
    struct fwlab_file_nand_holder_v2 holder;
    struct fwlab_file_nand_v2_config config;
    struct fwlab_nand_batch_v2 batch;
    struct j0_media_binding binding;
    struct j0_storage_factory factory;
    struct scale_storage_options options;
    uint8_t opened;
};

/* Explicit new-file format or matching-capacity recovery, no resize,
 * fallback/conversion. Select 64, 256 or 65536 MiB using the shared storage
 * geometry presets; this does not change Host transfer or queue limits. */
int native_scaled_media_open(struct native_scaled_media *media,
    struct native_context *owner, const char *directory,
    const uint8_t uuid[16], int format, uint32_t logical_mib);
/* Refuses while the owner's runtime is live. On an unexpected lower close
 * failure retain the owner for diagnosis; never pretend it was released. */
int native_scaled_media_close(struct native_scaled_media *media);

#endif
