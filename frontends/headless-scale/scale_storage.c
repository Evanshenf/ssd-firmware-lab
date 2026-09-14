/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "scale_storage.h"
#include "ftl_scale_internal.h"
#include "fwlab/private/nfc_scaled_model.h"
#include "fwlab/private/nfc_trace_window.h"
#include "fwlab/private/nfc_page_v2_model.h"
#include "fwlab/private/nfc_channel_v2.h"

#include <stdlib.h>
#include <string.h>

#define SCALE_STORAGE_MAGIC UINT64_C(0x5343414c4542494e)
#define SCALE_NFC_TRACE_ENTRIES 4096u
enum scale_binding { SCALE_C3, SCALE_PAGE2, SCALE_READ_LAB, SCALE_MUTATION_LAB,
                     SCALE_CHANNEL_LAB, SCALE_MULTIHEAD_LAB };

int scale_storage_capacity_mib(uint32_t logical_mib,
    struct fwlab_nfc_geometry *geometry, uint64_t *lba_count)
{
    struct fwlab_nfc_geometry g = {0};
    struct sf_layout layout;
    uint64_t lbas;
    if (!geometry || !lba_count ||
        (logical_mib != 64 && logical_mib != 256 && logical_mib != 65536))
        return 0;
    g.version = FWLAB_NFC_CONTRACT_VERSION;
    g.size = (uint16_t)sizeof(g);
    g.channels = logical_mib == 64 ? 1 : 2;
    g.luns_per_channel = g.channels;
    g.planes_per_lun = g.channels;
    g.blocks_per_plane = logical_mib == 64 ? 320 :
                        (logical_mib == 256 ? 160 : 40960);
    g.pages_per_block = 64;
    g.plane_parallelism_per_lun = g.planes_per_lun;
    g.main_bytes_per_page = SF_PAGE_BYTES;
    g.oob_bytes_per_page = SF_OOB_BYTES;
    g.max_programs_per_erase = 1;
    g.program_order = FWLAB_NFC_PROGRAM_ASCENDING;
    lbas = (uint64_t)logical_mib * 2048u;
    if (!sf_layout_make(&g, lbas, &layout))
        return 0;
    *geometry = g;
    *lba_count = lbas;
    return 1;
}

struct scale_storage {
    uint64_t magic;
    void *ftl_arena;
    void *nfc_arena;
    struct fwlab_ftl_scale *ftl;
    struct fwlab_nfc_model *nfc;
    struct fwlab_nfc_page_v2_model *page_nfc;
    struct fwlab_nfc_page_v2_lab *lab_nfc;
    struct fwlab_nfc_channel_v2 *channel_nfc;
    struct fwlab_nfc_channel_executor executor;
    bool stepped, finalized;
    uint64_t trace_windows;
};

static struct fwlab_nfc_model_config nfc_configuration(
    const struct fwlab_nfc_geometry *geometry)
{
    struct fwlab_nfc_model_config c;
    memset(&c, 0, sizeof(c));
    c.version = FWLAB_NFC_CONTRACT_VERSION;
    c.size = (uint16_t)sizeof(c);
    c.geometry = *geometry;
    c.ecc.version = FWLAB_NFC_CONTRACT_VERSION;
    c.ecc.size = (uint16_t)sizeof(c.ecc);
    c.ecc.main_covered_bytes = SF_PAGE_BYTES;
    c.ecc.oob_covered_bytes = SF_OOB_BYTES;
    c.ecc.main_step_bytes = 512;
    c.ecc.oob_step_bytes = 16;
    c.ecc.main_strength_bits = 8;
    c.ecc.oob_strength_bits = 4;
    c.ecc.max_retry_step = 3;
    c.timing.version = FWLAB_NFC_CONTRACT_VERSION;
    c.timing.size = (uint16_t)sizeof(c.timing);
    c.timing.command_ticks = 1;
    c.timing.transfer_ticks_per_unit = 1;
    c.timing.read_array_ticks = 8;
    c.timing.program_setup_ticks = 2;
    c.timing.program_ticks_per_unit = 4;
    c.timing.program_status_ticks = 1;
    c.timing.erase_setup_ticks = 2;
    c.timing.erase_ticks_per_page = 2;
    c.timing.erase_status_ticks = 1;
    c.timing.status_ticks = 1;
    c.fault.version = FWLAB_NFC_FAULT_PROFILE_VERSION;
    c.fault.size = (uint16_t)sizeof(c.fault);
    c.fault.profile_version = 1;
    c.fault.seed = UINT64_C(0x5343414c454e4643);
    c.capacity.version = FWLAB_NFC_CONTRACT_VERSION;
    c.capacity.size = (uint16_t)sizeof(c.capacity);
    c.capacity.operations = 4;
    c.capacity.request_registry = 4;
    c.capacity.terminal_events = 4;
    c.capacity.result_slots = 4;
    c.capacity.trace_entries = SCALE_NFC_TRACE_ENTRIES;
    c.capacity.scratch_main_bytes = SF_PAGE_BYTES;
    c.capacity.scratch_oob_bytes = SF_OOB_BYTES;
    c.capacity.operation_generation_limit = UINT32_MAX;
    c.capacity.cache_generation_limit = UINT32_MAX;
    c.capacity.controller_epoch_limit = UINT32_MAX;
    c.capacity.submit_sequence_limit = UINT32_MAX;
    c.capacity.operation_uid_limit = UINT64_MAX;
    c.capacity.virtual_tick_limit = UINT64_MAX / 2u;
    c.successful_erase_limit = UINT16_MAX;
    return c;
}

