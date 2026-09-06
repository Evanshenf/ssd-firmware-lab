/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_CONTRACTS_NFC_PAGE_V2_TYPES_H
#define FWLAB_CONTRACTS_NFC_PAGE_V2_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include "fwlab/portable/nfc_types.h"

#define FWLAB_NFC_PAGE_V2_VERSION 2u
#define FWLAB_NFC_PAGE_V2_PROFILE_R0 1u
#define FWLAB_NFC_PAGE_V2_MAX_PAGES 64u
#define FWLAB_NFC_PAGE_V2_MAIN_BYTES 4096u
#define FWLAB_NFC_PAGE_V2_OOB_BYTES 128u

enum fwlab_nfc_page_v2_kind {
    FWLAB_NFC_PAGE_V2_READ_GROUP = 1,
    FWLAB_NFC_PAGE_V2_PROGRAM_GROUP = 2,
    FWLAB_NFC_PAGE_V2_ERASE = 3
};
enum fwlab_nfc_page_v2_effect {
    FWLAB_NFC_PAGE_V2_EFFECT_NONE = 0,
    FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE = 1,
    FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN = 2,
    FWLAB_NFC_PAGE_V2_EFFECT_NONCOMPLETE = 3
};
enum fwlab_nfc_page_v2_fact {
    FWLAB_NFC_PAGE_V2_FACT_GENERATION = 1u,
    FWLAB_NFC_PAGE_V2_FACT_HEALTH = 2u,
    FWLAB_NFC_PAGE_V2_FACT_CELL = 4u,
    FWLAB_NFC_PAGE_V2_FACT_ECC = 8u,
    FWLAB_NFC_PAGE_V2_FACT_EFFECT = 16u
};

/* CPU spans are read synchronously only during the first accepted submission.
 * They are not DMA/Host-map authority or borrowed leases. R0 requires retry=0,
 * fault_tag=0. READ/ERASE have absent input spans; ERASE uses count=1/page=0.
 * PROGRAM/READ groups are contiguous full pages in one physical block. */
struct fwlab_nfc_page_v2_request {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nfc_operation_token operation;
    struct fwlab_nfc_ppa first;
    uint32_t page_count;
    uint16_t kind;
    uint8_t retry_step;
    uint8_t reserved1;
    uint64_t fault_tag;
    const uint8_t *main;
    size_t main_bytes;
    const uint8_t *oob;
    size_t oob_bytes;
};

/* Cell state uses fwlab_nand_page_state values. Only facts_valid fields are
 * authoritative. READ integrity describes the complete returned physical
 * image: clean ERASED (defined FF bytes) and VALID are COMPLETE, without
 * changing cell state/program_count; page or block-erase TORN is TORN.
 * ERASE uses page[0]; its applied_pages counts physical pages,
 * whereas PROGRAM records exact per-page byte counts. No payload digest. */
struct fwlab_nfc_page_v2_page_result {
    uint16_t base_erase_generation;
    uint16_t final_erase_generation;
    uint16_t corrected_main_bits;
    uint16_t corrected_oob_bits;
    uint32_t applied_main_bytes;
    uint32_t applied_oob_bytes;
    uint32_t applied_pages;
    uint8_t facts_valid;
    uint8_t effect;
    uint8_t integrity;
    uint8_t block_health;
    uint8_t page_state;
    uint8_t program_count;
    uint8_t ecc_status;
    uint8_t valid_region_mask;
    uint8_t applied_region_mask;
    uint8_t reason;
    uint8_t reserved[2];
};
struct fwlab_nfc_page_v2_result {
    uint16_t version;
    uint16_t size;
    uint32_t reserved0;
    struct fwlab_nfc_operation_token operation;
    struct fwlab_nfc_ppa first;
    uint32_t page_count;
    uint16_t kind;
    uint8_t terminal;
    uint8_t reason;
    uint8_t effect;
    uint8_t read_valid;
    uint16_t reserved1;
    uint32_t backend_status;
    /* Zero for discard, all failed reads, and non-read operations. */
    uint32_t delivered_pages;
    struct fwlab_nfc_page_v2_page_result page[FWLAB_NFC_PAGE_V2_MAX_PAGES];
};
struct fwlab_nfc_page_v2_output {
    uint8_t *main;
    size_t main_bytes;
    uint8_t *oob;
    size_t oob_bytes;
};
struct fwlab_nfc_page_v2_step_result {
    uint32_t units_used;
    uint32_t results_pending;
};

#endif
