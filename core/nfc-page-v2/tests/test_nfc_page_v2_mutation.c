/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Reuse the existing bounded fake bytes and helpers; its read suite is not
 * called from this executable. These timings are synthetic core-test values. */
#define main(...) existing_read_lab_fixture_main(__VA_ARGS__)
#include "test_nfc_page_v2_lab.c"
#undef main

struct mutation_fault {
    struct fake *fake;
    uint32_t mode, poison, program_calls, erase_calls, read_calls;
};
static struct mutation_fault fault;

static enum fwlab_nfc_api_result read_gate(void *opaque, const struct fwlab_nfc_ppa *p,
    uint32_t n, uint8_t *main, size_t mn, uint8_t *oob, size_t on,
    struct fwlab_nand_page_info *out, size_t capacity, struct fwlab_nand_block_info *block)
{
    CHECK(opaque == fault.fake && !fault.poison);
    ++fault.read_calls;
    if (fault.mode == 4 && !p->channel && !p->lun && p->page == 1) {
        fault.poison = 1; memset(main, 0xac, mn);
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    return read_pages(opaque, p, n, main, mn, oob, on, out, capacity, block);
}
static enum fwlab_nfc_api_result program_gate(void *opaque, const struct fwlab_nfc_ppa *p,
    uint32_t n, const uint8_t *main, size_t mn, const uint8_t *oob, size_t on,
    struct fwlab_nand_media_result *out, size_t capacity)
{
    CHECK(opaque == fault.fake && !fault.poison && n == 1);
    ++fault.program_calls;
    if (fault.mode >= 1 && fault.mode <= 3 && !p->channel && !p->lun && p->page == 1) {
        struct cells *c = cells(opaque, p);
        if (fault.mode == 2) {
            memcpy(c->main + p->page * MAIN, main, MAIN / 2);
            memset(out, 0xa1, sizeof(*out)); /* Untrustworthy on API failure. */
            fault.poison = 1;
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
        memset(out, 0, sizeof(*out));
        out->version = FWLAB_NFC_CONTRACT_VERSION; out->size = sizeof(*out);
        out->base_erase_generation = out->final_erase_generation = c->generation;
        out->reason = FWLAB_NFC_REASON_PROGRAM_FAILURE;
        if (fault.mode == 3) {
            memcpy(c->main + p->page * MAIN, main, MAIN / 2);
            memcpy(c->oob + p->page * OOB, oob, OOB / 2); ++c->next;
            out->physical_outcome = FWLAB_NFC_PHYS_APPLIED; out->integrity = FWLAB_NFC_INTEGRITY_TORN;
            out->applied_main_bytes = MAIN / 2; out->applied_oob_bytes = OOB / 2;
            out->applied_region_mask = FWLAB_NFC_REGION_MASK;
            fault.poison = 1;
        }
        return FWLAB_NFC_API_OK;
    }
    return program_pages(opaque, p, n, main, mn, oob, on, out, capacity);
}
static enum fwlab_nfc_api_result erase_gate(void *opaque, const struct fwlab_nfc_ppa *p,
    uint32_t n, uint8_t integrity, struct fwlab_nand_media_result *out)
{
    CHECK(opaque == fault.fake && !fault.poison);
    ++fault.erase_calls;
    if (fault.mode == 5) {
        fault.poison = 1; memset(out, 0xb2, sizeof(*out));
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    return erase(opaque, p, n, integrity, out);
}
static const struct fwlab_nand_media_ops mutation_scalar_ops = {
    FWLAB_NFC_CONTRACT_VERSION, sizeof(struct fwlab_nand_media_ops), 0,
    scalar_read, scalar_program, erase_gate, bad, hash
};
static const struct fwlab_nand_batch_v2_ops mutation_batch_ops = {
    FWLAB_NAND_BATCH_V2_VERSION, sizeof(struct fwlab_nand_batch_v2_ops), 0, read_gate, program_gate
};
static struct fwlab_nfc_page_v2_lab_mutation_config mutation_config(struct fixture *f)
{
    struct fwlab_nfc_page_v2_lab_mutation_config c = {0};
    c.version = FWLAB_NFC_PAGE_V2_LAB_MUTATION_VERSION; c.size = sizeof(c); c.read = f->config;
    c.program_confirm_ns = 5; c.array_program_ns = 70;
    c.erase_command_ns = 8; c.array_erase_ns = 140;
    c.status_command_ns = 7; c.status_response_bytes = 1;
    return c;
}
static struct fixture *mutation_create(bool slow_erase)
{
    struct fixture *f = create();
    struct fwlab_nfc_page_v2_lab_mutation_config c = mutation_config(f);
    if (slow_erase) c.array_erase_ns = 500;
    memset(&fault, 0, sizeof(fault)); fault.fake = &f->fake;
    for (unsigned l = 0; l < 4; ++l) for (unsigned p = 0; p < 2; ++p) {
        struct cells *b = &f->fake.cell[l][p]; b->next = b->generation = 0;
        memset(b->main, 0xff, sizeof(b->main)); memset(b->oob, 0xff, sizeof(b->oob));
    }
    f->media.ops = &mutation_batch_ops; f->media.scalar.ops = &mutation_scalar_ops;
    CHECK(fwlab_nfc_page_v2_lab_mutation_init(f->arena, fwlab_nfc_page_v2_lab_arena_size(),
        &c, &f->media, &f->model) == FWLAB_NFC_API_OK);
    f->provider = fwlab_nfc_page_v2_lab_provider(f->model);
    CHECK(stats(f).phase == FWLAB_NFC_PAGE_V2_LAB_TIMED_RW);
    CHECK(fwlab_nfc_page_v2_lab_begin_timed_read(f->model) == FWLAB_NFC_API_WRONG_STATE);
    return f;
}
static struct fwlab_nfc_page_v2_request program_request(struct fixture *f, uint64_t uid,
                                                       unsigned lun, unsigned pages)
{
    struct fwlab_nfc_page_v2_request r = request(uid, 0, lun, pages);
    r.kind = FWLAB_NFC_PAGE_V2_PROGRAM_GROUP;
    r.main = f->main + 1; r.main_bytes = pages * MAIN;
    r.oob = f->oob + 1; r.oob_bytes = pages * OOB;
    for (unsigned p = 0; p < pages; ++p) {
        memset(f->main + 1 + p * MAIN, 0x21 + (int)p, MAIN);
        memset(f->oob + 1 + p * OOB, 0x91 + (int)p, OOB);
    }
    return r;
}
static struct fwlab_nfc_page_v2_request erase_request(uint64_t uid, unsigned lun)
{
    struct fwlab_nfc_page_v2_request r = request(uid, 0, lun, 1);
    r.kind = FWLAB_NFC_PAGE_V2_ERASE; return r;
}
static void tick(struct fixture *f)
{
    struct fwlab_nfc_page_v2_step_result s;
    CHECK(f->provider.ops->step(f->model, 1, &s) == FWLAB_NFC_API_OK && s.units_used == 1);
}
static struct fwlab_nfc_page_v2_result mutation_take(struct fixture *f,
    const struct fwlab_nfc_page_v2_request *r)
{
    struct fwlab_nfc_page_v2_result out;
    CHECK(f->provider.ops->take_result(f->model, &r->operation, &out, NULL) == FWLAB_NFC_API_OK);
    CHECK(!out.read_valid && !out.delivered_pages && out.operation.operation_uid == r->operation.operation_uid);
    CHECK(f->provider.ops->take_result(f->model, &r->operation, &out, NULL) == FWLAB_NFC_API_STALE_TOKEN);
    return out;
}
static void only_none(const struct fwlab_nfc_page_v2_page_result *p)
{
    struct fwlab_nfc_page_v2_page_result expected = {0};
    expected.facts_valid = FWLAB_NFC_PAGE_V2_FACT_EFFECT;
    CHECK(!memcmp(p, &expected, sizeof(expected)));
}
static void snapshot_and_status(void)
{
    struct fixture *f = mutation_create(false);
    struct fwlab_nfc_page_v2_request r = program_request(f, 1, 0, 3);
    struct fwlab_nfc_page_v2_result out;
    submit(f, &r);
    memset(f->main, 0xca, sizeof(f->main)); memset(f->oob, 0xdb, sizeof(f->oob));
    submit(f, &r); /* Canonical duplicate does not snapshot the changed bytes. */
    for (unsigned i = 0; i < 7; ++i) tick(f);
    CHECK(stats(f).attempted_program_pages == 1 && stats(f).successful_program_pages == 1);
    CHECK(f->provider.ops->take_result(f->model, &r.operation, &out, NULL) == FWLAB_NFC_API_WRONG_STATE);
    CHECK(f->fake.cell[0][0].main[0] == 0x21 && f->fake.cell[0][0].oob[0] == 0x91 &&
          f->fake.cell[0][0].main[MAIN] == 0xff);
    drive(f); out = mutation_take(f, &r);
    CHECK(out.terminal == FWLAB_NFC_TERMINAL_SUCCESS && !out.reason &&
          out.effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE);
    for (unsigned p = 0; p < 3; ++p) {
        CHECK(out.page[p].effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE);
        for (unsigned i = 0; i < MAIN; ++i) CHECK(f->fake.cell[0][0].main[p * MAIN + i] == 0x21 + p);
        for (unsigned i = 0; i < OOB; ++i) CHECK(f->fake.cell[0][0].oob[p * OOB + i] == 0x91 + p);
    }
    struct fwlab_nfc_page_v2_lab_stats s = stats(f);
    CHECK(s.now_ns == 339 && s.accepted_program_groups == 1 && s.confirmed_program_groups == 1 &&
          s.attempted_program_pages == 3 && s.successful_program_pages == 3 && s.preflight_reads == 3 &&
          s.data_in_main_bytes == 3 * MAIN && s.data_in_oob_bytes == 3 * OOB &&
          s.program_main_bytes == 3 * MAIN && s.program_oob_bytes == 3 * OOB && s.status_response_bytes == 3);
    CHECK(s.program_array_busy_ns[0] == 210 && s.mutation_reservation_ns[0] == 339 && s.channel_busy_ns[0] == 129);
    r = request(2, 0, 0, 3); submit(f, &r); drive(f); (void)take(f, &r, true);
    destroy(f);
}
static void contention(void)
{
    struct fixture *f = mutation_create(false);
    struct fwlab_nfc_page_v2_request a = program_request(f, 1, 0, 2), b = program_request(f, 2, 1, 1);
    struct fwlab_nfc_page_v2_request e = erase_request(3, 0), r = request(4, 1, 0, 1);
    e.first.plane = 1;
    submit(f, &a); submit(f, &b); submit(f, &e); submit(f, &r); drive(f);
    CHECK(event_time(f, 1, 0, FWLAB_NFC_PAGE_V2_LAB_CONFIRM_END) < event_time(f, 2, 0, FWLAB_NFC_PAGE_V2_LAB_CONFIRM_END));
    CHECK(event_time(f, 2, 0, FWLAB_NFC_PAGE_V2_LAB_CONFIRM_END) < event_time(f, 1, 0, FWLAB_NFC_PAGE_V2_LAB_PROGRAM_EFFECT));
    CHECK(event_time(f, 2, 0, FWLAB_NFC_PAGE_V2_LAB_STATUS_BEGIN) > event_time(f, 2, 0, FWLAB_NFC_PAGE_V2_LAB_PROGRAM_EFFECT));
    CHECK(event_time(f, 2, 0, FWLAB_NFC_PAGE_V2_LAB_STATUS_END) <= event_time(f, 1, 1, FWLAB_NFC_PAGE_V2_LAB_CONFIRM_BEGIN));
    CHECK(event_time(f, 3, 0, FWLAB_NFC_PAGE_V2_LAB_ERASE_BEGIN) >= event_time(f, 1, 1, FWLAB_NFC_PAGE_V2_LAB_TERMINAL));
    struct fwlab_nfc_page_v2_lab_stats s = stats(f);
    uint64_t bus_owner[4] = {0};
    for (uint32_t i = 0; i < s.trace_count; ++i) {
        struct fwlab_nfc_page_v2_lab_trace t;
        CHECK(fwlab_nfc_page_v2_lab_trace_at(f->model, i, &t) == FWLAB_NFC_API_OK);
        bool start = t.event == FWLAB_NFC_PAGE_V2_LAB_LOAD_BEGIN || t.event == FWLAB_NFC_PAGE_V2_LAB_INPUT_BEGIN ||
            t.event == FWLAB_NFC_PAGE_V2_LAB_CONFIRM_BEGIN || t.event == FWLAB_NFC_PAGE_V2_LAB_ERASE_BEGIN ||
            t.event == FWLAB_NFC_PAGE_V2_LAB_STATUS_BEGIN || t.event == FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN ||
            t.event == FWLAB_NFC_PAGE_V2_LAB_DATA_BEGIN;
        bool end = t.event == FWLAB_NFC_PAGE_V2_LAB_LOAD_END || t.event == FWLAB_NFC_PAGE_V2_LAB_INPUT_END ||
            t.event == FWLAB_NFC_PAGE_V2_LAB_CONFIRM_END || t.event == FWLAB_NFC_PAGE_V2_LAB_ERASE_COMMAND_END ||
            t.event == FWLAB_NFC_PAGE_V2_LAB_STATUS_END || t.event == FWLAB_NFC_PAGE_V2_LAB_COMMAND_END ||
            t.event == FWLAB_NFC_PAGE_V2_LAB_DATA_END;
        if (start) { CHECK(!bus_owner[t.ppa.channel]); bus_owner[t.ppa.channel] = t.operation_uid; }
        if (end) { CHECK(bus_owner[t.ppa.channel] == t.operation_uid); bus_owner[t.ppa.channel] = 0; }
    }
    for (unsigned i = 0; i < 4; ++i) CHECK(!bus_owner[i]);
    CHECK(s.successful_program_pages == 3 && s.successful_erases == 1 && s.erased_pages == PAGES &&
          s.channel_busy_ns[0] == 145 && !s.held_luns && !s.busy_channels && s.results_pending == 4);
    CHECK(mutation_take(f, &a).terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(mutation_take(f, &b).terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(mutation_take(f, &e).page[0].final_erase_generation == 1);
    (void)take(f, &r, true); destroy(f);
}
static void cancel_boundaries(void)
{
    /* 0=queued, 1=load started, 3=input started, 5=confirm issued,
     * 7=first actual effect, 27=whole three-page group already DONE. */
    const unsigned cuts[] = {0, 1, 3, 5, 7, 27};
    for (unsigned n = 0; n < sizeof(cuts) / sizeof(cuts[0]); ++n) {
        struct fixture *f = mutation_create(false);
        struct fwlab_nfc_page_v2_request r = program_request(f, 1, 0, 3);
        struct fwlab_nfc_page_v2_result result;
        submit(f, &r);
        for (unsigned i = 0; i < cuts[n]; ++i) tick(f);
        CHECK(f->provider.ops->cancel(f->model, &r.operation) == FWLAB_NFC_API_OK);
        if (n % 2) CHECK(f->provider.ops->reset_begin(f->model, 71, 3) == FWLAB_NFC_API_OK);
        drive(f); result = mutation_take(f, &r);
        if (cuts[n] < 5) {
            CHECK(result.terminal == FWLAB_NFC_TERMINAL_CANCELLED && result.effect == FWLAB_NFC_PAGE_V2_EFFECT_NONE);
            CHECK(!stats(f).attempted_program_pages && !stats(f).confirmed_program_groups);
            for (unsigned p = 0; p < 3; ++p) only_none(&result.page[p]);
        } else {
            CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS && !result.reason &&
                  result.effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE && stats(f).successful_program_pages == 3);
        }
        CHECK(!stats(f).held_luns && !stats(f).busy_channels && !stats(f).active_slots);
        destroy(f);
    }
    for (unsigned started = 0; started < 2; ++started) {
        struct fixture *f = mutation_create(false);
        struct fwlab_nfc_page_v2_request r = erase_request(1, 0);
        submit(f, &r); if (started) tick(f);
        CHECK(f->provider.ops->cancel(f->model, &r.operation) == FWLAB_NFC_API_OK);
        CHECK(f->provider.ops->reset_begin(f->model, 71, 3) == FWLAB_NFC_API_OK);
        drive(f); struct fwlab_nfc_page_v2_result result = mutation_take(f, &r);
        CHECK(stats(f).attempted_erases == started && stats(f).successful_erases == started);
        CHECK(result.terminal == (started ? FWLAB_NFC_TERMINAL_SUCCESS : FWLAB_NFC_TERMINAL_CANCELLED));
        CHECK(result.page[0].final_erase_generation == started);
        destroy(f);
    }
}
static void partial_failures(void)
{
    for (unsigned mode = 1; mode <= 4; ++mode) {
        struct fixture *f = mutation_create(true);
        struct fwlab_nfc_page_v2_request r = program_request(f, 1, 0, 3), peer = erase_request(2, 1);
        fault.mode = mode; submit(f, &r); submit(f, &peer);
        /* Both groups are issued before the later PROGRAM/preflight fault. */
        for (unsigned i = 0; i < 9; ++i) tick(f);
        CHECK(stats(f).confirmed_program_groups == 1 && stats(f).issued_erases == 1);
        CHECK(f->provider.ops->cancel(f->model, &r.operation) == FWLAB_NFC_API_OK);
        drive(f);
        struct fwlab_nfc_page_v2_result result = mutation_take(f, &r), other = mutation_take(f, &peer);
        CHECK(result.terminal == FWLAB_NFC_TERMINAL_FAILED && result.reason != FWLAB_NFC_REASON_CANCELLED);
        CHECK(result.page[0].effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE &&
              result.page[0].applied_main_bytes == MAIN && result.page[0].applied_oob_bytes == OOB);
        only_none(&result.page[2]);
        if (mode == 2) {
            CHECK(result.effect == FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN && result.page[1].effect == FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN);
            CHECK(result.page[1].facts_valid == FWLAB_NFC_PAGE_V2_FACT_EFFECT && !result.page[1].applied_main_bytes);
        } else {
            CHECK(result.effect == FWLAB_NFC_PAGE_V2_EFFECT_NONCOMPLETE);
            CHECK(result.page[1].effect == (mode == 3 ? FWLAB_NFC_PAGE_V2_EFFECT_NONCOMPLETE : FWLAB_NFC_PAGE_V2_EFFECT_NONE));
            if (mode == 4) only_none(&result.page[1]);
        }
        CHECK(fault.program_calls == (mode == 4 ? 1u : 2u));
        CHECK(fault.erase_calls == (mode == 1 ? 1u : 0u));
        if (mode == 1) CHECK(other.terminal == FWLAB_NFC_TERMINAL_SUCCESS);
        else { CHECK(other.terminal == FWLAB_NFC_TERMINAL_FAILED); only_none(&other.page[0]); }
        CHECK(!stats(f).held_luns && !stats(f).busy_channels && !stats(f).active_slots);
        for (unsigned i = 0; i < MAIN; ++i) CHECK(f->fake.cell[0][0].main[2 * MAIN + i] == 0xff);
        destroy(f);
    }
    struct fixture *f = mutation_create(false);
    struct fwlab_nfc_page_v2_request r = erase_request(1, 0);
    fault.mode = 5; submit(f, &r); drive(f);
    struct fwlab_nfc_page_v2_result result = mutation_take(f, &r);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_FAILED && result.effect == FWLAB_NFC_PAGE_V2_EFFECT_UNKNOWN &&
          result.page[0].facts_valid == FWLAB_NFC_PAGE_V2_FACT_EFFECT && stats(f).attempted_erases == 1 &&
          !stats(f).successful_erases);
    destroy(f);
}
static void configuration_bounds(void)
{
    struct fixture *f = mutation_create(false);
    struct fwlab_nfc_page_v2_lab_mutation_config c = mutation_config(f);
    struct fwlab_nfc_page_v2_lab *unused;
    c.array_program_ns = 0;
    CHECK(fwlab_nfc_page_v2_lab_mutation_init(f->arena, fwlab_nfc_page_v2_lab_arena_size(), &c,
        &f->media, &unused) == FWLAB_NFC_API_INVALID_CONTRACT);
    c = mutation_config(f); c.status_command_ns = UINT64_MAX;
    CHECK(fwlab_nfc_page_v2_lab_mutation_init(f->arena, fwlab_nfc_page_v2_lab_arena_size(), &c,
        &f->media, &unused) == FWLAB_NFC_API_INVALID_CONTRACT);
    c = mutation_config(f); c.read.virtual_ns_limit = 156;
    CHECK(fwlab_nfc_page_v2_lab_mutation_init(f->arena, fwlab_nfc_page_v2_lab_arena_size(), &c,
        &f->media, &f->model) == FWLAB_NFC_API_OK);
    f->provider = fwlab_nfc_page_v2_lab_provider(f->model);
    struct fwlab_nfc_page_v2_request r = program_request(f, 1, 0, 2);
    CHECK(f->provider.ops->try_submit(f->model, &r).reason == FWLAB_NFC_REASON_RANGE);
    r.page_count = 1; r.main_bytes = MAIN; r.oob_bytes = OOB; submit(f, &r);
    struct fwlab_nfc_page_v2_request e = erase_request(2, 1);
    CHECK(f->provider.ops->try_submit(f->model, &e).reason == FWLAB_NFC_REASON_RANGE);
    drive(f); CHECK(mutation_take(f, &r).terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(stats(f).now_ns == 113); destroy(f);
}
int main(void)
{
    snapshot_and_status(); contention(); cancel_boundaries(); partial_failures(); configuration_bounds();
    puts("NFC_MUTATION_LAB_PASS|profile=LAB-RW-R2|synthetic_core_timings=1|owned_snapshot=1|effect_before_status=1|group_reservation=1|status_contention=1|cancel_confirm_boundary=1|known_prefix=1|quarantine_no_callbacks=1|disk_io=0");
    return 0;
}
