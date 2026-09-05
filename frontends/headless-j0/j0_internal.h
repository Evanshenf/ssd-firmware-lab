/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_HEADLESS_J0_INTERNAL_H
#define FWLAB_HEADLESS_J0_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "spine_internal.h"
#include "spine_publication_v1.h"
#include "m3p.h"
#include "file_nand.h"
#include "fwlab/contracts/host_data_v0.h"
#include "fwlab/portable/nfc_model.h"

#define J0_RUNTIME_VERSION 1u
#define J0_RUNTIME_MAGIC UINT64_C(0x4a30484541444c53)
#define J0_MAX_COMMANDS 32u
#define J0_MAX_DMA_OPERATIONS 32u
#define J0_MAX_TRANSFER_BYTES 8192u
#define J0_BUDGET_REFERENCE 0u
#define J0_BUDGET_LAB 1u

#define J0_LIFECYCLE_NONCE UINT64_C(0x4a304c4946450001)
#define J0_C43_ADAPTER_NONCE UINT64_C(0x4a30433433000001)
#define J0_LINUX_ADAPTER_NONCE UINT64_C(0x4a304c4e58000001)
#define J0_BUFFER_ISSUER_NONCE UINT64_C(0x4a30425546000001)
#define J0_AUTHORITY_ISSUER_NONCE UINT64_C(0x4a30484441000001)
#define J0_DMA_ISSUER_NONCE UINT64_C(0x4a30444d41000001)
#define J0_M3P_INSTANCE_NONCE UINT64_C(0x4a304d3350000001)
#define J0_M3P_PROVIDER_NONCE UINT64_C(0x4a30424c4b000001)
#define J0_NFC_INSTANCE_NONCE UINT64_C(0x4a304e4643000001)

enum j0_profile_literal {
    J0_PROFILE_C43_P1 = 1,
    J0_PROFILE_LINUX_V1 = 2
};

enum j0_media_mode {
    J0_MEDIA_FORMAT = 1,
    J0_MEDIA_RECOVER = 2
};

enum j0_admission_phase {
    J0_ADMISSION_FREE = 0,
    J0_ADMISSION_PLAN_HELD = 1,
    J0_ADMISSION_RESOURCES_HELD = 2,
    J0_ADMISSION_TRANSFERRED = 3,
    J0_ADMISSION_ROLLBACK = 4,
    J0_ADMISSION_FAILED = 5
};

enum j0_driver_state {
    J0_DRIVER_EMPTY = 0,
    J0_DRIVER_PREPARED = 1,
    J0_DRIVER_SUBMIT_UNKNOWN = 2,
    J0_DRIVER_ACCEPTED = 3,
    J0_DRIVER_TERMINAL_LATCHED = 4,
    J0_DRIVER_DRAINING = 5,
    J0_DRIVER_DRAINED = 6,
    J0_DRIVER_REJECTED_CLEAN = 7
};

struct j0_host_transfer {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint32_t direction;
    uint32_t exact_bytes;
    const uint8_t *input;
    uint32_t reserved1[4];
};

struct j0_host_binding {
    struct fwlab_host_data_port_v0 data;
    struct fwlab_host_action_driver_binding_v0 queue_driver;
    void *context;
    enum fwlab_spine_result_v0 (*endpoint_prepare)(
        void *context, const struct fwlab_nvme_command_handle *command,
        const struct fwlab_nvme_origin_token *origin, uint32_t direction,
        uint32_t exact_bytes, const uint8_t *input);
    enum fwlab_spine_result_v0 (*endpoint_read)(
        void *context, const struct fwlab_nvme_command_handle *command,
        const struct fwlab_nvme_origin_token *origin, void *output,
        size_t output_size);
    enum fwlab_spine_result_v0 (*endpoint_release)(
        void *context, const struct fwlab_nvme_command_handle *command,
        const struct fwlab_nvme_origin_token *origin);
    /* Referenced Host endpoints require NULL input; DMA reads their bytes
     * only when the typed DMA action executes after profile shape. */
    uint8_t inline_input;
};

struct j0_host_factory {
    enum fwlab_spine_result_v0 (*bind)(
        void *context, const struct fwlab_controller_buffer_port_v0 *buffer,
        uint32_t generation, struct j0_host_binding *binding);
    void *context;
};

