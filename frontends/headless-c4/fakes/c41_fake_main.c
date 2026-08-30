/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>

#include "c41_wire.h"
#include "fwlab/contracts/hif_action.h"
#include "fwlab/portable/nvme_codec.h"

static int write_bytes(const uint8_t *bytes, size_t size)
{
    return fwrite(bytes, 1, size, stdout) == size ? 0 : 1;
}

int main(int argc, char **argv)
{
    struct c41_raw_command raw = {0};
    struct c41_capture_context capture = {0};
    struct c41_publication_context publication = {0};
    struct fwlab_nvme_command command = {0};
    struct fwlab_nvme_completion_intent intent = {0};
    struct fwlab_nvme_profile profile;
    struct fwlab_hif_action_envelope action = {0};
    uint8_t sqe[C41_SQE_BYTES] = {0};
    uint8_t command_wire[FWLAB_NVME_COMMAND_WIRE_BYTES];
    uint8_t intent_wire[FWLAB_NVME_COMPLETION_WIRE_BYTES];
    uint8_t profile_wire[FWLAB_NVME_PROFILE_WIRE_BYTES];
    uint8_t cqe[C41_CQE_BYTES];

    sqe[0] = 2;
    sqe[4] = 1;
    sqe[24] = 1;
    capture.handle.instance_nonce = 1;
    capture.handle.command_uid = 2;
    capture.handle.controller_epoch = 3;
    capture.handle.generation = 4;
    capture.origin.word[0] = 5;
    capture.origin.word[1] = 6;
    capture.trace_cookie = 7;
    capture.safety_generation = 8;
    capture.queue_class = FWLAB_NVME_QUEUE_IO;

    if (c41_sqe_decode(sqe, sizeof(sqe), &raw) != C41_WIRE_OK ||
        c41_capture_command(&raw, &capture, &command) != C41_WIRE_OK) {
        return 1;
    }
    intent.version = FWLAB_NVME_COMPLETION_VERSION;
    intent.size = sizeof(intent);
    intent.handle = command.handle;
    intent.origin = command.origin;
    intent.actual_length = 512;
    intent.effect_class = FWLAB_NVME_EFFECT_FULL;
    publication.handle = command.handle;
    publication.origin = command.origin;
    publication.submission_queue_head = 1;
    publication.submission_queue_id = 1;
    publication.command_id = 2;
    publication.phase = 1;
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

    if (fwlab_nvme_profile_encode(&profile, profile_wire,
                                  sizeof(profile_wire)) != FWLAB_NVME_CODEC_OK ||
        fwlab_nvme_command_encode(&command, command_wire,
                                  sizeof(command_wire)) != FWLAB_NVME_CODEC_OK ||
        fwlab_nvme_completion_encode(&intent, intent_wire,
                                     sizeof(intent_wire)) !=
            FWLAB_NVME_CODEC_OK ||
        c41_completion_publish(&intent, &publication, cqe, sizeof(cqe)) !=
            C41_WIRE_OK || !fwlab_hif_action_envelope_valid(&action)) {
        return 1;
    }
    if (argc == 1) {
        puts("C4.1 headless fake link: PASS");
        return 0;
    }
    if (argc != 2 || strcmp(argv[1], "bytes") != 0) {
        return 2;
    }
    return write_bytes(profile_wire, sizeof(profile_wire)) ||
           write_bytes(command_wire, sizeof(command_wire)) ||
           write_bytes(intent_wire, sizeof(intent_wire)) ||
           write_bytes(cqe, sizeof(cqe));
}
