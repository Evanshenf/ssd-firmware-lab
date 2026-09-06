/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Included only in test_ftl.c. These fixed process cuts observe the real
 * state machines; they do not replace a provider or introduce a product hook. */
#include <errno.h>

enum named_cut {
    CUT_DATA_NO_MAP = 1, CUT_JOURNAL_A_AFTER_DATA, CUT_GC_COPY_NO_MAP,
    CUT_CP_INSTALL, CUT_CP_ROOT, CUT_ERASE_NO_DONE,
    CUT_CLOSE_WRITE, CUT_CLOSE_JOURNAL, CUT_CLOSE_GC, CUT_WRONG_EXPECTATION
};
#define CUT_EXIT_BASE 80
#define CUT_SNAPSHOT_PAGES 128u

struct cut_receipt {
    uint32_t kind;
    uint32_t work_phase;
    uint32_t meta_phase;
    uint32_t io_phase;
    uint32_t source;
    uint32_t destination;
    uint32_t live_pages;
    uint32_t erased_generation;
    uint64_t record_sequence;
    uint64_t map_sequence;
    uint64_t media_sequence;
    uint64_t root_generation;
    uint8_t zero_holders;
};

struct cut_snapshot {
    uint32_t count;
    uint32_t lpn[CUT_SNAPSHOT_PAGES];
    struct sf_map_entry map[CUT_SNAPSHOT_PAGES];
    uint8_t data[CUT_SNAPSHOT_PAGES][4096];
};

static int cut_receipt_fd = -1;

static const char *cut_name(uint32_t kind)
{
    static const char *const names[] = {
        "invalid", "DATA_NO_MAP", "JOURNAL_A_AFTER_DATA", "GC_COPY_NO_MAP",
        "CP_INSTALL_beforeRoot", "CP_ROOT_beforeOldRetire", "ERASE_NO_DONE",
        "CLOSE_WRITE", "CLOSE_JOURNAL", "CLOSE_GC", "WRONG_EXPECTATION"
    };
    CHECK(kind > 0 && kind < sizeof(names) / sizeof(names[0]));
    return names[kind];
}

static struct fwlab_ftl_scale *cut_ftl(struct fixture *f)
{
    struct fwlab_ftl_scale *ftl = f->runtime->block.context;
    CHECK(ftl && ftl->magic == SF_MAGIC && ftl->initialized);
    return ftl;
}

static void receipt_io(int fd, struct cut_receipt *receipt, int writing)
{
    uint8_t *bytes = (uint8_t *)receipt;
    size_t completed = 0;
    while (completed < sizeof(*receipt)) {
        ssize_t count = writing ? write(fd, bytes + completed, sizeof(*receipt) - completed)
                                : read(fd, bytes + completed, sizeof(*receipt) - completed);
        if (count < 0 && errno == EINTR) continue;
        CHECK(count > 0);
        completed += (size_t)count;
    }
}

static int completed_io(const struct fwlab_ftl_scale *ftl, uint8_t kind)
{
    return ftl->io.phase == SF_IO_DONE && ftl->io.result.kind == kind &&
        ftl->io.result.result == FWLAB_SPINE_V0_OK &&
        ftl->io.result.completion.terminal == FWLAB_NFC_TERMINAL_SUCCESS;
}

static int data_before_map(const struct fwlab_ftl_scale *ftl)
{
    return ftl->work.kind == SF_WORK_HOST &&
        ftl->work.request.operation == FWLAB_BLOCK_V0_WRITE && !sf_meta_busy(ftl) &&
        ((ftl->work.phase == SF_W_HOST_PROGRAM_WAIT && completed_io(ftl, SF_IO_PROGRAM)) ||
         (ftl->work.phase == SF_W_HOST_PAGE && ftl->work.page_index > 0));
}

static int gc_copy_before_map(const struct fwlab_ftl_scale *ftl)
{
    return ftl->work.kind == SF_WORK_GC && !sf_meta_busy(ftl) &&
        ((ftl->work.phase == SF_W_GC_PROGRAM_WAIT && completed_io(ftl, SF_IO_PROGRAM)) ||
         (ftl->work.phase == SF_W_GC_READ && ftl->work.moved > 0));
}