struct j0_runtime_config {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint8_t media_uuid[16];
    struct fwlab_file_nand_v0 *file;
    uint32_t media_mode;
    uint32_t generation;
    uint32_t execution_epoch;
    uint64_t volatile_nonce_seed;
    const struct j0_host_factory *host_factory;
    uint32_t budget_profile;
    uint32_t reserved1[4];
};

struct j0_controller_buffer_record {
    struct fwlab_controller_buffer_acquire_v0 request;
    struct fwlab_controller_buffer_lease_v0 lease;
    uint8_t bytes[J0_MAX_TRANSFER_BYTES];
    uint8_t occupied;
    uint8_t released;
    uint8_t reserved[6];
};

struct j0_controller_buffer {
    struct fwlab_controller_buffer_port_v0 port;
    struct j0_controller_buffer_record record[J0_MAX_COMMANDS];
    uint64_t next_buffer_uid;
    uint64_t next_lease_uid;
    uint64_t close_lifecycle_nonce;
    uint32_t generation;
    uint32_t close_execution_epoch;
    uint32_t active_leases;
    uint8_t admission_closed;
    uint8_t close_started;
    uint8_t poisoned;
    uint8_t reserved0;
};

struct j0_host_endpoint {
    struct fwlab_nvme_command_handle command;
    struct fwlab_nvme_origin_token origin;
    uint8_t bytes[J0_MAX_TRANSFER_BYTES];
    uint32_t exact_bytes;
    uint8_t direction;
    uint8_t occupied;
    uint8_t reserved0[2];
};

struct j0_host_authority_record {
    struct fwlab_host_dma_mint_request_v0 request;
    struct fwlab_host_dma_authority_ref_v0 authority;
    uint16_t endpoint_index;
    uint8_t occupied;
    uint8_t released;
    uint32_t reserved0;
};

struct j0_dma_record {
    struct fwlab_dma_op_token_v0 operation;
    struct fwlab_dma_request_v0 request;
    struct fwlab_dma_status_v0 status;
    uint8_t occupied;
    uint8_t request_valid;
    uint8_t retire_started;
    uint8_t drained;
    uint8_t reserved0[4];
};

struct j0_host_data {
    struct fwlab_host_data_port_v0 port;
    struct j0_controller_buffer *buffer;
    struct j0_host_endpoint endpoint[J0_MAX_COMMANDS];
    struct j0_host_authority_record authority[J0_MAX_COMMANDS];
    struct j0_dma_record dma[J0_MAX_DMA_OPERATIONS];
    uint64_t next_authority_uid;
    uint64_t next_dma_uid;
    uint64_t close_lifecycle_nonce;
    uint32_t generation;
    uint32_t close_execution_epoch;
    uint32_t active_authorities;
    uint32_t active_dma;
    uint8_t admission_closed;
    uint8_t close_started;
    uint8_t poisoned;
    uint8_t reserved0;
};

struct j0_action_record {
    struct fwlab_host_action_token_v0 token;
    struct fwlab_host_action_argument_ref_v0 argument_ref;
    struct fwlab_spine_profile_argument_v0 argument;
    struct fwlab_host_action_status_v0 terminal;
    struct fwlab_dma_op_token_v0 dma_token;
    struct fwlab_dma_request_v0 dma_request;
    struct fwlab_dma_status_v0 dma_status;
    struct fwlab_block_request_v0 block_request;
    struct fwlab_block_status_v0 block_status;
    uint32_t state;
    uint8_t token_valid;
    uint8_t result_latched;
    uint8_t lower_token_valid;
    uint8_t authority_released;
};

struct j0_admission_record {
    struct fwlab_nvme_command command;
    struct fwlab_host_action_program_v0 program;
    struct fwlab_spine_profile_binding_v0 binding;
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_spine_profile_argument_v0
        argument[FWLAB_HOST_ACTION_V0_MAX_ACTIONS];
    struct fwlab_controller_buffer_lease_v0 buffer;
    struct fwlab_controller_buffer_span_v0 span;
    struct fwlab_host_dma_authority_ref_v0 authority;
    struct j0_action_record action[FWLAB_HOST_ACTION_V0_MAX_ACTIONS];
    uint32_t phase;
    uint32_t profile;
    uint32_t transfer_direction;
    uint32_t transfer_bytes;
    uint32_t original_failure;
    uint8_t transfer_copy[J0_MAX_TRANSFER_BYTES];
    uint8_t occupied;
    uint8_t buffer_held;
    uint8_t authority_held;
    uint8_t endpoint_held;
    uint8_t lifecycle_owned;
    uint8_t intent_seen;
    uint8_t close_reaped;
    uint8_t reserved0;
};

