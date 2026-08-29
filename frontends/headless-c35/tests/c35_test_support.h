/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_TEST_SUPPORT_H
#define FWLAB_C35_TEST_SUPPORT_H

#include "../c35_bundle.h"
#include "../c35_headless.h"
#include "../bindings/c35_c34.h"
#include "../bindings/c35_scripted.h"
#include "c34_buffer.h"
#include "c34_memory_media.h"
#include "c34_file_media.h"
#include "c34_file_test_support.h"
#include "c31_fake_dma.h"
#include "fwlab/portable/nfc_model.h"

#include <stdalign.h>

enum c35_lane {
    C35_LANE_SCRIPTED = 0,
    C35_LANE_MEMORY = 1,
    C35_LANE_BYTE = 2,
    C35_LANE_POSIX = 3
};

union c35_arena {
    max_align_t alignment;
    uint8_t bytes[262144];
};

struct c35_storage {
    uint8_t lane;
    uint8_t file_slot;
    uint8_t initialized;
    uint8_t reserved;
    int fd;
    uint8_t uuid[16];
    struct c34_memory_media memory;
    struct c34f_memory_substrate byte;
    union c34f_test_arena file_arena[2];
    struct c34_file_media *file;
    struct c35_bundle bundle;
};

struct c35_runtime {
    union c35_arena nfc_arena;
    union c35_arena c34_arena;
    union c35_arena c31_arena;
    struct c34_fake_buffer buffer;
    struct c31_fake_dma_context dma;
    struct c31_fake_provider_context scripted_nfc;
    struct fwlab_nfc_model *nfc_model;
    struct c34 *firmware;
    struct fwlab_c31 *lifecycle;
    struct c35_bundle *bundle;
    struct c35_c34_binding c34_binding;
    struct c35_scripted_binding scripted_binding;
    struct c35_headless headless;
    struct c35_trace trace;
    struct c35_storage *storage;
    uint64_t nonce;
    uint64_t seed;
    uint64_t dma_next;
    uint8_t lane;
    uint8_t cache_enabled;
    uint8_t actor;
    uint8_t claimed;
};

struct fwlab_nfc_model_config c35_test_nfc_config(uint64_t seed);
int c35_storage_init(
    struct c35_storage *storage,
    enum c35_lane lane,
    const uint8_t uuid[16]
);
int c35_storage_restart(struct c35_storage *storage);
int c35_storage_close(struct c35_storage *storage);
int c35_storage_container(
    const struct c35_storage *storage,
    uint8_t bytes[C34_FILE_IMAGE_BYTES]
);

int c35_runtime_init(
    struct c35_runtime *runtime,
    struct c35_storage *storage,
    enum c35_lane lane,
    uint64_t nonce,
    uint64_t seed,
    uint8_t cache_enabled,
    uint8_t actor,
    uint32_t scenario
);
int c35_runtime_init_profile(
    struct c35_runtime *runtime,
    struct c35_storage *storage,
    enum c35_lane lane,
    uint64_t nonce,
    uint64_t seed,
    uint8_t cache_enabled,
    uint8_t actor,
    uint32_t scenario,
    const struct fwlab_nfc_model_config *nfc_config
);
int c35_runtime_teardown(struct c35_runtime *runtime);
int c35_runtime_projection(
    struct c35_runtime *runtime,
    uint8_t bytes[C35_RAW_PROJECTION_BYTES]
);
int c35_dma_capture(
    struct c35_runtime *runtime,
    const uint8_t input[C35_ATOM_BYTES],
    uint8_t output[C35_ATOM_BYTES]
);
int c35_run_command(
    struct c35_runtime *runtime,
    const struct c35_request *request,
    struct c35_semantic_result *result
);

struct c35_request c35_request_read(uint8_t atom);
struct c35_request c35_request_write(
    uint8_t atom,
    uint8_t durability,
    uint32_t sequence,
    const uint8_t payload[C35_ATOM_BYTES]
);
struct c35_request c35_request_trim(
    uint8_t atom,
    uint8_t durability,
    uint32_t sequence
);
struct c35_request c35_request_fence(uint32_t frontier);

#endif
