/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c34_internal.h"

#include <string.h>

static uint64_t binding_digest(
    const uint8_t main[C34_MAIN_BYTES],
    const uint8_t oob[C34_OOB_BYTES]
)
{
    uint64_t hash = c34_hash_bytes(UINT64_C(1469598103934665603),
                                   main, C34_MAIN_BYTES);

    return c34_hash_bytes(hash, oob, C34_OOB_BYTES);
}

static uint64_t c33_payload_digest(
    const uint8_t main[C34_MAIN_BYTES],
    const uint8_t oob[C34_OOB_BYTES]
)
{
    uint64_t main_hash = c34_hash_bytes(UINT64_C(1469598103934665603),
                                        main, C34_MAIN_BYTES);
    uint64_t oob_hash = c34_hash_bytes(UINT64_C(1469598103934665603),
                                       oob, C34_OOB_BYTES);
    uint8_t bytes[8];
    unsigned int index;

    for (index = 0; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(oob_hash >> (index * 8u));
    }
    return main_hash ^ c34_hash_bytes(UINT64_C(1469598103934665603),
                                      bytes, sizeof(bytes));
}

static enum c34_result next_inner(
    struct c34 *instance,
    struct fwlab_nfc_operation_token *token
)
{
    uint64_t uid = instance->next_inner_uid;

    if (uid == 0 || uid > instance->config.inner_uid_limit ||
        uid > UINT32_MAX) {
        return C34_COUNTER_EXHAUSTED;
    }
    memset(token, 0, sizeof(*token));
    token->instance_nonce = instance->config.instance_nonce;
    token->operation_uid = uid;
    token->controller_epoch = instance->current_epoch;
    token->generation = (uint32_t)uid;
    ++instance->next_inner_uid;
    return C34_OK;
}

static struct fwlab_nfc_buffer_ref main_ref(const struct c34 *instance)
{
    struct fwlab_nfc_buffer_ref reference;

    memset(&reference, 0, sizeof(reference));
    reference.controller_region = instance->config.controller_region;
    reference.offset = instance->config.controller_buffer_offset;
    reference.length = C34_MAIN_BYTES;
    return reference;
}

static struct fwlab_nfc_buffer_ref oob_ref(const struct c34 *instance)
{
    struct fwlab_nfc_buffer_ref reference;

    memset(&reference, 0, sizeof(reference));
    reference.controller_region = instance->config.controller_region;
    reference.offset = instance->config.controller_buffer_offset +
                       C34_MAIN_BYTES;
    reference.length = C34_OOB_BYTES;
    return reference;
}

static enum c34_result submit_request(
    struct c34 *instance,
    struct fwlab_nfc_request *request
)
{
    struct fwlab_nfc_submit_result submitted =
        instance->nfc.ops->try_submit(instance->nfc.context, request);

    if (submitted.disposition == FWLAB_NFC_BACKPRESSURE) {
        return C34_NO_CAPACITY;
    }
    if (submitted.disposition != FWLAB_NFC_ACCEPTED) {
        return C34_MEDIA_FAILURE;
    }
    instance->graph.inner = request->operation;
    instance->graph.operation_ppa = request->ppa;
    instance->graph.inner_pending = 1;
    return C34_OK;
}

static enum c34_result request_base(
    struct c34 *instance,
    uint8_t kind,
    const struct fwlab_nfc_ppa *ppa,
    struct fwlab_nfc_request *request
)
{
    enum c34_result result;

    memset(request, 0, sizeof(*request));
    request->version = FWLAB_NFC_CONTRACT_VERSION;
    request->size = sizeof(*request);
    result = next_inner(instance, &request->operation);
    if (result != C34_OK) {
        return result;
    }
    request->ppa = *ppa;
    request->kind = kind;
    request->cookie = UINT64_C(0xc340000000000000) |
                      request->operation.operation_uid;
    return C34_OK;
}

enum c34_result c34_nfc_program_transfer(
    struct c34 *instance,
    const struct fwlab_nfc_ppa *ppa
)
{
    struct fwlab_nfc_request request;
    struct fwlab_nfc_buffer_ref main = main_ref(instance);
    struct fwlab_nfc_buffer_ref oob = oob_ref(instance);
    enum c34_result result;

    if (!c34_instance_valid(instance) || ppa == NULL ||
        instance->graph.inner_pending) {
        return C34_INVALID_CONTRACT;
    }
    if (instance->buffers.ops->write(
            instance->buffers.context, &main, instance->graph.main,
            C34_MAIN_BYTES) != FWLAB_NFC_API_OK ||
        instance->buffers.ops->write(
            instance->buffers.context, &oob, instance->graph.oob,
            C34_OOB_BYTES) != FWLAB_NFC_API_OK) {
        return C34_INVARIANT_FAILURE;
    }
    result = request_base(
        instance, FWLAB_NFC_PROGRAM_TRANSFER, ppa, &request);
    if (result != C34_OK) {
        return result;
    }
    request.region_mask = FWLAB_NFC_REGION_MASK;
    request.main = main;
    request.oob = oob;
    return submit_request(instance, &request);
}

