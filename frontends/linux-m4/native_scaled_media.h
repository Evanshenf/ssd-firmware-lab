/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_NATIVE_SCALED_MEDIA_H
#define FWLAB_NATIVE_SCALED_MEDIA_H

#include "native_internal.h"
#include "../headless-scale/scale_storage.h"
#include "physical_nand.h"

#define NATIVE_SCALED_LBA_COUNT UINT64_C(131072)

/* Offline construction prerequisite, not a kernel ATTACH binding. Initialize
 * this owner to zero before first use; do not move it while open. The native
 * context outlives this owner and its runtime must finish before media close.
 * Factory release owns FTL/NFC allocations, never this physical-media holder. */
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

/* Explicit new-file format or existing-file recovery, no fallback/conversion.
 * Fixed 64 MiB/8 KiB-consumer profile on the bounded mapped tmpfs backend. */
int native_scaled_media_open(struct native_scaled_media *media,
    struct native_context *owner, const char *directory,
    const uint8_t uuid[16], int format);
/* Refuses while the owner's runtime is live. On an unexpected lower close
 * failure retain the owner for diagnosis; never pretend it was released. */
int native_scaled_media_close(struct native_scaled_media *media);

#endif
