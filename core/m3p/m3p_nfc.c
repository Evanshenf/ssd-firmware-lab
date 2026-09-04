/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "m3p_internal.h"

#include <string.h>

static enum fwlab_nfc_api_result staging_read(
    void *opaque, const struct fwlab_nfc_buffer_ref *source,
    uint8_t *destination, uint32_t length)
{
    struct fwlab_m3p *m3p = opaque;
    uint8_t frame;

    if (m3p == NULL || source == NULL || destination == NULL ||
        source->controller_region == 0 ||
        source->controller_region > M3P_STAGING_FRAMES ||
        source->reserved != 0 || source->length != length) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    frame = (uint8_t)(source->controller_region - 1u);
    if (source->offset == 0 && length == M3P_PAGE_BYTES) {
        memcpy(destination, m3p->frame_main[frame], length);
        return FWLAB_NFC_API_OK;
    }
    if (source->offset == M3P_PAGE_BYTES && length == M3P_OOB_BYTES) {
        memcpy(destination, m3p->frame_oob[frame], length);
        return FWLAB_NFC_API_OK;
    }
    return FWLAB_NFC_API_INVALID_CONTRACT;
}

static enum fwlab_nfc_api_result staging_write(
    void *opaque, const struct fwlab_nfc_buffer_ref *destination,
    const uint8_t *source, uint32_t length)
{
    struct fwlab_m3p *m3p = opaque;
    uint8_t frame;

    if (m3p == NULL || destination == NULL || source == NULL ||
        destination->controller_region == 0 ||
        destination->controller_region > M3P_STAGING_FRAMES ||
        destination->reserved != 0 || destination->length != length) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    frame = (uint8_t)(destination->controller_region - 1u);
    if (destination->offset == 0 && length == M3P_PAGE_BYTES) {
        memcpy(m3p->frame_main[frame], source, length);
        return FWLAB_NFC_API_OK;
    }
    if (destination->offset == M3P_PAGE_BYTES && length == M3P_OOB_BYTES) {
        memcpy(m3p->frame_oob[frame], source, length);
        return FWLAB_NFC_API_OK;
    }
    return FWLAB_NFC_API_INVALID_CONTRACT;
}

static const struct fwlab_nfc_buffer_ops staging_ops = {
    .version = FWLAB_NFC_CONTRACT_VERSION,
    .size = sizeof(struct fwlab_nfc_buffer_ops),
    .reserved = 0,
    .read = staging_read,
    .write = staging_write,
};

struct fwlab_nfc_buffer_provider m3p_staging_provider(struct fwlab_m3p *m3p)
{
    struct fwlab_nfc_buffer_provider provider;

    provider.ops = &staging_ops;
    provider.context = m3p;
    return provider;
}

static int token_available(const struct fwlab_m3p *m3p, uint32_t count)
{
    return m3p->next_child_uid != 0 && count != 0 &&
           m3p->next_child_uid <= m3p->config.nfc_operation_uid_limit &&
           count - 1u <= m3p->config.nfc_operation_uid_limit -
                            m3p->next_child_uid;
}

static struct fwlab_nfc_operation_token allocate_token(struct fwlab_m3p *m3p)
{
    struct fwlab_nfc_operation_token token;

    memset(&token, 0, sizeof(token));
    token.instance_nonce = m3p->config.nfc_instance_nonce;
    token.operation_uid = m3p->next_child_uid;
    token.controller_epoch = m3p->config.nfc_epoch;
    token.generation = (uint32_t)m3p->next_child_uid;
    ++m3p->next_child_uid;
    ++m3p->child_starts;
    return token;
}

static void request_common(struct fwlab_m3p *m3p,
                           struct fwlab_nfc_request *request,
                           uint8_t kind, struct fwlab_nfc_ppa ppa)
{
    memset(request, 0, sizeof(*request));
    request->version = FWLAB_NFC_CONTRACT_VERSION;
    request->size = (uint16_t)sizeof(*request);
    request->operation = allocate_token(m3p);
    request->ppa = ppa;
    request->cookie = request->operation.operation_uid;
    request->scheduling_group = 1;
    request->kind = kind;
}

static struct fwlab_nfc_buffer_ref main_ref(uint8_t frame)
{
    struct fwlab_nfc_buffer_ref reference;

    reference.controller_region = (uint32_t)frame + 1u;
    reference.offset = 0;
    reference.length = M3P_PAGE_BYTES;
    reference.reserved = 0;
    return reference;
}

