/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CONTRACTS_OWNER_CONTROL_V0_H
#define FWLAB_CONTRACTS_OWNER_CONTROL_V0_H

#include <stdint.h>

#include "fwlab/portable/host_action_program_v0.h"

#define FWLAB_OWNER_CONTROL_V0_VERSION 1u
#define FWLAB_OWNER_TRANSITION_V0_TAG UINT32_C(0x4f575452)
#define FWLAB_OWNER_ZERO_CERTIFICATE_V0_TAG UINT32_C(0x4f57435a)

enum fwlab_owner_kind_v0 {
    FWLAB_OWNER_V0_NONE = 0,
    FWLAB_OWNER_V0_HOST_NATIVE = 1,
    FWLAB_OWNER_V0_VFIO = 2
};

enum fwlab_owner_phase_v0 {
    FWLAB_OWNER_V0_OWNED = 1,
    FWLAB_OWNER_V0_REVOKING = 2,
    FWLAB_OWNER_V0_DRAINING = 3,
    FWLAB_OWNER_V0_NO_OWNER = 4,
    FWLAB_OWNER_V0_QUARANTINED = 5
};

enum fwlab_owner_revoke_policy_v0 {
    FWLAB_OWNER_V0_DRAIN_ONLY = 1,
    FWLAB_OWNER_V0_REQUIRE_DURABLE_FRONTIER = 2
};

struct fwlab_owner_stable_identity_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint64_t function_instance_nonce;
    uint8_t media_uuid[16];
    uint32_t media_format_version;
    uint32_t reserved1;
    uint8_t binding_manifest_sha256[32];
    uint32_t reserved2[4];
};

struct fwlab_owner_epoch_state_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint64_t function_instance_nonce;
    uint64_t owner_epoch;
    uint32_t controller_epoch;
    uint32_t execution_epoch;
    uint8_t owner_kind;
    uint8_t phase;
    uint8_t reserved1[6];
    uint8_t binding_manifest_sha256[32];
    uint32_t reserved2[4];
};

struct fwlab_owner_revoke_key_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_owner_epoch_state_v0 expected_owner;
    uint64_t client_uid;
    uint32_t policy;
    uint32_t reserved1[3];
};

struct fwlab_owner_transition_token_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t type_tag;
    uint64_t function_instance_nonce;
    uint64_t transition_uid;
    uint64_t old_owner_epoch;
    uint64_t no_owner_epoch;
    uint32_t old_controller_epoch;
    uint32_t old_execution_epoch;
    uint32_t generation;
    uint8_t old_owner_kind;
    uint8_t reserved0[3];
    uint32_t reserved1[4];
};

struct fwlab_owner_zero_certificate_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t type_tag;
    struct fwlab_owner_transition_token_v0 transition;
    uint64_t certificate_uid;
    uint32_t generation;
    uint32_t host_dma_authorities;
    uint32_t mapping_refs;
    uint32_t pin_refs;
    uint32_t dma_operations;
    uint32_t controller_buffer_leases;
    uint32_t lifecycle_commands;
    uint32_t aggregate_block_operations;
    uint32_t completion_leases;
    uint32_t cqe_workers;
    uint32_t irq_workers;
    uint32_t pba_pending_vectors;
    uint8_t sq_capture_closed;
    uint8_t capability_mint_closed;
    uint8_t ftl_epoch_quiescent;
    uint8_t nfc_epoch_quiescent;
    uint8_t routes_cleared;
    uint8_t bar_volatile_cleared;
    uint8_t reserved0[2];
    uint64_t ftl_epoch_proof[2];
    uint64_t nfc_epoch_proof[2];
    uint8_t binding_manifest_sha256[32];
    uint32_t reserved1[4];
};

struct fwlab_owner_revoke_status_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_owner_transition_token_v0 transition;
    struct fwlab_owner_epoch_state_v0 current;
    uint32_t fault_domain;
    uint32_t fault_code;
    uint8_t certificate_valid;
    uint8_t reserved1[3];
    struct fwlab_owner_zero_certificate_v0 certificate;
    uint32_t reserved2[4];
};

struct fwlab_owner_grant_key_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_owner_transition_token_v0 transition;
    struct fwlab_owner_zero_certificate_v0 certificate;
    uint64_t client_uid;
    uint8_t target_owner;
    uint8_t reserved1[7];
    uint32_t reserved2[4];
};

