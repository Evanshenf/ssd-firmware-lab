/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Reuse adjacent fake cell bytes/physical facts. Its original read suite is
 * linked but not executed here. These costs are synthetic core-test values. */
#define main(...) existing_plane_read_fixture_main(__VA_ARGS__)
#include "test_nfc_page_v2_lab.c"
#undef main

static struct fixture *plane_create(enum fwlab_nfc_page_v2_lab_read_policy policy, uint16_t cap)
{
    struct fixture *f = create();
    struct fwlab_nfc_page_v2_lab_mutation_config c = {0};
    CHECK(stats(f).read_policy == FWLAB_NFC_PAGE_V2_LAB_LUN_EXCLUSIVE);
    f->config.base.geometry.plane_parallelism_per_lun = cap;
    f->media.geometry = f->config.base.geometry;
    c.version = FWLAB_NFC_PAGE_V2_LAB_MUTATION_VERSION; c.size = sizeof(c); c.read = f->config;
    c.program_confirm_ns = 5; c.array_program_ns = 70;
    c.erase_command_ns = 8; c.array_erase_ns = 140;
    c.status_command_ns = 7; c.status_response_bytes = 1;
    if (policy == FWLAB_NFC_PAGE_V2_LAB_LUN_EXCLUSIVE)
        CHECK(fwlab_nfc_page_v2_lab_mutation_init(f->arena, fwlab_nfc_page_v2_lab_arena_size(),
            &c, &f->media, &f->model) == FWLAB_NFC_API_OK);
    else
        CHECK(fwlab_nfc_page_v2_lab_mutation_init_policy(f->arena, fwlab_nfc_page_v2_lab_arena_size(),
            &c, &f->media, policy, &f->model) == FWLAB_NFC_API_OK);
    f->provider = fwlab_nfc_page_v2_lab_provider(f->model);
    CHECK(stats(f).read_policy == (uint32_t)policy);
    CHECK(stats(f).phase == FWLAB_NFC_PAGE_V2_LAB_TIMED_RW);
    struct fwlab_nfc_page_v2_lab *unchanged = f->model;
    CHECK(fwlab_nfc_page_v2_lab_mutation_init_policy(f->arena, fwlab_nfc_page_v2_lab_arena_size(),
        &c, &f->media, (enum fwlab_nfc_page_v2_lab_read_policy)2, &unchanged) == FWLAB_NFC_API_INVALID_CONTRACT);
    CHECK(unchanged == f->model);
    return f;
}
static void plane_tick(struct fixture *f)
{
    struct fwlab_nfc_page_v2_step_result s;
    CHECK(f->provider.ops->step(f->model, 1, &s) == FWLAB_NFC_API_OK && s.units_used == 1);
}
static void no_resources(struct fixture *f)
{
    struct fwlab_nfc_page_v2_lab_stats s = stats(f);
    CHECK(!s.held_luns && !s.held_read_planes && !s.active_read_arrays && !s.busy_channels);
}
static void read_pair(enum fwlab_nfc_page_v2_lab_read_policy policy, bool same, uint16_t cap)
{
    struct fixture *f = plane_create(policy, cap);
    struct fwlab_nfc_page_v2_request a = request(1, 0, 0, 1), b = request(2, 0, 0, 1);
    bool parallel = policy == FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE && !same && cap == 2;
    unsigned peak = 0, arrays = 0;
    b.first.plane = same ? 0 : 1; b.first.page = 1;
    submit(f, &a); submit(f, &b);
    for (unsigned guard = 0; stats(f).results_pending != 2; ++guard) {
        CHECK(guard < 100);
        plane_tick(f);
        struct fwlab_nfc_page_v2_lab_stats s = stats(f);
        CHECK(s.held_luns <= 1 && s.busy_channels <= 1);
        if (peak < s.held_read_planes) peak = s.held_read_planes;
        if (arrays < s.active_read_arrays) arrays = s.active_read_arrays;
    }
    struct fwlab_nfc_page_v2_lab_stats s = stats(f);
    CHECK(s.now_ns == (parallel ? 150u : 260u));
    CHECK(peak == (parallel ? 2u : 1u) && arrays == peak);
    CHECK(s.channel_busy_ns[0] == 60 && s.array_busy_ns[0] == 200);
    CHECK(s.register_busy_ns[0] == (parallel ? 270u : 260u));
    CHECK(s.plane_array_busy_ns[0][0] == (same ? 200u : 100u));
    CHECK(s.plane_array_busy_ns[0][1] == (same ? 0u : 100u));
    CHECK(s.plane_register_busy_ns[0][0] + s.plane_register_busy_ns[0][1] == s.register_busy_ns[0]);
    CHECK(event_time(f, 2, 1, FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN) == (parallel ? 10u : 130u));
    CHECK(event_time(f, 2, 1, FWLAB_NFC_PAGE_V2_LAB_DATA_BEGIN) >=
          event_time(f, 1, 0, FWLAB_NFC_PAGE_V2_LAB_DATA_END));
    no_resources(f);
    bool idle = true;
    CHECK(fwlab_nfc_page_v2_lab_live_idle(f->model, &idle) == FWLAB_NFC_API_OK && !idle);
    (void)take(f, &b, true); (void)take(f, &a, true);
    CHECK(fwlab_nfc_page_v2_lab_live_idle(f->model, &idle) == FWLAB_NFC_API_OK && idle);
    CHECK(f->fake.reads == 2);
    destroy(f);
}
static struct fwlab_nfc_page_v2_result effect_take(struct fixture *f,
                                                  const struct fwlab_nfc_page_v2_request *r)
{
    struct fwlab_nfc_page_v2_result out;
    CHECK(f->provider.ops->take_result(f->model, &r->operation, &out, NULL) == FWLAB_NFC_API_OK);
    CHECK(out.terminal == FWLAB_NFC_TERMINAL_SUCCESS && out.reason == FWLAB_NFC_REASON_NONE &&
          out.effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE);
    return out;
}
static void mutation_first(bool erasing)
{
    struct fixture *f = plane_create(FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE, 2);
    struct fwlab_nfc_page_v2_request mutation = request(1, 0, 0, erasing ? 1 : 2);
    struct fwlab_nfc_page_v2_request read = request(2, 0, 0, 1); read.first.plane = 1;
    mutation.kind = erasing ? FWLAB_NFC_PAGE_V2_ERASE : FWLAB_NFC_PAGE_V2_PROGRAM_GROUP;
    if (!erasing) {
        struct cells *c = &f->fake.cell[0][0]; c->next = 0;
        memset(c->main, 0xff, sizeof(c->main)); memset(c->oob, 0xff, sizeof(c->oob));
        memset(f->main, 0x57, sizeof(f->main)); memset(f->oob, 0x83, sizeof(f->oob));
        mutation.main = f->main + 1; mutation.main_bytes = 2 * MAIN;
        mutation.oob = f->oob + 1; mutation.oob_bytes = 2 * OOB;
    }
    submit(f, &mutation); submit(f, &read);
    for (unsigned guard = 0; !(erasing ? stats(f).issued_erases : stats(f).confirmed_program_groups); ++guard) {
        CHECK(guard < 100); plane_tick(f);
    }
    CHECK(stats(f).held_luns == 1 && !stats(f).held_read_planes && !stats(f).active_read_arrays);
    CHECK(f->provider.ops->cancel(f->model, &mutation.operation) == FWLAB_NFC_API_OK);
    drive(f);
    CHECK(event_time(f, 2, 0, FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN) >=
          event_time(f, 1, erasing ? 0 : 1, FWLAB_NFC_PAGE_V2_LAB_TERMINAL));
    CHECK(stats(f).mutation_reservation_ns[0] == (erasing ? 156u : 226u));
    CHECK(f->fake.programs == (erasing ? 0u : 2u) && f->fake.erases == (erasing ? 1u : 0u));
    (void)effect_take(f, &mutation); (void)take(f, &read, true);
    no_resources(f); destroy(f);
}
static void reads_before_erase(void)
{
    struct fixture *f = plane_create(FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE, 2);
    struct fwlab_nfc_page_v2_request a = request(1, 0, 0, 1), b = request(2, 0, 0, 1);
    struct fwlab_nfc_page_v2_request erase_r = request(3, 0, 0, 1);
    b.first.plane = 1; erase_r.kind = FWLAB_NFC_PAGE_V2_ERASE;
    submit(f, &a); submit(f, &b); submit(f, &erase_r);
    for (unsigned guard = 0; stats(f).results_pending != 2; ++guard) {
        CHECK(guard < 100); plane_tick(f); CHECK(f->fake.erases == 0);
    }
    CHECK(stats(f).now_ns == 150);
    (void)take(f, &a, true); (void)take(f, &b, true);
    drive(f);
    CHECK(event_time(f, 3, 0, FWLAB_NFC_PAGE_V2_LAB_ERASE_BEGIN) == 150);
    (void)effect_take(f, &erase_r); no_resources(f); destroy(f);
}
static void cancelled_read(bool reset_all, bool unstarted)
{
    struct fixture *f = plane_create(FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE, 2);
    struct fwlab_nfc_page_v2_request a = request(1, 0, 0, 2), b = request(2, 0, 0, 2);
    b.first.plane = 1;
    submit(f, &a); submit(f, &b);
    if (!unstarted) {
        for (unsigned guard = 0; stats(f).active_read_arrays != 2; ++guard) {
            CHECK(guard < 100); plane_tick(f);
        }
        CHECK(stats(f).held_luns == 1 && stats(f).held_read_planes == 2);
    }
    if (reset_all) CHECK(f->provider.ops->reset_begin(f->model, 71, 3) == FWLAB_NFC_API_OK);
    else CHECK(f->provider.ops->cancel(f->model, &a.operation) == FWLAB_NFC_API_OK);
    drive(f);
    CHECK(f->fake.reads == (reset_all ? 2u : unstarted ? 2u : 3u));
    struct fwlab_nfc_page_v2_result result = take(f, &a, false);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_CANCELLED);
    bool quiet = true;
    CHECK(f->provider.ops->quiescent(f->model, 71, 3, &quiet) == FWLAB_NFC_API_OK && !quiet);
    result = take(f, &b, !reset_all);
    if (reset_all) CHECK(result.terminal == FWLAB_NFC_TERMINAL_CANCELLED);
    if (unstarted) CHECK(stats(f).plane_register_busy_ns[0][0] == 0);
    no_resources(f); destroy(f);
}
static void failed_read_release(void)
{
    struct fixture *f = plane_create(FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE, 2);
    struct fwlab_nfc_page_v2_request a = request(1, 0, 0, 1), b = request(2, 0, 0, 1);
    b.first.plane = 1; f->fake.error = 1;
    submit(f, &a); submit(f, &b); drive(f);
    CHECK(stats(f).quarantined && f->fake.reads == 1); /* No sibling callback after RW quarantine. */
    CHECK(take(f, &a, false).terminal == FWLAB_NFC_TERMINAL_FAILED);
    CHECK(take(f, &b, false).terminal == FWLAB_NFC_TERMINAL_FAILED);
    no_resources(f); destroy(f);
}
int main(void)
{
    read_pair(FWLAB_NFC_PAGE_V2_LAB_LUN_EXCLUSIVE, false, 2);
    read_pair(FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE, false, 2);
    read_pair(FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE, true, 2);
    read_pair(FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE, false, 1);
    mutation_first(false); mutation_first(true); reads_before_erase();
    cancelled_read(false, true); cancelled_read(false, false); cancelled_read(true, false);
    failed_read_release();
    puts("nfc PAGE2 IPR-LAB4K: paired timing/resources, cap, mutation exclusion, cancel/reset PASS");
    return 0;
}
