/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "physical_nand_batch.h"
#include "physical_nand_internal.h"
#include <string.h>

static enum fwlab_nfc_api_result read_pages(void *context,
    const struct fwlab_nfc_ppa *first, uint32_t count,
    uint8_t *main, size_t main_bytes, uint8_t *oob, size_t oob_bytes,
    struct fwlab_nand_page_info *pages, size_t capacity,
    struct fwlab_nand_block_info *block)
{
    return fwlab_file_nand_v2_read_pages(context, first, count, main,
        main_bytes, oob, oob_bytes, pages, capacity, block);
}
static enum fwlab_nfc_api_result program_pages(void *context,
    const struct fwlab_nfc_ppa *first, uint32_t count,
    const uint8_t *main, size_t main_bytes, const uint8_t *oob, size_t oob_bytes,
    struct fwlab_nand_media_result *results, size_t capacity)
{
    return fwlab_file_nand_v2_program_pages(context, first, count, main,
        main_bytes, oob, oob_bytes, results, capacity);
}
static const struct fwlab_nand_batch_v2_ops operations = {
    .version = FWLAB_NAND_BATCH_V2_VERSION,
    .size = sizeof(struct fwlab_nand_batch_v2_ops),
    .read_pages = read_pages,
    .program_pages = program_pages
};
struct fwlab_nand_batch_v2 fwlab_file_nand_v2_batch(
    struct fwlab_file_nand_v2 *media)
{
    struct fwlab_nand_batch_v2 out = {0};
    struct fwlab_nand_media scalar = fwlab_file_nand_v2_media(media);
    if (!scalar.ops) return out;
    out.version = FWLAB_NAND_BATCH_V2_VERSION;
    out.size = sizeof(out);
    out.ops = &operations;
    out.scalar = scalar;
    out.geometry = media->config.geometry;
    memcpy(out.media_uuid, media->config.media_uuid, sizeof(out.media_uuid));
    return out;
}
