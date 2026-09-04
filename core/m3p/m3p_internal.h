/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_M3P_INTERNAL_H
#define FWLAB_M3P_INTERNAL_H

#include "m3p.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define M3P_MAGIC UINT64_C(0x4d335046544c5631)
#define M3P_LPN_COUNT 256u
#define M3P_SECTORS_PER_PAGE 8u
#define M3P_PAGE_BYTES 4096u
#define M3P_OOB_BYTES 128u
#define M3P_PHYSICAL_PAGES 512u
#define M3P_BLOCKS 16u
#define M3P_PAGES_PER_BLOCK 32u
#define M3P_DATA_POOL_BLOCKS 10u
#define M3P_MAX_PENDING 4u
#define M3P_MAX_DELTAS 3u
#define M3P_STAGING_FRAMES 3u
#define M3P_MAX_RECOVERED_MAPS 64u
#define M3P_TRACE_BOUND 18433u

#define M3P_OOB_MAGIC UINT32_C(0x4d335031)
#define M3P_FORMAT_VERSION 1u
#define M3P_OOB_HEADER_BYTES 128u
#define M3P_MAP_MAIN_MAGIC UINT32_C(0x4d545831)
#define M3P_CHECKPOINT_COMMIT_MAGIC UINT32_C(0x43504331)

enum m3p_page_type {
    M3P_PAGE_DATA = 1,
    M3P_PAGE_MAP_TXN = 2,
    M3P_PAGE_CHECKPOINT_BODY = 3,
    M3P_PAGE_CHECKPOINT_COMMIT = 4
};

enum m3p_copy_kind {
    M3P_COPY_HOST = 1,
    M3P_COPY_GC = 2
};

enum m3p_map_subtype {
    M3P_MAP_WRITE = 1,
    M3P_MAP_TRIM = 2,
    M3P_MAP_RELOCATION = 3,
    M3P_MAP_GC_SWITCH = 4
};

enum m3p_l2p_state {
    M3P_L2P_UNMAPPED = 0,
    M3P_L2P_VALUE = 1,
    M3P_L2P_TOMBSTONE = 2
};

enum m3p_p2l_state {
    M3P_P2L_FREE = 0,
    M3P_P2L_LIVE = 1,
    M3P_P2L_STALE = 2,
    M3P_P2L_ORPHAN = 3,
    M3P_P2L_TORN = 4,
    M3P_P2L_DURABLE_PINNED = 5,
    M3P_P2L_VISIBLE_PENDING = 6,
    M3P_P2L_GC_SOURCE_PINNED = 7,
    M3P_P2L_GC_DEST_STAGED = 8,
    M3P_P2L_UNAVAILABLE = 9
};

enum m3p_block_role {
    M3P_ROLE_DATA = 1,
    M3P_ROLE_JOURNAL = 2,
    M3P_ROLE_CHECKPOINT = 3,
    M3P_ROLE_RESERVE = 4,
    M3P_ROLE_REPLACEMENT = 5,
    M3P_ROLE_UNAVAILABLE = 6
};

struct m3p_map_entry {
    uint8_t state;
    uint8_t valid_mask;
    uint8_t block;
    uint8_t page;
    uint16_t erase_generation;
    uint16_t reserved;
    uint32_t data_record_sequence;
    uint32_t map_sequence;
    uint32_t main_crc;
    uint32_t logical_version;
};

struct m3p_delta {
    uint16_t lpn;
    struct m3p_map_entry target;
    struct m3p_map_entry prior;
};

struct m3p_pending {
    uint8_t active;
    uint8_t subtype;
    uint8_t delta_count;
    uint8_t reserved0;
    uint32_t host_sequence;
    uint32_t transaction_sequence;
    struct m3p_delta delta[M3P_MAX_DELTAS];
};

struct m3p_oob {
    uint8_t page_type;
    uint8_t flags;
    uint8_t copy_kind;
    uint8_t valid_mask;
    uint32_t namespace_id;
    uint32_t lpn;
    uint16_t erase_generation;
    uint32_t record_sequence;
    uint32_t transaction_sequence;
    uint32_t predecessor_map_sequence;
    uint32_t resulting_map_sequence;
    uint32_t referenced_data_sequence;
    uint32_t main_length;
    uint32_t main_crc;
    uint32_t checkpoint_generation;
    uint32_t checkpoint_covered_sequence;
    uint32_t durable_frontier;
    uint8_t media_uuid[16];
    uint8_t source_block;
    uint8_t source_page;
    uint32_t source_data_sequence;
};

