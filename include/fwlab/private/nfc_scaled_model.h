/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PRIVATE_NFC_SCALED_MODEL_H
#define FWLAB_PRIVATE_NFC_SCALED_MODEL_H

#include "fwlab/portable/nfc_model.h"

/* An explicit construction profile for the existing NFC runtime. The original
 * constructor and its frozen limits remain unchanged. No post-init mutation
 * of geometry, duplicate scheduler or full-capacity execution claim. */
enum fwlab_nfc_api_result fwlab_nfc_scaled_config_validate(
    const struct fwlab_nfc_model_config *config);
size_t fwlab_nfc_scaled_arena_size(
    const struct fwlab_nfc_model_config *config);
enum fwlab_nfc_api_result fwlab_nfc_scaled_init(
    void *arena, size_t arena_size,
    const struct fwlab_nfc_model_config *config, uint64_t instance_nonce,
    const struct fwlab_nfc_buffer_provider *buffers,
    const struct fwlab_nand_media *media, struct fwlab_nfc_model **model);

#endif