struct j0_lower_close_record {
    uint64_t lifecycle_instance_nonce;
    uint32_t execution_epoch;
    uint8_t started;
    uint8_t close_acked;
    uint8_t quiescent;
    uint8_t reserved0;
};

struct j0_driver_lane {
    struct j0_runtime *runtime;
    struct j0_lower_close_record local_close;
    uint16_t kind;
    uint16_t reserved0;
};

struct j0_runtime {
    uint64_t magic;
    struct j0_runtime_config config;
    struct j0_controller_buffer buffer;
    struct j0_host_data host;
    struct j0_host_binding host_binding;
    struct fwlab_host_action_driver_table_v0 drivers;
    struct j0_driver_lane lane[FWLAB_HOST_ACTION_V0_KIND_COUNT];
    struct fwlab_host_profile_adapter_v0 c43_adapter;
    struct fwlab_host_profile_adapter_v0 linux_adapter;
    struct fwlab_spine_profile_binding_v0 c43_binding;
    struct fwlab_spine_profile_binding_v0 linux_binding;
    struct fwlab_nfc_model *nfc_model;
    struct fwlab_nfc_provider nfc_provider;
    uint32_t nfc_trace_windows;
    struct fwlab_m3p *m3p;
    struct fwlab_block_service_v0 block;
    struct fwlab_block_namespace_ref_v0 namespace_ref;
    struct j0_admission_record admission[J0_MAX_COMMANDS];
    struct j0_lower_close_record host_close;
    struct j0_lower_close_record block_close;
    void *lifecycle_arena;
    void *c43_arena;
    void *linux_arena;
    void *nfc_arena;
    void *m3p_arena;
    uint64_t lifecycle_instance_nonce;
    uint64_t m3p_instance_nonce;
    uint64_t nfc_instance_nonce;
    uint64_t next_client_uid;
    uint32_t fair_cursor;
    uint32_t active_admissions;
    uint32_t retained_intents;
    uint8_t ready;
    uint8_t admission_closed;
    uint8_t close_started;
    uint8_t lifecycle_finished;
    uint8_t m3p_finished;
    uint8_t poisoned;
    uint8_t profiles_retired;
    uint8_t reserved0;
};

struct j0_close_status {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint32_t host_authorities;
    uint32_t dma_operations;
    uint32_t buffers;
    uint32_t block_operations;
    uint32_t pending;
    uint32_t pinned;
    uint32_t nfc_operations;
    uint8_t profiles_retired;
    uint8_t quiescent;
    uint8_t reserved1[6];
};

/* The profile constructors stay private to this J0 frontend. */
size_t fwlab_c43_p1_adapter_v0_arena_size(void);
size_t fwlab_c43_p1_adapter_v0_arena_alignment(void);
enum fwlab_spine_result_v0 fwlab_c43_p1_adapter_v0_init(
    void *arena, size_t arena_size, uint64_t instance_nonce,
    uint32_t generation, struct fwlab_host_profile_adapter_v0 *adapter);
enum fwlab_spine_result_v0 fwlab_c43_p1_binding_v0(
    const struct fwlab_host_profile_adapter_v0 *adapter, uint32_t role,
    struct fwlab_spine_profile_binding_v0 *binding);

size_t fwlab_linux_profile_v1_adapter_arena_size(void);
size_t fwlab_linux_profile_v1_adapter_arena_alignment(void);
enum fwlab_spine_result_v0 fwlab_linux_profile_v1_adapter_init(
    void *arena, size_t arena_size, uint64_t instance_nonce,
    uint32_t generation, struct fwlab_host_profile_adapter_v0 *adapter);
enum fwlab_spine_result_v0 fwlab_linux_profile_v1_binding_v0(
    const struct fwlab_host_profile_adapter_v0 *adapter, uint32_t role,
    struct fwlab_spine_profile_binding_v0 *binding);

void j0_controller_buffer_init(
    struct j0_controller_buffer *buffer, uint64_t issuer_nonce,
    uint32_t generation);

void j0_host_data_init(
    struct j0_host_data *host, struct j0_controller_buffer *buffer,
    uint64_t authority_issuer_nonce, uint64_t dma_issuer_nonce,
    uint32_t generation);