static void storage_release(void *opaque)
{
    struct scale_storage *storage = opaque;
    if (!storage)
        return;
    if (storage->executor.ops) {
        /* release is valid only before first step or after successful fini.
         * A void release cannot return lost worker ownership to its caller. */
        if (storage->stepped && !storage->finalized) abort();
        bool complete = false;
        while (!complete) {
            bool advanced = false;
            if (storage->executor.ops->shutdown(storage->executor.context,
                &advanced, &complete) != FWLAB_NFC_API_OK) abort();
        }
    }
    free(storage->nfc_arena);
    free(storage->ftl_arena);
    storage->magic = 0;
    free(storage);
}

static enum fwlab_spine_result_v0 storage_step(
    void *opaque, uint32_t budget, uint32_t *used)
{
    struct scale_storage *storage = opaque;
    uint32_t total = 0;
    if (!storage || storage->magic != SCALE_STORAGE_MAGIC || !used || !budget)
        return FWLAB_SPINE_V0_INVALID;
    storage->stepped = true;
    while (total < budget) {
        uint32_t one = 0;
        enum fwlab_spine_result_v0 result =
            fwlab_ftl_scale_step(storage->ftl, 1, &one);
        if (result != FWLAB_SPINE_V0_OK && result != FWLAB_SPINE_V0_IN_PROGRESS) {
            *used = total + one;
            return result;
        }
        if (storage->nfc && fwlab_nfc_model_trace_count(storage->nfc) >=
            SCALE_NFC_TRACE_ENTRIES - 1024u) {
            uint32_t retired = 0;
            enum fwlab_nfc_api_result trace =
                fwlab_nfc_trace_window_retire(storage->nfc, &retired);
            if (trace == FWLAB_NFC_API_OK && retired)
                ++storage->trace_windows;
            else if (trace != FWLAB_NFC_API_OK &&
                     trace != FWLAB_NFC_API_WRONG_STATE) {
                *used = total + one;
                return FWLAB_SPINE_V0_POISONED;
            }
        }
        ++total;
    }
    *used = total;
    return FWLAB_SPINE_V0_OK;
}

static enum fwlab_spine_result_v0 storage_step_report(
    void *opaque, uint32_t budget, uint32_t *used,
    struct fwlab_execution_progress *progress)
{
    struct scale_storage *storage = opaque;
    if (!storage || storage->magic != SCALE_STORAGE_MAGIC ||
        (!storage->page_nfc && !storage->lab_nfc && !storage->channel_nfc))
        return FWLAB_SPINE_V0_INVALID;
    storage->stepped = true;
    return fwlab_ftl_scale_step_report(storage->ftl, budget, used, progress);
}

