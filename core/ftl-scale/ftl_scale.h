/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_FTL_SCALE_H
#define FWLAB_FTL_SCALE_H

#include <stddef.h>
#include <stdint.h>
#include "fwlab/contracts/block_service_v0.h"
#include "fwlab/contracts/controller_buffer_v0.h"
#include "fwlab/contracts/nfc_provider.h"
#include "fwlab/private/block_volume_v0.h"

#define FWLAB_FTL_SCALE_VERSION 1u
#define FWLAB_FTL_SCALE_LBA_BYTES 512u
#define FWLAB_FTL_SCALE_MAX_LBAS 16u

struct fwlab_ftl_scale;

/* This is a construction/resource contract, not the volume's capacity.
 * FORMAT supplies capacity; RECOVER obtains it only from persistent roots. */
struct fwlab_ftl_scale_config {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nfc_geometry geometry;
    uint8_t media_uuid[16];
    struct fwlab_block_namespace_ref_v0 namespace_ref;
    uint64_t instance_nonce;
    uint64_t provider_nonce;
    uint64_t nfc_instance_nonce;
    uint64_t nfc_operation_uid_limit;
    uint64_t host_sequence_limit;
    uint64_t record_sequence_limit;
    uint32_t mapping_slots;
    uint32_t generation;
    uint32_t execution_epoch;
    uint32_t nfc_epoch;
    uint32_t reserved1[4];
};

struct fwlab_ftl_scale_status {
    uint64_t record_sequence;
    uint64_t map_sequence;
    uint64_t durable_frontier;
    uint64_t next_block_uid;
    uint64_t nfc_children;
    uint64_t checkpoints;
    uint64_t garbage_collections;
    uint64_t arena_bytes;
    uint32_t free_blocks;
    uint32_t victim_blocks;
    uint32_t fault_code;
    uint8_t ready;
    uint8_t busy;
    uint8_t admission_closed;
    uint8_t quarantined;
};

int fwlab_ftl_scale_config_valid(const struct fwlab_ftl_scale_config *config);
size_t fwlab_ftl_scale_arena_alignment(void);
size_t fwlab_ftl_scale_arena_size(const struct fwlab_ftl_scale_config *config);
/* Obtain after allocating the arena, before constructing NFC. No operation
 * may execute until NFC and FTL initialization have both succeeded. */
struct fwlab_nfc_buffer_provider fwlab_ftl_scale_staging_provider(
    void *arena, size_t arena_size);
enum fwlab_spine_result_v0 fwlab_ftl_scale_init(
    void *arena, size_t arena_size,
    const struct fwlab_ftl_scale_config *config,
    const struct fwlab_controller_buffer_port_v0 *controller_buffer,
    const struct fwlab_nfc_provider *nfc,
    struct fwlab_ftl_scale **ftl);
enum fwlab_spine_result_v0 fwlab_ftl_scale_format_start(
    struct fwlab_ftl_scale *ftl, uint64_t lba_count);
enum fwlab_spine_result_v0 fwlab_ftl_scale_recover_start(
    struct fwlab_ftl_scale *ftl, uint64_t expected_lba_count);
struct fwlab_block_service_v0 fwlab_ftl_scale_block_service(
    struct fwlab_ftl_scale *ftl);
enum fwlab_spine_result_v0 fwlab_ftl_scale_volume_query(
    const struct fwlab_ftl_scale *ftl,
    struct fwlab_block_volume_binding_v0 *binding);
enum fwlab_spine_result_v0 fwlab_ftl_scale_step(
    struct fwlab_ftl_scale *ftl, uint32_t budget, uint32_t *used);
enum fwlab_spine_result_v0 fwlab_ftl_scale_query(
    const struct fwlab_ftl_scale *ftl, struct fwlab_ftl_scale_status *status);
enum fwlab_spine_result_v0 fwlab_ftl_scale_checkpoint_start(
    struct fwlab_ftl_scale *ftl);
enum fwlab_spine_result_v0 fwlab_ftl_scale_gc_start(
    struct fwlab_ftl_scale *ftl, uint32_t needed_pages);
enum fwlab_spine_result_v0 fwlab_ftl_scale_fini(struct fwlab_ftl_scale *ftl);

#endif
