/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>

#include "fwlab/contracts/hif_action.h"
#include "fwlab/portable/nvme_codec.h"

static int write_bytes(const uint8_t *bytes, size_t size)
{
    return fwrite(bytes, 1, size, stdout) == size ? 0 : 1;
}

int main(int argc, char **argv)
{
    struct fwlab_nvme_command command = {0};
    struct fwlab_nvme_completion_intent completion = {0};
    struct fwlab_nvme_profile profile;
    struct fwlab_hif_action_envelope action = {0};
    uint8_t command_wire[FWLAB_NVME_COMMAND_WIRE_BYTES];
    uint8_t completion_wire[FWLAB_NVME_COMPLETION_WIRE_BYTES];
    uint8_t profile_wire[FWLAB_NVME_PROFILE_WIRE_BYTES];

    command.version = FWLAB_NVME_COMMAND_VERSION;
    command.size = sizeof(command);
    command.handle.instance_nonce = 1;
    command.handle.command_uid = 2;
    command.handle.controller_epoch = 3;
    command.handle.generation = 4;
    command.origin.word[0] = 5;
    command.origin.word[1] = 6;
    command.trace_cookie = 7;
    command.safety_generation = 8;
    command.namespace_id = 1;
    command.queue_class = FWLAB_NVME_QUEUE_IO;
    command.data_pointer_format = FWLAB_NVME_DATA_POINTER_PRP;
    command.data_address_present = 1;

    completion.version = FWLAB_NVME_COMPLETION_VERSION;
    completion.size = sizeof(completion);
    completion.handle = command.handle;
    completion.origin = command.origin;
    completion.actual_length = 512;
    completion.effect_class = FWLAB_NVME_EFFECT_FULL;

    fwlab_nvme_profile_fixed(&profile);
    action.version = FWLAB_HIF_ACTION_VERSION;
    action.size = sizeof(action);
    action.token.command = command.handle;
    action.token.origin = command.origin;
    action.token.action_uid = 1;
    action.token.generation = 1;
    action.token.kind = FWLAB_HIF_ACTION_DMA;
    action.cookie = 1;
    action.requested_units = 1;
    if (fwlab_nvme_command_encode(&command, command_wire,
                                  sizeof(command_wire)) != FWLAB_NVME_CODEC_OK ||
        fwlab_nvme_completion_encode(&completion, completion_wire,
                                     sizeof(completion_wire)) !=
            FWLAB_NVME_CODEC_OK ||
        fwlab_nvme_profile_encode(&profile, profile_wire,
                                  sizeof(profile_wire)) != FWLAB_NVME_CODEC_OK ||
        !fwlab_hif_action_envelope_valid(&action)) {
        return 1;
    }
    if (argc == 1) {
        puts("C4.1 portable fake link: PASS");
        return 0;
    }
    if (argc != 2 || strcmp(argv[1], "bytes") != 0) {
        return 2;
    }
    return write_bytes(profile_wire, sizeof(profile_wire)) ||
           write_bytes(command_wire, sizeof(command_wire)) ||
           write_bytes(completion_wire, sizeof(completion_wire));
}
