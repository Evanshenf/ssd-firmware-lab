/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "fwlab/private/nfc_channel_v2.h"
#define main(...) existing_channel_fake_fixture_main(__VA_ARGS__)
#include "test_nfc_page_v2_lab.c"
#undef main

struct channel_fixture {
    struct fixture *child[4];
    struct fwlab_nand_channel_v2 assembly;
    struct fwlab_nfc_page_v2_lab_mutation_config timing;
    struct fwlab_nfc_channel_v2 *hub;
    struct fwlab_nfc_page_v2_provider provider;
    void *arena;
    uint8_t main[PAGES * MAIN + 1], oob[PAGES * OOB + 1];
};
static struct channel_fixture *channel_create(unsigned channels)
{
    struct channel_fixture *f = calloc(1, sizeof(*f));
    CHECK(f && channels && channels <= 4);
    for (unsigned c = 0; c < channels; ++c) {
        f->child[c] = create();
        f->child[c]->media.geometry.channels = 1;
        memset(f->child[c]->media.media_uuid, 0, 16); f->child[c]->media.media_uuid[0] = (uint8_t)(20u + c);
        f->assembly.channel[c] = f->child[c]->media;
        for (unsigned lun = 0; lun < 2; ++lun) for (unsigned p = 0; p < 2; ++p) {
            struct cells *cell = &f->child[c]->fake.cell[lun][p];
            for (size_t i = 0; i < sizeof(cell->main); ++i) cell->main[i] ^= (uint8_t)(31u * c);
            for (size_t i = 0; i < sizeof(cell->oob); ++i) cell->oob[i] ^= (uint8_t)(47u * c);
        }
    }
    f->assembly.version = FWLAB_NAND_CHANNEL_V2_VERSION; f->assembly.size = sizeof(f->assembly);
    f->assembly.geometry = f->child[0]->media.geometry;
    f->assembly.geometry.channels = (uint16_t)channels;
    f->assembly.media_uuid[0] = 97;
    /* Unused by hub execution; fake aggregate is construction-only here. */
    f->assembly.aggregate = f->child[0]->media.scalar;
    f->timing.version = FWLAB_NFC_PAGE_V2_LAB_MUTATION_VERSION; f->timing.size = sizeof(f->timing);
    f->timing.read = f->child[0]->config;
    f->timing.read.base.geometry = f->assembly.geometry;
    memcpy(f->timing.read.base.media_uuid, f->assembly.media_uuid, 16);
    memset(f->timing.read.lun, 0, sizeof(f->timing.read.lun));
    for (unsigned c = 0; c < channels; ++c) for (unsigned lun = 0; lun < 2; ++lun) {
        struct fwlab_nfc_page_v2_lab_lun *l = &f->timing.read.lun[c * 2u + lun];
        l->package = (uint16_t)c; l->die = (uint16_t)(c * 2u + lun); l->target_lun = (uint16_t)lun;
    }
    f->timing.program_confirm_ns = 5; f->timing.array_program_ns = 70;
    f->timing.erase_command_ns = 8; f->timing.array_erase_ns = 140;
    f->timing.status_command_ns = 7; f->timing.status_response_bytes = 1;
    CHECK(fwlab_nfc_channel_v2_arena_size() % fwlab_nfc_channel_v2_arena_alignment() == 0);
    f->arena = aligned_alloc(fwlab_nfc_channel_v2_arena_alignment(), fwlab_nfc_channel_v2_arena_size());
    CHECK(f->arena);
    CHECK(fwlab_nfc_channel_v2_init(f->arena, fwlab_nfc_channel_v2_arena_size(), &f->timing,
        &f->assembly, &f->hub) == FWLAB_NFC_API_OK);
    f->provider = fwlab_nfc_channel_v2_provider(f->hub); CHECK(f->provider.ops);
    return f;
}
static struct fwlab_nfc_channel_v2_stats channel_stats(struct channel_fixture *f)
{
    struct fwlab_nfc_channel_v2_stats s;
    CHECK(fwlab_nfc_channel_v2_snapshot(f->hub, &s) == FWLAB_NFC_API_OK); return s;
}
static void channel_tick(struct channel_fixture *f)
{
    struct fwlab_nfc_page_v2_step_result s;
    CHECK(f->provider.ops->step(f->hub, 1, &s) == FWLAB_NFC_API_OK && s.units_used == 1);
}
static void joined(struct channel_fixture *f)
{
    unsigned guard = 0;
    while (channel_stats(f).phase != FWLAB_NFC_CHANNEL_V2_JOINED) {
        CHECK(++guard < 5000); channel_tick(f);
    }
}
static void ack_all(struct channel_fixture *f)
{
    struct fwlab_nfc_page_v2_step_result s;
    CHECK(f->provider.ops->step(f->hub, 32, &s) == FWLAB_NFC_API_OK);
    CHECK(channel_stats(f).phase == FWLAB_NFC_CHANNEL_V2_BUILD && !channel_stats(f).occupied_credits);
}
static void channel_destroy(struct channel_fixture *f)
{
    bool quiet = false;
    unsigned guard = 0;
    CHECK(f->provider.ops->reset_begin(f->hub, 71, 3) == FWLAB_NFC_API_OK);
    do {
        CHECK(++guard < 100);
        CHECK(f->provider.ops->quiescent(f->hub, 71, 3, &quiet) == FWLAB_NFC_API_OK);
        if (!quiet) channel_tick(f);
    } while (!quiet);
    CHECK(channel_stats(f).phase == FWLAB_NFC_CHANNEL_V2_CLOSED);
    free(f->arena);
    for (unsigned c = 0; c < f->assembly.geometry.channels; ++c) destroy(f->child[c]);
    free(f);
}
static void channel_submit(struct channel_fixture *f, const struct fwlab_nfc_page_v2_request *r)
{ CHECK(f->provider.ops->try_submit(f->hub, r).disposition == FWLAB_NFC_ACCEPTED); }
static struct fwlab_nfc_page_v2_request channel_program(struct channel_fixture *f, uint64_t uid,
                                                       unsigned channel, unsigned pages)
{
    struct fwlab_nfc_page_v2_request r = request(uid, channel, 0, pages);
    struct cells *cell = &f->child[channel]->fake.cell[0][0];
    cell->next = 0; memset(cell->main, 0xff, sizeof(cell->main)); memset(cell->oob, 0xff, sizeof(cell->oob));
    r.kind = FWLAB_NFC_PAGE_V2_PROGRAM_GROUP;
    r.main = f->main + 1; r.main_bytes = pages * MAIN; r.oob = f->oob + 1; r.oob_bytes = pages * OOB;
    memset(f->main, 0x2d, sizeof(f->main)); memset(f->oob, 0x7e, sizeof(f->oob));
    return r;
}
static struct fwlab_nfc_page_v2_result channel_take(struct channel_fixture *f,
    const struct fwlab_nfc_page_v2_request *r, bool copy)
{
    struct fwlab_nfc_page_v2_result result;
    struct fwlab_nfc_page_v2_output out = {f->main + 1, (size_t)r->page_count * MAIN,
                                          f->oob + 1, (size_t)r->page_count * OOB};
    CHECK(f->provider.ops->take_result(f->hub, &r->operation, &result, copy ? &out : NULL) == FWLAB_NFC_API_OK);
    CHECK(result.first.channel == r->first.channel && result.operation.operation_uid == r->operation.operation_uid);
    if (copy) {
        struct cells *cell = &f->child[r->first.channel]->fake.cell[r->first.lun][r->first.plane];
        CHECK(result.read_valid && result.delivered_pages == r->page_count &&
              !memcmp(out.main, cell->main + r->first.page * MAIN, out.main_bytes) &&
              !memcmp(out.oob, cell->oob + r->first.page * OOB, out.oob_bytes));
    } else CHECK(!result.delivered_pages);
    return result;
}
static uint64_t channel_event(struct channel_fixture *f, unsigned channel, uint64_t uid, unsigned event)
{
    struct fwlab_nfc_channel_v2_stats s = channel_stats(f);
    for (uint32_t i = 0; i < s.channel[channel].trace_count; ++i) {
        struct fwlab_nfc_page_v2_lab_trace t;
        CHECK(fwlab_nfc_channel_v2_trace_at(f->hub, channel, i, &t) == FWLAB_NFC_API_OK && t.ppa.channel == channel);
        if (t.operation_uid == uid && t.event == event) return t.now_ns;
    }
    CHECK(0); return 0;
}
static void join_floor_retirement(void)
{
    struct channel_fixture *f = channel_create(4);
    struct fwlab_nfc_page_v2_request p = channel_program(f, 1, 0, 2), r = request(2, 1, 0, 1);
    struct fwlab_nfc_page_v2_request s = request(3, 0, 1, 1), t = request(4, 3, 0, 1), next = request(5, 2, 0, 1);
    struct fwlab_nfc_page_v2_result result;
    channel_submit(f, &p); channel_submit(f, &r); channel_submit(f, &s); channel_submit(f, &t);
    memset(f->main, 0x99, sizeof(f->main)); memset(f->oob, 0xaa, sizeof(f->oob));
    channel_submit(f, &p); /* Original snapshot, not changed caller spans. */
    CHECK(f->provider.ops->cancel(f->hub, &p.operation) == FWLAB_NFC_API_OK);
    CHECK(f->provider.ops->try_submit(f->hub, &next).disposition == FWLAB_NFC_BACKPRESSURE);
    unsigned guard = 0;
    while (channel_stats(f).phase != FWLAB_NFC_CHANNEL_V2_JOINED) {
        CHECK(++guard < 1000);
        CHECK(f->provider.ops->take_result(f->hub, &r.operation, &result, NULL) == FWLAB_NFC_API_WRONG_STATE);
        channel_tick(f);
    }
    struct fwlab_nfc_channel_v2_stats st = channel_stats(f);
    CHECK(st.accepted_requests == 4 && st.sealed_batches == 1 && st.joined_batches == 1 &&
          st.occupied_credits == 4 && st.results_pending == 4 && !st.retirement_pending);
    CHECK(st.now_ns == st.channel[0].now_ns && st.now_ns > st.channel[1].now_ns && st.channel[2].now_ns == 0);
    uint64_t first_join = st.now_ns;
    CHECK(st.snapshot_main_bytes == 2 * MAIN && st.snapshot_oob_bytes == 2 * OOB);
    CHECK(f->child[0]->fake.programs == 2 && f->child[0]->fake.cell[0][0].main[0] == 0x2d &&
          f->child[0]->fake.cell[0][0].oob[0] == 0x7e);
    struct fwlab_nfc_page_v2_output invalid = {f->arena, MAIN, f->oob + 1, OOB};
    CHECK(f->provider.ops->take_result(f->hub, &r.operation, &result, &invalid) == FWLAB_NFC_API_INVALID_CONTRACT);
    (void)channel_take(f, &r, true);
    CHECK(f->provider.ops->try_submit(f->hub, &r).reason == FWLAB_NFC_REASON_STALE);
    /* Retirement is now a real actor job/reply. Scheduling it is not its ACK;
     * the result's credit remains owned until the synchronized reply arrives. */
    channel_tick(f);
    CHECK(channel_stats(f).retired_acks == 0 && channel_stats(f).occupied_credits == 4);
    guard = 0;
    while (channel_stats(f).phase != FWLAB_NFC_CHANNEL_V2_JOINED) {
        CHECK(++guard < 32); channel_tick(f);
    }
    CHECK(channel_stats(f).retired_acks == 1 && channel_stats(f).occupied_credits == 3);
    CHECK(f->provider.ops->try_submit(f->hub, &next).disposition == FWLAB_NFC_BACKPRESSURE);
    struct fwlab_nfc_page_v2_step_result step;
    CHECK(f->provider.ops->step(f->hub, 32, &step) == FWLAB_NFC_API_OK && !step.units_used);
    result = channel_take(f, &p, false); CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS && result.effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE);
    (void)channel_take(f, &s, true); (void)channel_take(f, &t, false);
    CHECK(f->provider.ops->step(f->hub, 2, &step) == FWLAB_NFC_API_OK && step.units_used == 2);
    CHECK(f->provider.ops->try_submit(f->hub, &next).disposition == FWLAB_NFC_BACKPRESSURE);
    ack_all(f);
    bool idle = false;
    CHECK(fwlab_nfc_channel_v2_live_idle(f->hub, &idle) == FWLAB_NFC_API_OK && idle);
    channel_submit(f, &next); joined(f);
    CHECK(channel_event(f, 2, 5, FWLAB_NFC_PAGE_V2_LAB_ADMIT) == first_join);
    CHECK(channel_event(f, 2, 5, FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN) == first_join);
    CHECK(channel_stats(f).now_ns == first_join + 130);
    (void)channel_take(f, &next, true); ack_all(f); channel_destroy(f);
}
static void close_accepted(void)
{
    struct channel_fixture *f = channel_create(2);
    struct fwlab_nfc_page_v2_request a = channel_program(f, 1, 0, 1), b = channel_program(f, 2, 1, 1);
    channel_submit(f, &a); channel_submit(f, &b);
    CHECK(f->provider.ops->reset_begin(f->hub, 71, 3) == FWLAB_NFC_API_OK);
    struct fwlab_nfc_page_v2_request later = request(3, 0, 0, 1);
    CHECK(f->provider.ops->try_submit(f->hub, &later).reason == FWLAB_NFC_REASON_RESET);
    joined(f);
    bool quiet = true;
    CHECK(f->provider.ops->quiescent(f->hub, 71, 3, &quiet) == FWLAB_NFC_API_OK && !quiet);
    CHECK(channel_take(f, &a, false).terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(channel_take(f, &b, false).terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(f->provider.ops->quiescent(f->hub, 71, 3, &quiet) == FWLAB_NFC_API_OK && !quiet);
    CHECK(f->child[0]->fake.programs == 1 && f->child[1]->fake.programs == 1);
    channel_destroy(f);
}
static void failed_shard(void)
{
    struct channel_fixture *f = channel_create(2);
    f->child[0]->fake.error = 1;
    struct fwlab_nfc_page_v2_request r = request(1, 0, 0, 1), p = channel_program(f, 2, 1, 2);
    channel_submit(f, &r); channel_submit(f, &p); joined(f);
    CHECK(channel_stats(f).quarantined && !channel_stats(f).poisoned && channel_stats(f).results_pending == 2);
    CHECK(channel_take(f, &r, false).terminal == FWLAB_NFC_TERMINAL_FAILED);
    CHECK(channel_take(f, &p, false).terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(f->child[1]->fake.programs == 2);
    struct fwlab_nfc_page_v2_request later = request(3, 1, 0, 1);
    CHECK(f->provider.ops->try_submit(f->hub, &later).reason == FWLAB_NFC_REASON_INTERNAL);
    ack_all(f); channel_destroy(f);
}
static void construction_and_time_bounds(void)
{
    struct channel_fixture *f = channel_create(2);
    struct fwlab_nand_channel_v2 a = f->assembly;
    struct fwlab_nfc_channel_v2 *unused;
    memcpy(a.channel[1].media_uuid, a.channel[0].media_uuid, 16);
    CHECK(fwlab_nfc_channel_v2_init(f->arena, fwlab_nfc_channel_v2_arena_size(), &f->timing, &a, &unused) == FWLAB_NFC_API_INVALID_CONTRACT);
    a = f->assembly; a.channel[1].geometry.channels = 2;
    CHECK(fwlab_nfc_channel_v2_init(f->arena, fwlab_nfc_channel_v2_arena_size(), &f->timing, &a, &unused) == FWLAB_NFC_API_INVALID_CONTRACT);
    a = f->assembly; a.channel[1].scalar.context = a.channel[0].scalar.context;
    CHECK(fwlab_nfc_channel_v2_init(f->arena, fwlab_nfc_channel_v2_arena_size(), &f->timing, &a, &unused) == FWLAB_NFC_API_INVALID_CONTRACT);
    f->timing.read.virtual_ns_limit = 200;
    CHECK(fwlab_nfc_channel_v2_init(f->arena, fwlab_nfc_channel_v2_arena_size(), &f->timing,
        &f->assembly, &f->hub) == FWLAB_NFC_API_OK);
    f->provider = fwlab_nfc_channel_v2_provider(f->hub);
    struct fwlab_nfc_page_v2_request p = channel_program(f, 1, 0, 2), r = request(2, 0, 1, 1);
    CHECK(f->provider.ops->try_submit(f->hub, &p).reason == FWLAB_NFC_REASON_RANGE);
    p.page_count = 1; p.main_bytes = MAIN; p.oob_bytes = OOB; channel_submit(f, &p);
    CHECK(f->provider.ops->try_submit(f->hub, &r).reason == FWLAB_NFC_REASON_RANGE);
    r.first.channel = 1; channel_submit(f, &r); joined(f);
    CHECK(channel_stats(f).now_ns == 130);
    (void)channel_take(f, &p, false); (void)channel_take(f, &r, true); ack_all(f);
    r.operation.operation_uid = 3;
    CHECK(f->provider.ops->try_submit(f->hub, &r).reason == FWLAB_NFC_REASON_RANGE);
    channel_destroy(f);

    struct fixture *c = create(); begin(c);
    CHECK(fwlab_nfc_page_v2_lab_admission_floor(c->model, 100) == FWLAB_NFC_API_OK);
    r = request(1, 0, 0, 1); submit(c, &r);
    CHECK(fwlab_nfc_page_v2_lab_admission_floor(c->model, 200) == FWLAB_NFC_API_WRONG_STATE);
    drive(c); (void)take(c, &r, true);
    CHECK(stats(c).now_ns == 230);
    CHECK(fwlab_nfc_page_v2_lab_admission_floor(c->model, 229) == FWLAB_NFC_API_WRONG_STATE);
    CHECK(fwlab_nfc_page_v2_lab_admission_floor(c->model, 1000) == FWLAB_NFC_API_OK);
    CHECK(c->provider.ops->try_submit(c->model, &r).reason == FWLAB_NFC_REASON_STALE);
    destroy(c);
}
int main(void)
{
    join_floor_retirement(); close_accepted(); failed_shard(); construction_and_time_bounds();
    puts("NFC_CHANNEL_V2_PASS|profile=WAVE4-LAB4K|cooperative=1|total_credits=4|snapshot=1|sealed_ingress=1|join_before_visibility=1|retirement_ack=1|idle_floor=1|accepted_cancel_drains=1|shard_failure=1|disk_io=0");
    return 0;
}