static void named_cut_observer(struct fixture *f)
{
    struct fwlab_ftl_scale *ftl = cut_ftl(f);
    struct cut_receipt receipt;
    int reached = 0;
    int closing = f->cut_kind >= CUT_CLOSE_WRITE && f->cut_kind <= CUT_CLOSE_GC;
    switch (f->cut_kind) {
    case CUT_DATA_NO_MAP:
    case CUT_CLOSE_WRITE:
        reached = data_before_map(ftl);
        break;
    case CUT_JOURNAL_A_AFTER_DATA:
    case CUT_CLOSE_JOURNAL:
        reached = ftl->work.kind == SF_WORK_HOST && ftl->meta.record.kind == SF_MAP_GROUP &&
            ftl->meta.phase == SF_M_JOURNAL_B && !ftl->meta.pending && sf_io_idle(ftl);
        break;
    case CUT_GC_COPY_NO_MAP:
    case CUT_CLOSE_GC:
        reached = gc_copy_before_map(ftl);
        if (reached) {
            CHECK(ftl->work.live_count > 0);
            CHECK(ftl->blocks[ftl->work.source_block].live_pages == ftl->work.live_count);
            CHECK(ftl->blocks[ftl->work.destination_block].live_pages == 0);
        }
        break;
    case CUT_CP_INSTALL:
        reached = ftl->meta.phase == SF_M_CP_ROOT && !ftl->meta.pending && sf_io_idle(ftl);
        break;
    case CUT_CP_ROOT:
        reached = ftl->meta.phase == SF_M_CP_RETIRE && !ftl->meta.pending && sf_io_idle(ftl);
        break;
    case CUT_ERASE_NO_DONE:
        reached = ftl->work.kind == SF_WORK_GC &&
            ((ftl->work.phase == SF_W_ERASE_WAIT && completed_io(ftl, SF_IO_ERASE)) ||
             (ftl->work.phase == SF_W_ERASE_DONE_WAIT && ftl->meta.phase == SF_M_JOURNAL_A &&
              ftl->meta.record.kind == SF_ERASE_DONE && !ftl->meta.pending && sf_io_idle(ftl)));
        if (reached) {
            CHECK(ftl->work.live_count > 0 && ftl->erase_intent_sequence != 0);
            CHECK(ftl->blocks[ftl->work.source_block].live_pages == 0);
        }
        break;
    default:
        CHECK(0 && "unknown named process cut");
    }
    if (!reached) return;
    memset(&receipt, 0, sizeof(receipt));
    receipt.kind = f->cut_kind;
    receipt.work_phase = ftl->work.phase;
    receipt.meta_phase = ftl->meta.phase;
    receipt.io_phase = ftl->io.phase;
    receipt.source = ftl->work.source_block;
    receipt.destination = ftl->work.destination_block;
    receipt.live_pages = ftl->work.live_count;
    receipt.erased_generation = ftl->io.result.completion.final_erase_generation;
    receipt.record_sequence = ftl->record_sequence;
    receipt.map_sequence = ftl->map_sequence;
    receipt.media_sequence = fwlab_file_nand_v1_sequence(f->media);
    receipt.root_generation = ftl->root.generation;
    f->cut_observer = NULL;
    if (closing) {
        /* This never returns to command_profile: the child exits after the
         * existing close helper proves every Host/FTL/NFC holder is zero. */
        runtime_close(f);
        media_close(f);
        receipt.zero_holders = 1;
    }
    receipt_io(cut_receipt_fd, &receipt, 1);
    CHECK(close(cut_receipt_fd) == 0);
    _exit(CUT_EXIT_BASE + (int)receipt.kind);
}

static struct cut_snapshot *capture_live_pages(struct fixture *f)
{
    struct fwlab_ftl_scale *ftl = cut_ftl(f);
    struct cut_snapshot *snapshot = calloc(1, sizeof(*snapshot));
    uint32_t lpn;
    CHECK(snapshot);
    for (lpn = 0; lpn < ftl->root.layout.lpn_count; ++lpn) {
        uint32_t at;
        if (ftl->map[lpn].state != SF_VALUE) continue;
        at = snapshot->count++;
        CHECK(at < CUT_SNAPSHOT_PAGES);
        snapshot->lpn[at] = lpn;
        snapshot->map[at] = ftl->map[lpn];
        command(f, 2, (uint64_t)lpn * 8u, 8, NULL, snapshot->data[at], 0, 0);
    }
    CHECK(snapshot->count > 0);
    return snapshot;
}

