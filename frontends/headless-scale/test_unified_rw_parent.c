/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#define _GNU_SOURCE
#include "fwlab/private/nfc_channel_v2.h"
static enum fwlab_nfc_api_result rw_hub_init(void *, size_t,
    const struct fwlab_nfc_page_v2_lab_mutation_config *, const struct fwlab_nand_channel_v2 *,
    struct fwlab_nfc_channel_v2 **);
#define MH_LUNS 1u
#define MH_PLANES 2u
#define MH_BLOCKS 3u
#define MH_MEDIA_PREFIX "fwlab-d216-parent"
#define MH_FTL_ARENA_SIZE fwlab_ftl_scale_read_write_v3_arena_size
#define MH_FTL_INIT fwlab_ftl_scale_init_read_write_v3
#define MH_HUB_INIT rw_hub_init
#define MULTIHEAD_PARENT_ENTRY retained_unified_parent_fixture_entry
#include "test_multihead_parent.c"

static enum fwlab_nfc_api_result rw_hub_init(void *a, size_t n,
    const struct fwlab_nfc_page_v2_lab_mutation_config *t, const struct fwlab_nand_channel_v2 *m,
    struct fwlab_nfc_channel_v2 **h)
{ return fwlab_nfc_channel_v2_init_policy(a, n, t, m, FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE, NULL, h); }
static void binding(struct mh_fixture *m)
{
    struct fwlab_ftl_scale *f = m->base.ftl;
    CHECK(f->disk_format == SF_MULTIHEAD_FORMAT_VERSION && f->reads && f->writes &&
        f->parallel_reads && !f->read_only && f->physical_blocks == 24 &&
        f->root.layout.data_first_block == 12);
    CHECK((uintptr_t)f->writes + sf_write_pool_bytes() <= (uintptr_t)f->reads &&
        (uintptr_t)f->reads + sizeof(*f->reads) <= (uintptr_t)m->base.ftl_arena + f->arena_bytes);
    CHECK(!sf_read_pool_busy(f) || !sf_write_pool_busy(f));
    struct fwlab_nfc_channel_v2_stats s = mh_stats(m);
    for (unsigned c = 0; c < 4; ++c) CHECK(s.channel[c].read_policy == FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE);
}
static void gold_write(uint8_t *gold, uint64_t lba, uint32_t lbas, uint8_t seed)
{
    for (uint32_t i = 0; i < lbas * 512u; ++i) gold[lba * 512u + i] = pattern(lba * 512u + i, seed);
}
static void check_gold(struct fixture *f, const uint8_t *gold)
{ io(f, FWLAB_BLOCK_V0_READ, 0, MH_LBAS, 0); CHECK(!memcmp(f->buffer.bytes, gold, BUFFER_BYTES)); }
static struct fwlab_block_request_v0 flush_request(struct fixture *f)
{
    struct fwlab_block_request_v0 r = {0};
    struct fwlab_host_action_token_v0 *a = &r.operation_token.action;
    ++f->uid;
    r.version = FWLAB_BLOCK_SERVICE_V0_VERSION; r.size = sizeof(r);
    r.operation_token.version = FWLAB_BLOCK_SERVICE_V0_VERSION; r.operation_token.size = sizeof(r.operation_token);
    r.operation_token.type_tag = FWLAB_BLOCK_OP_TOKEN_V0_TAG;
    r.operation_token.provider_nonce = f->block.provider_nonce; r.operation_token.generation = f->block.generation;
    a->version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION; a->size = sizeof(*a); a->type_tag = FWLAB_HOST_ACTION_TOKEN_V0_TAG;
    a->command.instance_nonce = CLOSE_NONCE; a->command.command_uid = f->uid;
    a->command.controller_epoch = a->command.generation = 1;
    a->origin.word[0] = CLOSE_NONCE; a->origin.word[1] = f->uid;
    a->action_uid = f->uid; a->generation = 1; a->kind = FWLAB_HOST_ACTION_V0_BLOCK_FLUSH;
    r.namespace_ref = f->ftl->config.namespace_ref; r.operation = FWLAB_BLOCK_V0_FLUSH;
    r.durability = FWLAB_BLOCK_V0_DURABILITY_FRONTIER;
    CHECK(fwlab_block_request_v0_valid(&r)); return r;
}
static void flush(struct fixture *f)
{
    struct fwlab_block_request_v0 r = flush_request(f);
    admit(f, &r);
    struct fwlab_block_status_v0 s = finish(f, &r, 0);
    CHECK(s.outcome == FWLAB_BLOCK_V0_SUCCEEDED && s.durability_witness == FWLAB_BLOCK_V0_WITNESS_FRONTIER_DURABLE);
    CHECK(f->block.ops->retire_start(f->block.context, &r.operation_token) == FWLAB_SPINE_V0_OK);
    unsigned i;
    for (i = 0; i < 1000; ++i) {
        enum fwlab_spine_result_v0 rc = f->block.ops->retire_query(f->block.context, &r.operation_token, &s);
        if (rc == FWLAB_SPINE_V0_OK && s.state == FWLAB_BLOCK_V0_STATE_RETIRED) break;
        CHECK(rc == FWLAB_SPINE_V0_IN_PROGRESS); step(f, 0);
    }
    CHECK(i < 1000 && !f->ftl->parent.owned && !f->buffer.active);
}
static uint32_t partial_read(struct fixture *f, const struct fwlab_block_request_v0 *r)
{
    for (unsigned i = 0; i < STEPS; ++i) {
        const struct sf_parent *p = &f->ftl->parent;
        if (p->completed_lbas && p->completed_lbas < r->lba_count && sf_read_pool_busy(f->ftl)) {
            CHECK(p->owned && p->status.state == FWLAB_BLOCK_V0_STATE_ACCEPTED);
            return p->completed_lbas;
        }
        CHECK(p->status.state == FWLAB_BLOCK_V0_STATE_ACCEPTED); step(f, 0);
    }
    CHECK(0); return 0;
}
static void read_exclusion(struct mh_fixture *m, const uint8_t *gold)
{
    struct fixture *f = &m->base;
    struct fwlab_block_request_v0 r = request(f, FWLAB_BLOCK_V0_READ, 1, MH_LBAS - 1, 0);
    admit(f, &r); uint32_t prefix = partial_read(f, &r);
    CHECK(f->ftl->work.kind == SF_WORK_NONE && !sf_io_idle(f->ftl) && !sf_control_io_available(f->ftl));
    struct sf_map_entry saved[256]; memcpy(saved, f->ftl->map, sizeof(saved));
    uint64_t sequence[4], map = f->ftl->map_sequence, record = f->ftl->record_sequence;
    uint64_t uid = f->ftl->io.next_uid, cp = f->ftl->checkpoints, gc = f->ftl->garbage_collections;
    for (unsigned c = 0; c < 4; ++c) sequence[c] = fwlab_file_nand_v2_sequence(m->assembly.channel[c].scalar.context);
    /* A valid bufferless Flush supplies a distinct Host parent without
     * pretending a one-buffer fixture owns a second command's lease. */
    struct fwlab_block_request_v0 other = flush_request(f);
    struct fwlab_block_submit_result_v0 submitted;
    CHECK(f->block.ops->submit(f->block.context, &other, &submitted) == FWLAB_SPINE_V0_OK &&
        fwlab_block_submit_result_v0_matches_request(&submitted, &other) && submitted.disposition == FWLAB_HOST_ACTION_V0_BACKPRESSURE);
    CHECK(fwlab_ftl_scale_gc_start(f->ftl, 1) == FWLAB_SPINE_V0_WRONG_STATE);
    CHECK(fwlab_ftl_scale_checkpoint_start(f->ftl) == FWLAB_SPINE_V0_WRONG_STATE);
    CHECK(!memcmp(saved, f->ftl->map, sizeof(saved)) && map == f->ftl->map_sequence && record == f->ftl->record_sequence &&
        uid == f->ftl->io.next_uid && cp == f->ftl->checkpoints && gc == f->ftl->garbage_collections);
    for (unsigned c = 0; c < 4; ++c) CHECK(sequence[c] == fwlab_file_nand_v2_sequence(m->assembly.channel[c].scalar.context));
    CHECK(finish(f, &r, 0).outcome == FWLAB_BLOCK_V0_SUCCEEDED && !memcmp(f->buffer.bytes, gold + 512, r.buffer_span.length));
    retire(f, &r);
    printf("UNIFIED_RW_EXCLUSION|actual_API_boundary=1|published_lbas=%u|total_lbas=%u|read_pool_active=1|work_NONE=1|new_Flush_parent_BP=1|GC_CP_rejected=1|map_and_media_unchanged=1\n", prefix, r.lba_count);
}
static void maintenance(struct mh_fixture *m)
{
    struct fixture *f = &m->base;
    uint64_t gc = f->ftl->garbage_collections, cp = f->ftl->checkpoints;
    CHECK(fwlab_ftl_scale_gc_start(f->ftl, 1) == FWLAB_SPINE_V0_OK);
    unsigned i;
    for (i = 0; i < STEPS && (f->ftl->work.kind != SF_WORK_NONE || sf_meta_busy(f->ftl) || !sf_io_idle(f->ftl)); ++i) step(f, 0);
    CHECK(i < STEPS && f->ftl->garbage_collections == gc + 1);
    CHECK(f->ftl->map[1].ppa == 897 && f->ftl->map[2].ppa == 960);
    CHECK(fwlab_ftl_scale_checkpoint_start(f->ftl) == FWLAB_SPINE_V0_OK);
    for (i = 0; i < STEPS && (f->ftl->work.kind != SF_WORK_NONE || sf_meta_busy(f->ftl) || !sf_io_idle(f->ftl)); ++i) step(f, 0);
    CHECK(i < STEPS && f->ftl->checkpoints == cp + 1);
    uint64_t before = mh_stats(m).now_ns;
    io(f, FWLAB_BLOCK_V0_READ, 8, 16, 0); check_pattern(f->buffer.bytes, 8, 16, 0x72);
    CHECK(mh_stats(m).now_ns - before == 19448);
    puts("UNIFIED_RW_GC_IPR|actual_GC=1|actual_CP=1|ordinary_MAP897_960=1|same_LUN_two_planes=1|READ_model_ns=19448|still_writable=1");
}
static void cancel_then_write(struct mh_fixture *m, uint8_t *gold)
{
    struct fixture *f = &m->base;
    struct fwlab_block_request_v0 r = request(f, FWLAB_BLOCK_V0_READ, 1, MH_LBAS - 1, 0);
    admit(f, &r); uint32_t prefix = partial_read(f, &r);
    CHECK(f->block.ops->cancel(f->block.context, &r.operation_token) == FWLAB_SPINE_V0_OK);
    struct fwlab_block_status_v0 s = finish(f, &r, 0);
    CHECK(s.outcome == FWLAB_BLOCK_V0_CANCELLED && s.completed_lbas == prefix &&
        !sf_read_pool_busy(f->ftl) && !sf_write_pool_busy(f->ftl) && !f->ftl->quarantined);
    CHECK(!memcmp(f->buffer.bytes, gold + 512, (size_t)prefix * 512)); retire(f, &r);
    io(f, FWLAB_BLOCK_V0_WRITE, 128, 8, 0x75); gold_write(gold, 128, 8, 0x75);
    flush(f); check_gold(f, gold); binding(m);
    printf("UNIFIED_RW_CANCEL|published_prefix_lbas=%u|old_write_prefix_not_added=1|drained_cancel_then_WRITE_Flush_READ=1|no_quarantine_clear=1\n", prefix);
}
static void accepted_close(struct mh_fixture *m, uint8_t *gold, bool writing)
{
    struct fixture *f = &m->base;
    uint64_t frontier = f->ftl->durable_frontier;
    struct fwlab_block_request_v0 r = request(f, writing ? FWLAB_BLOCK_V0_WRITE : FWLAB_BLOCK_V0_READ,
        0, MH_LBAS, writing ? 0x74 : 0);
    admit(f, &r);
    unsigned i;
    for (i = 0; i < STEPS; ++i) {
        struct fwlab_nfc_channel_v2_stats s = mh_stats(m);
        if (s.phase == FWLAB_NFC_CHANNEL_V2_RUN && s.occupied_credits >= 2 &&
            (writing ? m->credit.outstanding >= 2 : sf_read_pool_busy(f->ftl))) break;
        step(f, 0);
    }
    CHECK(i < STEPS && f->ftl->parent.status.state == FWLAB_BLOCK_V0_STATE_ACCEPTED);
    CHECK(f->block.ops->epoch_close(f->block.context, CLOSE_NONCE, 1) == FWLAB_SPINE_V0_OK);
    CHECK(buffer_close(&f->buffer, CLOSE_NONCE, 1) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    struct fwlab_block_epoch_status_v0 epoch;
    CHECK(f->block.ops->epoch_quiescent(f->block.context, CLOSE_NONCE, 1, &epoch) == FWLAB_SPINE_V0_OK && !epoch.quiescent);
    struct fwlab_block_status_v0 done = finish(f, &r, 0);
    CHECK(done.outcome == FWLAB_BLOCK_V0_CANCELLED && !f->ftl->quarantined);
    if (writing) {
        CHECK(done.completed_lbas && done.completed_lbas < MH_LBAS &&
            done.durability_witness != FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE && f->ftl->durable_frontier == frontier);
        gold_write(gold, 0, done.completed_lbas, 0x74);
    } else CHECK(!done.completed_lbas && f->ftl->durable_frontier == frontier);
    retire(f, &r); mh_close(m); mh_open(m, false);
    CHECK(f->ftl->durable_frontier == frontier); binding(m); check_gold(f, gold);
    printf("UNIFIED_RW_CLOSE|writing=%d|accepted_NAND=1|completed_prefix_lbas=%u|zero_then_same_format3_reopen=1|frontier=%llu|full_namespace_exact=1\n",
        writing, done.completed_lbas, (unsigned long long)frontier);
}
int main(void)
{
    struct mh_fixture *m = mh_create(4); struct fixture *f = &m->base;
    uint8_t *gold = malloc(BUFFER_BYTES); CHECK(gold);
    mh_open(m, true); binding(m);
    struct fwlab_ftl_scale *identity = f->ftl;
    struct fwlab_nfc_page_v2_provider provider = f->ftl->page_nfc;
    io(f, FWLAB_BLOCK_V0_WRITE, 0, MH_LBAS, 0x71); gold_write(gold, 0, MH_LBAS, 0x71);
    read_exclusion(m, gold);
    io(f, FWLAB_BLOCK_V0_WRITE, 0, 24, 0x72); gold_write(gold, 0, 24, 0x72);
    flush(f); check_gold(f, gold); maintenance(m);
    io(f, FWLAB_BLOCK_V0_WRITE, 64, 16, 0x73); gold_write(gold, 64, 16, 0x73);
    flush(f); check_gold(f, gold); cancel_then_write(m, gold);
    CHECK(f->ftl == identity && f->ftl->page_nfc.ops == provider.ops && f->ftl->page_nfc.context == provider.context);
    accepted_close(m, gold, false); accepted_close(m, gold, true);
    io(f, FWLAB_BLOCK_V0_WRITE, MH_LBAS - 8, 8, 0x76); gold_write(gold, MH_LBAS - 8, 8, 0x76);
    flush(f); check_gold(f, gold); binding(m);
    mh_close(m); mh_destroy(m); free(gold);
    struct rusage usage; CHECK(getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss < 256 * 1024);
    printf("UNIFIED_RW_PARENT_PASS|one_mutable_format3=1|same_instance_interleaved_RW_Flush_GC_CP=1|NAND_IPR=1|real_media=1|old_media_preserved=1|peak_RSS_KiB=%ld|not_native_or_bandwidth=1\n", usage.ru_maxrss);
    return 0;
}
