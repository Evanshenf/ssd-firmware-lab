/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "native_owner.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <sys/ioctl.h>

#define OWNER_HEADER(value) do { \
    (value).version = FWLAB_OWNER_CONTROL_V0_VERSION; \
    (value).size = (uint16_t)sizeof(value); \
} while (0)

static void wire_init(struct native_owner *owner, uint32_t operation,
                       struct fwlab_m4_owner_message *message)
{
    memset(message, 0, sizeof(*message));
    message->version = FWLAB_M4_OWNER_VERSION;
    message->size = (uint32_t)sizeof(*message);
    message->operation = operation;
    message->function_nonce = owner->native->function_nonce;
    message->result = INT32_MIN;
    memcpy(message->binding_sha256, owner->port.stable.binding_manifest_sha256, 32);
}

static int wire_exchange(struct native_owner *owner,
                         struct fwlab_m4_owner_message *message)
{
    uint32_t operation = message->operation;

    if (ioctl(owner->native->descriptor, FWLAB_M4_OWNER_EXCHANGE, message) < 0) {
        message->result = INT32_MIN;
        return -errno;
    }
    if (message->version != FWLAB_M4_OWNER_VERSION || message->size != sizeof(*message) ||
        message->operation != operation || message->function_nonce != owner->native->function_nonce) {
        message->result = INT32_MIN;
        return -EPROTO;
    }
    return message->result;
}

static void state_decode(const struct fwlab_m4_owner_message *wire,
                          struct fwlab_owner_epoch_state_v0 *state)
{
    memset(state, 0, sizeof(*state));
    OWNER_HEADER(*state);
    state->function_instance_nonce = wire->function_nonce;
    state->owner_epoch = wire->owner_epoch;
    state->controller_epoch = wire->controller_epoch;
    state->execution_epoch = wire->execution_epoch;
    state->owner_kind = (uint8_t)wire->owner_kind;
    state->phase = (uint8_t)wire->phase;
    memcpy(state->binding_manifest_sha256, wire->binding_sha256, 32);
}

static int revoke_decode(const struct fwlab_m4_owner_message *wire,
                          struct fwlab_owner_revoke_status_v0 *status)
{
    struct fwlab_owner_transition_token_v0 *transition;

    memset(status, 0, sizeof(*status));
    OWNER_HEADER(*status);
    state_decode(wire, &status->current);
    transition = &status->transition;
    OWNER_HEADER(*transition);
    transition->type_tag = FWLAB_OWNER_TRANSITION_V0_TAG;
    transition->function_instance_nonce = wire->function_nonce;
    transition->transition_uid = wire->transition_uid;
    transition->old_owner_epoch = wire->old_owner_epoch;
    transition->no_owner_epoch = wire->owner_epoch;
    transition->old_controller_epoch = wire->old_controller_epoch;
    transition->old_execution_epoch = wire->old_execution_epoch;
    transition->old_owner_kind = (uint8_t)wire->old_owner_kind;
    transition->generation = wire->generation;
    if (wire->certificate_uid) {
        struct fwlab_owner_zero_certificate_v0 *certificate = &status->certificate;
        if (wire->proof_flags != FWLAB_M4_OWNER_PROOFS)
            return 0;
        status->certificate_valid = 1;
        OWNER_HEADER(*certificate);
        certificate->type_tag = FWLAB_OWNER_ZERO_CERTIFICATE_V0_TAG;
        certificate->transition = *transition;
        certificate->certificate_uid = wire->certificate_uid;
        certificate->generation = wire->generation;
        certificate->host_dma_authorities = wire->host_dma_authorities;
        certificate->mapping_refs = wire->mapping_refs;
        certificate->pin_refs = wire->pin_refs;
        certificate->dma_operations = wire->dma_operations;
        certificate->controller_buffer_leases = wire->controller_buffer_leases;
        certificate->lifecycle_commands = wire->lifecycle_commands;
        certificate->aggregate_block_operations = wire->aggregate_block_operations;
        certificate->completion_leases = wire->completion_leases;
        certificate->cqe_workers = wire->cqe_workers;
        certificate->irq_workers = wire->irq_workers;
        certificate->pba_pending_vectors = wire->pba_pending_vectors;
        certificate->sq_capture_closed = 1;
        certificate->capability_mint_closed = 1;
        certificate->ftl_epoch_quiescent = 1;
        certificate->nfc_epoch_quiescent = 1;
        certificate->routes_cleared = 1;
        certificate->bar_volatile_cleared = 1;
        memcpy(certificate->ftl_epoch_proof, wire->ftl_epoch_proof, sizeof(wire->ftl_epoch_proof));
        memcpy(certificate->nfc_epoch_proof, wire->nfc_epoch_proof, sizeof(wire->nfc_epoch_proof));
        memcpy(certificate->binding_manifest_sha256, wire->binding_sha256, 32);
    }
    return fwlab_owner_revoke_status_v0_valid(status);
}

