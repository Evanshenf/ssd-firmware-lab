/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PORTABLE_NVME_CODEC_H
#define FWLAB_PORTABLE_NVME_CODEC_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/portable/nvme_types.h"

enum fwlab_nvme_codec_result {
    FWLAB_NVME_CODEC_OK = 0,
    FWLAB_NVME_CODEC_INVALID_ARGUMENT = 1,
    FWLAB_NVME_CODEC_INVALID_VALUE = 2,
    FWLAB_NVME_CODEC_UNSUPPORTED_VERSION = 3,
    FWLAB_NVME_CODEC_BAD_ENCODING = 4
};

int fwlab_nvme_command_valid(const struct fwlab_nvme_command *command);
int fwlab_nvme_completion_valid(
    const struct fwlab_nvme_completion_intent *completion
);
int fwlab_nvme_profile_valid(const struct fwlab_nvme_profile *profile);

enum fwlab_nvme_codec_result fwlab_nvme_command_encode(
    const struct fwlab_nvme_command *command,
    uint8_t *output,
    size_t output_size
);
enum fwlab_nvme_codec_result fwlab_nvme_command_decode(
    const uint8_t *input,
    size_t input_size,
    struct fwlab_nvme_command *command
);
enum fwlab_nvme_codec_result fwlab_nvme_completion_encode(
    const struct fwlab_nvme_completion_intent *completion,
    uint8_t *output,
    size_t output_size
);
enum fwlab_nvme_codec_result fwlab_nvme_completion_decode(
    const uint8_t *input,
    size_t input_size,
    struct fwlab_nvme_completion_intent *completion
);
enum fwlab_nvme_codec_result fwlab_nvme_profile_encode(
    const struct fwlab_nvme_profile *profile,
    uint8_t *output,
    size_t output_size
);
enum fwlab_nvme_codec_result fwlab_nvme_profile_decode(
    const uint8_t *input,
    size_t input_size,
    struct fwlab_nvme_profile *profile
);

void fwlab_nvme_profile_fixed(struct fwlab_nvme_profile *profile);

#endif
