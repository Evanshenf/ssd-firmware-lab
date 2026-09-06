/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_PHYSICAL_NAND_V2_CODEC_H
#define FWLAB_PHYSICAL_NAND_V2_CODEC_H

#include "physical_nand_internal.h"
#include <stdbool.h>

#define FNV2_MAGIC UINT64_C(0x464e414e44303032)
#define FNV2_SUPER UINT32_C(0x32534e46)
#define FNV2_BLOCK UINT32_C(0x32424e46)
#define FNV2_PAGE UINT32_C(0x32504e46)
#define FNV2_INTENT UINT32_C(0x32494e46)
#define FNV2_TERMINAL UINT32_C(0x32544e46)
#define FNV2_UNKNOWN_PAGE 3u
enum fnv2_kind { FNV2_PROGRAM = 1, FNV2_ERASE = 2, FNV2_MARK_BAD = 3 };
enum fnv2_disposition { FNV2_COMMIT = 1, FNV2_ABORT = 2, FNV2_BOOTSTRAP = 3 };

struct fnv2_block {
    struct fwlab_nand_block_info info;
    uint16_t erased_prefix;
    uint64_t sequence;
};
struct fnv2_intent {
    uint64_t sequence, predecessor;
    uint32_t block, crc;
    uint16_t kind, first, count, maximum_prefix;
    struct fnv2_block base;
};
struct fnv2_terminal {
    uint64_t sequence, predecessor;
    uint32_t intent_crc, kind;
    uint16_t disposition;
};

uint16_t fnv2_get16(const uint8_t *p);
uint32_t fnv2_get32(const uint8_t *p);
uint64_t fnv2_get64(const uint8_t *p);
void fnv2_put16(uint8_t *p, uint16_t value);
void fnv2_put32(uint8_t *p, uint32_t value);
void fnv2_put64(uint8_t *p, uint64_t value);
bool fnv2_all(const uint8_t *p, size_t n, uint8_t value);
uint32_t fnv2_crc(const uint8_t *p, size_t n);
bool fnv2_record_crc(const uint8_t *p, size_t n);
bool fnv2_layout(const struct fwlab_file_nand_v2_config *config,
                 uint32_t *blocks, uint32_t *pages, uint64_t *page_meta,
                 uint64_t *block_meta, uint64_t *image_bytes);
void fnv2_super_encode(const struct fwlab_file_nand_v2 *m, uint8_t *out);
bool fnv2_super_valid(const struct fwlab_file_nand_v2 *m, const uint8_t *bytes);
void fnv2_block_encode(uint8_t *out, uint32_t id, const struct fnv2_block *block);
bool fnv2_block_decode(const struct fwlab_file_nand_v2 *m, const uint8_t *bytes,
                       uint32_t id, uint64_t maximum_sequence, struct fnv2_block *block);
void fnv2_page_encode(uint8_t *out, uint32_t id, uint16_t generation,
                      uint8_t state, uint64_t sequence, const uint8_t *main,
                      const uint8_t *oob);
bool fnv2_page_valid(const uint8_t *bytes, uint32_t id, uint64_t maximum_sequence);
void fnv2_intent_encode(const struct fwlab_file_nand_v2 *m,
                        const struct fnv2_intent *intent, uint8_t *out);
bool fnv2_intent_decode(const struct fwlab_file_nand_v2 *m, const uint8_t *bytes,
                        unsigned bank, struct fnv2_intent *intent);
void fnv2_terminal_encode(const struct fwlab_file_nand_v2 *m,
                          const struct fnv2_terminal *terminal, uint8_t *out);
bool fnv2_terminal_decode(const struct fwlab_file_nand_v2 *m, const uint8_t *bytes,
                          unsigned bank, struct fnv2_terminal *terminal);
bool fnv2_terminal_matches(const struct fnv2_terminal *terminal,
                           const struct fnv2_intent *intent);

#endif
