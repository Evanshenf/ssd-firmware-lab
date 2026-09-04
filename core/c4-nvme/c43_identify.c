/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_internal.h"

#include <string.h>

static void put_u32(uint8_t *output, size_t offset, uint32_t value)
{
    output[offset] = (uint8_t)value;
    output[offset + 1] = (uint8_t)(value >> 8);
    output[offset + 2] = (uint8_t)(value >> 16);
    output[offset + 3] = (uint8_t)(value >> 24);
}

static void put_u64(uint8_t *output, size_t offset, uint64_t value)
{
    uint32_t index;

    for (index = 0; index < 8; ++index) {
        output[offset + index] = (uint8_t)(value >> (index * 8));
    }
}

static void copy_space_padded(
    uint8_t *output,
    size_t offset,
    size_t width,
    const char *value,
    size_t value_size)
{
    memset(output + offset, ' ', width);
    memcpy(output + offset, value, value_size);
}

int fwlab_c43_identify_recipe_valid(
    const struct fwlab_c43_identify_recipe *recipe)
{
    if (recipe == NULL || recipe->version != FWLAB_C43_POLICY_VERSION ||
        recipe->size != sizeof(*recipe) || recipe->reserved0 != 0 ||
        recipe->kind < FWLAB_C43_IDENTIFY_CONTROLLER ||
        recipe->kind > FWLAB_C43_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST ||
        recipe->payload_bytes != FWLAB_C43_IDENTIFY_BYTES ||
        recipe->identity_version != 1 ||
        !c43_bytes_zero(recipe->reserved1, sizeof(recipe->reserved1))) {
        return 0;
    }
    if (recipe->kind == FWLAB_C43_IDENTIFY_CONTROLLER) {
        return recipe->namespace_id == 0;
    }
    if (recipe->kind == FWLAB_C43_IDENTIFY_NAMESPACE ||
        recipe->kind == FWLAB_C43_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST) {
        return recipe->namespace_id == 1;
    }
    /* Active Namespace List preserves the sanitized start-after NSID. */
    return 1;
}

enum fwlab_c43_api_result fwlab_c43_identify_encode(
    const struct fwlab_c43_identify_recipe *recipe,
    uint8_t *output,
    size_t output_size)
{
    static const char serial[] = "FWLABC43P1-000000001";
    static const char model[] = "SSD Firmware Lab C43-P1";
    static const char firmware[] = "C43P1";
    uint8_t payload[FWLAB_C43_IDENTIFY_BYTES];

    _Static_assert(sizeof(serial) - 1 == 20,
                   "C43-P1 serial must fill exactly 20 bytes");
    _Static_assert(sizeof(model) - 1 <= 40,
                   "C43-P1 model exceeds Identify field");
    _Static_assert(sizeof(firmware) - 1 <= 8,
                   "C43-P1 firmware exceeds Identify field");

    if (!fwlab_c43_identify_recipe_valid(recipe) || output == NULL ||
        output_size != FWLAB_C43_IDENTIFY_BYTES ||
        c43_ranges_overlap(recipe, sizeof(*recipe), output, output_size)) {
        return FWLAB_C43_API_INVALID;
    }
    memset(payload, 0, sizeof(payload));
    switch (recipe->kind) {
    case FWLAB_C43_IDENTIFY_CONTROLLER:
        copy_space_padded(payload, 4, 20, serial, sizeof(serial) - 1);
        copy_space_padded(payload, 24, 40, model, sizeof(model) - 1);
        copy_space_padded(payload, 64, 8, firmware, sizeof(firmware) - 1);
        put_u32(payload, 516, 1);
        payload[525] = 1;
        break;
    case FWLAB_C43_IDENTIFY_NAMESPACE:
        put_u64(payload, 0, 8);
        put_u64(payload, 8, 8);
        put_u64(payload, 16, 8);
        payload[130] = 9;
        break;
    case FWLAB_C43_IDENTIFY_ACTIVE_NAMESPACE_LIST:
        if (recipe->namespace_id < 1) {
            put_u32(payload, 0, 1);
        }
        break;
    case FWLAB_C43_IDENTIFY_NAMESPACE_DESCRIPTOR_LIST:
        break;
    default:
        return FWLAB_C43_API_POISONED;
    }
    memcpy(output, payload, sizeof(payload));
    return FWLAB_C43_API_OK;
}
