/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>

#include "fwlab/portable/nvme_codec.h"

int main(void)
{
    uint32_t state;
    uint32_t states = 0;
    uint32_t transitions = 0;

    for (state = 0; state < 4096; ++state) {
        struct fwlab_nvme_command command = {0};
        struct fwlab_nvme_command decoded = {0};
        uint8_t wire[FWLAB_NVME_COMMAND_WIRE_BYTES];

        command.version = FWLAB_NVME_COMMAND_VERSION;
        command.size = sizeof(command);
        command.handle.instance_nonce = UINT64_C(0x1000000000000001);
        command.handle.command_uid = (uint64_t)state + 1;
        command.handle.controller_epoch = 1;
        command.handle.generation = 1;
        command.origin.word[0] = UINT64_C(0x2000000000000001);
        command.origin.word[1] = (uint64_t)state + 1;
        command.trace_cookie = (uint64_t)state + 1;
        command.safety_generation = 1;
        command.namespace_id = 1;
        command.opcode = (uint8_t)state;
        command.queue_class = (uint8_t)((state & 1u) + 1u);
        command.fuse = (uint8_t)((state >> 1) & 3u);
        command.data_pointer_format = (uint8_t)((state >> 3) & 3u);
        command.data_address_present = (uint8_t)((state >> 5) & 1u);
        command.metadata_address_present = (uint8_t)((state >> 6) & 1u);
        command.command_flags_reserved = (uint8_t)((state >> 7) & 0x0fu);
        command.transport_fault = (state >> 11) & 1u;
        if (!fwlab_nvme_command_valid(&command) ||
            fwlab_nvme_command_encode(&command, wire, sizeof(wire)) !=
                FWLAB_NVME_CODEC_OK ||
            fwlab_nvme_command_decode(wire, sizeof(wire), &decoded) !=
                FWLAB_NVME_CODEC_OK ||
            memcmp(&command, &decoded, sizeof(command)) != 0) {
            fprintf(stderr, "C4.1 model failure at state %u\n", state);
            return 1;
        }
        ++states;
        transitions += 8;
    }
    if (states != 4096 || transitions != 32768) {
        return 1;
    }
    printf("C4.1 bounded canonical model: PASS states=%u transitions=%u "
           "depth=12 successors=8\n", states, transitions);
    return 0;
}