static enum fwlab_spine_result_v0 storage_volume(
    void *opaque, struct fwlab_block_volume_binding_v0 *binding)
{
    struct scale_storage *storage = opaque;
    return storage && storage->magic == SCALE_STORAGE_MAGIC
        ? fwlab_ftl_scale_volume_query(storage->ftl, binding)
        : FWLAB_SPINE_V0_INVALID;
}

static enum fwlab_spine_result_v0 storage_fini(void *opaque)
{
    struct scale_storage *storage = opaque;
    if (!storage || storage->magic != SCALE_STORAGE_MAGIC) return FWLAB_SPINE_V0_INVALID;
    enum fwlab_spine_result_v0 result = fwlab_ftl_scale_fini(storage->ftl);
    if (result == FWLAB_SPINE_V0_OK) storage->finalized = true;
    return result;
}

static enum fwlab_spine_result_v0 storage_bind_common(
    void *opaque, const struct j0_runtime_config *config,
    const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_block_namespace_ref_v0 *namespace_ref,
    uint64_t lifecycle_nonce, uint64_t ftl_nonce, uint64_t nfc_nonce,
    struct j0_storage_runner *runner, struct fwlab_block_service_v0 *service,
    enum scale_binding binding)
{
    const struct scale_storage_options *options = opaque;
    struct scale_storage *storage;
    struct fwlab_ftl_scale_config f;
    struct fwlab_nfc_model_config n;
    struct fwlab_nfc_buffer_provider staging;
    struct fwlab_nfc_provider provider;
    struct fwlab_ftl_scale_extended_config extended = {0};
    enum fwlab_spine_result_v0 result;
    size_t ftl_bytes, nfc_bytes;
    uint32_t blocks, pages;
    bool window_v2 = binding != SCALE_C3;
    bool parallel = binding == SCALE_READ_LAB;
    bool mutation = binding == SCALE_MUTATION_LAB;
    bool multihead = binding == SCALE_MULTIHEAD_LAB;
    bool channel = binding == SCALE_CHANNEL_LAB || multihead;
    bool lab = parallel || mutation;
    (void)lifecycle_nonce;
    if (!config || !config->media_binding || !buffer || !namespace_ref ||
        !runner || !service ||
        !sf_geometry_counts(&config->media_binding->geometry, &blocks, &pages))
        return FWLAB_SPINE_V0_INVALID;
    if (parallel && (!options || !options->read_lab_config)) return FWLAB_SPINE_V0_INVALID;
    if (options && options->channel_executor && (!multihead ||
        !options->channel_executor->ops || !options->channel_executor->context ||
        !options->channel_executor->ops->submit || !options->channel_executor->ops->poll ||
        !options->channel_executor->ops->shutdown)) return FWLAB_SPINE_V0_INVALID;
    if ((mutation || channel) && (!options || !options->mutation_lab_config)) return FWLAB_SPINE_V0_INVALID;
    if (channel && (!options->channel_media ||
        options->channel_media->aggregate.context != config->media_binding->media.context ||
        options->channel_media->aggregate.ops != config->media_binding->media.ops ||
        memcmp(&options->channel_media->geometry, &config->media_binding->geometry,
               sizeof(config->media_binding->geometry)) ||
        memcmp(options->channel_media->media_uuid, config->media_binding->media_uuid, 16) ||
        memcmp(options->channel_media->media_uuid, config->media_uuid, 16)))
        return FWLAB_SPINE_V0_INVALID;
    if (window_v2 && !channel && (!options || !options->page_v2_media ||
        options->page_v2_media->scalar.context != config->media_binding->media.context ||
        options->page_v2_media->scalar.ops != config->media_binding->media.ops ||
        memcmp(&options->page_v2_media->geometry, &config->media_binding->geometry,
               sizeof(config->media_binding->geometry)) ||
        memcmp(options->page_v2_media->media_uuid, config->media_binding->media_uuid, 16) ||
        memcmp(options->page_v2_media->media_uuid, config->media_uuid, 16)))
        return FWLAB_SPINE_V0_INVALID;
    memset(&f, 0, sizeof(f));
    f.version = FWLAB_FTL_SCALE_VERSION;
    f.size = (uint16_t)sizeof(f);
    f.geometry = config->media_binding->geometry;
    memcpy(f.media_uuid, config->media_uuid, sizeof(f.media_uuid));
    f.namespace_ref = *namespace_ref;
    f.instance_nonce = ftl_nonce;
    f.provider_nonce = J0_M3P_PROVIDER_NONCE + config->volatile_nonce_seed;
    f.nfc_instance_nonce = nfc_nonce;
    f.nfc_operation_uid_limit = UINT64_MAX;
    f.host_sequence_limit = UINT64_MAX - 1u;
    f.record_sequence_limit = UINT64_MAX - 1u;
    f.mapping_slots = options && options->mapping_slots ? options->mapping_slots : pages;
    f.generation = config->generation;
    f.execution_epoch = config->execution_epoch;
    f.nfc_epoch = 1;
    n = nfc_configuration(&f.geometry);
    extended.version = FWLAB_FTL_SCALE_EXTENDED_VERSION;
    extended.size = sizeof(extended);
    extended.base = f;
    extended.max_transfer_lbas = FWLAB_FTL_SCALE_EXTENDED_MAX_LBAS;
    ftl_bytes = multihead ? fwlab_ftl_scale_multihead_v3_arena_size(&extended) :
               parallel ? fwlab_ftl_scale_parallel_read_arena_size(&extended) :
               window_v2 ? fwlab_ftl_scale_window_v2_arena_size(&extended) :
                           fwlab_ftl_scale_arena_size(&f);
    nfc_bytes = channel ? fwlab_nfc_channel_v2_arena_size() :
               lab ? fwlab_nfc_page_v2_lab_arena_size() :
               window_v2 ? fwlab_nfc_page_v2_arena_size() :
                           fwlab_nfc_scaled_arena_size(&n);
    if (!ftl_bytes || !nfc_bytes)
        return FWLAB_SPINE_V0_INVALID;
    storage = calloc(1, sizeof(*storage));
    if (!storage)
        return FWLAB_SPINE_V0_NO_CAPACITY;
    if (multihead && options->channel_executor) storage->executor = *options->channel_executor;
    storage->ftl_arena = calloc(1, ftl_bytes);
    storage->nfc_arena = window_v2
        ? aligned_alloc(channel ? fwlab_nfc_channel_v2_arena_alignment() :
                        lab ? fwlab_nfc_page_v2_lab_arena_alignment() :
                                   fwlab_nfc_page_v2_arena_alignment(), nfc_bytes)
        : calloc(1, nfc_bytes);
    result = FWLAB_SPINE_V0_NO_CAPACITY;
    if (!storage->ftl_arena || !storage->nfc_arena)
        goto failed;
    result = FWLAB_SPINE_V0_INVALID;
    if (window_v2) {
        struct fwlab_nfc_page_v2_config page = {0};
        struct fwlab_nfc_page_v2_provider page_provider;
        page.version = FWLAB_NFC_PAGE_V2_VERSION;
        page.size = sizeof(page);
        page.profile = FWLAB_NFC_PAGE_V2_PROFILE_R0;
        page.geometry = f.geometry;
        memcpy(page.media_uuid, f.media_uuid, sizeof(page.media_uuid));
        page.instance_nonce = nfc_nonce;
        page.operation_uid_limit = f.nfc_operation_uid_limit;
        page.controller_epoch = f.nfc_epoch;
        page.generation = f.generation;
        if (channel) {
            struct fwlab_nfc_page_v2_lab_mutation_config timed = *options->mutation_lab_config;
            timed.read.base = page;
            enum fwlab_nfc_api_result initialized = storage->executor.ops ?
                fwlab_nfc_channel_v2_init_executor(storage->nfc_arena, nfc_bytes, &timed,
                    options->channel_media, &storage->executor, &storage->channel_nfc) :
                fwlab_nfc_channel_v2_init(storage->nfc_arena, nfc_bytes, &timed,
                    options->channel_media, &storage->channel_nfc);
            if (initialized != FWLAB_NFC_API_OK) goto failed;
            page_provider = fwlab_nfc_channel_v2_provider(storage->channel_nfc);
            result = multihead ? fwlab_ftl_scale_init_multihead_v3(storage->ftl_arena, ftl_bytes,
                &extended, buffer, &page_provider, &storage->ftl) :
                fwlab_ftl_scale_init_window_v2(storage->ftl_arena, ftl_bytes,
                &extended, buffer, &page_provider, &storage->ftl);
        } else if (parallel) {
            struct fwlab_nfc_page_v2_lab_config lab = *options->read_lab_config;
            lab.base = page;
            if (fwlab_nfc_page_v2_lab_init(storage->nfc_arena, nfc_bytes, &lab,
                    options->page_v2_media, &storage->lab_nfc) != FWLAB_NFC_API_OK) goto failed;
            page_provider = fwlab_nfc_page_v2_lab_provider(storage->lab_nfc);
            result = fwlab_ftl_scale_init_parallel_read(storage->ftl_arena, ftl_bytes,
                &extended, buffer, &page_provider, &storage->ftl);
        } else if (mutation) {
            struct fwlab_nfc_page_v2_lab_mutation_config timed = *options->mutation_lab_config;
            timed.read.base = page;
            if (fwlab_nfc_page_v2_lab_mutation_init(storage->nfc_arena, nfc_bytes, &timed,
                    options->page_v2_media, &storage->lab_nfc) != FWLAB_NFC_API_OK) goto failed;
            page_provider = fwlab_nfc_page_v2_lab_provider(storage->lab_nfc);
            result = fwlab_ftl_scale_init_window_v2(storage->ftl_arena, ftl_bytes,
                &extended, buffer, &page_provider, &storage->ftl);
        } else {
            if (fwlab_nfc_page_v2_init(storage->nfc_arena, nfc_bytes, &page,
                options->page_v2_media, &storage->page_nfc) != FWLAB_NFC_API_OK)
                goto failed;
            page_provider = fwlab_nfc_page_v2_provider(storage->page_nfc);
            result = fwlab_ftl_scale_init_window_v2(storage->ftl_arena, ftl_bytes,
                &extended, buffer, &page_provider, &storage->ftl);
        }
    } else {
        staging = fwlab_ftl_scale_staging_provider(storage->ftl_arena, ftl_bytes);
        if (!staging.ops || fwlab_nfc_scaled_init(
            storage->nfc_arena, nfc_bytes, &n, nfc_nonce, &staging,
            &config->media_binding->media, &storage->nfc) != FWLAB_NFC_API_OK)
            goto failed;
        provider = fwlab_nfc_model_provider(storage->nfc);
        result = fwlab_ftl_scale_init(storage->ftl_arena, ftl_bytes, &f,
                                  buffer, &provider, &storage->ftl);
    }
    if (result != FWLAB_SPINE_V0_OK)
        goto failed;
    result = config->media_mode == J0_MEDIA_FORMAT
        ? fwlab_ftl_scale_format_start(storage->ftl, config->format_lba_count)
        : fwlab_ftl_scale_recover_start(storage->ftl, config->expected_lba_count);
    if (result != FWLAB_SPINE_V0_OK)
        goto failed;
    storage->magic = SCALE_STORAGE_MAGIC;
    memset(runner, 0, sizeof(*runner));
    runner->context = storage;
    runner->step = storage_step;
    runner->volume_query = storage_volume;
    runner->fini = storage_fini;
    runner->release = storage_release;
    runner->step_report = storage->page_nfc || storage->lab_nfc || storage->channel_nfc
        ? storage_step_report : NULL;
    *service = fwlab_ftl_scale_block_service(storage->ftl);
    return FWLAB_SPINE_V0_OK;
failed:
    /* bind/start perform no IO; the lower model has accepted no operation. */
    storage_release(storage);
    return result;
}

