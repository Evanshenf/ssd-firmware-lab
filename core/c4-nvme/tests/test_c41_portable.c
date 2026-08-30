/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>

#include "fwlab/contracts/hif_action.h"
#include "fwlab/portable/nvme_codec.h"

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "C4.1 portable check failed at line %d: %s\n", \
                __LINE__, #expression); \
        return 1; \
    } \
} while (0)

static const uint8_t COMMAND_GOLDEN[FWLAB_NVME_COMMAND_WIRE_BYTES] = {
    [0] = 'F', [1] = '4', [2] = 'N', [3] = 'C', [4] = 1, [6] = 128,
    [8] = 0x08, [9] = 0x07, [10] = 0x06, [11] = 0x05,
    [12] = 0x04, [13] = 0x03, [14] = 0x02, [15] = 0x01,
    [16] = 0x18, [17] = 0x17, [18] = 0x16, [19] = 0x15,
    [20] = 0x14, [21] = 0x13, [22] = 0x12, [23] = 0x11,
    [24] = 0x24, [25] = 0x23, [26] = 0x22, [27] = 0x21,
    [28] = 0x34, [29] = 0x33, [30] = 0x32, [31] = 0x31,
    [32] = 0x48, [33] = 0x47, [34] = 0x46, [35] = 0x45,
    [36] = 0x44, [37] = 0x43, [38] = 0x42, [39] = 0x41,
    [40] = 0x58, [41] = 0x57, [42] = 0x56, [43] = 0x55,
    [44] = 0x54, [45] = 0x53, [46] = 0x52, [47] = 0x51,
    [48] = 0x68, [49] = 0x67, [50] = 0x66, [51] = 0x65,
    [52] = 0x64, [53] = 0x63, [54] = 0x62, [55] = 0x61,
    [56] = 0x74, [57] = 0x73, [58] = 0x72, [59] = 0x71,
    [60] = 0x84, [61] = 0x83, [62] = 0x82, [63] = 0x81,
    [64] = 0x94, [65] = 0x93, [66] = 0x92, [67] = 0x91,
    [68] = 0xa4, [69] = 0xa3, [70] = 0xa2, [71] = 0xa1,
    [72] = 0xb4, [73] = 0xb3, [74] = 0xb2, [75] = 0xb1,
    [76] = 0xc4, [77] = 0xc3, [78] = 0xc2, [79] = 0xc1,
    [80] = 0xd4, [81] = 0xd3, [82] = 0xd2, [83] = 0xd1,
    [84] = 0xe4, [85] = 0xe3, [86] = 0xe2, [87] = 0xe1,
    [88] = 0xf4, [89] = 0xf3, [90] = 0xf2, [91] = 0xf1,
    [92] = 0x04, [93] = 0x03, [94] = 0x02, [95] = 0x01,
    [100] = 0x02, [101] = 0x02, [102] = 0x01,
    [104] = 0x01, [105] = 0x01, [106] = 0x0a,
};

static const uint8_t COMPLETION_GOLDEN[FWLAB_NVME_COMPLETION_WIRE_BYTES] = {
    [0] = 'F', [1] = '4', [2] = 'C', [3] = 'I', [4] = 1, [6] = 64,
    [8] = 0x08, [9] = 0x07, [10] = 0x06, [11] = 0x05,
    [12] = 0x04, [13] = 0x03, [14] = 0x02, [15] = 0x01,
    [16] = 0x18, [17] = 0x17, [18] = 0x16, [19] = 0x15,
    [20] = 0x14, [21] = 0x13, [22] = 0x12, [23] = 0x11,
    [24] = 0x24, [25] = 0x23, [26] = 0x22, [27] = 0x21,
    [28] = 0x34, [29] = 0x33, [30] = 0x32, [31] = 0x31,
    [32] = 0x48, [33] = 0x47, [34] = 0x46, [35] = 0x45,
    [36] = 0x44, [37] = 0x43, [38] = 0x42, [39] = 0x41,
    [40] = 0x58, [41] = 0x57, [42] = 0x56, [43] = 0x55,
    [44] = 0x54, [45] = 0x53, [46] = 0x52, [47] = 0x51,
    [48] = 0x44, [49] = 0x33, [50] = 0x22, [51] = 0x11,
    [52] = 0x00, [53] = 0x10, [56] = 0x7f,
    [58] = 0x02, [59] = 0x01, [60] = 0x01, [61] = 0x01, [62] = 0x01,
};

static const uint8_t PROFILE_GOLDEN[FWLAB_NVME_PROFILE_WIRE_BYTES] = {
    [0] = 'F', [1] = '4', [2] = 'P', [3] = 'F', [4] = 1, [6] = 64,
    [8] = 1,
    [12] = 0x00, [13] = 0x02,
    [16] = 8,
    [20] = 0x00, [21] = 0x10,
    [24] = 0x00, [25] = 0x10,
    [28] = 1, [30] = 4, [32] = 32, [34] = 2,
    [36] = 0x3f,
};

