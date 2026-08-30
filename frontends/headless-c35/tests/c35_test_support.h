/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_TEST_SUPPORT_H
#define FWLAB_C35_TEST_SUPPORT_H

#include "../c35_bundle.h"
#include "../c35_finalizer.h"
#include "../c35_headless.h"
#include "../c35_trace.h"
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
    uint32_t image_serial;
    struct c35_profile_descriptor profile;
    struct c35_persistent_credits credits;
    struct c34_memory_media memory;
    struct c34f_memory_substrate byte;
    union c34f_test_arena file_arena[2];
    struct c34_file_media *file;
    struct c35_bundle bundle;
};

enum c35_dma_phase {
    C35_DMA_REGISTER_BUFFER = 0x100,
    C35_DMA_ADD_SCENARIO,
    C35_DMA_SUBMIT,
    C35_DMA_SUBMIT_QUERY,
    C35_DMA_WAIT_READY,
    C35_DMA_ACQUIRE,
    C35_DMA_ACQUIRE_QUERY,
    C35_DMA_CAPTURE,
    C35_DMA_CONSUME,
    C35_DMA_CONSUME_QUERY,
    C35_DMA_DONE
};

struct c35_dma_transaction {
    uint8_t used;
    uint8_t finished;
    uint8_t command_valid;
    uint8_t lease_valid;
    uint8_t reserved0[4];
    uint32_t phase;
    uint32_t outcome;
    uint32_t commit_state;
    uint32_t cleanup_state;
    uint32_t cause_domain;
    uint32_t cause_code;
    int32_t capability_index;
    struct c35_operation_token token;
    struct fwlab_c31_command_descriptor descriptor;
    struct c31_fake_dma_scenario scenario;
    struct fwlab_c31_command_handle command;
    struct fwlab_c31_completion_lease lease;
    struct fwlab_c31_completion_intent intent;
    struct c35_publication publication;
    uint8_t input[C35_ATOM_BYTES];
    uint8_t output[C35_ATOM_BYTES];
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
    struct c35_lifecycle_port lifecycle_port;
    struct c35_bundle *bundle;
    struct c35_c34_binding c34_binding;
    struct c35_scripted_binding scripted_binding;
    struct c35_headless headless;
    struct c35_finalizer finalizer;
    struct c35_trace trace;
    struct c35_publication teardown_publication;
    uint8_t last_observation;
    uint8_t teardown_committed;
    uint8_t bundle_released;
    uint8_t teardown_observed;
    uint8_t reserved0[4];
    struct c35_storage *storage;
    uint64_t nonce;
    uint64_t seed;
    uint64_t dma_next;
    struct c35_dma_transaction dma_operation;
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
enum c35_result c35_dma_start(
    struct c35_runtime *runtime,
    const uint8_t input[C35_ATOM_BYTES],
    struct c35_operation_token *token
);
enum c35_result c35_dma_progress(
    struct c35_runtime *runtime,
    const struct c35_operation_token *token,
    uint32_t budget,
    struct c35_operation_status *status
);
enum c35_result c35_dma_query(
    const struct c35_runtime *runtime,
    const struct c35_operation_token *token,
    struct c35_operation_status *status
);
enum c35_result c35_dma_finalize(
    const struct c35_runtime *runtime,
    const struct c35_operation_token *token,
    struct c35_operation_status *status,
    uint8_t output[C35_ATOM_BYTES],
    struct c35_publication *publication
);
enum c35_result c35_dma_retire(
    struct c35_runtime *runtime,
    const struct c35_operation_token *token
);
enum c35_result c35_dma_capture_status(
    struct c35_runtime *runtime,
    const uint8_t input[C35_ATOM_BYTES],
    uint8_t output[C35_ATOM_BYTES],
    struct c35_operation_status *status,
    struct c35_publication *publication
);
int c35_run_command(
    struct c35_runtime *runtime,
    const struct c35_request *request,
    struct c35_semantic_result *result
);
enum c35_result c35_run_command_status(
    struct c35_runtime *runtime,
    const struct c35_request *request,
    struct c35_semantic_result *result,
    struct c35_operation_status *status
);

struct c35_request c35_request_read(uint8_t atom);
struct c35_request c35_request_write(
    uint8_t atom,
    uint8_t durability,
    uint32_t sequence,
    const uint8_t payload[C35_ATOM_BYTES]
);
struct c35_request c35_request_write_mask(
    uint8_t mask,
    uint8_t durability,
    uint32_t sequence,
    const uint8_t payload[C35_ATOMS][C35_ATOM_BYTES]
);
struct c35_request c35_request_trim(
    uint8_t atom,
    uint8_t durability,
    uint32_t sequence
);
struct c35_request c35_request_trim_mask(
    uint8_t mask,
    uint8_t durability,
    uint32_t sequence
);
struct c35_request c35_request_fence(uint32_t frontier);

#endif
