/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "nfc_internal.h"

#include <limits.h>

static unsigned int region_count(uint8_t mask)
{
    return (unsigned int)((mask & FWLAB_NFC_REGION_MAIN) != 0) +
           (unsigned int)((mask & FWLAB_NFC_REGION_OOB) != 0);
}

uint64_t c33_operation_duration(
    const struct fwlab_nfc_model *model,
    const struct fwlab_nfc_request *request
)
{
    const struct fwlab_nfc_timing_profile *timing = &model->config.timing;
    uint64_t units = region_count(request->region_mask);

    switch ((enum fwlab_nfc_operation_kind)request->kind) {
    case FWLAB_NFC_READ_TRIGGER:
        return (uint64_t)timing->command_ticks + timing->read_array_ticks;
    case FWLAB_NFC_READ_TRANSFER:
    case FWLAB_NFC_PROGRAM_TRANSFER:
        return (uint64_t)timing->command_ticks +
               units * timing->transfer_ticks_per_unit;
    case FWLAB_NFC_PROGRAM_EXECUTE:
        return (uint64_t)timing->program_setup_ticks +
               units * timing->program_ticks_per_unit +
               timing->program_status_ticks;
    case FWLAB_NFC_ERASE:
        return (uint64_t)timing->erase_setup_ticks +
               (uint64_t)model->config.geometry.pages_per_block *
                   timing->erase_ticks_per_page +
               timing->erase_status_ticks;
    case FWLAB_NFC_STATUS:
        return timing->status_ticks;
    default:
        return 0;
    }
}

static uint64_t maximum(uint64_t left, uint64_t right)
{
    return left > right ? left : right;
}

int c33_schedule_operation(
    struct fwlab_nfc_model *model,
    struct c33_operation *operation
)
{
    const struct fwlab_nfc_request *request = &operation->request;
    const struct fwlab_nfc_geometry *geometry = &model->config.geometry;
    uint32_t plane = c33_plane_index(geometry, &request->ppa);
    uint32_t lun = c33_lun_index(geometry, &request->ppa);
    uint32_t parallel = geometry->plane_parallelism_per_lun;
    uint32_t lane = lun * parallel + request->ppa.plane % parallel;
    uint64_t start = model->virtual_now;
    uint64_t duration = c33_operation_duration(model, request);
    int transfer = request->kind == FWLAB_NFC_READ_TRANSFER ||
                   request->kind == FWLAB_NFC_PROGRAM_TRANSFER ||
                   request->kind == FWLAB_NFC_STATUS;

    if (duration == 0 || plane >= geometry->channels *
                                      geometry->luns_per_channel *
                                      geometry->planes_per_lun) {
        return 0;
    }
    start = maximum(start, model->cache_tail[plane]);
    if (transfer) {
        start = maximum(start, model->channel_tail[request->ppa.channel]);
    } else {
        start = maximum(start, model->array_tail[lane]);
    }
    if (start > model->config.capacity.virtual_tick_limit - duration) {
        return 0;
    }
    operation->start_tick = start;
    operation->next_tick = start;
    operation->finish_tick = start + duration;
    operation->resource_kind = transfer ? 0u : 1u;
    model->cache_tail[plane] = operation->finish_tick;
    if (transfer) {
        model->channel_tail[request->ppa.channel] = operation->finish_tick;
    } else {
        model->array_tail[lane] = operation->finish_tick;
    }
    return 1;
}
