// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: BSD-3-Clause

#include "spine_anchor_internal.h"

#include "fwlab/contracts/controller_buffer_v0.h"
#include "fwlab/contracts/hif_action.h"
#include "fwlab/contracts/host_data_v0.h"

#if defined(FWLAB_NEGATIVE_HDAR_DMOP)
void fwlab_negative_hdar_dmop(
    struct fwlab_host_dma_authority_ref_v0 **destination,
    struct fwlab_dma_op_token_v0 *source)
{
    *destination = source;
}
#elif defined(FWLAB_NEGATIVE_HDAR_CBLS)
void fwlab_negative_hdar_cbls(
    struct fwlab_host_dma_authority_ref_v0 **destination,
    struct fwlab_controller_buffer_lease_v0 *source)
{
    *destination = source;
}
#elif defined(FWLAB_NEGATIVE_HDAR_CPLS)
void fwlab_negative_hdar_cpls(
    struct fwlab_host_dma_authority_ref_v0 **destination,
    struct fwlab_completion_lease_v0 *source)
{
    *destination = source;
}
#elif defined(FWLAB_NEGATIVE_DMOP_CBLS)
void fwlab_negative_dmop_cbls(
    struct fwlab_dma_op_token_v0 **destination,
    struct fwlab_controller_buffer_lease_v0 *source)
{
    *destination = source;
}
#elif defined(FWLAB_NEGATIVE_DMOP_CPLS)
void fwlab_negative_dmop_cpls(
    struct fwlab_dma_op_token_v0 **destination,
    struct fwlab_completion_lease_v0 *source)
{
    *destination = source;
}
#elif defined(FWLAB_NEGATIVE_CBLS_CPLS)
void fwlab_negative_cbls_cpls(
    struct fwlab_controller_buffer_lease_v0 **destination,
    struct fwlab_completion_lease_v0 *source)
{
    *destination = source;
}
#elif defined(FWLAB_NEGATIVE_LEGACY_ACTION)
void fwlab_negative_legacy_action(
    struct fwlab_host_action_token_v0 **destination,
    struct fwlab_hif_action_token *source)
{
    *destination = source;
}
#elif defined(FWLAB_NEGATIVE_MISSING_ANCHORS)
int main(void)
{
    return fwlab_spine_construction_valid() ? 0 : 1;
}
#elif defined(FWLAB_NEGATIVE_DUPLICATE_ANCHORS)
const struct fwlab_sq_consumer_anchor_v0
    fwlab_authoritative_sq_consumer_v0 = {
        .type_tag = FWLAB_SQ_CONSUMER_ANCHOR_V0_TAG,
    };

const struct fwlab_cqe_publisher_anchor_v0
    fwlab_authoritative_cqe_publisher_v0 = {
        .type_tag = FWLAB_CQE_PUBLISHER_ANCHOR_V0_TAG,
    };
#else
#error "select one bounded S0-A negative case"
#endif
