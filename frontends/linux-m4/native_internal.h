/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_LINUX_M4_NATIVE_INTERNAL_H
#define FWLAB_LINUX_M4_NATIVE_INTERNAL_H

#include "../headless-j0/j0_internal.h"
#include "fwlab/unstable/m4_native.h"
#include "fwlab/unstable/m4_canary_native.h"

#define NATIVE_COMMANDS 32u

struct native_media {
    int directory_fd;
    void *arena;
    struct fwlab_file_nand_v0 *file;
    struct fwlab_file_nand_holder_v0 holder;
    uint8_t uuid[16];
};

struct native_slot {
    struct fwlab_m4_native_message capture;
    struct fwlab_nvme_command command;
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_completion_lease_v0 completion;
    struct fwlab_nvme_completion_intent intent;
    struct fwlab_host_dma_authority_ref_v0 authority;
    struct fwlab_dma_op_token_v0 dma_token;
    struct fwlab_dma_request_v0 dma_request;
    struct fwlab_dma_status_v0 dma_status;
    struct fwlab_host_action_token_v0 queue_token;
    struct fwlab_host_action_argument_ref_v0 queue_reference;
    struct fwlab_spine_profile_argument_v0 queue_argument;
    struct fwlab_host_action_status_v0 queue_status;
    uint64_t authority_uid;
    uint64_t dma_uid;
    uint32_t bytes;
    uint32_t direction;
    uint8_t occupied;
    uint8_t admitted;
    uint8_t completion_acquired;
    uint8_t publication_started;
    uint8_t publication_known;
    uint8_t firmware_retired;
    uint8_t authority_live;
    uint8_t token_reserved;
    uint8_t dma_submitted;
    uint8_t dma_terminal;
    uint8_t dma_retire_started;
    uint8_t dma_drained;
    uint8_t queue_valid;
    uint8_t queue_terminal;
    uint8_t queue_retire_started;
    uint8_t queue_drained;
    uint8_t reserved;
    uint8_t bounce[FWLAB_M4_NATIVE_MAX_BYTES];
};

struct native_context {
    struct j0_runtime *runtime;
    struct native_slot slot[NATIVE_COMMANDS];
    struct fwlab_controller_buffer_port_v0 buffer;
    uint64_t function_nonce;
    uint64_t authority_issuer;
    uint64_t dma_issuer;
    uint64_t close_nonce;
    uint32_t epoch;
    uint32_t generation;
    uint32_t close_epoch;
    struct j0_close_status last_closed;
    uint64_t last_ftl_nonce;
    uint64_t last_nfc_nonce;
    uint32_t last_closed_epoch;
    uint64_t next_runtime_seed;
    struct {
        struct fwlab_spine_command_ticket_v0 ticket;
        struct fwlab_completion_lease_v0 lease;
        uint64_t held_origin;
        uint64_t new_lease_uid;
        uint8_t capture, saved, hold;
        uint8_t probe_bytes[4096];
    } canary;
    int descriptor;
    uint8_t closing;
};

void native_message_init(struct native_context *context,
                        const struct native_slot *slot, uint32_t operation,
                        struct fwlab_m4_native_message *message);
int native_exchange(struct native_context *context,
                    struct fwlab_m4_native_message *message);
enum fwlab_spine_result_v0 native_host_bind(
    void *context, const struct fwlab_controller_buffer_port_v0 *buffer,
    uint32_t generation, struct j0_host_binding *binding);
int native_runtime_create(struct native_context *context,
                          struct native_media *media, int format);
enum fwlab_spine_result_v0 native_runtime_close_step(
    struct native_context *context, uint32_t budget);
int native_canary_control(struct native_context *context,
                          struct fwlab_m4_canary_message *message);

#endif /* FWLAB_LINUX_M4_NATIVE_INTERNAL_H */