static void verify_snapshot(struct fixture *f, const struct cut_snapshot *snapshot,
                            uint32_t changed_lpn, uint8_t replacement, int change_mode,
                            int exact_old_map)
{
    struct fwlab_ftl_scale *ftl = cut_ftl(f);
    uint8_t output[4096], newer[4096];
    uint32_t i;
    int found = changed_lpn == SF_NONE;
    memset(newer, replacement, sizeof(newer));
    for (i = 0; i < snapshot->count; ++i) {
        uint32_t lpn = snapshot->lpn[i];
        command(f, 2, (uint64_t)lpn * 8u, 8, NULL, output, 0, 0);
        if (lpn == changed_lpn) {
            int old = memcmp(output, snapshot->data[i], sizeof(output)) == 0;
            int current = memcmp(output, newer, sizeof(output)) == 0;
            CHECK(change_mode == 0 ? old : (change_mode == 1 ? current : old || current));
            found = 1;
        } else {
            CHECK(memcmp(output, snapshot->data[i], sizeof(output)) == 0);
        }
        if (exact_old_map)
            CHECK(memcmp(&ftl->map[lpn], &snapshot->map[i], sizeof(snapshot->map[i])) == 0);
    }
    CHECK(found && ftl->host_head == SF_NONE);
    for (i = ftl->root.layout.data_first_block; i < ftl->physical_blocks; ++i)
        CHECK(ftl->blocks[i].disk.role != SF_RECLAIM_PENDING &&
              ftl->blocks[i].disk.role != SF_GC_DEST);
}

static void start_gc_cut(struct fixture *f)
{
    struct fwlab_ftl_scale *ftl = cut_ftl(f);
    CHECK(ftl->host_head == SF_NONE && ftl->victim_count > 0);
    CHECK(ftl->blocks[ftl->victim_heap[0]].live_pages > 0);
    CHECK(ftl->blocks[ftl->victim_heap[0]].live_pages <= 61);
    CHECK(scale_storage_gc(f->runtime, 3) == FWLAB_SPINE_V0_OK);
    wait_idle(f);
    CHECK(0 && "named live-GC cut was not observed");
}

static struct cut_receipt fork_operation_cut(struct fixture *f, uint32_t kind,
                                             uint32_t target_lpn, uint8_t replacement)
{
    int channel[2], status;
    pid_t child, waited;
    struct cut_receipt receipt;
    CHECK(!f->cut_observer);
    runtime_close(f);
    media_close(f);
    CHECK(pipe(channel) == 0);
    CHECK(fflush(NULL) == 0);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        uint8_t bytes[4096];
        CHECK(close(channel[0]) == 0);
        cut_receipt_fd = channel[1];
        media_open(f, 0);
        runtime_start(f, 0, 0);
        wait_ready(f); /* Recovery's own checkpoint is deliberately unhooked. */
        f->cut_kind = kind;
        f->cut_observer = named_cut_observer;
        if (kind == CUT_GC_COPY_NO_MAP || kind == CUT_ERASE_NO_DONE || kind == CUT_CLOSE_GC)
            start_gc_cut(f);
        else if (kind == CUT_CP_INSTALL || kind == CUT_CP_ROOT) {
            CHECK(scale_storage_checkpoint(f->runtime) == FWLAB_SPINE_V0_OK);
            wait_idle(f);
        } else {
            memset(bytes, replacement, sizeof(bytes));
            command(f, 1, (uint64_t)target_lpn * 8u, 8, bytes, NULL, 1, 0);
        }
        CHECK(0 && "requested cut did not fire before operation completion");
    }
    CHECK(close(channel[1]) == 0);
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    CHECK(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == CUT_EXIT_BASE + (int)kind);
    memset(&receipt, 0, sizeof(receipt));
    receipt_io(channel[0], &receipt, 0);
    CHECK(close(channel[0]) == 0 && receipt.kind == kind);
    /* The child consumed an incarnation in its private address space. Reserve
     * it here too, so the parent's subsequent model has a different nonce. */
    f->incarnation += 2;
    media_open(f, 0);
    runtime_start(f, 0, 0);
    wait_ready(f);
    return receipt;
}

