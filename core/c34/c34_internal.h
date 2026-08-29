/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CORE_C34_INTERNAL_H
#define FWLAB_CORE_C34_INTERNAL_H

#include "c34.h"

#define C34_MAGIC UINT64_C(0x43333446544c3031)
#define C34_RECORD_LIMIT 15u
#define C34_CHECKPOINT_LIMIT 2u
#define C34_REQUEST_SLOTS 2u
#define C34_SIDECAR_SLOTS 2u
#define C34_OBLIGATION_SLOTS 2u
#define C34_FLAG_COMMIT UINT8_C(0x01)
#define C34_OOB_MAGIC UINT32_C(0x47503446)

enum c34_record_type {
    C34_RECORD_DATA = 1,
    C34_RECORD_MAP = 2,
    C34_RECORD_TOMBSTONE = 3,
    C34_RECORD_RELOCATION = 4,
    C34_RECORD_CHECKPOINT = 5,
    C34_RECORD_ANCHOR = 6
};

enum c34_p2l_kind {
    C34_P2L_FREE = 0,
    C34_P2L_RESERVED = 1,
    C34_P2L_LIVE = 2,
    C34_P2L_STALE = 3,
    C34_P2L_ORPHAN = 4,
    C34_P2L_TORN = 5
};

enum c34_graph_kind {
    C34_GRAPH_NONE = 0,
    C34_GRAPH_OUTER = 1,
    C34_GRAPH_CHECKPOINT = 2,
    C34_GRAPH_RELOCATION = 3
};

enum c34_graph_state {
    C34_GRAPH_CAPTURED = 1,
    C34_GRAPH_PREPARE_DATA_TRANSFER = 2,
    C34_GRAPH_WAIT_DATA_TRANSFER = 3,
    C34_GRAPH_PREPARE_DATA_EXECUTE = 4,
    C34_GRAPH_WAIT_DATA_EXECUTE = 5,
    C34_GRAPH_PREPARE_META_TRANSFER = 6,
    C34_GRAPH_WAIT_META_TRANSFER = 7,
    C34_GRAPH_PREPARE_META_EXECUTE = 8,
    C34_GRAPH_WAIT_META_EXECUTE = 9,
    C34_GRAPH_PREPARE_READ_TRIGGER = 10,
    C34_GRAPH_WAIT_READ_TRIGGER = 11,
    C34_GRAPH_PREPARE_READ_TRANSFER = 12,
    C34_GRAPH_WAIT_READ_TRANSFER = 13,
    C34_GRAPH_PREPARE_ERASE = 14,
    C34_GRAPH_WAIT_ERASE = 15,
    C34_GRAPH_DONE = 16,
    C34_GRAPH_FAILED = 17
};

enum c34_record_decode_result {
    C34_DECODE_OK = 0,
    C34_DECODE_ERASED = 1,
    C34_DECODE_TORN = 2,
    C34_DECODE_INVALID = 3
};

struct c34_checkpoint_entry {
    uint8_t atom;
    uint8_t kind;
    uint8_t version;
    uint8_t copy_sequence;
    uint32_t logical_state_id;
    uint32_t authority_record_id;
    uint32_t data_record_id;
    struct fwlab_nfc_ppa data_ppa;
    uint16_t data_erase_generation;
    uint16_t reserved0;
    uint32_t value_crc32c;
};

struct c34_record {
    uint8_t type;
    uint8_t atom;
    uint8_t logical_version;
    uint8_t copy_sequence;
    uint32_t record_id;
    uint32_t logical_state_id;
    uint32_t predecessor_state_id;
    uint32_t commit_sequence;
    uint32_t mutation_id;
    uint32_t value_crc32c;
    uint16_t erase_generation;
    uint16_t payload_length;
    uint8_t data[C34_ATOM_BYTES];
    struct fwlab_nfc_ppa target_ppa;
    struct fwlab_nfc_ppa source_ppa;
    uint16_t target_erase_generation;
    uint16_t source_erase_generation;
    uint32_t target_data_record_id;
    uint32_t source_authority_record_id;
    uint32_t checkpoint_generation;
    uint32_t covered_commit_sequence;
    uint32_t next_record_id;
    uint32_t next_logical_state_id;
    struct c34_checkpoint_entry checkpoint[C34_ATOMS];
    uint8_t checkpoint_slot;
    struct fwlab_nfc_ppa checkpoint_ppa;
    uint32_t checkpoint_record_id;
    uint32_t checkpoint_payload_crc32c;
    uint16_t checkpoint_erase_generation;
    uint16_t reserved0;
};