static struct fwlab_nvme_command sample_command(void)
{
    struct fwlab_nvme_command command = {0};
    const uint32_t dword[6] = {
        0xb1b2b3b4u, 0xc1c2c3c4u, 0xd1d2d3d4u,
        0xe1e2e3e4u, 0xf1f2f3f4u, 0x01020304u,
    };
    size_t index;

    command.version = FWLAB_NVME_COMMAND_VERSION;
    command.size = sizeof(command);
    command.handle.instance_nonce = UINT64_C(0x0102030405060708);
    command.handle.command_uid = UINT64_C(0x1112131415161718);
    command.handle.controller_epoch = 0x21222324u;
    command.handle.generation = 0x31323334u;
    command.origin.word[0] = UINT64_C(0x4142434445464748);
    command.origin.word[1] = UINT64_C(0x5152535455565758);
    command.trace_cookie = UINT64_C(0x6162636465666768);
    command.safety_generation = 0x71727374u;
    command.namespace_id = 0x81828384u;
    command.command_dword2 = 0x91929394u;
    command.command_dword3 = 0xa1a2a3a4u;
    for (index = 0; index < 6; ++index) {
        command.command_dword10_15[index] = dword[index];
    }
    command.opcode = 0x02;
    command.queue_class = FWLAB_NVME_QUEUE_IO;
    command.fuse = FWLAB_NVME_FUSE_FIRST;
    command.data_pointer_format = FWLAB_NVME_DATA_POINTER_PRP;
    command.data_address_present = 1;
    command.metadata_address_present = 1;
    command.command_flags_reserved = 0x0a;
    return command;
}

static struct fwlab_nvme_completion_intent sample_completion(void)
{
    struct fwlab_nvme_completion_intent completion = {0};
    struct fwlab_nvme_command command = sample_command();

    completion.version = FWLAB_NVME_COMPLETION_VERSION;
    completion.size = sizeof(completion);
    completion.handle = command.handle;
    completion.origin = command.origin;
    completion.result_dword0 = 0x11223344u;
    completion.actual_length = 4096;
    completion.status_code = 0x7f;
    completion.status_code_type = 2;
    completion.command_retry_delay = 1;
    completion.more = 1;
    completion.do_not_retry = 1;
    completion.effect_class = FWLAB_NVME_EFFECT_FULL;
    return completion;
}

static int test_command(void)
{
    struct fwlab_nvme_command command = sample_command();
    struct fwlab_nvme_command decoded = {0};
    struct fwlab_nvme_command sentinel;
    uint8_t wire[FWLAB_NVME_COMMAND_WIRE_BYTES];
    uint8_t broken[FWLAB_NVME_COMMAND_WIRE_BYTES];

    CHECK(fwlab_nvme_command_valid(&command));
    CHECK(fwlab_nvme_command_encode(&command, wire, sizeof(wire)) ==
          FWLAB_NVME_CODEC_OK);
    CHECK(memcmp(wire, COMMAND_GOLDEN, sizeof(wire)) == 0);
    CHECK(fwlab_nvme_command_decode(COMMAND_GOLDEN, sizeof(COMMAND_GOLDEN),
                                    &decoded) == FWLAB_NVME_CODEC_OK);
    CHECK(memcmp(&decoded, &command, sizeof(command)) == 0);

    memcpy(broken, COMMAND_GOLDEN, sizeof(broken));
    broken[107] = 1;
    memset(&sentinel, 0xa5, sizeof(sentinel));
    decoded = sentinel;
    CHECK(fwlab_nvme_command_decode(broken, sizeof(broken), &decoded) ==
          FWLAB_NVME_CODEC_BAD_ENCODING);
    CHECK(memcmp(&decoded, &sentinel, sizeof(decoded)) == 0);
    broken[107] = 0;
    broken[4] = 2;
    CHECK(fwlab_nvme_command_decode(broken, sizeof(broken), &decoded) ==
          FWLAB_NVME_CODEC_UNSUPPORTED_VERSION);
    command.reserved2[4] = 1;
    memset(wire, 0xa5, sizeof(wire));
    CHECK(fwlab_nvme_command_encode(&command, wire, sizeof(wire)) ==
          FWLAB_NVME_CODEC_INVALID_VALUE);
    CHECK(wire[0] == 0xa5 && wire[sizeof(wire) - 1] == 0xa5);
    return 0;
}

