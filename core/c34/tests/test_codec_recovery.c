/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "../c34_internal.h"
#include "../fakes/c34_memory_media.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,       \
                    __LINE__, #condition);                                   \
            return 0;                                                        \
        }                                                                    \
    } while (0)

static struct c34_record data_record(
    uint8_t atom,
    uint8_t version,
    uint32_t state,
    uint32_t record_id,
    uint32_t sequence,
    struct fwlab_nfc_ppa ppa,
    uint8_t fill
)
{
    struct c34_record record;

    memset(&record, 0, sizeof(record));
    record.type = C34_RECORD_DATA;
    record.atom = atom;
    record.logical_version = version;
    record.copy_sequence = 0;
    record.record_id = record_id;
    record.logical_state_id = state;
    record.commit_sequence = sequence;
    record.mutation_id = state;
    record.erase_generation = 0;
    record.payload_length = C34_ATOM_BYTES;
    record.target_ppa = ppa;
    memset(record.data, fill, sizeof(record.data));
    record.value_crc32c = c34_crc32c(record.data, sizeof(record.data));
    return record;
}

static struct c34_record map_record(
    const struct c34_record *data,
    uint32_t record_id,
    uint32_t sequence,
    uint32_t predecessor_state,
    uint32_t source_authority
)
{
    struct c34_record record;

    memset(&record, 0, sizeof(record));
    record.type = C34_RECORD_MAP;
    record.atom = data->atom;
    record.logical_version = data->logical_version;
    record.copy_sequence = data->copy_sequence;
    record.record_id = record_id;
    record.logical_state_id = data->logical_state_id;
    record.predecessor_state_id = predecessor_state;
    record.commit_sequence = sequence;
    record.mutation_id = data->mutation_id;
    record.value_crc32c = data->value_crc32c;
    record.payload_length = 64;
    record.target_ppa = data->target_ppa;
    record.target_data_record_id = data->record_id;
    record.source_authority_record_id = source_authority;
    return record;
}

static int test_crc_and_codec(void)
{
    static const uint8_t vector[] = "123456789";
    struct c34_record source = data_record(
        0, 1, 1, 1, 1, c34_ppa(0, 0), 0x5a);
    struct c34_record decoded;
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
    uint8_t main[C34_MAIN_BYTES];
    uint8_t oob[C34_OOB_BYTES];

    CHECK(c34_crc32c(vector, sizeof(vector) - 1u) ==
          UINT32_C(0xe3069283));
    CHECK(c34_record_encode(&source, main, oob) == C34_OK);
    CHECK(main[0] == 0x5a && main[15] == 0x5a && main[16] == 0xff);
    CHECK(oob[0] == 'F' && oob[1] == '4' && oob[2] == 'P' &&
          oob[3] == 'G');
    memset(&page, 0, sizeof(page));
    page.version = FWLAB_NFC_CONTRACT_VERSION;
    page.size = sizeof(page);
    page.state = FWLAB_NAND_PAGE_VALID;
    page.program_count = 1;
    memset(&block, 0, sizeof(block));
    block.version = FWLAB_NFC_CONTRACT_VERSION;
    block.size = sizeof(block);
    block.health = FWLAB_NFC_BLOCK_GOOD;
    block.erase_state = FWLAB_NAND_ERASE_CLEAN;
    CHECK(c34_record_decode(main, oob, &page, &block, &decoded) ==
          C34_DECODE_OK);
    CHECK(decoded.type == C34_RECORD_DATA && decoded.record_id == 1 &&
          decoded.logical_state_id == 1 &&
          memcmp(decoded.data, source.data, C34_ATOM_BYTES) == 0);
    oob[52] = 1;
    CHECK(c34_record_decode(main, oob, &page, &block, &decoded) ==
          C34_DECODE_INVALID);
    return 1;
}