struct c34_raw_page {
    uint8_t main[C34_MAIN_BYTES];
    uint8_t oob[C34_OOB_BYTES];
    struct fwlab_nand_page_info page;
};

struct c34_raw_image {
    struct c34_raw_page pages[C34_TOTAL_PAGES];
    struct fwlab_nand_block_info blocks[C34_BLOCKS];
};

struct c34_p2l_entry {
    uint8_t kind;
    uint8_t atom;
    uint8_t version;
    uint8_t copy_sequence;
    uint32_t data_record_id;
    uint32_t logical_state_id;
    uint32_t value_crc32c;
};

struct c34_registry_entry {
    uint8_t used;
    uint8_t reserved[7];
    struct fwlab_c31_request_token token;
    struct c34_request request;
};

struct c34_sidecar {
    uint8_t used;
    uint8_t ready;
    uint8_t reserved[6];
    struct c34_command_result result;
};

struct c34_obligation_entry {
    uint8_t used;
    uint8_t externally_volatile;
    uint8_t reserved[6];
    struct fwlab_c31_command_handle command;
    struct fwlab_persist_obligation obligation;
};

struct c34_mutation {
    uint8_t used;
    uint8_t atom;
    uint8_t target_version;
    uint8_t predecessor_version;
    uint8_t copy_sequence;
    uint8_t reserved0[3];
    uint32_t logical_state_id;
    uint32_t predecessor_state_id;
    uint32_t mutation_id;
    uint32_t data_record_id;
    uint32_t metadata_record_id;
    struct fwlab_nfc_ppa data_ppa;
    uint16_t data_erase_generation;
    uint16_t reserved1;
    struct fwlab_persist_atom_fact fact;
    uint8_t payload[C34_ATOM_BYTES];
};

struct c34_graph {
    uint8_t kind;
    uint8_t state;
    uint8_t atom_cursor;
    uint8_t active_atom;
    uint8_t outer_event_ready;
    uint8_t outer_event_sent;
    uint8_t terminal;
    uint8_t failure_effect;
    uint8_t inner_pending;
    uint8_t physical_bound;
    uint8_t maintenance_result;
    uint8_t obligation_index;
    uint8_t sidecar_index;
    uint8_t cancel_requested;
    uint8_t result_present_mask;
    uint8_t reserved0;
    struct fwlab_c31_operation_token outer;
    struct c34_request request;
    struct fwlab_persist_request persist_request;
    struct fwlab_persist_witness witness;
    struct c34_mutation mutation[C34_ATOMS];
    struct fwlab_nfc_operation_token inner;
    struct fwlab_nfc_cache_token cache;
    struct fwlab_nfc_completion completion;
    struct c34_record record;
    struct c34_record checkpoint_record;
    struct fwlab_nfc_ppa operation_ppa;
    struct fwlab_nfc_ppa victim;
    struct fwlab_nfc_ppa destination;
    uint8_t main[C34_MAIN_BYTES];
    uint8_t oob[C34_OOB_BYTES];
};

struct c34 {
    uint64_t magic;
    struct c34_config config;
    struct fwlab_nfc_buffer_provider buffers;
    struct fwlab_nfc_provider nfc;
    struct fwlab_nand_media raw_media;
    struct c34_physical_txn_provider physical;
    uint32_t current_epoch;
    uint32_t phase;
    uint32_t reset_old_epoch;
    uint32_t reserved_epoch;
    uint64_t next_inner_uid;
    uint64_t next_physical_op_id;
    uint32_t next_physical_sequence;
    uint32_t next_record_id;
    uint32_t next_logical_state_id;
    uint32_t next_commit_sequence;
    uint32_t checkpoint_generation;
    uint32_t checkpoint_watermark;
    uint32_t last_sequence;
    struct c34_logical_entry l2p[C34_ATOMS];
    struct c34_p2l_entry p2l[C34_DATA_PAGES];
    struct fwlab_nand_block_info blocks[C34_BLOCKS];
    uint8_t overlay_valid[C34_ATOMS];
    uint8_t overlay_kind[C34_ATOMS];
    uint8_t overlay_payload[C34_ATOMS][C34_ATOM_BYTES];
    struct c34_registry_entry registry[C34_REQUEST_SLOTS];
    struct c34_sidecar sidecar[C34_SIDECAR_SLOTS];
    struct c34_obligation_entry obligations[C34_OBLIGATION_SLOTS];
    struct c34_graph graph;
};

