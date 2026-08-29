/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fwlab/portable/nfc_codec.h"

#include <string.h>

struct nfc_encoder {
    uint8_t *wire;
    size_t capacity;
    size_t offset;
    int failed;
};

struct nfc_decoder {
    const uint8_t *wire;
    size_t length;
    size_t offset;
    int failed;
};

static void put_u8(struct nfc_encoder *encoder, uint8_t value)
{
    if (encoder->offset >= encoder->capacity) {
        encoder->failed = 1;
        return;
    }
    encoder->wire[encoder->offset++] = value;
}

static void put_u16(struct nfc_encoder *encoder, uint16_t value)
{
    put_u8(encoder, (uint8_t)value);
    put_u8(encoder, (uint8_t)(value >> 8));
}

static void put_u32(struct nfc_encoder *encoder, uint32_t value)
{
    put_u16(encoder, (uint16_t)value);
    put_u16(encoder, (uint16_t)(value >> 16));
}

static void put_u64(struct nfc_encoder *encoder, uint64_t value)
{
    put_u32(encoder, (uint32_t)value);
    put_u32(encoder, (uint32_t)(value >> 32));
}

static uint8_t get_u8(struct nfc_decoder *decoder)
{
    if (decoder->offset >= decoder->length) {
        decoder->failed = 1;
        return 0;
    }
    return decoder->wire[decoder->offset++];
}

static uint16_t get_u16(struct nfc_decoder *decoder)
{
    uint16_t value = get_u8(decoder);

    value |= (uint16_t)((uint16_t)get_u8(decoder) << 8);
    return value;
}

static uint32_t get_u32(struct nfc_decoder *decoder)
{
    uint32_t value = get_u16(decoder);

    value |= (uint32_t)get_u16(decoder) << 16;
    return value;
}

static uint64_t get_u64(struct nfc_decoder *decoder)
{
    uint64_t value = get_u32(decoder);

    value |= (uint64_t)get_u32(decoder) << 32;
    return value;
}

static void put_magic(struct nfc_encoder *encoder, const char magic[4])
{
    unsigned int index;

    for (index = 0; index < 4; ++index) {
        put_u8(encoder, (uint8_t)magic[index]);
    }
}

static int get_magic(struct nfc_decoder *decoder, const char magic[4])
{
    unsigned int index;

    for (index = 0; index < 4; ++index) {
        if (get_u8(decoder) != (uint8_t)magic[index]) {
            return 0;
        }
    }
    return decoder->failed == 0;
}

static void put_operation(
    struct nfc_encoder *encoder,
    const struct fwlab_nfc_operation_token *operation
)
{
    put_u64(encoder, operation->instance_nonce);
    put_u64(encoder, operation->operation_uid);
    put_u32(encoder, operation->controller_epoch);
    put_u32(encoder, operation->generation);
}

static void get_operation(
    struct nfc_decoder *decoder,
    struct fwlab_nfc_operation_token *operation
)
{
    operation->instance_nonce = get_u64(decoder);
    operation->operation_uid = get_u64(decoder);
    operation->controller_epoch = get_u32(decoder);
    operation->generation = get_u32(decoder);
}

static void put_ppa(
    struct nfc_encoder *encoder,
    const struct fwlab_nfc_ppa *ppa
)
{
    put_u16(encoder, ppa->channel);
    put_u16(encoder, ppa->lun);
    put_u16(encoder, ppa->plane);
    put_u16(encoder, ppa->block);
    put_u16(encoder, ppa->page);
    put_u16(encoder, ppa->reserved);
}

static void get_ppa(
    struct nfc_decoder *decoder,
    struct fwlab_nfc_ppa *ppa
)
{
    ppa->channel = get_u16(decoder);
    ppa->lun = get_u16(decoder);
    ppa->plane = get_u16(decoder);
    ppa->block = get_u16(decoder);
    ppa->page = get_u16(decoder);
    ppa->reserved = get_u16(decoder);
}

