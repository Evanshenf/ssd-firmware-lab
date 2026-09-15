/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_NATIVE_SCALED_MEDIA_H
#define FWLAB_NATIVE_SCALED_MEDIA_H

#include "native_internal.h"
#include "../headless-scale/scale_storage.h"
#include "../headless-scale/nfc_channel_workers.h"
#include "physical_nand.h"
#include "channel_volume.h"

#define NATIVE_SCALED_DEFAULT_MIB 64u

enum native_nand_profile {
    NATIVE_NAND_R0 = 0,
    NATIVE_NAND_CHANNEL_LAB4K = 1
};

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
    enum native_nand_profile profile;
    struct fwlab_nand_channel_volume *volume;
    struct fwlab_nand_channel_v2 channels;
    struct fwlab_nfc_page_v2_lab_mutation_config timing;
    /* Configuration is process-lived. The transport and its descriptor belong
     * to one retained native runtime association, including failed startup. */
    struct fwlab_nfc_channel_workers_config worker_config;
    struct fwlab_nfc_channel_workers *workers;
    struct fwlab_nfc_channel_executor executor;
    uint8_t opened;
};

/* Explicit new-file format or matching-capacity recovery, no resize,
 * fallback/conversion. Select 64, 256 or 65536 MiB using the shared storage
 * geometry presets; this does not change Host transfer or queue limits. */
int native_scaled_media_open(struct native_scaled_media *media,
    struct native_context *owner, const char *directory,
    const uint8_t uuid[16], int format, uint32_t logical_mib);
/* Explicit construction only. R0 is the unchanged default above. CHANNEL_LAB4K
 * selects the 64/256/65536-MiB, four-channel strict POSIX assembly and mutable
 * format3/IPR cooperative runtime. No implicit threads, mmap or format conversion.
 * Fresh format needs an empty private directory; recovery uses its manifest
 * identities and checks the selected geometry before any runtime is created. */
int native_scaled_media_open_profile(struct native_scaled_media *media,
    struct native_context *owner, const char *directory,
    const uint8_t uuid[16], int format, uint32_t logical_mib,
    enum native_nand_profile profile);
/* Opt in while no runtime/association is active. No thread starts here. Only
 * opened channel LAB4K media accepts 1 or 4 workers; configuration cannot
 * be replaced.
 * Every runtime prepare gets fresh workers, without changing media or timing. */
int native_scaled_media_enable_workers(struct native_scaled_media *media,
                                      uint32_t workers);
/* Refuses while the owner's runtime or resource association is live.
 * On an unexpected lower close
 * failure retain the owner for diagnosis; never pretend it was released. */
int native_scaled_media_close(struct native_scaled_media *media);

#endif