static int test_completion(void)
{
    struct fwlab_nvme_completion_intent completion = sample_completion();
    struct fwlab_nvme_completion_intent decoded = {0};
    uint8_t wire[FWLAB_NVME_COMPLETION_WIRE_BYTES];
    uint8_t broken[FWLAB_NVME_COMPLETION_WIRE_BYTES];

    CHECK(fwlab_nvme_completion_valid(&completion));
    CHECK(fwlab_nvme_completion_encode(&completion, wire, sizeof(wire)) ==
          FWLAB_NVME_CODEC_OK);
    CHECK(memcmp(wire, COMPLETION_GOLDEN, sizeof(wire)) == 0);
    CHECK(fwlab_nvme_completion_decode(
              COMPLETION_GOLDEN, sizeof(COMPLETION_GOLDEN), &decoded) ==
          FWLAB_NVME_CODEC_OK);
    CHECK(memcmp(&decoded, &completion, sizeof(completion)) == 0);
    memcpy(broken, COMPLETION_GOLDEN, sizeof(broken));
    broken[63] = 1;
    CHECK(fwlab_nvme_completion_decode(broken, sizeof(broken), &decoded) ==
          FWLAB_NVME_CODEC_BAD_ENCODING);
    completion.status_code = 0x100;
    CHECK(!fwlab_nvme_completion_valid(&completion));
    return 0;
}

static int test_profile(void)
{
    struct fwlab_nvme_profile profile;
    struct fwlab_nvme_profile decoded = {0};
    uint8_t wire[FWLAB_NVME_PROFILE_WIRE_BYTES];
    uint8_t broken[FWLAB_NVME_PROFILE_WIRE_BYTES];

    fwlab_nvme_profile_fixed(&profile);
    CHECK(fwlab_nvme_profile_valid(&profile));
    CHECK(fwlab_nvme_profile_encode(&profile, wire, sizeof(wire)) ==
          FWLAB_NVME_CODEC_OK);
    CHECK(memcmp(wire, PROFILE_GOLDEN, sizeof(wire)) == 0);
    CHECK(fwlab_nvme_profile_decode(PROFILE_GOLDEN, sizeof(PROFILE_GOLDEN),
                                    &decoded) == FWLAB_NVME_CODEC_OK);
    CHECK(memcmp(&decoded, &profile, sizeof(profile)) == 0);
    memcpy(broken, PROFILE_GOLDEN, sizeof(broken));
    broken[40] = 1;
    CHECK(fwlab_nvme_profile_decode(broken, sizeof(broken), &decoded) ==
          FWLAB_NVME_CODEC_BAD_ENCODING);
    profile.maximum_transfer_bytes = 8192;
    CHECK(!fwlab_nvme_profile_valid(&profile));
    return 0;
}

static int test_action_envelope(void)
{
    struct fwlab_nvme_command command = sample_command();
    struct fwlab_hif_action_envelope envelope = {0};
    struct fwlab_hif_action_submit_result submit = {0};
    struct fwlab_hif_action_terminal terminal = {0};

    envelope.version = FWLAB_HIF_ACTION_VERSION;
    envelope.size = sizeof(envelope);
    envelope.token.command = command.handle;
    envelope.token.origin = command.origin;
    envelope.token.action_uid = 1;
    envelope.token.generation = 1;
    envelope.token.kind = FWLAB_HIF_ACTION_DMA;
    envelope.cookie = 2;
    envelope.dependency_ordinal = 3;
    envelope.requested_units = 4;
    CHECK(fwlab_hif_action_envelope_valid(&envelope));

    submit.version = FWLAB_HIF_ACTION_VERSION;
    submit.size = sizeof(submit);
    submit.token = envelope.token;
    submit.disposition = FWLAB_HIF_ACTION_ACCEPTED;
    submit.retry = FWLAB_HIF_ACTION_RETRY_NONE;
    submit.effect_class = FWLAB_NVME_EFFECT_NONE;
    CHECK(fwlab_hif_action_submit_result_valid(&submit));

    terminal.version = FWLAB_HIF_ACTION_VERSION;
    terminal.size = sizeof(terminal);
    terminal.token = envelope.token;
    terminal.cookie = envelope.cookie;
    terminal.units_completed = 4;
    terminal.terminal_kind = FWLAB_HIF_ACTION_SUCCESS;
    terminal.effect_class = FWLAB_NVME_EFFECT_FULL;
    terminal.retry = FWLAB_HIF_ACTION_RETRY_NONE;
    CHECK(fwlab_hif_action_terminal_valid(&terminal));
    terminal.token.origin.word[0] = 0;
    terminal.token.origin.word[1] = 0;
    CHECK(!fwlab_hif_action_terminal_valid(&terminal));
    submit.disposition = 3;
    CHECK(!fwlab_hif_action_submit_result_valid(&submit));
    envelope.reserved1[3] = 1;
    CHECK(!fwlab_hif_action_envelope_valid(&envelope));
    return 0;
}

int main(void)
{
    CHECK(test_command() == 0);
    CHECK(test_completion() == 0);
    CHECK(test_profile() == 0);
    CHECK(test_action_envelope() == 0);
    puts("C4.1 portable profile/canonical codec: PASS");
    return 0;
}