struct m3p_map_record {
    uint8_t subtype;
    uint8_t delta_count;
    uint16_t flags;
    uint32_t transaction_sequence;
    uint32_t predecessor_map_sequence;
    uint32_t resulting_map_sequence;
    uint32_t host_sequence;
    uint32_t captured_frontier;
    uint32_t gc_uid;
    uint8_t gc_source_block;
    uint8_t gc_destination_block;
    uint16_t gc_expected_live;
    uint16_t gc_moved;
    struct m3p_delta delta[M3P_MAX_DELTAS];
    uint32_t relocation_sequence[25];
};

struct m3p_checkpoint_commit {
    uint32_t generation;
    uint8_t body_block;
    uint8_t body_page;
    uint16_t body_erase_generation;
    uint32_t body_main_crc;
    uint32_t body_oob_crc;
    uint32_t covered_map_sequence;
    uint32_t durable_frontier;
    uint32_t journal_generation;
    uint32_t commit_record_sequence;
    uint8_t media_uuid[16];
};

enum m3p_child_kind {
    M3P_CHILD_NONE = 0,
    M3P_CHILD_READ = 1,
    M3P_CHILD_PROGRAM = 2,
    M3P_CHILD_ERASE = 3
};

enum m3p_child_state {
    M3P_CHILD_IDLE = 0,
    M3P_CHILD_SUBMIT_FIRST = 1,
    M3P_CHILD_WAIT_FIRST = 2,
    M3P_CHILD_SUBMIT_SECOND = 3,
    M3P_CHILD_WAIT_SECOND = 4,
    M3P_CHILD_DONE = 5,
    M3P_CHILD_FAILED = 6
};

struct m3p_child {
    uint8_t kind;
    uint8_t state;
    uint8_t frame;
    uint8_t reserved0;
    struct fwlab_nfc_ppa ppa;
    struct fwlab_nfc_request first;
    struct fwlab_nfc_request second;
    struct fwlab_nfc_completion completion;
    uint32_t fault_code;
};

enum m3p_operation_state {
    M3P_OPERATION_FREE = 0,
    M3P_OPERATION_PREPARED = 1,
    M3P_OPERATION_PAGE_PREPARE = 2,
    M3P_OPERATION_PAGE_READ = 3,
    M3P_OPERATION_PAGE_PROGRAM = 4,
    M3P_OPERATION_PENDING_INSTALL = 5,
    M3P_OPERATION_COMMIT_PENDING = 6,
    M3P_OPERATION_COMMIT_PROGRAM = 7,
    M3P_OPERATION_CHECKPOINT = 8,
    M3P_OPERATION_READ_PAGE = 9,
    M3P_OPERATION_READ_PUBLISH = 10,
    M3P_OPERATION_TERMINAL = 11,
    M3P_OPERATION_DRAINING = 12,
    M3P_OPERATION_DRAINED = 13,
    M3P_OPERATION_RETIRED = 14,
    M3P_OPERATION_FAILED = 15
};

struct m3p_operation {
    uint8_t state;
    uint8_t cancel_requested;
    uint8_t effect_seen;
    uint8_t page_index;
    uint8_t delta_count;
    uint8_t commit_slot;
    uint8_t reserved0[2];
    uint16_t first_lpn;
    uint16_t last_lpn;
    uint16_t current_lpn;
    uint16_t reserved1;
    uint32_t host_sequence;
    uint32_t captured_frontier;
    uint32_t fault_code;
    struct fwlab_block_request_v0 request;
    struct fwlab_block_status_v0 status;
    struct m3p_delta delta[M3P_MAX_DELTAS];
    uint8_t io_bytes[FWLAB_M3P_MAX_LBAS * FWLAB_M3P_LBA_BYTES];
};

enum m3p_work_state {
    M3P_WORK_IDLE = 0,
    M3P_FORMAT_BODY = 1,
    M3P_FORMAT_COMMIT = 2,
    M3P_FORMAT_DONE = 3,
    M3P_RECOVERY_SCAN_START = 10,
    M3P_RECOVERY_SCAN_WAIT = 11,
    M3P_RECOVERY_FINALIZE = 12,
    M3P_RECOVERY_DONE = 13,
    M3P_RECOVERY_CHECKPOINT_ERASE = 14,
    M3P_RECOVERY_JOURNAL_ERASE = 15,
    M3P_GC_PREPARE = 20,
    M3P_GC_READ = 21,
    M3P_GC_PROGRAM = 22,
    M3P_GC_RELOCATION = 23,
    M3P_GC_SWITCH = 24,
    M3P_GC_ERASE = 25,
    M3P_GC_DONE = 26,
    M3P_WORK_FAILED = 31
};