static int recover(
    struct c34_memory_media *media,
    struct c34_logical_entry l2p[C34_ATOMS],
    struct c34_p2l_entry p2l[C34_DATA_PAGES],
    uint32_t *generation,
    uint32_t *watermark
)
{
    struct fwlab_nand_media provider = c34_memory_media_provider(media);
    struct c34_raw_image image;
    uint32_t next_record;
    uint32_t next_state;
    uint32_t next_sequence;

    return c34_scan_raw_media(&provider, &image) == C34_OK &&
           c34_recover_image(
               &image, l2p, p2l, generation, watermark, &next_record,
               &next_state, &next_sequence) == C34_OK;
}

static int test_blank_write_trim(void)
{
    struct c34_memory_media media;
    struct c34_logical_entry l2p[C34_ATOMS];
    struct c34_p2l_entry p2l[C34_DATA_PAGES];
    struct c34_record data;
    struct c34_record map;
    struct c34_record tomb;
    uint32_t generation;
    uint32_t watermark;
    unsigned int index;
    unsigned int reserved = 0;

    c34_memory_media_init(&media);
    CHECK(recover(&media, l2p, p2l, &generation, &watermark));
    CHECK(generation == 0 && watermark == 0 &&
          l2p[0].kind == C34_LOGICAL_NONE &&
          l2p[1].kind == C34_LOGICAL_NONE);
    for (index = 0; index < C34_DATA_PAGES; ++index) {
        reserved += p2l[index].kind == C34_P2L_RESERVED;
    }
    CHECK(reserved == C34_PAGES_PER_BLOCK);

    data = data_record(0, 1, 1, 1, 1, c34_ppa(0, 0), 0x11);
    map = map_record(&data, 2, 2, 0, 0);
    CHECK(c34_memory_media_put_record(&media, &data.target_ppa, &data) ==
          C34_OK);
    {
        struct fwlab_nfc_ppa ppa = c34_ppa(C34_JOURNAL_BLOCK, 0);
        CHECK(c34_memory_media_put_record(&media, &ppa, &map) == C34_OK);
    }
    CHECK(recover(&media, l2p, p2l, &generation, &watermark));
    CHECK(l2p[0].kind == C34_LOGICAL_VALUE && l2p[0].version == 1 &&
          l2p[0].authority_record_id == 2 &&
          p2l[0].kind == C34_P2L_LIVE);

    memset(&tomb, 0, sizeof(tomb));
    tomb.type = C34_RECORD_TOMBSTONE;
    tomb.atom = 0;
    tomb.logical_version = 2;
    tomb.copy_sequence = 0;
    tomb.record_id = 3;
    tomb.logical_state_id = 2;
    tomb.predecessor_state_id = 1;
    tomb.commit_sequence = 3;
    tomb.mutation_id = 2;
    tomb.payload_length = 0;
    {
        struct fwlab_nfc_ppa ppa = c34_ppa(C34_JOURNAL_BLOCK, 1);
        CHECK(c34_memory_media_put_record(&media, &ppa, &tomb) == C34_OK);
    }
    CHECK(recover(&media, l2p, p2l, &generation, &watermark));
    CHECK(l2p[0].kind == C34_LOGICAL_TOMBSTONE &&
          l2p[0].version == 2 && p2l[0].kind == C34_P2L_STALE);
    return 1;
}

