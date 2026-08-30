/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c35_test_support.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

#define TWO_ATOM_RESULTS 10u
#define TWO_ATOM_RAW_STAGES 3u

struct two_atom_transcript {
    struct c35_semantic_result result[TWO_ATOM_RESULTS];
    uint8_t raw[TWO_ATOM_RAW_STAGES][C35_RAW_PROJECTION_BYTES];
};

static uint32_t crc32c(const uint8_t *bytes, size_t length)
{
    uint32_t crc = UINT32_MAX;
    size_t index;

    for (index = 0; index < length; ++index) {
        unsigned int bit;

        crc ^= bytes[index];
        for (bit = 0; bit < 8; ++bit) {
            uint32_t mask = 0u - (crc & 1u);

            crc = (crc >> 1) ^ (UINT32_C(0x82f63b78) & mask);
        }
    }
    return ~crc;
}

static int check_value_state(
    const struct c35_semantic_result *result,
    const uint8_t payload[C35_ATOMS][C35_ATOM_BYTES]
)
{
    unsigned int atom;

    CHECK(result->status == C34_COMMAND_SUCCESS);
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        CHECK(result->logical_kind[atom] == C34_LOGICAL_VALUE);
        CHECK(result->logical_version[atom] == 1);
        CHECK(result->logical_copy[atom] == 0);
        CHECK(result->value_crc[atom] ==
              crc32c(payload[atom], C35_ATOM_BYTES));
    }
    return 1;
}

static int check_read_value(
    const struct c35_semantic_result *result,
    unsigned int atom,
    const uint8_t payload[C35_ATOMS][C35_ATOM_BYTES]
)
{
    CHECK(result->status == C34_COMMAND_SUCCESS);
    CHECK(result->request_kind == C35_READ);
    CHECK(result->atom_mask == (uint8_t)(1u << atom));
    CHECK(result->present_mask == (uint8_t)(1u << atom));
    CHECK(memcmp(result->payload[atom], payload[atom], C35_ATOM_BYTES) == 0);
    CHECK(result->logical_kind[atom] == C34_LOGICAL_VALUE);
    CHECK(result->logical_version[atom] == 1);
    CHECK(result->logical_copy[atom] == 0);
    CHECK(result->value_crc[atom] ==
          crc32c(payload[atom], C35_ATOM_BYTES));
    return 1;
}

static int check_trim_state(const struct c35_semantic_result *result)
{
    unsigned int atom;

    CHECK(result->status == C34_COMMAND_SUCCESS);
    CHECK(result->request_kind == C35_TRIM);
    CHECK(result->atom_mask == UINT8_C(0x03));
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        CHECK(result->logical_kind[atom] == C34_LOGICAL_TOMBSTONE);
        CHECK(result->logical_version[atom] == 2);
        CHECK(result->logical_copy[atom] == 0);
        CHECK(result->value_crc[atom] == 0);
    }
    return 1;
}

static int check_read_absent(
    const struct c35_semantic_result *result,
    unsigned int atom
)
{
    CHECK(result->status == C34_COMMAND_SUCCESS);
    CHECK(result->request_kind == C35_READ);
    CHECK(result->atom_mask == (uint8_t)(1u << atom));
    CHECK(result->present_mask == 0);
    CHECK(result->logical_kind[atom] == C34_LOGICAL_TOMBSTONE);
    CHECK(result->logical_version[atom] == 2);
    CHECK(result->logical_copy[atom] == 0);
    CHECK(result->value_crc[atom] == 0);
    return 1;
}

static int runtime_restart(
    struct c35_runtime *runtime,
    struct c35_storage *storage,
    enum c35_lane lane,
    uint64_t nonce,
    uint64_t seed,
    uint32_t scenario
)
{
    return c35_runtime_teardown(runtime) && c35_storage_restart(storage) &&
           c35_runtime_init(
               runtime, storage, lane, nonce, seed, 0, 0, scenario);
}

