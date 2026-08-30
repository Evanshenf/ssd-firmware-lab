/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdio.h>
#include <string.h>

#include "c41_wire.h"
#include "fwlab/portable/nvme_codec.h"

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "C4.1 HIF wire check failed at line %d: %s\n", \
                __LINE__, #expression); \
        return 1; \
    } \
} while (0)

static const uint8_t SQE_GOLDEN[C41_SQE_BYTES] = {
    [0] = 0x02, [1] = 0x29, [2] = 0xef, [3] = 0xbe,
    [4] = 0x84, [5] = 0x83, [6] = 0x82, [7] = 0x81,
    [8] = 0x94, [9] = 0x93, [10] = 0x92, [11] = 0x91,
    [12] = 0xa4, [13] = 0xa3, [14] = 0xa2, [15] = 0xa1,
    [16] = 0x04, [17] = 0x03, [18] = 0x02, [19] = 0x01,
    [20] = 0xef, [21] = 0xbe, [22] = 0xad, [23] = 0xde,
    [24] = 0x11, [25] = 0x22, [26] = 0x33, [27] = 0x44,
    [28] = 0x55, [29] = 0x66, [30] = 0x77, [31] = 0x88,
    [32] = 0x80, [33] = 0x70, [34] = 0x60, [35] = 0x50,
    [36] = 0x40, [37] = 0x30, [38] = 0x20, [39] = 0x10,
    [40] = 0xb4, [41] = 0xb3, [42] = 0xb2, [43] = 0xb1,
    [44] = 0xc4, [45] = 0xc3, [46] = 0xc2, [47] = 0xc1,
    [48] = 0xd4, [49] = 0xd3, [50] = 0xd2, [51] = 0xd1,
    [52] = 0xe4, [53] = 0xe3, [54] = 0xe2, [55] = 0xe1,
    [56] = 0xf4, [57] = 0xf3, [58] = 0xf2, [59] = 0xf1,
    [60] = 0x04, [61] = 0x03, [62] = 0x02, [63] = 0x01,
};

