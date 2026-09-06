/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_FTL_SCALE_INTERNAL_H
#define FWLAB_FTL_SCALE_INTERNAL_H

#include "ftl_scale.h"
#include <stdbool.h>

#define SF_MAGIC UINT64_C(0x5343414c4546544c)
#define SF_FORMAT_VERSION 1u
#define SF_PAGE_BYTES 4096u
#define SF_OOB_BYTES 128u
#define SF_SECTORS_PER_PAGE 8u
#define SF_FRAMES 3u
#define SF_MAX_HOST_DELTAS 3u
#define SF_MAX_DELTAS 64u
#define SF_NONE UINT32_MAX
#define SF_HEAP_NONE UINT32_MAX
#define SF_FAULT_METADATA UINT32_C(0x53464d44)
#define SF_FAULT_IO UINT32_C(0x5346494f)
#define SF_FAULT_STATE UINT32_C(0x53465354)

enum sf_map_state { SF_UNMAPPED = 0, SF_VALUE = 1, SF_TOMBSTONE = 2 };
enum sf_block_role {
    SF_META = 1, SF_FREE, SF_HOST_OPEN, SF_GC_DEST, SF_CLOSED,
    SF_RECLAIM_PENDING, SF_BAD
};
struct sf_map_entry {
    uint32_t ppa;
    uint16_t erase_generation;
    uint8_t valid_mask;
    uint8_t state;
    uint64_t data_uid;
};
struct sf_block_disk {
    uint64_t block_uid;
    uint16_t erase_generation;
    uint16_t allocation_end;
    uint8_t role;
    uint8_t health;
    uint16_t flags;
};
struct sf_block {
    struct sf_block_disk disk;
    uint32_t free_heap_pos;
    uint32_t victim_heap_pos;
    uint32_t wear;
    uint16_t live_pages;
    uint16_t reserved_pages;
};
struct sf_delta {
    uint32_t lpn;
    uint32_t reserved;
    struct sf_map_entry before;
    struct sf_map_entry after;
};
_Static_assert(sizeof(struct sf_map_entry) == 16, "compact resident map");
_Static_assert(sizeof(struct sf_block_disk) == 16, "persistent block facts");
_Static_assert(sizeof(struct sf_block) == 32, "bounded block summary");
_Static_assert(sizeof(struct sf_delta) == 40, "one page holds a GC transaction");

struct sf_layout {
    struct fwlab_nfc_geometry geometry;
    uint64_t lba_count;
    uint32_t lpn_count;
    uint32_t physical_pages;
    uint32_t physical_blocks;
    uint32_t cp_map_pages;
    uint32_t cp_block_pages;
    uint32_t cp_blocks;
    uint32_t journal_slots; /* Includes the slot-zero rail header. */
    uint32_t journal_blocks;
    uint32_t cp_base[2];
    uint32_t journal_base[2][2];
    uint32_t data_first_block;
};
struct sf_root {
    uint8_t media_uuid[16];
    struct sf_layout layout;
    uint64_t generation;
    uint64_t covered_record_seq;
    uint64_t covered_map_seq;
    uint64_t durable_frontier;
    uint64_t next_block_uid;
    uint64_t cp_digest;
    uint64_t rail_header_digest[2];
    uint32_t bank;
};
enum sf_record_kind {
    SF_OPEN_HOST = 1, SF_OPEN_GC_DEST, SF_CLOSE, SF_MAP_GROUP,
    SF_GC_COMMIT, SF_ERASE_INTENT, SF_ERASE_DONE
};
struct sf_record {
    uint64_t epoch;
    uint64_t sequence;
    uint64_t predecessor;
    uint64_t before_map_seq;
    uint64_t after_map_seq;
    uint64_t durable_frontier;
    uint64_t block_uid;
    uint64_t other_block_uid;
    uint64_t intent_sequence;
    uint32_t block;
    uint32_t other_block;
    uint16_t erase_generation;
    uint16_t final_erase_generation;
    uint16_t count;
    uint8_t kind;
    uint8_t health;
    struct sf_delta delta[SF_MAX_DELTAS];
};

enum sf_io_kind { SF_IO_READ = 1, SF_IO_PROGRAM = 2, SF_IO_ERASE = 3 };
enum sf_io_phase {
    SF_IO_IDLE = 0, SF_IO_SUBMIT_FIRST, SF_IO_WAIT_FIRST,
    SF_IO_SUBMIT_SECOND, SF_IO_WAIT_SECOND, SF_IO_DONE
};
struct sf_io_result {
    enum fwlab_spine_result_v0 result;
    struct fwlab_nfc_completion completion;
    uint32_t ppa;
    uint8_t kind;
    uint8_t frame;
    uint8_t read_valid;
    uint8_t reserved;
};
struct sf_io {
    struct fwlab_nfc_request first;
    struct fwlab_nfc_request second;
    struct sf_io_result result;
    uint64_t next_uid;
    uint32_t phase;
    uint8_t main[SF_FRAMES][SF_PAGE_BYTES];
    uint8_t oob[SF_FRAMES][SF_OOB_BYTES];
};

/* Private state layouts are owned by their implementation files. They are
 * embedded, never dynamically registered and never shared across FTL engines. */
#include "ftl_scale_meta.h"
#include "ftl_scale_work.h"