struct m3p_recovery_record {
    uint8_t valid;
    uint8_t block;
    uint8_t page;
    uint8_t reserved0;
    struct m3p_oob oob;
    struct m3p_map_record map;
};

struct fwlab_m3p {
    uint64_t magic;
    struct fwlab_m3p_config config;
    struct fwlab_controller_buffer_port_v0 controller_buffer;
    struct fwlab_nfc_provider nfc;
    struct fwlab_block_service_v0 service;
    uint8_t initialized;
    uint8_t ready;
    uint8_t admission_closed;
    uint8_t quarantined;
    uint8_t work_kind;
    uint8_t work_state;
    uint8_t fair_cursor;
    uint8_t pending_count;
    uint8_t pending_head;
    uint8_t pending_tail;
    uint8_t active_journal_block;
    uint8_t inactive_journal_block;
    uint8_t active_checkpoint_block;
    uint8_t inactive_checkpoint_block;
    uint8_t journal_page;
    uint8_t checkpoint_page;
    uint8_t reserve_block;
    uint8_t replacement_block;
    uint8_t replacement_used;
    uint8_t recovery_completed;
    uint8_t recovery_resume_gc;
    uint8_t recovery_checkpoint_cleanup;
    uint8_t recovery_journal_cleanup;
    uint8_t recovery_resume_state;
    uint8_t nfc_close_started;
    uint8_t nfc_quiescent;
    uint8_t checkpoint_flow;
    uint8_t checkpoint_return_state;
    uint8_t checkpoint_target_block;
    uint8_t checkpoint_target_page;
    uint8_t checkpoint_old_journal;
    uint8_t checkpoint_rotating;
    uint8_t checkpoint_old_block;
    uint32_t record_sequence;
    uint32_t map_sequence;
    uint32_t host_sequence;
    uint32_t durable_frontier;
    uint32_t checkpoint_generation;
    uint32_t checkpoint_covered_sequence;
    uint32_t journal_generation;
    uint32_t checkpoint_body_main_crc;
    uint32_t checkpoint_body_oob_crc;
    uint32_t child_starts;
    uint32_t cancel_count;
    uint64_t next_child_uid;
    uint64_t close_lifecycle_nonce;
    uint32_t close_execution_epoch;
    struct m3p_map_entry durable[M3P_LPN_COUNT];
    struct m3p_map_entry visible[M3P_LPN_COUNT];
    uint8_t p2l[M3P_PHYSICAL_PAGES];
    uint8_t block_role[M3P_BLOCKS];
    uint8_t block_health[M3P_BLOCKS];
    uint8_t block_next_page[M3P_BLOCKS];
    uint16_t block_erase_generation[M3P_BLOCKS];
    uint16_t block_erase_count[M3P_BLOCKS];
    struct m3p_pending pending[M3P_MAX_PENDING];
    struct m3p_operation operation;
    struct m3p_child child;
    uint8_t frame_main[M3P_STAGING_FRAMES][M3P_PAGE_BYTES];
    uint8_t frame_oob[M3P_STAGING_FRAMES][M3P_OOB_BYTES];
    uint32_t recovery_page;
    uint32_t recovery_map_count;
    uint32_t recovery_fault_code;
    struct m3p_recovery_record recovered_map[M3P_MAX_RECOVERED_MAPS];
    struct m3p_map_entry recovered_checkpoint[M3P_LPN_COUNT];
    struct m3p_map_entry checkpoint_candidate[M3P_LPN_COUNT];
    struct m3p_checkpoint_commit recovered_commit;
    uint8_t recovery_have_body;
    uint8_t recovery_have_commit;
    uint8_t recovery_body_block;
    uint8_t recovery_body_page;
    uint8_t recovery_tail_seen[M3P_BLOCKS];
    uint8_t recovery_data_valid[M3P_PHYSICAL_PAGES];
    struct m3p_oob recovered_data[M3P_PHYSICAL_PAGES];
    uint32_t recovery_body_crc;
    uint32_t gc_uid;
    uint8_t gc_victim;
    uint8_t gc_destination;
    uint8_t gc_live_count;
    uint8_t gc_moved;
    uint8_t gc_reclaimable;
    uint8_t reserved_gc;
    uint8_t gc_source_page[25];
    uint16_t gc_source_lpn[25];
    struct m3p_delta gc_delta[25];
    uint32_t gc_relocation_sequence[25];
    uint32_t gc_switch_sequence;
    uint32_t gc_fault_code;
};