struct fwlab_owner_grant_status_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_owner_epoch_state_v0 current;
    uint32_t fault_domain;
    uint32_t fault_code;
    uint32_t reserved1[4];
};

struct fwlab_owner_step_result_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint32_t requested_budget;
    uint32_t units_executed;
    uint32_t transitions;
    uint32_t reserved1[4];
};

typedef enum fwlab_spine_result_v0
(*fwlab_owner_revoke_fn_v0)(
    void *context,
    const struct fwlab_owner_revoke_key_v0 *key,
    struct fwlab_owner_revoke_status_v0 *status
);

typedef enum fwlab_spine_result_v0
(*fwlab_owner_drain_step_fn_v0)(
    void *context,
    const struct fwlab_owner_transition_token_v0 *transition,
    uint32_t budget,
    struct fwlab_owner_step_result_v0 *result
);

typedef enum fwlab_spine_result_v0
(*fwlab_owner_grant_fn_v0)(
    void *context,
    const struct fwlab_owner_grant_key_v0 *key,
    struct fwlab_owner_grant_status_v0 *status
);

typedef enum fwlab_spine_result_v0
(*fwlab_owner_observe_fn_v0)(
    void *context,
    struct fwlab_owner_epoch_state_v0 *state
);

struct fwlab_owner_control_ops_v0 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    fwlab_owner_revoke_fn_v0 revoke_start;
    fwlab_owner_revoke_fn_v0 revoke_query;
    fwlab_owner_drain_step_fn_v0 drain_step;
    fwlab_owner_grant_fn_v0 grant_start;
    fwlab_owner_grant_fn_v0 grant_query;
    fwlab_owner_observe_fn_v0 observe;
    uint32_t reserved1[4];
};

/*
 * revoke_start is the only owner-epoch increment: no_owner_epoch is exactly
 * old_owner_epoch + 1.  grant_start reuses no_owner_epoch and creates fresh
 * monotonic controller/execution successor epochs only on its first successful
 * call.  A UINT32_MAX predecessor fails closed.  Exact-key start/query retries
 * recover the retained result without advancing identity.
 * grant_start validates the retained certificate; caller bytes alone confer
 * no authority.
 */

struct fwlab_owner_control_port_v0 {
    const struct fwlab_owner_control_ops_v0 *ops;
    void *context;
    struct fwlab_owner_stable_identity_v0 stable;
    uint64_t generation;
    uint32_t reserved[4];
};

int fwlab_owner_stable_identity_v0_valid(
    const struct fwlab_owner_stable_identity_v0 *identity
);
int fwlab_owner_epoch_state_v0_valid(
    const struct fwlab_owner_epoch_state_v0 *state
);
int fwlab_owner_revoke_key_v0_valid(
    const struct fwlab_owner_revoke_key_v0 *key
);
int fwlab_owner_transition_token_v0_valid(
    const struct fwlab_owner_transition_token_v0 *transition
);
int fwlab_owner_zero_certificate_v0_valid(
    const struct fwlab_owner_zero_certificate_v0 *certificate
);
int fwlab_owner_revoke_status_v0_valid(
    const struct fwlab_owner_revoke_status_v0 *status
);
int fwlab_owner_grant_key_v0_valid(
    const struct fwlab_owner_grant_key_v0 *key
);
int fwlab_owner_grant_key_v0_valid_for_port(
    const struct fwlab_owner_grant_key_v0 *key,
    const struct fwlab_owner_control_port_v0 *port
);
int fwlab_owner_grant_status_v0_valid(
    const struct fwlab_owner_grant_status_v0 *status
);
int fwlab_owner_grant_status_v0_matches_key(
    const struct fwlab_owner_grant_status_v0 *status,
    const struct fwlab_owner_grant_key_v0 *key
);
int fwlab_owner_grant_status_v0_matches_key_for_port(
    const struct fwlab_owner_grant_status_v0 *status,
    const struct fwlab_owner_grant_key_v0 *key,
    const struct fwlab_owner_control_port_v0 *port
);
int fwlab_owner_step_result_v0_valid(
    const struct fwlab_owner_step_result_v0 *result
);
int fwlab_owner_control_ops_v0_valid(
    const struct fwlab_owner_control_ops_v0 *ops
);
int fwlab_owner_control_port_v0_valid(
    const struct fwlab_owner_control_port_v0 *port
);

#endif /* FWLAB_CONTRACTS_OWNER_CONTROL_V0_H */
