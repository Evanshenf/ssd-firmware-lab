/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_CORE_C34_H
#define FWLAB_CORE_C34_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fwlab/contracts/c31_provider.h"
#include "fwlab/contracts/nand_media.h"
#include "fwlab/contracts/nfc_provider.h"
#include "fwlab/private/c34_physical_txn.h"
#include "fwlab/portable/persistence_policy.h"

#define C34_CONTRACT_VERSION 1u
#define C34_ATOMS 2u
#define C34_ATOM_BYTES 16u
#define C34_BLOCKS 6u
#define C34_PAGES_PER_BLOCK 4u
#define C34_TOTAL_PAGES 24u
#define C34_DATA_BLOCKS 3u
#define C34_DATA_PAGES 12u
#define C34_MAIN_BYTES 96u
#define C34_OOB_BYTES 64u
#define C34_JOURNAL_BLOCK 3u
#define C34_CHECKPOINT_BLOCK0 4u
#define C34_CHECKPOINT_BLOCK1 5u

enum c34_result {
    C34_OK = 0,
    C34_INVALID_CONTRACT = 1,
    C34_UNSUPPORTED_VERSION = 2,
    C34_NO_CAPACITY = 3,
    C34_WRONG_STATE = 4,
    C34_STALE_TOKEN = 5,
    C34_NOT_FOUND = 6,
    C34_MEDIA_FAILURE = 7,
    C34_CORRUPT = 8,
    C34_COUNTER_EXHAUSTED = 9,
    C34_INVARIANT_FAILURE = 10
};

enum c34_request_kind {
    C34_REQUEST_READ = 0,
    C34_REQUEST_WRITE = 1,
    C34_REQUEST_TRIM = 2,
    C34_REQUEST_FENCE = 3
};

enum c34_logical_kind {
    C34_LOGICAL_NONE = 0,
    C34_LOGICAL_VALUE = 1,
    C34_LOGICAL_TOMBSTONE = 2
};

enum c34_command_status {
    C34_COMMAND_SUCCESS = 0,
    C34_COMMAND_INVALID = 1,
    C34_COMMAND_NO_SPACE = 2,
    C34_COMMAND_MEDIA_FAILURE = 3,
    C34_COMMAND_INDETERMINATE = 4,
    C34_COMMAND_CANCELLED = 5
};

struct c34_config {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    uint64_t instance_nonce;
    uint32_t controller_epoch;
    uint32_t controller_region;
    uint32_t controller_buffer_offset;
    uint32_t controller_buffer_length;
    struct fwlab_persist_profile persistence;
    uint64_t inner_uid_limit;
    uint64_t physical_op_limit;
    uint32_t physical_sequence_limit;
    uint32_t reserved1;
};

struct c34_request {
    uint16_t version;
    uint16_t size;
    uint8_t kind;
    uint8_t durability_kind;
    uint8_t atom_mask;
    uint8_t atom;
    uint32_t owner_epoch;
    uint32_t scope;
    uint32_t sequence;
    uint32_t frontier;
    uint32_t reserved0[2];
    uint8_t payload[C34_ATOMS][C34_ATOM_BYTES];
};

struct c34_command_result {
    uint16_t version;
    uint16_t size;
    uint8_t status;
    uint8_t request_kind;
    uint8_t atom_mask;
    uint8_t present_mask;
    struct fwlab_c31_operation_token outer;
    struct fwlab_persist_witness witness;
    uint8_t payload[C34_ATOMS][C34_ATOM_BYTES];
    uint64_t physical_digest;
    uint32_t reserved[2];
};

struct c34_logical_entry {
    uint8_t kind;
    uint8_t version;
    uint8_t copy_sequence;
    uint8_t atom;
    uint32_t logical_state_id;
    uint32_t authority_record_id;
    uint32_t data_record_id;
    struct fwlab_nfc_ppa data_ppa;
    uint16_t data_erase_generation;
    uint16_t reserved0;
    uint32_t value_crc32c;
};

struct c34;

size_t c34_arena_alignment(void);
size_t c34_arena_size(const struct c34_config *config);

enum c34_result c34_init(
    void *arena,
    size_t arena_size,
    const struct c34_config *config,
    const struct fwlab_nfc_buffer_provider *buffers,
    const struct fwlab_nfc_provider *nfc,
    const struct fwlab_nand_media *raw_media,
    const struct c34_physical_txn_provider *physical,
    struct c34 **instance
);

enum c34_result c34_recover(struct c34 *instance);

enum c34_result c34_request_register(
    struct c34 *instance,
    const struct fwlab_c31_request_token *token,
    const struct c34_request *request
);

struct fwlab_c31_provider c34_c31_provider(struct c34 *instance);

enum c34_result c34_result_read(
    const struct c34 *instance,
    const struct fwlab_c31_command_handle *command,
    struct c34_command_result *result
);

enum c34_result c34_result_ack(
    struct c34 *instance,
    const struct fwlab_c31_command_handle *command
);

enum c34_result c34_checkpoint_start(struct c34 *instance);
enum c34_result c34_relocation_start(struct c34 *instance);
enum c34_result c34_maintenance_quiescent(
    const struct c34 *instance,
    bool *quiescent
);

enum c34_result c34_logical_state(
    const struct c34 *instance,
    uint8_t atom,
    struct c34_logical_entry *entry
);

uint64_t c34_state_hash(const struct c34 *instance);

#endif
