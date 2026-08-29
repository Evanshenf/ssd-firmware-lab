/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C34_MEMORY_MEDIA_H
#define FWLAB_C34_MEMORY_MEDIA_H

#include "../c34_internal.h"

struct c34_memory_media {
    uint8_t main[C34_TOTAL_PAGES][C34_MAIN_BYTES];
    uint8_t oob[C34_TOTAL_PAGES][C34_OOB_BYTES];
    struct fwlab_nand_page_info page[C34_TOTAL_PAGES];
    struct fwlab_nand_block_info block[C34_BLOCKS];
    struct c34_physical_binding binding;
    struct c34_physical_receipt receipt;
    uint8_t binding_used;
    uint8_t receipt_used;
    uint8_t reserved[6];
    uint64_t physical_generation;
};

void c34_memory_media_init(struct c34_memory_media *media);
struct fwlab_nand_media c34_memory_media_provider(
    struct c34_memory_media *media
);
struct c34_physical_txn_provider c34_memory_txn_provider(
    struct c34_memory_media *media
);

enum c34_result c34_memory_media_put_record(
    struct c34_memory_media *media,
    const struct fwlab_nfc_ppa *ppa,
    const struct c34_record *record
);

#endif