static enum c34_result bind_physical(
    struct c34 *instance,
    const struct fwlab_nfc_operation_token *inner,
    const struct fwlab_nfc_ppa *ppa,
    uint8_t kind,
    uint64_t digest,
    uint32_t main_length,
    uint32_t oob_length
)
{
    struct c34_physical_binding binding;
    enum c34_physical_txn_result result;

    if (instance->next_physical_op_id == 0 ||
        instance->next_physical_op_id > instance->config.physical_op_limit ||
        instance->next_physical_sequence == 0 ||
        instance->next_physical_sequence >
            instance->config.physical_sequence_limit) {
        return C34_COUNTER_EXHAUSTED;
    }
    memset(&binding, 0, sizeof(binding));
    binding.version = C34_PHYSICAL_TXN_VERSION;
    binding.size = sizeof(binding);
    binding.physical_op_id = instance->next_physical_op_id;
    binding.commit_sequence = instance->next_physical_sequence;
    binding.inner = *inner;
    binding.outer = instance->graph.outer;
    binding.mutation = instance->graph.persist_request.token;
    binding.ppa = *ppa;
    binding.payload_digest = digest;
    binding.main_length = main_length;
    binding.oob_length = oob_length;
    binding.operation_kind = kind;
    result = instance->physical.ops->bind(
        instance->physical.context, &binding);
    if (result == C34_PHYSICAL_TXN_BUSY) {
        return C34_NO_CAPACITY;
    }
    if (result != C34_PHYSICAL_TXN_OK) {
        return C34_INVARIANT_FAILURE;
    }
    ++instance->next_physical_op_id;
    ++instance->next_physical_sequence;
    instance->graph.physical_bound = 1;
    return C34_OK;
}

enum c34_result c34_nfc_program_execute(struct c34 *instance)
{
    struct fwlab_nfc_request request;
    uint64_t digest;
    enum c34_result result;

    if (!c34_instance_valid(instance) || instance->graph.inner_pending ||
        instance->graph.physical_bound) {
        return C34_INVALID_CONTRACT;
    }
    result = request_base(
        instance, FWLAB_NFC_PROGRAM_EXECUTE,
        &instance->graph.operation_ppa, &request);
    if (result != C34_OK) {
        return result;
    }
    request.region_mask = FWLAB_NFC_REGION_MASK;
    request.cache = instance->graph.cache;
    digest = binding_digest(instance->graph.main, instance->graph.oob);
    result = bind_physical(
        instance, &request.operation, &request.ppa,
        FWLAB_NFC_PROGRAM_EXECUTE, digest, C34_MAIN_BYTES, C34_OOB_BYTES);
    if (result != C34_OK) {
        return result;
    }
    result = submit_request(instance, &request);
    if (result != C34_OK) {
        (void)instance->physical.ops->abandon(
            instance->physical.context, &request.operation);
        instance->graph.physical_bound = 0;
    }
    return result;
}

enum c34_result c34_nfc_read_trigger(
    struct c34 *instance,
    const struct fwlab_nfc_ppa *ppa
)
{
    struct fwlab_nfc_request request;
    enum c34_result result;

    if (!c34_instance_valid(instance) || ppa == NULL ||
        instance->graph.inner_pending) {
        return C34_INVALID_CONTRACT;
    }
    result = request_base(
        instance, FWLAB_NFC_READ_TRIGGER, ppa, &request);
    if (result != C34_OK) {
        return result;
    }
    request.region_mask = FWLAB_NFC_REGION_MASK;
    return submit_request(instance, &request);
}

enum c34_result c34_nfc_read_transfer(struct c34 *instance)
{
    struct fwlab_nfc_request request;
    enum c34_result result;

    if (!c34_instance_valid(instance) || instance->graph.inner_pending) {
        return C34_INVALID_CONTRACT;
    }
    result = request_base(
        instance, FWLAB_NFC_READ_TRANSFER,
        &instance->graph.operation_ppa, &request);
    if (result != C34_OK) {
        return result;
    }
    request.region_mask = FWLAB_NFC_REGION_MASK;
    request.cache = instance->graph.cache;
    request.main = main_ref(instance);
    request.oob = oob_ref(instance);
    return submit_request(instance, &request);
}