void j0_headless_host_binding(
    struct j0_host_data *host, struct j0_host_binding *binding);
enum fwlab_spine_result_v0 j0_host_endpoint_prepare(
    struct j0_host_data *host,
    const struct fwlab_nvme_command_handle *command,
    const struct fwlab_nvme_origin_token *origin, uint32_t direction,
    uint32_t exact_bytes, const uint8_t *input);
enum fwlab_spine_result_v0 j0_host_endpoint_read(
    const struct j0_host_data *host,
    const struct fwlab_nvme_command_handle *command,
    const struct fwlab_nvme_origin_token *origin, void *output,
    size_t output_size);
enum fwlab_spine_result_v0 j0_host_endpoint_release(
    struct j0_host_data *host,
    const struct fwlab_nvme_command_handle *command,
    const struct fwlab_nvme_origin_token *origin);

void j0_action_drivers_init(
    struct j0_runtime *runtime,
    struct fwlab_host_action_driver_table_v0 *drivers);
void j0_action_close_unaccepted(struct j0_admission_record *record);
int j0_action_all_drained(const struct j0_admission_record *record);

enum fwlab_spine_result_v0 j0_runtime_init(
    struct j0_runtime *runtime,
    const struct j0_runtime_config *config);
enum fwlab_spine_result_v0 j0_runtime_admit_start(
    struct j0_runtime *runtime, uint32_t profile,
    const struct fwlab_nvme_command *command,
    const struct j0_host_transfer *transfer,
    struct fwlab_spine_command_ticket_v0 *ticket);
enum fwlab_spine_result_v0 j0_runtime_admit_referenced(
    struct j0_runtime *runtime, uint32_t profile,
    const struct fwlab_nvme_command *command,
    struct fwlab_spine_command_ticket_v0 *ticket);
enum fwlab_spine_result_v0 j0_runtime_action_argument(
    struct j0_runtime *runtime, const struct fwlab_host_action_token_v0 *token,
    const struct fwlab_host_action_argument_ref_v0 *reference,
    struct fwlab_spine_profile_argument_v0 *argument);
enum fwlab_spine_result_v0 j0_runtime_action_result(
    struct j0_runtime *runtime, const struct fwlab_host_action_token_v0 *token,
    const struct fwlab_host_action_status_v0 *status);
enum fwlab_spine_result_v0 j0_runtime_step(
    struct j0_runtime *runtime, uint32_t budget, uint32_t *units);
enum fwlab_spine_result_v0 j0_runtime_intent_read(
    struct j0_runtime *runtime,
    const struct fwlab_spine_command_ticket_v0 *ticket,
    struct fwlab_nvme_completion_intent *intent);
enum fwlab_spine_result_v0 j0_runtime_host_read(
    struct j0_runtime *runtime,
    const struct fwlab_spine_command_ticket_v0 *ticket,
    void *output, size_t output_size);
enum fwlab_spine_result_v0 j0_runtime_close_start(
    struct j0_runtime *runtime);
enum fwlab_spine_result_v0 j0_runtime_close_query(
    struct j0_runtime *runtime, struct j0_close_status *status);
enum fwlab_spine_result_v0 j0_runtime_fini(struct j0_runtime *runtime);

enum fwlab_spine_result_v0 j0_runtime_publication_acquire(
    struct j0_runtime *runtime,
    const struct fwlab_spine_command_ticket_v0 *ticket,
    struct fwlab_completion_lease_v0 *lease,
    struct fwlab_nvme_completion_intent *intent);
enum fwlab_spine_result_v0 j0_runtime_publication_finish(
    struct j0_runtime *runtime,
    const struct fwlab_spine_command_ticket_v0 *ticket,
    const struct fwlab_completion_lease_v0 *lease, uint32_t decision);

int j0_handle_equal(
    const struct fwlab_nvme_command_handle *left,
    const struct fwlab_nvme_command_handle *right);
int j0_origin_equal(
    const struct fwlab_nvme_origin_token *left,
    const struct fwlab_nvme_origin_token *right);
int j0_action_token_equal(
    const struct fwlab_host_action_token_v0 *left,
    const struct fwlab_host_action_token_v0 *right);
int j0_bytes_zero(const void *value, size_t size);

#endif /* FWLAB_HEADLESS_J0_INTERNAL_H */