static void wrong_expectation_cut(struct fixture *f)
{
    pid_t child, waited;
    int status;
    uint64_t before;
    runtime_close(f);
    media_close(f);
    CHECK(fflush(NULL) == 0);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        uint32_t i;
        struct fwlab_ftl_scale_status observed;
        media_open(f, 0);
        before = fwlab_file_nand_v1_sequence(f->media);
        runtime_start(f, 0, f->lbas - 8u);
        for (i = 0; i < STEP_LIMIT; ++i) {
            uint32_t used;
            enum fwlab_spine_result_v0 result = j0_runtime_step(f->runtime, 3, &used);
            CHECK(scale_storage_query(f->runtime, &observed) == FWLAB_SPINE_V0_OK);
            if (observed.quarantined) break;
            CHECK(result == FWLAB_SPINE_V0_OK && !f->runtime->ready);
        }
        CHECK(i < STEP_LIMIT && observed.fault_code == SF_FAULT_METADATA);
        CHECK(!f->runtime->ready && !f->runtime->namespace_bound);
        CHECK(fwlab_file_nand_v1_sequence(f->media) == before);
        _exit(CUT_EXIT_BASE + CUT_WRONG_EXPECTATION);
    }
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    CHECK(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == CUT_EXIT_BASE + CUT_WRONG_EXPECTATION);
    f->incarnation += 2;
    media_open(f, 0);
    before = fwlab_file_nand_v1_sequence(f->media);
    runtime_start(f, 0, 0);
    CHECK(fwlab_file_nand_v1_sequence(f->media) == before);
    wait_ready(f);
    puts("SCALE_FTL_CUT_PASS|case=WRONG_EXPECTATION|metadata_before_cleanup=1|media_unchanged=1");
}

static void cuts_journey(struct fixture *f)
{
    struct cut_snapshot *snapshot;
    struct cut_receipt receipt;
    uint8_t bytes[4096];
    uint32_t kind;
    const uint32_t target_lpn = 64;
    /* This mode follows the bounded functional workload, not --full. Four
     * distinct committed pages guarantee a live closed candidate after reopen. */
    for (kind = 0; kind < 4; ++kind) {
        memset(bytes, (int)(0x61u + kind), sizeof(bytes));
        command(f, 1, (uint64_t)(2048u + kind) * 8u, 8, bytes, NULL, 1, 0);
    }
    memset(bytes, 0x91, sizeof(bytes));
    command(f, 1, (uint64_t)target_lpn * 8u, 8, bytes, NULL, 1, 0);
    for (kind = CUT_DATA_NO_MAP; kind <= CUT_CLOSE_GC; ++kind) {
        uint32_t changed = (kind == CUT_DATA_NO_MAP || kind == CUT_JOURNAL_A_AFTER_DATA ||
                            kind == CUT_CLOSE_WRITE || kind == CUT_CLOSE_JOURNAL) ? target_lpn : SF_NONE;
        uint8_t replacement = (uint8_t)(0xa0u + kind);
        int change_mode = kind == CUT_JOURNAL_A_AFTER_DATA || kind == CUT_CLOSE_JOURNAL ? 1 :
                          (kind == CUT_CLOSE_WRITE ? 2 : 0);
        int exact_map = kind == CUT_DATA_NO_MAP || kind == CUT_GC_COPY_NO_MAP ||
                        kind == CUT_CP_INSTALL || kind == CUT_CP_ROOT;
        snapshot = capture_live_pages(f);
        receipt = fork_operation_cut(f, kind, target_lpn, replacement);
        verify_snapshot(f, snapshot, changed, replacement, change_mode, exact_map);
        if (kind == CUT_GC_COPY_NO_MAP)
            CHECK(cut_ftl(f)->blocks[receipt.destination].disk.role == SF_FREE);
        if (kind == CUT_ERASE_NO_DONE) {
            const struct sf_block *source = &cut_ftl(f)->blocks[receipt.source];
            CHECK(source->disk.role == SF_FREE && !source->live_pages);
            CHECK(source->disk.erase_generation > receipt.erased_generation);
        }
        if (kind >= CUT_CLOSE_WRITE) CHECK(receipt.zero_holders);
        printf("SCALE_FTL_CUT_PASS|case=%s|pages=%u|work_phase=%u|meta_phase=%u|io_phase=%u|live_victim=%u|record=%llu|map=%llu|root=%llu|zero=%u\n",
            cut_name(kind), snapshot->count, receipt.work_phase, receipt.meta_phase,
            receipt.io_phase, receipt.live_pages, (unsigned long long)receipt.record_sequence,
            (unsigned long long)receipt.map_sequence, (unsigned long long)receipt.root_generation,
            receipt.zero_holders);
        free(snapshot);
    }
    wrong_expectation_cut(f);
    identify(f);
}
