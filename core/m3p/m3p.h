/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_M3P_H
#define FWLAB_M3P_H

#include <stddef.h>
#include <stdint.h>

#include "fwlab/contracts/block_service_v0.h"
#include "fwlab/contracts/controller_buffer_v0.h"
#include "fwlab/contracts/nfc_provider.h"

#define FWLAB_M3P_VERSION 1u
#define FWLAB_M3P_NAMESPACE_LBAS 2048u
#define FWLAB_M3P_LBA_BYTES 512u
#define FWLAB_M3P_MAX_LBAS 16u

struct fwlab_m3p;

struct fwlab_m3p_config {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint8_t media_uuid[16];
    struct fwlab_block_namespace_ref_v0 namespace_ref;
    uint64_t instance_nonce;
    uint64_t provider_nonce;
    uint64_t nfc_instance_nonce;
    uint64_t next_nfc_operation_uid;
    uint32_t generation;
    uint32_t execution_epoch;
    uint32_t nfc_epoch;
    uint32_t nfc_operation_uid_limit;
    uint32_t host_sequence_limit;
    uint32_t record_sequence_limit;
    uint32_t reserved1[4];
};

enum fwlab_m3p_maintenance_kind {
    FWLAB_M3P_MAINTENANCE_NONE = 0,
    FWLAB_M3P_MAINTENANCE_FORMAT = 1,
    FWLAB_M3P_MAINTENANCE_RECOVERY = 2,
    FWLAB_M3P_MAINTENANCE_GC = 3
};

enum fwlab_m3p_maintenance_state {
    FWLAB_M3P_MAINTENANCE_IDLE = 0,
    FWLAB_M3P_MAINTENANCE_RUNNING = 1,
    FWLAB_M3P_MAINTENANCE_SUCCEEDED = 2,
    FWLAB_M3P_MAINTENANCE_FAILED = 3,
    FWLAB_M3P_MAINTENANCE_QUARANTINED = 4
};

struct fwlab_m3p_recovery_status {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint32_t state;
    uint32_t pages_scanned;
    uint32_t checkpoint_generation;
    uint32_t durable_map_sequence;
    uint32_t durable_frontier;
    uint32_t fault_code;
    uint32_t reserved1[4];
};

struct fwlab_m3p_gc_status {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint32_t state;
    uint32_t victim_block;
    uint32_t destination_block;
    uint32_t live_pages;
    uint32_t moved_pages;
    uint32_t reclaimable_pages;
    uint32_t gc_uid;
    uint32_t switch_map_sequence;
    uint32_t successful_erase_count;
    uint32_t fault_code;
    uint32_t reserved1[4];
};

struct fwlab_m3p_step_result {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint32_t units_used;
    uint32_t ftl_transitions;
    uint32_t nfc_transitions;
    uint32_t nfc_events;
    uint32_t reserved1[4];
};

int fwlab_m3p_config_valid(const struct fwlab_m3p_config *config);
size_t fwlab_m3p_arena_alignment(void);
size_t fwlab_m3p_arena_size(const struct fwlab_m3p_config *config);

enum fwlab_spine_result_v0 fwlab_m3p_init(
    void *arena,
    size_t arena_size,
    const struct fwlab_m3p_config *config,
    const struct fwlab_controller_buffer_port_v0 *controller_buffer,
    const struct fwlab_nfc_provider *nfc,
    struct fwlab_m3p **m3p
);

enum fwlab_spine_result_v0 fwlab_m3p_format_start(struct fwlab_m3p *m3p);
enum fwlab_spine_result_v0 fwlab_m3p_recover_start(struct fwlab_m3p *m3p);
enum fwlab_spine_result_v0 fwlab_m3p_recovery_query(
    const struct fwlab_m3p *m3p,
    struct fwlab_m3p_recovery_status *status
);
struct fwlab_block_service_v0 fwlab_m3p_block_service(
    struct fwlab_m3p *m3p
);
enum fwlab_spine_result_v0 fwlab_m3p_step(
    struct fwlab_m3p *m3p,
    uint32_t budget,
    struct fwlab_m3p_step_result *result
);
enum fwlab_spine_result_v0 fwlab_m3p_force_gc_start(struct fwlab_m3p *m3p);
enum fwlab_spine_result_v0 fwlab_m3p_force_gc_query(
    const struct fwlab_m3p *m3p,
    struct fwlab_m3p_gc_status *status
);
enum fwlab_spine_result_v0 fwlab_m3p_epoch_query(
    const struct fwlab_m3p *m3p,
    struct fwlab_block_epoch_status_v0 *status
);
enum fwlab_spine_result_v0 fwlab_m3p_fini(struct fwlab_m3p *m3p);

#endif /* FWLAB_M3P_H */
