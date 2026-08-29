/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "nfc_internal.h"

uint64_t c33_hash_bytes(const uint8_t *bytes, size_t length)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    if (bytes == NULL && length != 0) {
        return 0;
    }
    for (index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t mix_u64(uint64_t hash, uint64_t value)
{
    unsigned int byte;

    for (byte = 0; byte < 8; ++byte) {
        hash ^= (uint8_t)(value >> (byte * 8u));
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

uint64_t c33_fault_word(
    const struct fwlab_nfc_model *model,
    const struct c33_operation *operation,
    uint16_t erase_generation,
    uint8_t program_count,
    uint32_t read_ordinal
)
{
    const struct fwlab_nfc_request *request = &operation->request;
    uint64_t hash = UINT64_C(1469598103934665603);

    hash = mix_u64(hash, model->config.fault.seed);
    hash = mix_u64(hash, model->config.fault.profile_version);
    hash = mix_u64(hash, model->instance_nonce);
    hash = mix_u64(hash, operation->submit_sequence);
    hash = mix_u64(hash, request->operation.operation_uid);
    hash = mix_u64(hash, request->operation.generation);
    hash = mix_u64(hash, request->kind);
    hash = mix_u64(hash, request->ppa.channel);
    hash = mix_u64(hash, request->ppa.lun);
    hash = mix_u64(hash, request->ppa.plane);
    hash = mix_u64(hash, request->ppa.block);
    hash = mix_u64(hash, request->ppa.page);
    hash = mix_u64(hash, erase_generation);
    hash = mix_u64(hash, program_count);
    hash = mix_u64(hash, read_ordinal);
    hash = mix_u64(hash, request->retry_step);
    hash = mix_u64(hash, request->cookie);
    return mix_u64(hash, request->fault_tag);
}

static int modulus_hit(uint64_t word, uint32_t modulus)
{
    return modulus != 0 && word % modulus == 0;
}

enum c33_fault_class c33_fault_classify(
    const struct fwlab_nfc_model *model,
    const struct c33_operation *operation,
    uint64_t fault_word
)
{
    const struct fwlab_nfc_fault_profile *profile = &model->config.fault;

    if (operation->request.kind != FWLAB_NFC_PROGRAM_EXECUTE &&
        operation->request.kind != FWLAB_NFC_ERASE) {
        return C33_FAULT_CLEAN;
    }
    if (modulus_hit(fault_word, profile->grown_bad_modulus)) {
        return C33_FAULT_GROWN_BAD;
    }
    if (operation->request.kind == FWLAB_NFC_PROGRAM_EXECUTE) {
        if (modulus_hit(fault_word, profile->program_no_effect_modulus)) {
            return C33_FAULT_NO_EFFECT;
        }
        if (modulus_hit(fault_word, profile->program_torn_modulus)) {
            return C33_FAULT_TORN;
        }
    } else {
        if (modulus_hit(fault_word, profile->erase_no_effect_modulus)) {
            return C33_FAULT_NO_EFFECT;
        }
        if (modulus_hit(fault_word, profile->erase_torn_modulus)) {
            return C33_FAULT_TORN;
        }
    }
    return C33_FAULT_CLEAN;
}

void c33_fault_read_counts(
    const struct fwlab_nfc_model *model,
    const struct c33_operation *operation,
    uint64_t fault_word,
    uint16_t *main_errors,
    uint16_t *oob_errors
)
{
    uint32_t modulus = model->config.fault.read_error_modulus;
    uint32_t main_range = model->config.ecc.main_strength_bits + 2u;
    uint32_t oob_range = model->config.ecc.oob_strength_bits + 2u;

    (void)operation;
    *main_errors = 0;
    *oob_errors = 0;
    if (modulus == 0 || fault_word % modulus != 0) {
        return;
    }
    *main_errors = (uint16_t)((fault_word >> 8) % main_range);
    *oob_errors = (uint16_t)((fault_word >> 32) % oob_range);
}
