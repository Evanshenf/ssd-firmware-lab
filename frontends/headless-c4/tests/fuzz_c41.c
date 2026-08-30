/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>

#include "c41_wire.h"
#include "fwlab/portable/nvme_codec.h"

static void put_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

int main(void)
{
    struct c41_capture_context context = {0};
    uint8_t input[C41_SQE_BYTES] = {0};
    uint8_t baseline[FWLAB_NVME_COMMAND_WIRE_BYTES];
    uint32_t iteration;

    input[0] = 2;
    input[4] = 1;
    input[16] = 1;
    input[24] = 1;
    input[32] = 1;
    context.handle.instance_nonce = 1;
    context.handle.command_uid = 2;
    context.handle.controller_epoch = 3;
    context.handle.generation = 4;
    context.origin.word[0] = 5;
    context.origin.word[1] = 6;
    context.trace_cookie = 7;
    context.safety_generation = 8;
    context.queue_class = FWLAB_NVME_QUEUE_IO;

    for (iteration = 0; iteration < 65536; ++iteration) {
        struct c41_raw_command raw = {0};
        struct fwlab_nvme_command command = {0};
        uint8_t wire[FWLAB_NVME_COMMAND_WIRE_BYTES];

        put_u16(input + 2, (uint16_t)iteration);
        put_u16(input + 16, (uint16_t)(iteration | 1u));
        put_u16(input + 24, (uint16_t)((iteration ^ 0x55aau) | 1u));
        put_u16(input + 32, (uint16_t)((iteration ^ 0xaa55u) | 1u));
        if (c41_sqe_decode(input, sizeof(input), &raw) != C41_WIRE_OK ||
            c41_capture_command(&raw, &context, &command) != C41_WIRE_OK ||
            fwlab_nvme_command_encode(&command, wire, sizeof(wire)) !=
                FWLAB_NVME_CODEC_OK) {
            return 1;
        }
        if (iteration == 0) {
            memcpy(baseline, wire, sizeof(baseline));
        } else if (memcmp(baseline, wire, sizeof(baseline)) != 0) {
            fprintf(stderr, "raw identity/address leaked at iteration %u\n",
                    iteration);
            return 1;
        }
    }
    puts("C4.1 raw-private mutation corpus: PASS cases=65536");
    return 0;
}