static int run_lane(
    enum c35_lane lane,
    struct two_atom_transcript *transcript,
    uint8_t container[C34_FILE_IMAGE_BYTES],
    int *has_container
)
{
    struct c35_storage *storage = calloc(1, sizeof(*storage));
    struct c35_runtime *runtime = calloc(1, sizeof(*runtime));
    struct c35_request request;
    uint8_t payload[C35_ATOMS][C35_ATOM_BYTES];
    const uint8_t (*payload_view)[C35_ATOM_BYTES];
    uint8_t uuid[16] = {
        0x35, 0xa2, 0x5a, 0xc3, 0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88, 0x44, 0x33, 0x22, 0x11
    };
    uint64_t nonce = UINT64_C(0x35a4000011223344);
    uint64_t seed = UINT64_C(0x7f6e5d4c3b2a1908);
    unsigned int atom;
    unsigned int out = 0;
    int ok;

    CHECK(storage != NULL && runtime != NULL && transcript != NULL &&
          container != NULL && has_container != NULL);
    memset(transcript, 0, sizeof(*transcript));
    for (atom = 0; atom < C35_ATOM_BYTES; ++atom) {
        payload[0][atom] = (uint8_t)(0x20u + atom * 3u);
        payload[1][atom] = (uint8_t)(0xe0u - atom * 5u);
    }
    payload_view = (const uint8_t (*)[C35_ATOM_BYTES])(const void *)payload;
    CHECK(c35_storage_init(storage, lane, uuid));
    CHECK(c35_runtime_init(
        runtime, storage, lane, nonce, seed, 0, 0, 0x35a40001));

    request = c35_request_write_mask(
        UINT8_C(0x03), FWLAB_PERSIST_SELF_DURABLE, 1, payload_view);
    CHECK(c35_run_command(runtime, &request, &transcript->result[out]));
    CHECK(transcript->result[out].request_kind == C35_WRITE &&
          transcript->result[out].atom_mask == UINT8_C(0x03));
    CHECK(check_value_state(&transcript->result[out++], payload_view));
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        request = c35_request_read((uint8_t)atom);
        CHECK(c35_run_command(runtime, &request, &transcript->result[out]));
        CHECK(check_read_value(
            &transcript->result[out++], atom, payload_view));
    }
    CHECK(c35_runtime_projection(runtime, transcript->raw[0]));

    CHECK(runtime_restart(
        runtime, storage, lane, nonce, seed, 0x35a40002));
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        request = c35_request_read((uint8_t)atom);
        CHECK(c35_run_command(runtime, &request, &transcript->result[out]));
        CHECK(check_read_value(
            &transcript->result[out++], atom, payload_view));
    }
    CHECK(c35_runtime_projection(runtime, transcript->raw[1]));

    request = c35_request_trim_mask(
        UINT8_C(0x03), FWLAB_PERSIST_SELF_DURABLE, 2);
    CHECK(c35_run_command(runtime, &request, &transcript->result[out]));
    CHECK(check_trim_state(&transcript->result[out++]));
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        request = c35_request_read((uint8_t)atom);
        CHECK(c35_run_command(runtime, &request, &transcript->result[out]));
        CHECK(check_read_absent(&transcript->result[out++], atom));
    }

    CHECK(runtime_restart(
        runtime, storage, lane, nonce, seed, 0x35a40003));
    for (atom = 0; atom < C35_ATOMS; ++atom) {
        request = c35_request_read((uint8_t)atom);
        CHECK(c35_run_command(runtime, &request, &transcript->result[out]));
        CHECK(check_read_absent(&transcript->result[out++], atom));
    }
    CHECK(out == TWO_ATOM_RESULTS);
    CHECK(c35_runtime_projection(runtime, transcript->raw[2]));
    CHECK(c35_runtime_teardown(runtime));
    *has_container = lane == C35_LANE_BYTE || lane == C35_LANE_POSIX;
    if (*has_container) CHECK(c35_storage_container(storage, container));
    ok = c35_storage_close(storage);
    free(runtime);
    free(storage);
    CHECK(ok);
    return 1;
}

int main(void)
{
    struct two_atom_transcript *memory = calloc(1, sizeof(*memory));
    struct two_atom_transcript *byte = calloc(1, sizeof(*byte));
    struct two_atom_transcript *posix = calloc(1, sizeof(*posix));
    uint8_t *byte_container = malloc(C34_FILE_IMAGE_BYTES);
    uint8_t *posix_container = malloc(C34_FILE_IMAGE_BYTES);
    uint8_t unused[C34_FILE_IMAGE_BYTES];
    int has_memory;
    int has_byte;
    int has_posix;

    CHECK(memory != NULL && byte != NULL && posix != NULL &&
          byte_container != NULL && posix_container != NULL);
    CHECK(run_lane(C35_LANE_MEMORY, memory, unused, &has_memory));
    CHECK(run_lane(C35_LANE_BYTE, byte, byte_container, &has_byte));
    CHECK(run_lane(C35_LANE_POSIX, posix, posix_container, &has_posix));
    CHECK(!has_memory && has_byte && has_posix);
    CHECK(memcmp(memory, byte, sizeof(*memory)) == 0);
    CHECK(memcmp(memory, posix, sizeof(*memory)) == 0);
    CHECK(memcmp(byte_container, posix_container, C34_FILE_IMAGE_BYTES) == 0);
    free(posix_container);
    free(byte_container);
    free(posix);
    free(byte);
    free(memory);
    puts("C3.5a two-atom: PASS (M/B/P write03 + trim03 + two restarts; "
         "semantic/raw/container exact)");
    return 0;
}