static enum fwlab_spine_result_v0 quarantine(struct native_owner *owner)
{
    struct fwlab_m4_owner_message wire;

    owner->quarantined = 1;
    wire_init(owner, FWLAB_M4_OWNER_QUARANTINE, &wire);
    (void)wire_exchange(owner, &wire);
    return FWLAB_SPINE_V0_QUARANTINED;
}

static enum fwlab_spine_result_v0 owner_observe(
    void *opaque, struct fwlab_owner_epoch_state_v0 *state)
{
    struct native_owner *owner = opaque;
    struct fwlab_m4_owner_message wire;
    struct fwlab_owner_epoch_state_v0 observed;

    if (!state)
        return FWLAB_SPINE_V0_INVALID;
    wire_init(owner, FWLAB_M4_OWNER_OBSERVE, &wire);
    if (wire_exchange(owner, &wire))
        return FWLAB_SPINE_V0_IN_PROGRESS;
    state_decode(&wire, &observed);
    if (wire.owner_kind > 2 || wire.phase > 5 ||
        !fwlab_owner_epoch_state_v0_valid(&observed))
        return quarantine(owner);
    *state = observed;
    return FWLAB_SPINE_V0_OK;
}

static void revoke_wire(struct native_owner *owner, uint32_t operation,
                         struct fwlab_m4_owner_message *wire)
{
    const struct fwlab_owner_revoke_key_v0 *key = &owner->revoke_key;

    wire_init(owner, operation, wire);
    wire->client_uid = key->client_uid;
    wire->owner_epoch = key->expected_owner.owner_epoch;
    wire->owner_kind = key->expected_owner.owner_kind;
    wire->controller_epoch = key->expected_owner.controller_epoch;
    wire->execution_epoch = key->expected_owner.execution_epoch;
    wire->policy = key->policy;
}