static enum fwlab_spine_result_v0 storage_bind(
    void *opaque, const struct j0_runtime_config *config,
    const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_block_namespace_ref_v0 *namespace_ref,
    uint64_t lifecycle_nonce, uint64_t ftl_nonce, uint64_t nfc_nonce,
    struct j0_storage_runner *runner, struct fwlab_block_service_v0 *service)
{
    return storage_bind_common(opaque, config, buffer, namespace_ref,
        lifecycle_nonce, ftl_nonce, nfc_nonce, runner, service, SCALE_C3);
}
static enum fwlab_spine_result_v0 storage_bind_window_v2(
    void *opaque, const struct j0_runtime_config *config,
    const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_block_namespace_ref_v0 *namespace_ref,
    uint64_t lifecycle_nonce, uint64_t ftl_nonce, uint64_t nfc_nonce,
    struct j0_storage_runner *runner, struct fwlab_block_service_v0 *service)
{
    return storage_bind_common(opaque, config, buffer, namespace_ref,
        lifecycle_nonce, ftl_nonce, nfc_nonce, runner, service, SCALE_PAGE2);
}
static enum fwlab_spine_result_v0 storage_bind_parallel_read_lab(
    void *opaque, const struct j0_runtime_config *config,
    const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_block_namespace_ref_v0 *namespace_ref,
    uint64_t lifecycle_nonce, uint64_t ftl_nonce, uint64_t nfc_nonce,
    struct j0_storage_runner *runner, struct fwlab_block_service_v0 *service)
{
    return storage_bind_common(opaque, config, buffer, namespace_ref,
        lifecycle_nonce, ftl_nonce, nfc_nonce, runner, service, SCALE_READ_LAB);
}
static enum fwlab_spine_result_v0 storage_bind_mutation_lab(
    void *opaque, const struct j0_runtime_config *config,
    const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_block_namespace_ref_v0 *namespace_ref,
    uint64_t lifecycle_nonce, uint64_t ftl_nonce, uint64_t nfc_nonce,
    struct j0_storage_runner *runner, struct fwlab_block_service_v0 *service)
{
    return storage_bind_common(opaque, config, buffer, namespace_ref,
                              lifecycle_nonce, ftl_nonce, nfc_nonce, runner, service, SCALE_MUTATION_LAB);
}

