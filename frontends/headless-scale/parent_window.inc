/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

/* Finite D136 cases share the legacy parent fixture and real media runner. */
static uint64_t window_nanoseconds(void)
{
    struct timespec now;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

static void window_io(struct fixture *f, uint32_t operation, uint64_t lba,
                      uint32_t lbas, uint8_t seed)
{
    struct media_counts before = f->counts;
    uint64_t programs = f->ftl->window.program_groups, reads = f->ftl->window.read_groups;
    uint64_t cp = f->ftl->checkpoints, gc = f->ftl->garbage_collections;
    uint64_t children = f->ftl->nfc_children, sequence = media_sequence(f);
    uint64_t opens = f->open_records, closes = f->close_records, maps = f->window_records;
    uint32_t buffer_reads = f->buffer.reads, buffer_writes = f->buffer.writes;
    uint64_t elapsed = 0;
    CHECK(f->window_v2 && lbas && !(lbas % 2048u));
    for (uint32_t offset = 0; offset < lbas; offset += 2048u) {
        struct fwlab_block_request_v0 r = request(f, operation, lba + offset, 2048, seed);
        struct fwlab_block_status_v0 s;
        uint64_t started = window_nanoseconds();
        admit(f, &r); s = finish(f, &r, 0); retire(f, &r);
        elapsed += window_nanoseconds() - started;
        CHECK(s.outcome == FWLAB_BLOCK_V0_SUCCEEDED && s.effect == FWLAB_BLOCK_V0_EFFECT_FULL &&
              s.completed_lbas == 2048 &&
              (operation != FWLAB_BLOCK_V0_WRITE || s.durability_witness == FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE));
        if (operation == FWLAB_BLOCK_V0_READ) check_pattern(f->buffer.bytes, lba + offset, 2048, seed);
    }
    if (operation == FWLAB_BLOCK_V0_WRITE)
        CHECK(f->ftl->window.program_groups > programs && f->ftl->window.max_program_pages > 1);
    else CHECK(f->ftl->window.read_groups > reads && f->ftl->window.max_read_pages > 1);
    printf("PARENT_WINDOW_COUNTS|phase=%s-%uMiB|host_bytes=%u|elapsed_ns=%llu|read_calls=%llu|write_calls=%llu|sync_calls=%llu|read_bytes=%llu|write_bytes=%llu|media_commits=%llu|nfc_children=%llu|program_groups=%llu|read_groups=%llu|max_program_pages=%u|max_read_pages=%u|OPEN=%llu|CLOSE=%llu|MAP_WINDOW=%llu|CP=%llu|GC=%llu|buffer_reads=%u|buffer_writes=%u|ftl_window_bytes=%u|nfc_arena_bytes=%zu|snapshot_copy=1|payload_setup_in_timing=0|read_check_in_timing=0|paired_acceptance=0|runtime_io=%s|constructor_preparation_in_timing=0\n",
        operation == FWLAB_BLOCK_V0_WRITE ? "write" : "read", lbas / 2048u, lbas * 512u,
        (unsigned long long)elapsed,
        (unsigned long long)(f->counts.reads - before.reads),
        (unsigned long long)(f->counts.writes - before.writes),
        (unsigned long long)(f->counts.syncs - before.syncs),
        (unsigned long long)(f->counts.read_bytes - before.read_bytes),
        (unsigned long long)(f->counts.write_bytes - before.write_bytes),
        (unsigned long long)(media_sequence(f) - sequence),
        (unsigned long long)(f->ftl->nfc_children - children),
        (unsigned long long)(f->ftl->window.program_groups - programs),
        (unsigned long long)(f->ftl->window.read_groups - reads),
        f->ftl->window.max_program_pages, f->ftl->window.max_read_pages,
        (unsigned long long)(f->open_records - opens),
        (unsigned long long)(f->close_records - closes),
        (unsigned long long)(f->window_records - maps),
        (unsigned long long)(f->ftl->checkpoints - cp),
        (unsigned long long)(f->ftl->garbage_collections - gc),
        f->buffer.reads - buffer_reads, f->buffer.writes - buffer_writes,
        SF_WINDOW_BYTES, fwlab_nfc_page_v2_arena_size(),
        f->byte_profile == POSIX_BYTE_MAPPED ? "MAP_SHARED_prefaulted" : "POSIX");
}

static void window_holes(struct fixture *f)
{
    io(f, FWLAB_BLOCK_V0_WRITE, 98304, 8, 0x35);
    io(f, FWLAB_BLOCK_V0_WRITE, 98328, 8, 0x36);
    io(f, FWLAB_BLOCK_V0_WRITE, 98337, 1, 0x37);
    io(f, FWLAB_BLOCK_V0_READ, 98304, 40, 0);
    check_pattern(f->buffer.bytes, 98304, 8, 0x35);
    for (uint32_t i = 8 * 512; i < 24 * 512; ++i) CHECK(!f->buffer.bytes[i]);
    check_pattern(f->buffer.bytes + 24 * 512, 98328, 8, 0x36);
    for (uint32_t i = 32 * 512; i < 33 * 512; ++i) CHECK(!f->buffer.bytes[i]);
    check_pattern(f->buffer.bytes + 33 * 512, 98337, 1, 0x37);
    for (uint32_t i = 34 * 512; i < 40 * 512; ++i) CHECK(!f->buffer.bytes[i]);
    uint64_t children = f->ftl->nfc_children, reads = f->counts.reads;
    io(f, FWLAB_BLOCK_V0_READ, 65536, 2048, 0);
    for (uint32_t i = 0; i < BUFFER_BYTES; ++i) CHECK(!f->buffer.bytes[i]);
    CHECK(f->ftl->nfc_children == children && f->counts.reads == reads);
    puts("PARENT_WINDOW_HOLES_PASS|mixed_values_holes_partial_page=1|unmapped_1MiB_zero=1|hole_no_physical_read=1");
}

static void cancel_after_data(int closing, int byte_profile)
{
    struct fixture *f = create(1, byte_profile); uint32_t i;
    struct fwlab_block_request_v0 r;
    struct fwlab_block_status_v0 s;
    size_t map_bytes = (VOLUME_LBAS / 8u) * sizeof(struct sf_map_entry);
    struct sf_map_entry *before = malloc(map_bytes); CHECK(before);
    open_lower(f, 1, 1);
    io(f, FWLAB_BLOCK_V0_WRITE, 0, 2048, 0x91);
    r = request(f, FWLAB_BLOCK_V0_WRITE, 0, 2048, 0x92); admit(f, &r);
    uint64_t frontier = f->ftl->parent.base_frontier, map_sequence = f->ftl->map_sequence;
    memcpy(before, f->ftl->map, map_bytes);
    for (i = 0; i < STEPS; ++i) {
        if (f->ftl->work.phase == SF_W_HOST_PROGRAM_WAIT &&
            f->ftl->io.phase == SF_IO_DONE && f->ftl->io.result.kind == SF_IO_PROGRAM &&
            f->ftl->io.window_transfer && !f->ftl->parent.completed_groups) break;
        step(f, 0);
    }
    CHECK(i < STEPS && f->ftl->io.result.effect == SF_EFFECT_COMPLETE &&
          f->ftl->io.result.result == FWLAB_SPINE_V0_OK && !f->ftl->parent.completed_lbas);
    CHECK(!memcmp(before, f->ftl->map, map_bytes) && f->ftl->map_sequence == map_sequence);
    uint32_t prefix = f->ftl->work.request.lba_count;
    uint64_t sequence = media_sequence(f), syncs = f->counts.syncs;
    CHECK(prefix > 8 && prefix < 2048 && !sf_meta_busy(f->ftl));
    if (closing) {
        CHECK(f->block.ops->epoch_close(f->block.context, CLOSE_NONCE, 1) == FWLAB_SPINE_V0_OK);
        CHECK(buffer_close(&f->buffer, CLOSE_NONCE, 1) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    } else CHECK(f->block.ops->cancel(f->block.context, &r.operation_token) == FWLAB_SPINE_V0_OK);
    s = finish(f, &r, 0);
    CHECK(s.outcome == FWLAB_BLOCK_V0_CANCELLED && s.effect == FWLAB_BLOCK_V0_EFFECT_EXACT_PREFIX &&
          s.completed_lbas == prefix && s.data_bytes == prefix * 512u && !s.durability_witness);
    CHECK(f->ftl->durable_frontier == frontier && f->ftl->map_sequence == map_sequence + 1 &&
          media_sequence(f) == sequence + 2 && f->counts.syncs == syncs + 6 && !f->ftl->quarantined);
    retire(f, &r); free(before);
    if (closing) { close_lower(f); open_lower(f, 0, 1); }
    io(f, FWLAB_BLOCK_V0_READ, 0, 2048, 0);
    check_pattern(f->buffer.bytes, 0, prefix, 0x92);
    check_pattern(f->buffer.bytes + prefix * 512u, prefix, 2048 - prefix, 0x91);
    close_lower(f); destroy(f);
    printf("PARENT_WINDOW_POSTDATA_CANCEL_PASS|case=%s|DATA_complete_before_cancel=1|map_unpublished_at_cut=1|A_B_drained=1|rail_commits=2|rail_syncs=6|prefix=%u|frontier_unchanged=1|zero_close=1\n",
           closing ? "epoch_close" : "cancel", prefix);
}

static void format_mismatch(int written_window_v2, int byte_profile)
{
    struct fixture *f = create(1, byte_profile); int status;
    f->window_v2 = (uint8_t)written_window_v2;
    open_lower(f, 1, 1); io(f, FWLAB_BLOCK_V0_WRITE, 0, 16, 0x48); close_lower(f);
    CHECK(fflush(NULL) == 0); pid_t child = fork(); CHECK(child >= 0);
    if (!child) {
        f->window_v2 = (uint8_t)!written_window_v2;
        construct_lower(f, 0, 1);
        uint64_t sequence = media_sequence(f), writes = f->counts.writes, syncs = f->counts.syncs;
        CHECK(fwlab_ftl_scale_recover_start(f->ftl, VOLUME_LBAS) == FWLAB_SPINE_V0_OK);
        uint32_t i;
        for (i = 0; i < STEPS && !f->ftl->quarantined; ++i) step(f, 1);
        CHECK(i < STEPS && !f->ftl->ready && f->ftl->fault_code == SF_FAULT_METADATA &&
              !f->buffer.active && !f->ftl->parent.owned);
        CHECK(media_sequence(f) == sequence && f->counts.writes == writes && f->counts.syncs == syncs);
        CHECK(fwlab_file_nand_v2_close(f->media_v2) == FWLAB_NFC_API_OK);
        _exit(89); /* Rejected FTL stays quarantined; no successful fini claim. */
    }
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 89);
    open_lower(f, 0, 1); io(f, FWLAB_BLOCK_V0_READ, 0, 16, 0);
    check_pattern(f->buffer.bytes, 0, 16, 0x48);
    io(f, FWLAB_BLOCK_V0_WRITE, 16, 16, 0x49);
    io(f, FWLAB_BLOCK_V0_READ, 16, 16, 0); check_pattern(f->buffer.bytes, 16, 16, 0x49);
    close_lower(f); destroy(f);
    printf("PARENT_WINDOW_FORMAT_REJECT_PASS|disk_format=%u|constructor_format=%u|same_Cprime_media=1|before_FTL_writes=1|correct_constructor_restart_continue=1|zero_close_after_recovery=1\n",
           written_window_v2 ? 2u : 1u, written_window_v2 ? 1u : 2u);
}