uint32_t c34_crc32c(const uint8_t *bytes, size_t length);
uint64_t c34_hash_bytes(uint64_t hash, const uint8_t *bytes, size_t length);
int c34_ppa_equal(
    const struct fwlab_nfc_ppa *left,
    const struct fwlab_nfc_ppa *right
);
uint32_t c34_page_index(const struct fwlab_nfc_ppa *ppa);
struct fwlab_nfc_ppa c34_ppa(uint16_t block, uint16_t page);

enum c34_result c34_record_encode(
    const struct c34_record *record,
    uint8_t main[C34_MAIN_BYTES],
    uint8_t oob[C34_OOB_BYTES]
);

enum c34_record_decode_result c34_record_decode(
    const uint8_t main[C34_MAIN_BYTES],
    const uint8_t oob[C34_OOB_BYTES],
    const struct fwlab_nand_page_info *page,
    const struct fwlab_nand_block_info *block,
    struct c34_record *record
);

enum c34_result c34_scan_raw_media(
    const struct fwlab_nand_media *media,
    struct c34_raw_image *image
);

enum c34_result c34_recover_image(
    const struct c34_raw_image *image,
    struct c34_logical_entry l2p[C34_ATOMS],
    struct c34_p2l_entry p2l[C34_DATA_PAGES],
    uint32_t *checkpoint_generation,
    uint32_t *checkpoint_watermark,
    uint32_t *next_record_id,
    uint32_t *next_logical_state_id,
    uint32_t *next_commit_sequence
);

enum c34_result c34_refresh(struct c34 *instance, bool clear_overlay);
enum c34_result c34_drive_one(struct c34 *instance);
void c34_graph_fail(
    struct c34 *instance,
    enum c34_command_status status,
    uint8_t effect
);

int c34_instance_valid(const struct c34 *instance);
int c34_outer_equal(
    const struct fwlab_c31_operation_token *left,
    const struct fwlab_c31_operation_token *right
);
int c34_inner_equal(
    const struct fwlab_nfc_operation_token *left,
    const struct fwlab_nfc_operation_token *right
);

enum c34_result c34_allocate_data(
    struct c34 *instance,
    struct fwlab_nfc_ppa *ppa
);
enum c34_result c34_allocate_journal(
    const struct c34 *instance,
    struct fwlab_nfc_ppa *ppa
);
enum c34_result c34_choose_relocation(
    const struct c34 *instance,
    uint8_t *atom,
    struct fwlab_nfc_ppa *source,
    struct fwlab_nfc_ppa *destination
);

enum c34_result c34_build_data_record(
    struct c34 *instance,
    struct c34_mutation *mutation,
    const struct fwlab_nfc_ppa *ppa,
    uint8_t copy_sequence,
    const uint8_t payload[C34_ATOM_BYTES],
    struct c34_record *record
);
enum c34_result c34_build_mapping_record(
    struct c34 *instance,
    const struct c34_mutation *mutation,
    struct c34_record *record
);
enum c34_result c34_build_tombstone_record(
    struct c34 *instance,
    const struct c34_mutation *mutation,
    struct c34_record *record
);
enum c34_result c34_build_relocation_record(
    struct c34 *instance,
    const struct c34_mutation *mutation,
    const struct c34_logical_entry *source,
    struct c34_record *record
);
enum c34_result c34_build_checkpoint_record(
    struct c34 *instance,
    uint32_t generation,
    struct c34_record *record
);
enum c34_result c34_build_anchor_record(
    struct c34 *instance,
    const struct c34_record *checkpoint,
    uint8_t slot,
    uint32_t payload_crc,
    struct c34_record *record
);

enum c34_result c34_nfc_program_transfer(
    struct c34 *instance,
    const struct fwlab_nfc_ppa *ppa
);
enum c34_result c34_nfc_program_execute(struct c34 *instance);
enum c34_result c34_nfc_read_trigger(
    struct c34 *instance,
    const struct fwlab_nfc_ppa *ppa
);
enum c34_result c34_nfc_read_transfer(struct c34 *instance);
enum c34_result c34_nfc_erase(
    struct c34 *instance,
    const struct fwlab_nfc_ppa *ppa
);
enum c34_result c34_nfc_progress(
    struct c34 *instance,
    bool *completed
);
enum c34_result c34_nfc_finish_physical(struct c34 *instance);

enum c34_result c34_accept_outer(
    struct c34 *instance,
    const struct fwlab_c31_provider_request *outer,
    const struct c34_request *request
);
enum c34_result c34_cancel_outer(
    struct c34 *instance,
    const struct fwlab_c31_operation_token *outer
);

#endif