static enum fwlab_spine_result_v0 storage_bind_channel_lab(
    void *opaque, const struct j0_runtime_config *config,
    const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_block_namespace_ref_v0 *namespace_ref,
    uint64_t lifecycle_nonce, uint64_t ftl_nonce, uint64_t nfc_nonce,
    struct j0_storage_runner *runner, struct fwlab_block_service_v0 *service)
{
    return storage_bind_common(opaque, config, buffer, namespace_ref,
                              lifecycle_nonce, ftl_nonce, nfc_nonce, runner, service, SCALE_CHANNEL_LAB);
}

static enum fwlab_spine_result_v0 storage_bind_multihead_lab(
    void *opaque, const struct j0_runtime_config *config,
    const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_block_namespace_ref_v0 *namespace_ref,
    uint64_t lifecycle_nonce, uint64_t ftl_nonce, uint64_t nfc_nonce,
    struct j0_storage_runner *runner, struct fwlab_block_service_v0 *service)
{
    return storage_bind_common(opaque, config, buffer, namespace_ref,
                              lifecycle_nonce, ftl_nonce, nfc_nonce, runner, service, SCALE_MULTIHEAD_LAB);
}

void scale_storage_factory_init(struct j0_storage_factory *factory,
                                 struct scale_storage_options *options)
{
    factory->context = options;
    factory->bind = storage_bind;
}