uint32_t m3p_crc32c(const uint8_t *bytes, size_t size);
uint64_t m3p_hash64(const uint8_t *bytes, size_t size);
uint16_t m3p_get_le16(const uint8_t *bytes);
uint32_t m3p_get_le32(const uint8_t *bytes);
void m3p_put_le16(uint8_t *bytes, uint16_t value);
void m3p_put_le32(uint8_t *bytes, uint32_t value);
int m3p_bytes_zero(const void *value, size_t size);
void m3p_encode_oob(uint8_t bytes[128], const struct m3p_oob *oob);
int m3p_decode_oob(const uint8_t bytes[128], struct m3p_oob *oob);
void m3p_encode_map(uint8_t bytes[4096], const struct m3p_map_record *record);
int m3p_decode_map(const uint8_t bytes[4096], struct m3p_map_record *record);
void m3p_encode_checkpoint_body(
    uint8_t bytes[4096], const struct m3p_map_entry map[M3P_LPN_COUNT]);
int m3p_decode_checkpoint_body(
    const uint8_t bytes[4096], struct m3p_map_entry map[M3P_LPN_COUNT]);
void m3p_encode_checkpoint_commit(
    uint8_t bytes[4096], const struct m3p_checkpoint_commit *commit);
int m3p_decode_checkpoint_commit(
    const uint8_t bytes[4096], struct m3p_checkpoint_commit *commit);

uint16_t m3p_physical_index(uint8_t block, uint8_t page);
int m3p_map_entry_equal(const struct m3p_map_entry *left,
                        const struct m3p_map_entry *right);
int m3p_namespace_equal(const struct fwlab_block_namespace_ref_v0 *left,
                        const struct fwlab_block_namespace_ref_v0 *right);
int m3p_token_equal(const struct fwlab_block_op_token_v0 *left,
                    const struct fwlab_block_op_token_v0 *right);
void m3p_mapping_reset(struct fwlab_m3p *m3p);
int m3p_allocate_data_page(struct fwlab_m3p *m3p,
                           struct fwlab_nfc_ppa *ppa);
void m3p_publish_visible_delta(struct fwlab_m3p *m3p,
                               const struct m3p_delta *delta);
void m3p_publish_durable_delta(struct fwlab_m3p *m3p,
                               const struct m3p_delta *delta,
                               uint32_t map_sequence);
int m3p_pending_append(struct fwlab_m3p *m3p, uint8_t subtype,
                       uint32_t host_sequence,
                       const struct m3p_delta *delta, uint8_t delta_count);
struct m3p_pending *m3p_pending_front(struct fwlab_m3p *m3p);
void m3p_pending_pop(struct fwlab_m3p *m3p);
int m3p_pending_overlaps(const struct fwlab_m3p *m3p, uint16_t first_lpn,
                         uint16_t last_lpn);

enum fwlab_spine_result_v0 m3p_child_read_start(
    struct fwlab_m3p *m3p, uint8_t frame, struct fwlab_nfc_ppa ppa);
enum fwlab_spine_result_v0 m3p_child_program_start(
    struct fwlab_m3p *m3p, uint8_t frame, struct fwlab_nfc_ppa ppa);
enum fwlab_spine_result_v0 m3p_child_erase_start(
    struct fwlab_m3p *m3p, struct fwlab_nfc_ppa ppa);
int m3p_nfc_submit_or_step(struct fwlab_m3p *m3p,
                           uint32_t *provider_transitions);
int m3p_nfc_poll(struct fwlab_m3p *m3p, uint32_t *events);
void m3p_child_consume(struct fwlab_m3p *m3p);
struct fwlab_nfc_buffer_provider m3p_staging_provider(struct fwlab_m3p *m3p);

int m3p_runtime_drive(struct fwlab_m3p *m3p);
int m3p_recovery_drive(struct fwlab_m3p *m3p);
int m3p_gc_drive(struct fwlab_m3p *m3p);
int m3p_gc_collect_live_pages(struct fwlab_m3p *m3p);
void m3p_operation_fail(struct fwlab_m3p *m3p, uint32_t fault_code,
                        int effect_seen);
void m3p_operation_succeed(struct fwlab_m3p *m3p,
                           uint32_t durability_witness,
                           uint32_t frontier_sequence);
int m3p_start_checkpoint(struct fwlab_m3p *m3p, uint8_t return_state);
int m3p_checkpoint_drive(struct fwlab_m3p *m3p);

#endif /* FWLAB_M3P_INTERNAL_H */
