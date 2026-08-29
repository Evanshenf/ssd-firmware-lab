/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PORTABLE_NFC_MODEL_H
#define FWLAB_PORTABLE_NFC_MODEL_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/contracts/nand_media.h"
#include "fwlab/contracts/nfc_provider.h"

struct fwlab_nfc_model;

enum fwlab_nfc_api_result fwlab_nfc_model_config_validate(
    const struct fwlab_nfc_model_config *config
);

size_t fwlab_nfc_model_arena_alignment(void);

size_t fwlab_nfc_model_arena_size(
    const struct fwlab_nfc_model_config *config
);

enum fwlab_nfc_api_result fwlab_nfc_model_init(
    void *arena,
    size_t arena_size,
    const struct fwlab_nfc_model_config *config,
    uint64_t instance_nonce,
    const struct fwlab_nfc_buffer_provider *buffers,
    const struct fwlab_nand_media *media,
    struct fwlab_nfc_model **model
);

struct fwlab_nfc_provider fwlab_nfc_model_provider(
    struct fwlab_nfc_model *model
);

enum fwlab_nfc_api_result fwlab_nfc_model_inject_cut(
    struct fwlab_nfc_model *model,
    enum fwlab_nfc_cut_kind cut
);

uint32_t fwlab_nfc_model_trace_count(const struct fwlab_nfc_model *model);

enum fwlab_nfc_api_result fwlab_nfc_model_trace_read(
    const struct fwlab_nfc_model *model,
    uint32_t ordinal,
    struct fwlab_nfc_trace_entry *entry
);

uint64_t fwlab_nfc_model_state_hash(const struct fwlab_nfc_model *model);
uint64_t fwlab_nfc_model_media_hash(const struct fwlab_nfc_model *model);

#endif
