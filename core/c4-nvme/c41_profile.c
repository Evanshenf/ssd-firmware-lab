/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fwlab/portable/nvme_codec.h"

void fwlab_nvme_profile_fixed(struct fwlab_nvme_profile *profile)
{
    struct fwlab_nvme_profile local = {0};

    if (profile == NULL) {
        return;
    }
    local.version = FWLAB_NVME_PROFILE_VERSION;
    local.size = sizeof(local);
    local.namespace_count = 1;
    local.lba_bytes = 512;
    local.lba_count = 8;
    local.memory_page_bytes = 4096;
    local.maximum_transfer_bytes = 4096;
    local.maximum_io_queue_pairs = 1;
    local.integration_queue_depth = 4;
    local.queue_depth_hard_maximum = 32;
    local.data_segments_hard_maximum = 2;
    local.feature_flags = FWLAB_NVME_PROFILE_READ |
                          FWLAB_NVME_PROFILE_WRITE |
                          FWLAB_NVME_PROFILE_FLUSH |
                          FWLAB_NVME_PROFILE_FUA |
                          FWLAB_NVME_PROFILE_VOLATILE_WRITE_CACHE |
                          FWLAB_NVME_PROFILE_PRP_DIRECT;
    *profile = local;
}