static void put_buffer(
    struct nfc_encoder *encoder,
    const struct fwlab_nfc_buffer_ref *buffer
)
{
    put_u32(encoder, buffer->controller_region);
    put_u32(encoder, buffer->offset);
    put_u32(encoder, buffer->length);
    put_u32(encoder, buffer->reserved);
}

static void get_buffer(
    struct nfc_decoder *decoder,
    struct fwlab_nfc_buffer_ref *buffer
)
{
    buffer->controller_region = get_u32(decoder);
    buffer->offset = get_u32(decoder);
    buffer->length = get_u32(decoder);
    buffer->reserved = get_u32(decoder);
}

static void put_cache(
    struct nfc_encoder *encoder,
    const struct fwlab_nfc_cache_token *cache
)
{
    put_u64(encoder, cache->instance_nonce);
    put_u32(encoder, cache->controller_epoch);
    put_u32(encoder, cache->generation);
    put_u16(encoder, cache->channel);
    put_u16(encoder, cache->lun);
    put_u16(encoder, cache->plane);
    put_u16(encoder, cache->reserved);
}

static void get_cache(
    struct nfc_decoder *decoder,
    struct fwlab_nfc_cache_token *cache
)
{
    cache->instance_nonce = get_u64(decoder);
    cache->controller_epoch = get_u32(decoder);
    cache->generation = get_u32(decoder);
    cache->channel = get_u16(decoder);
    cache->lun = get_u16(decoder);
    cache->plane = get_u16(decoder);
    cache->reserved = get_u16(decoder);
}

static int operation_valid(const struct fwlab_nfc_operation_token *operation)
{
    return operation->instance_nonce != 0 && operation->operation_uid != 0 &&
           operation->controller_epoch != 0 && operation->generation != 0;
}

static int buffer_valid(const struct fwlab_nfc_buffer_ref *buffer)
{
    return buffer->reserved == 0;
}

static int cache_shape_valid(const struct fwlab_nfc_cache_token *cache)
{
    return cache->reserved == 0;
}