enum c34_result c34_nfc_erase(
    struct c34 *instance,
    const struct fwlab_nfc_ppa *ppa
)
{
    struct fwlab_nfc_request request;
    struct fwlab_nfc_ppa block;
    enum c34_result result;

    if (!c34_instance_valid(instance) || ppa == NULL ||
        instance->graph.inner_pending || instance->graph.physical_bound) {
        return C34_INVALID_CONTRACT;
    }
    block = *ppa;
    block.page = 0;
    result = request_base(instance, FWLAB_NFC_ERASE, &block, &request);
    if (result != C34_OK) {
        return result;
    }
    result = bind_physical(
        instance, &request.operation, &request.ppa, FWLAB_NFC_ERASE, 0, 0,
        0);
    if (result != C34_OK) {
        return result;
    }
    result = submit_request(instance, &request);
    if (result != C34_OK) {
        (void)instance->physical.ops->abandon(
            instance->physical.context, &request.operation);
        instance->graph.physical_bound = 0;
    }
    return result;
}

enum c34_result c34_nfc_progress(
    struct c34 *instance,
    bool *completed
)
{
    struct fwlab_nfc_step_result step;
    struct fwlab_nfc_completion completion;
    uint32_t count = 0;

    if (!c34_instance_valid(instance) || completed == NULL ||
        !instance->graph.inner_pending) {
        return C34_INVALID_CONTRACT;
    }
    *completed = false;
    if (instance->nfc.ops->step(
            instance->nfc.context, 1, &step) != FWLAB_NFC_API_OK ||
        instance->nfc.ops->poll(
            instance->nfc.context, 1, &completion, 1, &count) !=
            FWLAB_NFC_API_OK || count > 1) {
        return C34_INVARIANT_FAILURE;
    }
    if (count == 0) {
        return C34_OK;
    }
    if (completion.version != FWLAB_NFC_CONTRACT_VERSION ||
        completion.size != sizeof(completion) ||
        !c34_inner_equal(&completion.operation, &instance->graph.inner) ||
        !c34_ppa_equal(&completion.ppa, &instance->graph.operation_ppa)) {
        return C34_INVARIANT_FAILURE;
    }
    instance->graph.completion = completion;
    instance->graph.inner_pending = 0;
    *completed = true;
    (void)step;
    return C34_OK;
}

enum c34_result c34_nfc_finish_physical(struct c34 *instance)
{
    const struct fwlab_nfc_completion *completion;
    struct c34_physical_receipt receipt;
    enum c34_physical_txn_result result;
    uint64_t digest;
    int grown_bad;

    if (!c34_instance_valid(instance) || !instance->graph.physical_bound ||
        instance->graph.inner_pending) {
        return C34_INVALID_CONTRACT;
    }
    completion = &instance->graph.completion;
    result = instance->physical.ops->receipt(
        instance->physical.context, &instance->graph.inner, &receipt);
    if (result == C34_PHYSICAL_TXN_NOT_FOUND &&
        completion->physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT &&
        completion->block_health != FWLAB_NFC_BLOCK_RUNTIME_BAD) {
        if (instance->physical.ops->abandon(
                instance->physical.context, &instance->graph.inner) !=
            C34_PHYSICAL_TXN_OK) {
            return C34_INVARIANT_FAILURE;
        }
        instance->graph.physical_bound = 0;
        return C34_OK;
    }
    if (result != C34_PHYSICAL_TXN_OK ||
        receipt.version != C34_PHYSICAL_TXN_VERSION ||
        receipt.size != sizeof(receipt) || receipt.committed != 1 ||
        !c34_inner_equal(&receipt.inner, &instance->graph.inner) ||
        !c34_ppa_equal(&receipt.ppa, &instance->graph.operation_ppa) ||
        receipt.operation_kind != completion->operation_kind ||
        receipt.base_erase_generation !=
            completion->base_erase_generation ||
        receipt.final_erase_generation !=
            completion->final_erase_generation) {
        return C34_INVARIANT_FAILURE;
    }
    digest = completion->operation_kind == FWLAB_NFC_PROGRAM_EXECUTE ?
        binding_digest(instance->graph.main, instance->graph.oob) : 0;
    if (receipt.payload_digest != digest) {
        return C34_INVARIANT_FAILURE;
    }
    grown_bad = completion->block_health == FWLAB_NFC_BLOCK_RUNTIME_BAD &&
                completion->physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT &&
                receipt.physical_outcome == FWLAB_NFC_PHYS_APPLIED;
    if (!grown_bad &&
        (receipt.physical_outcome != completion->physical_outcome ||
         receipt.integrity != completion->integrity ||
         receipt.applied_region_mask != completion->applied_region_mask)) {
        return C34_INVARIANT_FAILURE;
    }
    if (completion->operation_kind == FWLAB_NFC_PROGRAM_EXECUTE &&
        completion->terminal == FWLAB_NFC_TERMINAL_SUCCESS &&
        (receipt.applied_main_bytes != C34_MAIN_BYTES ||
         receipt.applied_oob_bytes != C34_OOB_BYTES ||
         completion->payload_digest != c33_payload_digest(
             instance->graph.main, instance->graph.oob))) {
        return C34_INVARIANT_FAILURE;
    }
    instance->graph.physical_bound = 0;
    instance->graph.completion.payload_digest ^= receipt.physical_generation;
    return C34_OK;
}