void scale_storage_window_v2_factory_init(struct j0_storage_factory *factory,
                                           struct scale_storage_options *options)
{
    factory->context = options;
    factory->bind = storage_bind_window_v2;
}
void scale_storage_parallel_read_lab_factory_init(struct j0_storage_factory *factory,
                                                   struct scale_storage_options *options)
{
    factory->context = options;
    factory->bind = storage_bind_parallel_read_lab;
}
void scale_storage_mutation_lab_factory_init(struct j0_storage_factory *factory,
                                              struct scale_storage_options *options)
{
    factory->context = options;
    factory->bind = storage_bind_mutation_lab;
}

void scale_storage_channel_lab_factory_init(struct j0_storage_factory *factory,
                                             struct scale_storage_options *options)
{
    if (!factory) return;
    factory->context = options;
    factory->bind = storage_bind_channel_lab;
}

void scale_storage_multihead_lab_factory_init(struct j0_storage_factory *factory,
                                              struct scale_storage_options *options)
{
    if (!factory) return;
    factory->context = options;
    factory->bind = storage_bind_multihead_lab;
}

static struct scale_storage *from_runtime(const struct j0_runtime *runtime)
{
    struct scale_storage *storage;
    if (!runtime || runtime->storage.step != storage_step)
        return NULL;
    storage = runtime->storage.context;
    return storage && storage->magic == SCALE_STORAGE_MAGIC ? storage : NULL;
}
enum fwlab_spine_result_v0 scale_storage_begin_timed_read(struct j0_runtime *runtime)
{
    struct scale_storage *storage = from_runtime(runtime);
    enum fwlab_spine_result_v0 result;
    bool idle = false;
    if (!storage || !storage->lab_nfc) return FWLAB_SPINE_V0_INVALID;
    if (runtime->active_admissions) return FWLAB_SPINE_V0_WRONG_STATE;
    result = fwlab_ftl_scale_can_enter_read_only(storage->ftl);
    if (result != FWLAB_SPINE_V0_OK) return result;
    if (fwlab_nfc_page_v2_lab_live_idle(storage->lab_nfc, &idle) != FWLAB_NFC_API_OK || !idle)
        return FWLAB_SPINE_V0_WRONG_STATE;
    /* Both transitions are local state changes, with no callbacks or caller
     * interleaving between the preconditions and their application. */
    if (fwlab_nfc_page_v2_lab_begin_timed_read(storage->lab_nfc) != FWLAB_NFC_API_OK)
        return FWLAB_SPINE_V0_WRONG_STATE;
    return fwlab_ftl_scale_enter_read_only(storage->ftl);
}
enum fwlab_spine_result_v0 scale_storage_lab_snapshot(const struct j0_runtime *runtime,
    struct fwlab_nfc_page_v2_lab_stats *out)
{
    struct scale_storage *storage = from_runtime(runtime);
    return storage && storage->lab_nfc &&
        fwlab_nfc_page_v2_lab_snapshot(storage->lab_nfc, out) == FWLAB_NFC_API_OK ?
        FWLAB_SPINE_V0_OK : FWLAB_SPINE_V0_INVALID;
}
enum fwlab_spine_result_v0 scale_storage_lab_trace_at(const struct j0_runtime *runtime,
    uint32_t index, struct fwlab_nfc_page_v2_lab_trace *out)
{
    struct scale_storage *storage = from_runtime(runtime);
    return storage && storage->lab_nfc &&
        fwlab_nfc_page_v2_lab_trace_at(storage->lab_nfc, index, out) == FWLAB_NFC_API_OK ?
        FWLAB_SPINE_V0_OK : FWLAB_SPINE_V0_INVALID;
}