static enum fwlab_nfc_api_result request_valid(
    const struct fwlab_nfc_request *request
)
{
    if (request->version != FWLAB_NFC_CONTRACT_VERSION) {
        return FWLAB_NFC_API_UNSUPPORTED_VERSION;
    }
    if (request->size != sizeof(*request) || request->reserved0 != 0 ||
        request->reserved1[0] != 0 || request->reserved1[1] != 0 ||
        request->reserved1[2] != 0 || request->reserved2[0] != 0 ||
        request->reserved2[1] != 0 || request->ppa.reserved != 0 ||
        !operation_valid(&request->operation) ||
        !buffer_valid(&request->main) || !buffer_valid(&request->oob) ||
        !cache_shape_valid(&request->cache) ||
        request->kind > FWLAB_NFC_STATUS ||
        (request->region_mask & ~FWLAB_NFC_REGION_MASK) != 0 ||
        request->retry_step > FWLAB_NFC_MODEL_MAX_RETRY_STEP) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result completion_valid(
    const struct fwlab_nfc_completion *completion
)
{
    if (completion->version != FWLAB_NFC_CONTRACT_VERSION) {
        return FWLAB_NFC_API_UNSUPPORTED_VERSION;
    }
    if (completion->size != sizeof(*completion) ||
        completion->reserved0 != 0 || completion->reserved1 != 0 ||
        completion->reserved2[0] != 0 || completion->reserved2[1] != 0 ||
        completion->ppa.reserved != 0 ||
        !operation_valid(&completion->operation) ||
        !cache_shape_valid(&completion->cache) ||
        completion->terminal > FWLAB_NFC_TERMINAL_FAILED ||
        completion->physical_outcome > FWLAB_NFC_PHYS_APPLIED ||
        completion->integrity > FWLAB_NFC_INTEGRITY_TORN ||
        completion->reason > FWLAB_NFC_REASON_INTERNAL ||
        completion->ecc_status > FWLAB_NFC_ECC_UNCORRECTABLE ||
        (completion->requested_region_mask & ~FWLAB_NFC_REGION_MASK) != 0 ||
        (completion->valid_region_mask &
         ~completion->requested_region_mask) != 0 ||
        (completion->applied_region_mask & ~FWLAB_NFC_REGION_MASK) != 0 ||
        completion->block_health > FWLAB_NFC_BLOCK_RUNTIME_BAD ||
        completion->operation_kind > FWLAB_NFC_STATUS) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result trace_valid(
    const struct fwlab_nfc_trace_entry *entry
)
{
    if (entry->version != FWLAB_NFC_CONTRACT_VERSION) {
        return FWLAB_NFC_API_UNSUPPORTED_VERSION;
    }
    if (entry->size != sizeof(*entry) || entry->reserved != 0 ||
        entry->ppa.reserved != 0 ||
        entry->kind < FWLAB_NFC_TRACE_INIT ||
        entry->kind > FWLAB_NFC_TRACE_FAULT) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    if (entry->kind != FWLAB_NFC_TRACE_INIT &&
        entry->kind != FWLAB_NFC_TRACE_RESET &&
        entry->kind != FWLAB_NFC_TRACE_POWER &&
        !operation_valid(&entry->operation)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    return FWLAB_NFC_API_OK;
}

static int padding_zero(const struct nfc_decoder *decoder)
{
    size_t index;

    if (decoder->failed) {
        return 0;
    }
    for (index = decoder->offset; index < decoder->length; ++index) {
        if (decoder->wire[index] != 0) {
            return 0;
        }
    }
    return 1;
}

enum fwlab_nfc_api_result fwlab_nfc_request_encode(
    const struct fwlab_nfc_request *request,
    uint8_t *wire,
    size_t wire_size
)
{
    struct nfc_encoder encoder;
    enum fwlab_nfc_api_result result;

    if (request == NULL || wire == NULL ||
        wire_size != FWLAB_NFC_REQUEST_WIRE_SIZE) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    result = request_valid(request);
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    memset(wire, 0, wire_size);
    encoder = (struct nfc_encoder){wire, wire_size, 0, 0};
    put_magic(&encoder, "NFR1");
    put_u16(&encoder, request->version);
    put_u16(&encoder, request->size);
    put_u32(&encoder, request->reserved0);
    put_operation(&encoder, &request->operation);
    put_ppa(&encoder, &request->ppa);
    put_buffer(&encoder, &request->main);
    put_buffer(&encoder, &request->oob);
    put_cache(&encoder, &request->cache);
    put_u64(&encoder, request->cookie);
    put_u64(&encoder, request->fault_tag);
    put_u32(&encoder, request->scheduling_group);
    put_u16(&encoder, request->priority);
    put_u8(&encoder, request->kind);
    put_u8(&encoder, request->region_mask);
    put_u8(&encoder, request->retry_step);
    put_u8(&encoder, request->reserved1[0]);
    put_u8(&encoder, request->reserved1[1]);
    put_u8(&encoder, request->reserved1[2]);
    put_u32(&encoder, request->reserved2[0]);
    put_u32(&encoder, request->reserved2[1]);
    return encoder.failed ? FWLAB_NFC_API_INVALID_CONTRACT :
                            FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_nfc_request_decode(
    const uint8_t *wire,
    size_t wire_size,
    struct fwlab_nfc_request *request
)
{
    struct nfc_decoder decoder;

    if (wire == NULL || request == NULL ||
        wire_size != FWLAB_NFC_REQUEST_WIRE_SIZE) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(request, 0, sizeof(*request));
    decoder = (struct nfc_decoder){wire, wire_size, 0, 0};
    if (!get_magic(&decoder, "NFR1")) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    request->version = get_u16(&decoder);
    request->size = get_u16(&decoder);
    request->reserved0 = get_u32(&decoder);
    get_operation(&decoder, &request->operation);
    get_ppa(&decoder, &request->ppa);
    get_buffer(&decoder, &request->main);
    get_buffer(&decoder, &request->oob);
    get_cache(&decoder, &request->cache);
    request->cookie = get_u64(&decoder);
    request->fault_tag = get_u64(&decoder);
    request->scheduling_group = get_u32(&decoder);
    request->priority = get_u16(&decoder);
    request->kind = get_u8(&decoder);
    request->region_mask = get_u8(&decoder);
    request->retry_step = get_u8(&decoder);
    request->reserved1[0] = get_u8(&decoder);
    request->reserved1[1] = get_u8(&decoder);
    request->reserved1[2] = get_u8(&decoder);
    request->reserved2[0] = get_u32(&decoder);
    request->reserved2[1] = get_u32(&decoder);
    if (!padding_zero(&decoder)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    return request_valid(request);
}

enum fwlab_nfc_api_result fwlab_nfc_completion_encode(
    const struct fwlab_nfc_completion *completion,
    uint8_t *wire,
    size_t wire_size
)
{
    struct nfc_encoder encoder;
    enum fwlab_nfc_api_result result;

    if (completion == NULL || wire == NULL ||
        wire_size != FWLAB_NFC_COMPLETION_WIRE_SIZE) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    result = completion_valid(completion);
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    memset(wire, 0, wire_size);
    encoder = (struct nfc_encoder){wire, wire_size, 0, 0};
    put_magic(&encoder, "NFC1");
    put_u16(&encoder, completion->version);
    put_u16(&encoder, completion->size);
    put_u32(&encoder, completion->reserved0);
    put_operation(&encoder, &completion->operation);
    put_ppa(&encoder, &completion->ppa);
    put_cache(&encoder, &completion->cache);
    put_u64(&encoder, completion->cookie);
    put_u64(&encoder, completion->frozen_fault_word);
    put_u64(&encoder, completion->payload_digest);
    put_u64(&encoder, completion->accepted_tick);
    put_u64(&encoder, completion->begin_tick);
    put_u64(&encoder, completion->outcome_tick);
    put_u64(&encoder, completion->status_tick);
    put_u32(&encoder, completion->submit_sequence);
    put_u32(&encoder, completion->array_sequence);
    put_u16(&encoder, completion->base_erase_generation);
    put_u16(&encoder, completion->final_erase_generation);
    put_u16(&encoder, completion->corrected_main_bits);
    put_u16(&encoder, completion->corrected_oob_bits);
    put_u8(&encoder, completion->terminal);
    put_u8(&encoder, completion->physical_outcome);
    put_u8(&encoder, completion->integrity);
    put_u8(&encoder, completion->reason);
    put_u8(&encoder, completion->ecc_status);
    put_u8(&encoder, completion->requested_region_mask);
    put_u8(&encoder, completion->valid_region_mask);
    put_u8(&encoder, completion->applied_region_mask);
    put_u8(&encoder, completion->retry_step);
    put_u8(&encoder, completion->block_health);
    put_u8(&encoder, completion->operation_kind);
    put_u8(&encoder, completion->reserved1);
    put_u32(&encoder, completion->reserved2[0]);
    put_u32(&encoder, completion->reserved2[1]);
    return encoder.failed ? FWLAB_NFC_API_INVALID_CONTRACT :
                            FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_nfc_completion_decode(
    const uint8_t *wire,
    size_t wire_size,
    struct fwlab_nfc_completion *completion
)
{
    struct nfc_decoder decoder;

    if (wire == NULL || completion == NULL ||
        wire_size != FWLAB_NFC_COMPLETION_WIRE_SIZE) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(completion, 0, sizeof(*completion));
    decoder = (struct nfc_decoder){wire, wire_size, 0, 0};
    if (!get_magic(&decoder, "NFC1")) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    completion->version = get_u16(&decoder);
    completion->size = get_u16(&decoder);
    completion->reserved0 = get_u32(&decoder);
    get_operation(&decoder, &completion->operation);
    get_ppa(&decoder, &completion->ppa);
    get_cache(&decoder, &completion->cache);
    completion->cookie = get_u64(&decoder);
    completion->frozen_fault_word = get_u64(&decoder);
    completion->payload_digest = get_u64(&decoder);
    completion->accepted_tick = get_u64(&decoder);
    completion->begin_tick = get_u64(&decoder);
    completion->outcome_tick = get_u64(&decoder);
    completion->status_tick = get_u64(&decoder);
    completion->submit_sequence = get_u32(&decoder);
    completion->array_sequence = get_u32(&decoder);
    completion->base_erase_generation = get_u16(&decoder);
    completion->final_erase_generation = get_u16(&decoder);
    completion->corrected_main_bits = get_u16(&decoder);
    completion->corrected_oob_bits = get_u16(&decoder);
    completion->terminal = get_u8(&decoder);
    completion->physical_outcome = get_u8(&decoder);
    completion->integrity = get_u8(&decoder);
    completion->reason = get_u8(&decoder);
    completion->ecc_status = get_u8(&decoder);
    completion->requested_region_mask = get_u8(&decoder);
    completion->valid_region_mask = get_u8(&decoder);
    completion->applied_region_mask = get_u8(&decoder);
    completion->retry_step = get_u8(&decoder);
    completion->block_health = get_u8(&decoder);
    completion->operation_kind = get_u8(&decoder);
    completion->reserved1 = get_u8(&decoder);
    completion->reserved2[0] = get_u32(&decoder);
    completion->reserved2[1] = get_u32(&decoder);
    if (!padding_zero(&decoder)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    return completion_valid(completion);
}

enum fwlab_nfc_api_result fwlab_nfc_trace_encode(
    const struct fwlab_nfc_trace_entry *entry,
    uint8_t *wire,
    size_t wire_size
)
{
    struct nfc_encoder encoder;
    enum fwlab_nfc_api_result result;

    if (entry == NULL || wire == NULL ||
        wire_size != FWLAB_NFC_TRACE_WIRE_SIZE) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    result = trace_valid(entry);
    if (result != FWLAB_NFC_API_OK) {
        return result;
    }
    memset(wire, 0, wire_size);
    encoder = (struct nfc_encoder){wire, wire_size, 0, 0};
    put_magic(&encoder, "NFT1");
    put_u16(&encoder, entry->version);
    put_u16(&encoder, entry->size);
    put_u32(&encoder, entry->kind);
    put_u64(&encoder, entry->sequence);
    put_u64(&encoder, entry->virtual_tick);
    put_operation(&encoder, &entry->operation);
    put_ppa(&encoder, &entry->ppa);
    put_u32(&encoder, entry->from_state);
    put_u32(&encoder, entry->to_state);
    put_u32(&encoder, entry->detail);
    put_u32(&encoder, entry->reserved);
    return encoder.failed ? FWLAB_NFC_API_INVALID_CONTRACT :
                            FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_nfc_trace_decode(
    const uint8_t *wire,
    size_t wire_size,
    struct fwlab_nfc_trace_entry *entry
)
{
    struct nfc_decoder decoder;

    if (wire == NULL || entry == NULL ||
        wire_size != FWLAB_NFC_TRACE_WIRE_SIZE) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    memset(entry, 0, sizeof(*entry));
    decoder = (struct nfc_decoder){wire, wire_size, 0, 0};
    if (!get_magic(&decoder, "NFT1")) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    entry->version = get_u16(&decoder);
    entry->size = get_u16(&decoder);
    entry->kind = get_u32(&decoder);
    entry->sequence = get_u64(&decoder);
    entry->virtual_tick = get_u64(&decoder);
    get_operation(&decoder, &entry->operation);
    get_ppa(&decoder, &entry->ppa);
    entry->from_state = get_u32(&decoder);
    entry->to_state = get_u32(&decoder);
    entry->detail = get_u32(&decoder);
    entry->reserved = get_u32(&decoder);
    if (!padding_zero(&decoder)) {
        return FWLAB_NFC_API_INVALID_CONTRACT;
    }
    return trace_valid(entry);
}