struct fwlab_ftl_scale {
    uint64_t magic;
    struct fwlab_ftl_scale_config config;
    struct fwlab_controller_buffer_port_v0 controller_buffer;
    struct fwlab_nfc_provider nfc;
    struct fwlab_block_service_v0 service;
    struct sf_root root;
    struct sf_map_entry *map;
    struct sf_block *blocks;
    uint8_t *validity;
    uint32_t *free_heap;
    uint32_t *victim_heap;
    uint32_t free_count;
    uint32_t victim_count;
    uint32_t host_head;
    uint32_t physical_blocks;
    uint32_t physical_pages;
    uint32_t journal_next;
    uint32_t fault_code;
    uint64_t record_sequence;
    uint64_t map_sequence;
    uint64_t durable_frontier;
    uint64_t next_block_uid;
    uint64_t expected_lba_count;
    uint64_t erase_intent_sequence;
    uint32_t erase_intent_block;
    uint32_t close_execution_epoch;
    uint64_t close_lifecycle_nonce;
    uint64_t checkpoints;
    uint64_t garbage_collections;
    uint64_t nfc_children;
    size_t arena_bytes;
    uint8_t ready;
    uint8_t initialized;
    uint8_t admission_closed;
    uint8_t quarantined;
    uint8_t nfc_close_started;
    uint8_t nfc_quiescent;
    uint8_t reserved0[2];
    struct sf_io io;
    struct sf_meta meta;
    struct sf_work work;
};

/* NFC boundary: consumes only NFC completion facts, never media page_info. */
enum fwlab_spine_result_v0 sf_io_read_start(
    struct fwlab_ftl_scale *ftl, uint32_t ppa, uint8_t frame);
enum fwlab_spine_result_v0 sf_io_program_start(
    struct fwlab_ftl_scale *ftl, uint32_t ppa, uint8_t frame);
enum fwlab_spine_result_v0 sf_io_erase_start(
    struct fwlab_ftl_scale *ftl, uint32_t block);
bool sf_io_idle(const struct fwlab_ftl_scale *ftl);
bool sf_io_take(struct fwlab_ftl_scale *ftl, struct sf_io_result *result);
bool sf_io_step(struct fwlab_ftl_scale *ftl);
struct fwlab_nfc_ppa sf_ppa(const struct fwlab_ftl_scale *ftl, uint32_t linear);

/* Geometry/codec and durable metadata runner. */
bool sf_geometry_counts(const struct fwlab_nfc_geometry *geometry,
                        uint32_t *blocks, uint32_t *pages);
bool sf_layout_make(const struct fwlab_nfc_geometry *geometry, uint64_t lbas,
                    struct sf_layout *layout);
uint32_t sf_crc32c(const uint8_t *bytes, size_t size);
uint64_t sf_digest(uint64_t prior, const uint8_t *bytes, size_t size);
bool sf_bytes_zero(const void *bytes, size_t size);
bool sf_bytes_ff(const uint8_t *bytes, size_t size);
void sf_data_oob_encode(const struct fwlab_ftl_scale *ftl, uint32_t lpn,
                        const struct sf_map_entry *entry, uint64_t block_uid,
                        const uint8_t main[SF_PAGE_BYTES],
                        uint8_t oob[SF_OOB_BYTES]);
bool sf_data_oob_validate(const struct fwlab_ftl_scale *ftl, uint32_t lpn,
                          const struct sf_map_entry *entry, uint64_t block_uid,
                          const uint8_t main[SF_PAGE_BYTES],
                          const uint8_t oob[SF_OOB_BYTES]);
/* GC discovers the LPN from a live page, then validates it against the map. */
bool sf_data_oob_lpn(const uint8_t oob[SF_OOB_BYTES], uint32_t *lpn);
enum fwlab_spine_result_v0 sf_format_start(
    struct fwlab_ftl_scale *ftl, uint64_t lbas);
enum fwlab_spine_result_v0 sf_recover_start(
    struct fwlab_ftl_scale *ftl, uint64_t expected_lbas);
enum fwlab_spine_result_v0 sf_checkpoint_start(struct fwlab_ftl_scale *ftl);
/* Stamps current epoch/next sequence/predecessor and map sequence. Caller
 * supplies semantic fields and requested frontier. On dual success the one
 * semantic apply implementation below runs before journal completion. */
enum fwlab_spine_result_v0 sf_journal_start(
    struct fwlab_ftl_scale *ftl, const struct sf_record *record);
bool sf_meta_step(struct fwlab_ftl_scale *ftl);
bool sf_meta_busy(const struct fwlab_ftl_scale *ftl);
enum fwlab_spine_result_v0 sf_meta_result(const struct fwlab_ftl_scale *ftl);

/* The same atomic semantic apply is used after dual-rail live completion and
 * ordered recovery. Validate the ENTIRE record before changing any map. */
bool sf_record_validate_apply(struct fwlab_ftl_scale *ftl,
                              const struct sf_record *record);
bool sf_rebuild_indexes(struct fwlab_ftl_scale *ftl);
bool sf_seal_recovered_heads(struct fwlab_ftl_scale *ftl);
bool sf_next_reclaim_pending(struct fwlab_ftl_scale *ftl, uint32_t *block);
bool sf_work_busy(const struct fwlab_ftl_scale *ftl);
bool sf_work_step(struct fwlab_ftl_scale *ftl);
void sf_fail(struct fwlab_ftl_scale *ftl, uint32_t fault_code);

#endif