static struct fwlab_nfc_buffer_ref oob_ref(uint8_t frame)
{
    struct fwlab_nfc_buffer_ref reference;

    reference.controller_region = (uint32_t)frame + 1u;
    reference.offset = M3P_PAGE_BYTES;
    reference.length = M3P_OOB_BYTES;
    reference.reserved = 0;
    return reference;
}

enum fwlab_spine_result_v0 m3p_child_read_start(
    struct fwlab_m3p *m3p, uint8_t frame, struct fwlab_nfc_ppa ppa)
{
    if (m3p == NULL || m3p->child.state != M3P_CHILD_IDLE ||
        frame >= M3P_STAGING_FRAMES || !token_available(m3p, 2)) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    memset(&m3p->child, 0, sizeof(m3p->child));
    m3p->child.kind = M3P_CHILD_READ;
    m3p->child.state = M3P_CHILD_SUBMIT_FIRST;
    m3p->child.frame = frame;
    m3p->child.ppa = ppa;
    request_common(m3p, &m3p->child.first, FWLAB_NFC_READ_TRIGGER, ppa);
    m3p->child.first.region_mask = FWLAB_NFC_REGION_MASK;
    request_common(m3p, &m3p->child.second, FWLAB_NFC_READ_TRANSFER, ppa);
    m3p->child.second.region_mask = FWLAB_NFC_REGION_MASK;
    m3p->child.second.main = main_ref(frame);
    m3p->child.second.oob = oob_ref(frame);
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 m3p_child_program_start(
    struct fwlab_m3p *m3p, uint8_t frame, struct fwlab_nfc_ppa ppa)
{
    if (m3p == NULL || m3p->child.state != M3P_CHILD_IDLE ||
        frame >= M3P_STAGING_FRAMES || !token_available(m3p, 2)) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    memset(&m3p->child, 0, sizeof(m3p->child));
    m3p->child.kind = M3P_CHILD_PROGRAM;
    m3p->child.state = M3P_CHILD_SUBMIT_FIRST;
    m3p->child.frame = frame;
    m3p->child.ppa = ppa;
    request_common(m3p, &m3p->child.first, FWLAB_NFC_PROGRAM_TRANSFER, ppa);
    m3p->child.first.region_mask = FWLAB_NFC_REGION_MASK;
    m3p->child.first.main = main_ref(frame);
    m3p->child.first.oob = oob_ref(frame);
    request_common(m3p, &m3p->child.second, FWLAB_NFC_PROGRAM_EXECUTE, ppa);
    m3p->child.second.region_mask = FWLAB_NFC_REGION_MASK;
    return FWLAB_SPINE_V0_OK;
}

enum fwlab_spine_result_v0 m3p_child_erase_start(
    struct fwlab_m3p *m3p, struct fwlab_nfc_ppa ppa)
{
    if (m3p == NULL || m3p->child.state != M3P_CHILD_IDLE ||
        !token_available(m3p, 1)) {
        return FWLAB_SPINE_V0_NO_CAPACITY;
    }
    memset(&m3p->child, 0, sizeof(m3p->child));
    m3p->child.kind = M3P_CHILD_ERASE;
    m3p->child.state = M3P_CHILD_SUBMIT_FIRST;
    ppa.page = 0;
    m3p->child.ppa = ppa;
    request_common(m3p, &m3p->child.first, FWLAB_NFC_ERASE, ppa);
    return FWLAB_SPINE_V0_OK;
}

static int operation_token_equal(
    const struct fwlab_nfc_operation_token *left,
    const struct fwlab_nfc_operation_token *right)
{
    return left->instance_nonce == right->instance_nonce &&
           left->operation_uid == right->operation_uid &&
           left->controller_epoch == right->controller_epoch &&
           left->generation == right->generation;
}

int m3p_nfc_submit_or_step(struct fwlab_m3p *m3p,
                           uint32_t *provider_transitions)
{
    struct fwlab_nfc_request *request;
    struct fwlab_nfc_submit_result submit;
    struct fwlab_nfc_step_result step;
    enum fwlab_nfc_api_result result;

    if (m3p->child.state == M3P_CHILD_SUBMIT_FIRST ||
        m3p->child.state == M3P_CHILD_SUBMIT_SECOND) {
        request = m3p->child.state == M3P_CHILD_SUBMIT_FIRST ?
            &m3p->child.first : &m3p->child.second;
        submit = m3p->nfc.ops->try_submit(m3p->nfc.context, request);
        if (submit.disposition == FWLAB_NFC_ACCEPTED) {
            m3p->child.state = m3p->child.state == M3P_CHILD_SUBMIT_FIRST ?
                M3P_CHILD_WAIT_FIRST : M3P_CHILD_WAIT_SECOND;
            ++*provider_transitions;
            return 1;
        }
        if (submit.disposition == FWLAB_NFC_REJECTED) {
            m3p->child.state = M3P_CHILD_FAILED;
            m3p->child.fault_code = submit.reason == 0 ?
                FWLAB_NFC_REASON_INTERNAL : submit.reason;
            ++*provider_transitions;
            return 1;
        }
        return 0;
    }
    if (m3p->child.state != M3P_CHILD_WAIT_FIRST &&
        m3p->child.state != M3P_CHILD_WAIT_SECOND) {
        return 0;
    }
    memset(&step, 0, sizeof(step));
    result = m3p->nfc.ops->step(m3p->nfc.context, 1, &step);
    if (result != FWLAB_NFC_API_OK) {
        m3p->child.state = M3P_CHILD_FAILED;
        m3p->child.fault_code = result;
        return 1;
    }
    *provider_transitions += step.transitions;
    return step.units_used != 0 || step.transitions != 0;
}

int m3p_nfc_poll(struct fwlab_m3p *m3p, uint32_t *events)
{
    struct fwlab_nfc_completion completion;
    struct fwlab_nfc_completion first_completion;
    const struct fwlab_nfc_request *request;
    uint32_t count = 0;
    enum fwlab_nfc_api_result result;

    if (m3p->child.state != M3P_CHILD_WAIT_FIRST &&
        m3p->child.state != M3P_CHILD_WAIT_SECOND) {
        return 0;
    }
    memset(&completion, 0, sizeof(completion));
    result = m3p->nfc.ops->poll(m3p->nfc.context, 1, &completion, 1, &count);
    if (result != FWLAB_NFC_API_OK) {
        m3p->child.state = M3P_CHILD_FAILED;
        m3p->child.fault_code = result;
        return 1;
    }
    if (count == 0) {
        return 0;
    }
    request = m3p->child.state == M3P_CHILD_WAIT_FIRST ?
        &m3p->child.first : &m3p->child.second;
    first_completion = m3p->child.completion;
    if (count != 1 || !operation_token_equal(&completion.operation,
                                              &request->operation) ||
        completion.operation_kind != request->kind ||
        memcmp(&completion.ppa, &request->ppa, sizeof(request->ppa)) != 0 ||
        completion.terminal != FWLAB_NFC_TERMINAL_SUCCESS ||
        (completion.physical_outcome == FWLAB_NFC_PHYS_NO_EFFECT &&
         (request->kind == FWLAB_NFC_PROGRAM_EXECUTE ||
          request->kind == FWLAB_NFC_ERASE))) {
        m3p->child.completion = completion;
        m3p->child.state = M3P_CHILD_FAILED;
        m3p->child.fault_code = completion.reason == 0 ?
            FWLAB_NFC_REASON_INTERNAL : completion.reason;
        ++*events;
        return 1;
    }
    if (m3p->child.state == M3P_CHILD_WAIT_FIRST &&
        m3p->child.kind != M3P_CHILD_ERASE) {
        m3p->child.completion = completion;
        m3p->child.second.cache = completion.cache;
        m3p->child.state = M3P_CHILD_SUBMIT_SECOND;
    } else {
        if (m3p->child.kind == M3P_CHILD_READ) {
            completion.base_erase_generation =
                first_completion.base_erase_generation;
            completion.final_erase_generation =
                first_completion.final_erase_generation;
            completion.block_health = first_completion.block_health;
            completion.ecc_status = first_completion.ecc_status;
            completion.corrected_main_bits =
                first_completion.corrected_main_bits;
            completion.corrected_oob_bits =
                first_completion.corrected_oob_bits;
        }
        m3p->child.completion = completion;
        m3p->child.state = M3P_CHILD_DONE;
    }
    ++*events;
    return 1;
}

void m3p_child_consume(struct fwlab_m3p *m3p)
{
    memset(&m3p->child, 0, sizeof(m3p->child));
}