static int test_checkpoint_and_corruption(void)
{
    struct c34_memory_media media;
    struct c34_logical_entry l2p[C34_ATOMS];
    struct c34_p2l_entry p2l[C34_DATA_PAGES];
    struct c34_record data = data_record(
        0, 1, 1, 1, 1, c34_ppa(0, 0), 0x33);
    struct c34_record map = map_record(&data, 2, 2, 0, 0);
    struct c34_record checkpoint;
    struct c34_record anchor;
    struct fwlab_nfc_ppa journal = c34_ppa(C34_JOURNAL_BLOCK, 0);
    struct fwlab_nfc_ppa checkpoint_ppa =
        c34_ppa(C34_CHECKPOINT_BLOCK0, 0);
    struct fwlab_nfc_ppa anchor_ppa =
        c34_ppa(C34_CHECKPOINT_BLOCK0, 1);
    uint8_t main[C34_MAIN_BYTES];
    uint8_t oob[C34_OOB_BYTES];
    uint32_t generation;
    uint32_t watermark;

    c34_memory_media_init(&media);
    CHECK(c34_memory_media_put_record(&media, &data.target_ppa, &data) ==
          C34_OK);
    CHECK(c34_memory_media_put_record(&media, &journal, &map) == C34_OK);
    memset(&checkpoint, 0, sizeof(checkpoint));
    checkpoint.type = C34_RECORD_CHECKPOINT;
    checkpoint.atom = 0xff;
    checkpoint.logical_version = 0xff;
    checkpoint.copy_sequence = 0xff;
    checkpoint.record_id = 3;
    checkpoint.commit_sequence = 3;
    checkpoint.payload_length = C34_MAIN_BYTES;
    checkpoint.checkpoint_generation = 1;
    checkpoint.covered_commit_sequence = 2;
    checkpoint.next_record_id = 5;
    checkpoint.next_logical_state_id = 2;
    checkpoint.checkpoint[0].atom = 0;
    checkpoint.checkpoint[0].kind = C34_LOGICAL_VALUE;
    checkpoint.checkpoint[0].version = 1;
    checkpoint.checkpoint[0].logical_state_id = 1;
    checkpoint.checkpoint[0].authority_record_id = 2;
    checkpoint.checkpoint[0].data_record_id = 1;
    checkpoint.checkpoint[0].data_ppa = data.target_ppa;
    checkpoint.checkpoint[0].value_crc32c = data.value_crc32c;
    checkpoint.checkpoint[1].atom = 1;
    checkpoint.checkpoint[1].kind = C34_LOGICAL_NONE;
    CHECK(c34_record_encode(&checkpoint, main, oob) == C34_OK);
    memset(&anchor, 0, sizeof(anchor));
    anchor.type = C34_RECORD_ANCHOR;
    anchor.atom = 0xff;
    anchor.logical_version = 0xff;
    anchor.copy_sequence = 0xff;
    anchor.record_id = 4;
    anchor.commit_sequence = 4;
    anchor.payload_length = 32;
    anchor.checkpoint_generation = 1;
    anchor.checkpoint_slot = 0;
    anchor.checkpoint_ppa = checkpoint_ppa;
    anchor.checkpoint_record_id = 3;
    anchor.checkpoint_payload_crc32c = c34_crc32c(main, C34_MAIN_BYTES);
    anchor.covered_commit_sequence = 2;
    CHECK(c34_memory_media_put_record(&media, &checkpoint_ppa,
                                      &checkpoint) == C34_OK);
    CHECK(c34_memory_media_put_record(&media, &anchor_ppa, &anchor) ==
          C34_OK);
    CHECK(recover(&media, l2p, p2l, &generation, &watermark));
    CHECK(generation == 1 && watermark == 2 &&
          l2p[0].kind == C34_LOGICAL_VALUE &&
          l2p[0].data_record_id == 1);
    media.main[c34_page_index(&anchor_ppa)][0] ^= 1;
    {
        struct fwlab_nand_media provider = c34_memory_media_provider(&media);
        struct c34_raw_image image;
        uint32_t next_record;
        uint32_t next_state;
        uint32_t next_sequence;

        CHECK(c34_scan_raw_media(&provider, &image) == C34_OK);
        CHECK(c34_recover_image(
                  &image, l2p, p2l, &generation, &watermark,
                  &next_record, &next_state, &next_sequence) == C34_CORRUPT);
    }
    return 1;
}

int main(void)
{
    CHECK(test_crc_and_codec());
    CHECK(test_blank_write_trim());
    CHECK(test_checkpoint_and_corruption());
    puts("C3.4 firmware codec/recovery unit: PASS (3 cases)");
    return 0;
}
