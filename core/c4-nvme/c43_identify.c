/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c43_internal.h"

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
    if (!fwlab_c43_identify_recipe_valid(recipe) || output == NULL ||
        output_size != FWLAB_C43_IDENTIFY_BYTES) {
        return FWLAB_C43_API_INVALID;
    }
    /* C4.3 phase 1 freezes the ABI only; phase 3 implements exact bytes. */
    return FWLAB_C43_API_NOT_IMPLEMENTED;
}