enum fwlab_spine_result_v0 scale_storage_channel_snapshot(const struct j0_runtime *runtime,
    struct fwlab_nfc_channel_v2_stats *out)
{
    struct scale_storage *storage = from_runtime(runtime);
    return storage && storage->channel_nfc &&
        fwlab_nfc_channel_v2_snapshot(storage->channel_nfc, out) == FWLAB_NFC_API_OK ?
        FWLAB_SPINE_V0_OK : FWLAB_SPINE_V0_INVALID;
}

const struct fwlab_nfc_channel_v2 *scale_storage_channel_hub(const struct j0_runtime *runtime)
{
    struct scale_storage *storage = from_runtime(runtime);
    return storage ? storage->channel_nfc : NULL;
}

enum fwlab_spine_result_v0 scale_storage_query(
    const struct j0_runtime *runtime, struct fwlab_ftl_scale_status *status)
{
    struct scale_storage *storage = from_runtime(runtime);
    return storage ? fwlab_ftl_scale_query(storage->ftl, status)
                   : FWLAB_SPINE_V0_INVALID;
}

enum fwlab_spine_result_v0 scale_storage_checkpoint(struct j0_runtime *runtime)
{
    struct scale_storage *storage = from_runtime(runtime);
    return storage ? fwlab_ftl_scale_checkpoint_start(storage->ftl)
                   : FWLAB_SPINE_V0_INVALID;
}

enum fwlab_spine_result_v0 scale_storage_gc(
    struct j0_runtime *runtime, uint32_t needed_pages)
{
    struct scale_storage *storage = from_runtime(runtime);
    return storage ? fwlab_ftl_scale_gc_start(storage->ftl, needed_pages)
                   : FWLAB_SPINE_V0_INVALID;
}