static enum fwlab_spine_result_v0 revoke_reconcile(struct native_owner *owner,
                                                  uint32_t operation)
{
    struct fwlab_m4_owner_message wire;
    struct fwlab_owner_revoke_status_v0 status;
    int result;

    revoke_wire(owner, operation, &wire);
    result = wire_exchange(owner, &wire);
    if (result) {
        if (wire.result == INT32_MIN)
            return FWLAB_SPINE_V0_IN_PROGRESS;
        owner->revoke_pending = 0;
        return FWLAB_SPINE_V0_STALE;
    }
    if (!revoke_decode(&wire, &status))
        return quarantine(owner);
    if (!owner->revoke_known) {
        struct j0_runtime *runtime = owner->native->runtime;
        if (!runtime || owner->native->epoch != status.transition.old_execution_epoch)
            return quarantine(owner);
        owner->expected_ftl_nonce = runtime->m3p_instance_nonce;
        owner->expected_nfc_nonce = runtime->nfc_instance_nonce;
        if (j0_runtime_close_start(runtime) != FWLAB_SPINE_V0_OK)
            return quarantine(owner);
    }
    owner->revoke_status = status;
    owner->revoke_known = 1;
    owner->revoke_pending = 0;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 owner_revoke_query(
    void *opaque, const struct fwlab_owner_revoke_key_v0 *key,
    struct fwlab_owner_revoke_status_v0 *status)
{
    struct native_owner *owner = opaque;
    enum fwlab_spine_result_v0 result;

    if (!status || !fwlab_owner_revoke_key_v0_valid(key))
        return FWLAB_SPINE_V0_INVALID;
    if (memcmp(key, &owner->revoke_key, sizeof(*key)))
        return FWLAB_SPINE_V0_STALE;
    result = revoke_reconcile(owner, FWLAB_M4_OWNER_REVOKE_QUERY);
    if (result == FWLAB_SPINE_V0_OK)
        *status = owner->revoke_status;
    return result;
}

static enum fwlab_spine_result_v0 owner_revoke_start(
    void *opaque, const struct fwlab_owner_revoke_key_v0 *key,
    struct fwlab_owner_revoke_status_v0 *status)
{
    struct native_owner *owner = opaque;
    enum fwlab_spine_result_v0 result;

    if (!status || !fwlab_owner_revoke_key_v0_valid(key) ||
        key->policy != FWLAB_OWNER_V0_DRAIN_ONLY ||
        key->expected_owner.function_instance_nonce != owner->port.stable.function_instance_nonce ||
        memcmp(key->expected_owner.binding_manifest_sha256,
               owner->port.stable.binding_manifest_sha256, 32))
        return FWLAB_SPINE_V0_INVALID;
    if (owner->quarantined)
        return FWLAB_SPINE_V0_QUARANTINED;
    if (owner->revoke_key.client_uid == key->client_uid)
        return owner_revoke_query(owner, key, status);
    if (owner->revoke_pending || owner->grant_pending ||
        (owner->revoke_known && !owner->revoke_status.certificate_valid))
        return FWLAB_SPINE_V0_WRONG_STATE;
    owner->revoke_key = *key;
    owner->revoke_pending = 1;
    owner->revoke_known = 0;
    owner->grant_known = 0;
    result = revoke_reconcile(owner, FWLAB_M4_OWNER_REVOKE);
    if (result == FWLAB_SPINE_V0_OK)
        *status = owner->revoke_status;
    return result;
}

static enum fwlab_spine_result_v0 owner_drain_step(
    void *opaque, const struct fwlab_owner_transition_token_v0 *transition,
    uint32_t budget, struct fwlab_owner_step_result_v0 *result)
{
    struct native_owner *owner = opaque;
    struct native_context *native = owner->native;
    struct fwlab_m4_owner_message wire;
    struct fwlab_owner_step_result_v0 step = { 0 };
    enum fwlab_spine_result_v0 progress;

    if (!result || !budget || !fwlab_owner_transition_token_v0_valid(transition))
        return FWLAB_SPINE_V0_INVALID;
    if (!owner->revoke_known || memcmp(transition, &owner->revoke_status.transition, sizeof(*transition)))
        return FWLAB_SPINE_V0_STALE;
    OWNER_HEADER(step);
    step.requested_budget = budget;
    if (owner->revoke_status.certificate_valid) {
        *result = step;
        return FWLAB_SPINE_V0_OK;
    }
    progress = native_runtime_close_step(native, 1);
    step.units_executed = 1;
    if (progress != FWLAB_SPINE_V0_OK && progress != FWLAB_SPINE_V0_IN_PROGRESS)
        return quarantine(owner);
    if (progress == FWLAB_SPINE_V0_IN_PROGRESS) {
        *result = step;
        return FWLAB_SPINE_V0_OK;
    }
    if (native->last_closed_epoch != transition->old_execution_epoch ||
        native->last_ftl_nonce != owner->expected_ftl_nonce ||
        native->last_nfc_nonce != owner->expected_nfc_nonce ||
        !native->last_closed.quiescent || !native->last_closed.profiles_retired ||
        native->last_closed.host_authorities || native->last_closed.dma_operations ||
        native->last_closed.buffers || native->last_closed.block_operations ||
        native->last_closed.nfc_operations || native->last_closed.pending || native->last_closed.pinned)
        return quarantine(owner);
    wire_init(owner, FWLAB_M4_OWNER_CERTIFY, &wire);
    wire.owner_epoch = transition->no_owner_epoch;
    wire.transition_uid = transition->transition_uid;
    wire.proof_flags = FWLAB_M4_OWNER_PROOFS;
    wire.ftl_epoch_proof[0] = native->last_ftl_nonce;
    wire.ftl_epoch_proof[1] = native->last_closed_epoch;
    wire.nfc_epoch_proof[0] = native->last_nfc_nonce;
    wire.nfc_epoch_proof[1] = native->last_closed_epoch;
    if (wire_exchange(owner, &wire)) {
        if (wire.result != INT32_MIN && wire.result != -EBUSY)
            return quarantine(owner);
        *result = step;
        return FWLAB_SPINE_V0_OK;
    }
    if (!revoke_decode(&wire, &owner->revoke_status) || !owner->revoke_status.certificate_valid)
        return quarantine(owner);
    step.transitions = 1;
    *result = step;
    return FWLAB_SPINE_V0_OK;
}

static void grant_wire(struct native_owner *owner, uint32_t operation,
                        struct fwlab_m4_owner_message *wire)
{
    const struct fwlab_owner_grant_key_v0 *key = &owner->grant_key;

    wire_init(owner, operation, wire);
    wire->client_uid = key->client_uid;
    wire->owner_epoch = key->transition.no_owner_epoch;
    wire->transition_uid = key->transition.transition_uid;
    wire->certificate_uid = key->certificate.certificate_uid;
    wire->target_owner = key->target_owner;
    wire->controller_epoch = key->transition.old_controller_epoch + 1;
    wire->execution_epoch = key->transition.old_execution_epoch + 1;
}

static enum fwlab_spine_result_v0 grant_reconcile(struct native_owner *owner,
                                                 uint32_t operation)
{
    struct fwlab_m4_owner_message wire;
    struct fwlab_owner_grant_status_v0 status = { 0 };
    int result;

    grant_wire(owner, operation, &wire);
    result = wire_exchange(owner, &wire);
    if (result) {
        if (wire.result == INT32_MIN)
            return FWLAB_SPINE_V0_IN_PROGRESS;
        /* An indeterminate start is resolved by query, never another grant.
         * A definite rejection leaves the prepared, non-Host runtime to be
         * closed by service before any later grant can be considered. */
        owner->grant_pending = 0;
        return FWLAB_SPINE_V0_STALE;
    }
    OWNER_HEADER(status);
    state_decode(&wire, &status.current);
    if (!fwlab_owner_grant_status_v0_matches_key_for_port(&status, &owner->grant_key, &owner->port))
        return quarantine(owner);
    owner->grant_status = status;
    owner->grant_pending = 0;
    owner->grant_known = 1;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 owner_grant_query(
    void *opaque, const struct fwlab_owner_grant_key_v0 *key,
    struct fwlab_owner_grant_status_v0 *status)
{
    struct native_owner *owner = opaque;
    enum fwlab_spine_result_v0 result;

    if (!status || !fwlab_owner_grant_key_v0_valid_for_port(key, &owner->port))
        return FWLAB_SPINE_V0_INVALID;
    if (memcmp(key, &owner->grant_key, sizeof(*key)))
        return FWLAB_SPINE_V0_STALE;
    result = grant_reconcile(owner, FWLAB_M4_OWNER_GRANT_QUERY);
    if (result == FWLAB_SPINE_V0_OK)
        *status = owner->grant_status;
    return result;
}

static enum fwlab_spine_result_v0 owner_grant_start(
    void *opaque, const struct fwlab_owner_grant_key_v0 *key,
    struct fwlab_owner_grant_status_v0 *status)
{
    struct native_owner *owner = opaque;
    enum fwlab_spine_result_v0 result;

    if (!status || !fwlab_owner_grant_key_v0_valid_for_port(key, &owner->port))
        return FWLAB_SPINE_V0_INVALID;
    if (owner->quarantined)
        return FWLAB_SPINE_V0_QUARANTINED;
    if (owner->grant_key.client_uid == key->client_uid)
        return owner_grant_query(owner, key, status);
    if (owner->revoke_pending || owner->grant_pending || owner->native->runtime ||
        !owner->revoke_status.certificate_valid ||
        memcmp(&key->certificate, &owner->revoke_status.certificate, sizeof(key->certificate)))
        return FWLAB_SPINE_V0_WRONG_STATE;
    owner->grant_key = *key;
    owner->grant_pending = 1;
    owner->grant_known = 0;
    owner->native->epoch = key->transition.old_execution_epoch + 1;
    if (!native_runtime_create(owner->native, owner->media, 0))
        return quarantine(owner);
    result = grant_reconcile(owner, FWLAB_M4_OWNER_GRANT);
    if (result == FWLAB_SPINE_V0_OK)
        *status = owner->grant_status;
    return result;
}

static const struct fwlab_owner_control_ops_v0 owner_ops = {
    .version = FWLAB_OWNER_CONTROL_V0_VERSION, .size = sizeof(struct fwlab_owner_control_ops_v0),
    .revoke_start = owner_revoke_start, .revoke_query = owner_revoke_query,
    .drain_step = owner_drain_step, .grant_start = owner_grant_start,
    .grant_query = owner_grant_query, .observe = owner_observe,
};

int native_owner_init(struct native_owner *owner, struct native_context *native,
                      struct native_media *media)
{
    struct fwlab_m4_owner_message wire;

    if (!owner || !native || !media || !native->runtime)
        return 0;
    memset(owner, 0, sizeof(*owner));
    owner->native = native;
    owner->media = media;
    wire_init(owner, FWLAB_M4_OWNER_OBSERVE, &wire);
    if (wire_exchange(owner, &wire))
        return 0;
    owner->port.ops = &owner_ops;
    owner->port.context = owner;
    owner->port.generation = wire.generation;
    OWNER_HEADER(owner->port.stable);
    owner->port.stable.function_instance_nonce = wire.function_nonce;
    memcpy(owner->port.stable.media_uuid, wire.media_uuid, 16);
    owner->port.stable.media_format_version = wire.media_format_version;
    memcpy(owner->port.stable.binding_manifest_sha256, wire.binding_sha256, 32);
    return fwlab_owner_control_port_v0_valid(&owner->port);
}

int native_owner_blocks_commands(const struct native_owner *owner)
{
    return owner && (owner->quarantined || owner->revoke_pending || owner->grant_pending ||
                     (owner->revoke_known && !owner->grant_known));
}

enum fwlab_spine_result_v0 native_owner_service(struct native_owner *owner)
{
    if (owner->quarantined)
        return FWLAB_SPINE_V0_QUARANTINED;
    if (owner->revoke_pending)
        return revoke_reconcile(owner, FWLAB_M4_OWNER_REVOKE_QUERY);
    if (owner->grant_pending)
        return grant_reconcile(owner, FWLAB_M4_OWNER_GRANT_QUERY);
    if (owner->revoke_known && !owner->revoke_status.certificate_valid) {
        struct fwlab_owner_step_result_v0 result;
        return owner_drain_step(owner, &owner->revoke_status.transition, 1, &result);
    }
    if (owner->revoke_known && owner->revoke_status.certificate_valid &&
        !owner->grant_known && owner->native->runtime) {
        enum fwlab_spine_result_v0 result = native_runtime_close_step(owner->native, 1);
        return result == FWLAB_SPINE_V0_OK || result == FWLAB_SPINE_V0_IN_PROGRESS
                   ? FWLAB_SPINE_V0_OK : quarantine(owner);
    }
    return FWLAB_SPINE_V0_OK;
}