static const uint8_t CANONICAL_GOLDEN[FWLAB_NVME_COMMAND_WIRE_BYTES] = {
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

static const uint8_t CQE_GOLDEN[C41_CQE_BYTES] = {
    [0] = 0x44, [1] = 0x33, [2] = 0x22, [3] = 0x11,
    [8] = 0x34, [9] = 0x12, [10] = 0x78, [11] = 0x56,
    [12] = 0xbc, [13] = 0x9a, [14] = 0xff, [15] = 0xd4,
};

static struct c41_capture_context sample_capture(void)
{
    struct c41_capture_context context = {0};

    context.handle.instance_nonce = UINT64_C(0x0102030405060708);
    context.handle.command_uid = UINT64_C(0x1112131415161718);
    context.handle.controller_epoch = 0x21222324u;
    context.handle.generation = 0x31323334u;
    context.origin.word[0] = UINT64_C(0x4142434445464748);
    context.origin.word[1] = UINT64_C(0x5152535455565758);
    context.trace_cookie = UINT64_C(0x6162636465666768);
    context.safety_generation = 0x71727374u;
    context.queue_class = FWLAB_NVME_QUEUE_IO;
    return context;
}

static struct fwlab_nvme_completion_intent sample_intent(
    const struct fwlab_nvme_command *command)
{
    struct fwlab_nvme_completion_intent intent = {0};

    intent.version = FWLAB_NVME_COMPLETION_VERSION;
    intent.size = sizeof(intent);
    intent.handle = command->handle;
    intent.origin = command->origin;
    intent.result_dword0 = 0x11223344u;
    intent.actual_length = 4096;
    intent.status_code = 0x7f;
    intent.status_code_type = 2;
    intent.command_retry_delay = 1;
    intent.more = 1;
    intent.do_not_retry = 1;
    intent.effect_class = FWLAB_NVME_EFFECT_FULL;
    return intent;
}

static int test_capture(void)
{
    struct c41_raw_command raw = {0};
    struct c41_raw_command changed;
    struct c41_raw_command sentinel;
    struct c41_capture_context context = sample_capture();
    struct fwlab_nvme_command command = {0};
    struct fwlab_nvme_command second = {0};
    uint8_t wire[FWLAB_NVME_COMMAND_WIRE_BYTES];

    memset(&sentinel, 0xa5, sizeof(sentinel));
    raw = sentinel;
    CHECK(c41_sqe_decode(SQE_GOLDEN, C41_SQE_BYTES - 1, &raw) ==
          C41_WIRE_INVALID_ARGUMENT);
    CHECK(memcmp(&raw, &sentinel, sizeof(raw)) == 0);
    CHECK(c41_sqe_decode(SQE_GOLDEN, sizeof(SQE_GOLDEN), &raw) == C41_WIRE_OK);
    CHECK(raw.opcode == 0x02 && raw.fuse == 1 &&
          raw.command_flags_reserved == 0x0a && raw.data_pointer_format == 0);
    CHECK(raw.command_id == 0xbeefu);
    CHECK(raw.metadata_pointer == UINT64_C(0xdeadbeef01020304));
    CHECK(raw.data_pointer1 == UINT64_C(0x8877665544332211));
    CHECK(raw.data_pointer2 == UINT64_C(0x1020304050607080));
    CHECK(c41_capture_command(&raw, &context, &command) == C41_WIRE_OK);
    CHECK(fwlab_nvme_command_encode(&command, wire, sizeof(wire)) ==
          FWLAB_NVME_CODEC_OK);
    CHECK(memcmp(wire, CANONICAL_GOLDEN, sizeof(wire)) == 0);

    changed = raw;
    changed.command_id ^= 0xffffu;
    changed.metadata_pointer ^= UINT64_C(0x0101010101010101);
    changed.data_pointer1 ^= UINT64_C(0x1111111111111111);
    changed.data_pointer2 ^= UINT64_C(0x2222222222222222);
    CHECK(c41_capture_command(&changed, &context, &second) == C41_WIRE_OK);
    CHECK(memcmp(&command, &second, sizeof(command)) == 0);
    return 0;
}

static int test_completion(void)
{
    struct c41_raw_completion decoded = {0};
    struct c41_raw_completion sentinel;
    struct c41_publication_context context = {0};
    struct c41_capture_context capture = sample_capture();
    struct c41_raw_command raw = {0};
    struct fwlab_nvme_command command = {0};
    struct fwlab_nvme_completion_intent intent;
    uint8_t output[C41_CQE_BYTES];
    uint8_t broken[C41_CQE_BYTES];

    CHECK(c41_sqe_decode(SQE_GOLDEN, sizeof(SQE_GOLDEN), &raw) == C41_WIRE_OK);
    CHECK(c41_capture_command(&raw, &capture, &command) == C41_WIRE_OK);
    intent = sample_intent(&command);
    context.handle = command.handle;
    context.origin = command.origin;
    context.submission_queue_head = 0x1234;
    context.submission_queue_id = 0x5678;
    context.command_id = 0x9abc;
    context.phase = 1;
    CHECK(c41_completion_publish(&intent, &context, output, sizeof(output)) ==
          C41_WIRE_OK);
    CHECK(memcmp(output, CQE_GOLDEN, sizeof(output)) == 0);
    CHECK(c41_cqe_decode(CQE_GOLDEN, sizeof(CQE_GOLDEN), &decoded) ==
          C41_WIRE_OK);
    CHECK(decoded.result_dword0 == 0x11223344u &&
          decoded.submission_queue_head == 0x1234u &&
          decoded.submission_queue_id == 0x5678u &&
          decoded.command_id == 0x9abcu && decoded.phase == 1 &&
          decoded.status_code == 0x7fu && decoded.status_code_type == 2 &&
          decoded.command_retry_delay == 1 && decoded.more == 1 &&
          decoded.do_not_retry == 1);

    memcpy(broken, CQE_GOLDEN, sizeof(broken));
    broken[4] = 1;
    memset(&sentinel, 0xa5, sizeof(sentinel));
    decoded = sentinel;
    CHECK(c41_cqe_decode(broken, sizeof(broken), &decoded) ==
          C41_WIRE_BAD_ENCODING);
    CHECK(memcmp(&decoded, &sentinel, sizeof(decoded)) == 0);
    context.handle.command_uid++;
    memset(output, 0xa5, sizeof(output));
    CHECK(c41_completion_publish(&intent, &context, output, sizeof(output)) ==
          C41_WIRE_IDENTITY_MISMATCH);
    CHECK(output[0] == 0xa5 && output[sizeof(output) - 1] == 0xa5);
    return 0;
}

int main(void)
{
    CHECK(test_capture() == 0);
    CHECK(test_completion() == 0);
    puts("C4.1 headless raw capture/CQE codec: PASS");
    return 0;
}
