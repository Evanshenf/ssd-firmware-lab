/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Reuse the existing small real physical-v2 fixture and its ownership checks.
 * The linker selects policy only at construction; no live model replacement. */
#define MUTATION_MEDIA_PLANES 2u
#define MUTATION_MEDIA_PREFIX "fwlab-d215-plane"
#define main(...) retained_mutation_media_entry(__VA_ARGS__)
#include "test_nfc_lab_mutation_media.c"
#undef main

static enum fwlab_nfc_page_v2_lab_read_policy selected_policy;
enum fwlab_nfc_api_result __wrap_fwlab_nfc_page_v2_lab_mutation_init(void *arena, size_t bytes,
    const struct fwlab_nfc_page_v2_lab_mutation_config *config,
    const struct fwlab_nand_batch_v2 *media, struct fwlab_nfc_page_v2_lab **out)
{
    return fwlab_nfc_page_v2_lab_mutation_init_policy(arena, bytes, config, media, selected_policy, out);
}
static struct fwlab_nfc_page_v2_request plane_request(struct mutation_media *f,
    uint64_t uid, unsigned plane, unsigned block, unsigned page, uint16_t kind, unsigned n)
{
    struct fwlab_nfc_page_v2_request r = request(f, uid, 0, kind, n);
    r.first.plane = (uint16_t)plane; r.first.block = (uint16_t)block; r.first.page = (uint16_t)page;
    return r;
}
static void check_bytes(struct mutation_media *f, unsigned n, int main_byte, int oob_byte)
{
    for (unsigned i = 0; i < n * MAIN; ++i) CHECK(f->main[i] == main_byte);
    for (unsigned i = 0; i < n * OOB; ++i) CHECK(f->oob[i] == oob_byte);
}
static void no_resources(struct mutation_media *f)
{
    struct fwlab_nfc_page_v2_lab_stats s = stats(f);
    CHECK(!s.active_slots && !s.held_luns && !s.busy_channels &&
        !s.held_read_planes && !s.active_read_arrays && !s.quarantined);
}
static struct mutation_media *plane_create(enum fwlab_nfc_page_v2_lab_read_policy policy)
{
    selected_policy = policy;
    struct mutation_media *f = create_media(0);
    for (unsigned plane = 0; plane < 2; ++plane) {
        struct fwlab_nfc_page_v2_request r = plane_request(f, plane + 1, plane, 0, 0,
            FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, 3);
        memset(f->main, plane ? 0x43 : 0x31, sizeof(f->main));
        memset(f->oob, plane ? 0x84 : 0x72, sizeof(f->oob));
        submit(f, &r);
        CHECK(take(f, &r, 0).effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE);
    }
    CHECK(stats(f).read_policy == (uint32_t)policy && stats(f).successful_program_pages == 6);
    no_resources(f); return f;
}
static void compare_reads(struct mutation_media *f, uint64_t uid, bool different)
{
    struct fwlab_nfc_page_v2_lab_stats before = stats(f), after;
    uint64_t hash = f->actual.scalar.ops->hash(f->actual.scalar.context);
    uint64_t sequence = fwlab_file_nand_v2_sequence(f->media);
    struct fwlab_nfc_page_v2_request a = plane_request(f, uid, 0, 0, 0, FWLAB_NFC_PAGE_V2_READ_GROUP, 1);
    struct fwlab_nfc_page_v2_request b = plane_request(f, uid + 1, different ? 1 : 0, 0, 1,
        FWLAB_NFC_PAGE_V2_READ_GROUP, 1);
    unsigned peak_array = 0, peak_planes = 0, guard = 0;
    submit(f, &a); submit(f, &b);
    while (stats(f).results_pending != 2) {
        CHECK(++guard < 100); step(f); after = stats(f);
        if (peak_array < after.active_read_arrays) peak_array = after.active_read_arrays;
        if (peak_planes < after.held_read_planes) peak_planes = after.held_read_planes;
        CHECK(after.held_luns <= 1 && after.busy_channels <= 1);
    }
    CHECK(take(f, &a, 1).read_valid); check_bytes(f, 1, 0x31, 0x72);
    CHECK(take(f, &b, 1).read_valid); check_bytes(f, 1, different ? 0x43 : 0x31, different ? 0x84 : 0x72);
    after = stats(f); no_resources(f);
    bool overlap = different && selected_policy == FWLAB_NFC_PAGE_V2_LAB_INDEPENDENT_PLANE;
    CHECK(after.now_ns - before.now_ns == (overlap ? 19448u : 30448u));
    CHECK(peak_array == (overlap ? 2u : 1u));
    if (overlap) CHECK(peak_planes == 2);
    CHECK(after.materialized_pages == before.materialized_pages + 2 &&
        after.array_busy_ns[0] == before.array_busy_ns[0] + 20000 &&
        after.channel_busy_ns[0] == before.channel_busy_ns[0] + 10448);
    CHECK(after.plane_array_busy_ns[0][0] - before.plane_array_busy_ns[0][0] == (different ? 10000u : 20000u));
    CHECK(after.plane_array_busy_ns[0][1] - before.plane_array_busy_ns[0][1] == (different ? 10000u : 0u));
    uint64_t first_array_start = event_time(f, uid, 0, FWLAB_NFC_PAGE_V2_LAB_COMMAND_END);
    uint64_t first_array_end = event_time(f, uid, 0, FWLAB_NFC_PAGE_V2_LAB_ARRAY_READY);
    uint64_t second_array_start = event_time(f, uid + 1, 1, FWLAB_NFC_PAGE_V2_LAB_COMMAND_END);
    CHECK(first_array_end - first_array_start == 10000);
    if (overlap) CHECK(first_array_end - second_array_start == 9000);
    else CHECK(second_array_start >= first_array_end);
    CHECK(event_time(f, uid + 1, 1, FWLAB_NFC_PAGE_V2_LAB_DATA_BEGIN) >=
        event_time(f, uid, 0, FWLAB_NFC_PAGE_V2_LAB_DATA_END));
    CHECK(f->actual.scalar.ops->hash(f->actual.scalar.context) == hash && fwlab_file_nand_v2_sequence(f->media) == sequence);
    printf("PLANE_MEDIA_READ|policy=%u|different_plane=%d|elapsed_model_ns=%llu|peak_arrays=%u|main_OOB_exact=1|shared_bus_serial=1|media_hash_sequence_unchanged=1\n",
        (unsigned)selected_policy, different, (unsigned long long)(after.now_ns - before.now_ns), peak_array);
}
static void mutation_exclusion(struct mutation_media *f)
{
    struct fwlab_nfc_page_v2_request a = plane_request(f, 7, 0, 0, 0, FWLAB_NFC_PAGE_V2_READ_GROUP, 1);
    struct fwlab_nfc_page_v2_request b = plane_request(f, 8, 1, 0, 0, FWLAB_NFC_PAGE_V2_READ_GROUP, 1);
    struct fwlab_nfc_page_v2_request p = plane_request(f, 9, 1, 1, 0, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, 2);
    memset(f->main, 0x65, sizeof(f->main)); memset(f->oob, 0x98, sizeof(f->oob));
    submit(f, &a); submit(f, &b); submit(f, &p);
    CHECK(take(f, &p, 0).terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(take(f, &a, 1).read_valid); check_bytes(f, 1, 0x31, 0x72);
    CHECK(take(f, &b, 1).read_valid); check_bytes(f, 1, 0x43, 0x84);
    CHECK(event_time(f, 9, 0, FWLAB_NFC_PAGE_V2_LAB_LOAD_BEGIN) >=
        event_time(f, 8, 0, FWLAB_NFC_PAGE_V2_LAB_DATA_END));
    p = plane_request(f, 10, 0, 1, 0, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP, 2);
    b = plane_request(f, 11, 1, 0, 0, FWLAB_NFC_PAGE_V2_READ_GROUP, 1);
    memset(f->main, 0x55, sizeof(f->main)); memset(f->oob, 0x97, sizeof(f->oob));
    submit(f, &p); submit(f, &b);
    CHECK(take(f, &b, 1).read_valid); check_bytes(f, 1, 0x43, 0x84);
    CHECK(take(f, &p, 0).terminal == FWLAB_NFC_TERMINAL_SUCCESS);
    CHECK(event_time(f, 11, 0, FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN) >=
        event_time(f, 10, 1, FWLAB_NFC_PAGE_V2_LAB_TERMINAL));
    struct fwlab_nfc_page_v2_request e = plane_request(f, 12, 0, 1, 0, FWLAB_NFC_PAGE_V2_ERASE, 1);
    b = plane_request(f, 13, 1, 1, 0, FWLAB_NFC_PAGE_V2_READ_GROUP, 1);
    submit(f, &e); submit(f, &b);
    CHECK(take(f, &b, 1).read_valid); check_bytes(f, 1, 0x65, 0x98);
    struct fwlab_nfc_page_v2_result erased = take(f, &e, 0);
    CHECK(erased.terminal == FWLAB_NFC_TERMINAL_SUCCESS && erased.page[0].final_erase_generation > erased.page[0].base_erase_generation);
    CHECK(event_time(f, 13, 0, FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN) >=
        event_time(f, 12, 0, FWLAB_NFC_PAGE_V2_LAB_TERMINAL));
    no_resources(f);
    puts("PLANE_MEDIA_MUTATION|READ_then_PROGRAM=excluded|whole_PROGRAM_group_then_other_plane_READ=excluded|ERASE_then_other_plane_READ=excluded|real_bytes_generation=1");
}
static void cancellation_reset(struct mutation_media *f)
{
    struct fwlab_nfc_page_v2_request a = plane_request(f, 14, 0, 0, 0, FWLAB_NFC_PAGE_V2_READ_GROUP, 2);
    uint64_t reads = stats(f).materialized_pages;
    submit(f, &a); CHECK(f->port.ops->cancel(f->port.context, &a.operation) == FWLAB_NFC_API_OK);
    CHECK(take(f, &a, 0).terminal == FWLAB_NFC_TERMINAL_CANCELLED && stats(f).materialized_pages == reads);
    a = plane_request(f, 15, 0, 0, 0, FWLAB_NFC_PAGE_V2_READ_GROUP, 2);
    struct fwlab_nfc_page_v2_request b = plane_request(f, 16, 1, 0, 0, FWLAB_NFC_PAGE_V2_READ_GROUP, 2);
    submit(f, &a); submit(f, &b);
    unsigned guard = 0;
    while (stats(f).active_read_arrays != 2) { CHECK(++guard < 100); step(f); }
    CHECK(f->port.ops->cancel(f->port.context, &a.operation) == FWLAB_NFC_API_OK);
    struct fwlab_nfc_page_v2_result result = take(f, &a, 0);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_CANCELLED && !result.read_valid);
    result = take(f, &b, 1); CHECK(result.read_valid && result.delivered_pages == 2); check_bytes(f, 2, 0x43, 0x84);
    CHECK(stats(f).materialized_pages == reads + 3); no_resources(f);
    a = plane_request(f, 17, 0, 0, 0, FWLAB_NFC_PAGE_V2_READ_GROUP, 2);
    b = plane_request(f, 18, 1, 0, 0, FWLAB_NFC_PAGE_V2_READ_GROUP, 2);
    submit(f, &a); submit(f, &b); guard = 0;
    while (stats(f).active_read_arrays != 2) { CHECK(++guard < 100); step(f); }
    CHECK(f->port.ops->reset_begin(f->port.context, NONCE, 1) == FWLAB_NFC_API_OK);
    result = take(f, &a, 0); CHECK(result.terminal == FWLAB_NFC_TERMINAL_CANCELLED && result.reason == FWLAB_NFC_REASON_RESET && !result.read_valid);
    result = take(f, &b, 0); CHECK(result.terminal == FWLAB_NFC_TERMINAL_CANCELLED && result.reason == FWLAB_NFC_REASON_RESET && !result.read_valid);
    CHECK(stats(f).materialized_pages == reads + 5); no_resources(f);
    bool quiet = false;
    CHECK(f->port.ops->quiescent(f->port.context, NONCE, 1, &quiet) == FWLAB_NFC_API_OK && quiet);
    puts("PLANE_MEDIA_DRAIN|unstarted_cancel_no_effect=1|started_cancel_one_page=1|other_plane_unaffected=1|reset_two_planes=zero_resources");
}
int main(void)
{
    for (unsigned policy = 0; policy <= 1; ++policy) {
        struct mutation_media *f = plane_create((enum fwlab_nfc_page_v2_lab_read_policy)policy);
        compare_reads(f, 3, true); compare_reads(f, 5, false);
        if (policy) { mutation_exclusion(f); cancellation_reset(f); }
        destroy(f);
    }
    puts("PLANE_MEDIA_PASS|same_actual_physical_v2=1|only_explicit_read_policy_differs=1|tmpfs_not_vendor_or_wallclock_performance=1");
    return 0;
}
