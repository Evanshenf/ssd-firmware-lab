/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Included after ftl_cuts.inc: one required C-prime consumer-tail case. */
#define V2_TAIL_KIND 11u
#define V2_TAIL_EXIT (CUT_EXIT_BASE + (int)V2_TAIL_KIND)
struct v2_tail_hook {
    struct fixture *fixture;
    struct fnv2_io original;
    struct cut_receipt receipt;
    int fired;
};
static struct cut_receipt v2_tail_expected;
static unsigned v2_tail_seen;
static enum fwlab_nfc_api_result v2_tail_read(void *p, uint64_t at, void *v, size_t n)
{ struct v2_tail_hook *h = p; return h->original.read(h->original.context, at, v, n); }
static enum fwlab_nfc_api_result v2_tail_write(void *p, uint64_t at, const void *v, size_t n)
{ struct v2_tail_hook *h = p; return h->original.write(h->original.context, at, v, n); }
static enum fwlab_nfc_api_result v2_tail_sync(void *p)
{
    struct v2_tail_hook *h = p; struct fixture *f = h->fixture;
    struct fwlab_ftl_scale *ftl = cut_ftl(f);
    enum fwlab_nfc_api_result r = h->original.sync(h->original.context);
    if (r != FWLAB_NFC_API_OK || h->fired || ftl->meta.phase != SF_M_JOURNAL_A ||
        ftl->meta.record.kind != SF_MAP_GROUP) return r;
    /* This is the FIRST physical sync for this A cell: INTENT is durable,
     * but no main/page/block home or COMMIT write can follow our error. */
    h->fired = 1; h->receipt.kind = V2_TAIL_KIND;
    h->receipt.work_phase = ftl->work.phase; h->receipt.meta_phase = ftl->meta.phase;
    h->receipt.io_phase = ftl->io.phase; h->receipt.root_generation = ftl->root.generation;
    h->receipt.source = ftl->root.layout.journal_base[ftl->root.bank][0] *
        ftl->config.geometry.pages_per_block + ftl->journal_next;
    h->receipt.destination = ftl->root.layout.journal_base[ftl->root.bank][1] *
        ftl->config.geometry.pages_per_block + ftl->journal_next;
    h->receipt.record_sequence = ftl->record_sequence; h->receipt.map_sequence = ftl->map_sequence;
    h->receipt.media_sequence = media_sequence(f);
    return FWLAB_NFC_API_INVARIANT_FAILURE;
}
static enum fwlab_nfc_api_result v2_tail_resize(void *p, uint64_t n)
{ struct v2_tail_hook *h = p; return h->original.resize(h->original.context, n); }
static enum fwlab_nfc_api_result v2_tail_size(void *p, uint64_t *n)
{ struct v2_tail_hook *h = p; return h->original.size(h->original.context, n); }
static enum fwlab_nfc_api_result v2_tail_close(void *p)
{ struct v2_tail_hook *h = p; return h->original.close(h->original.context); }
static void v2_tail_recovery_observer(struct fixture *f)
{
    struct fwlab_ftl_scale *ftl = cut_ftl(f);
    const struct sf_io_result *io = &ftl->io.result;
    if (ftl->root.generation != v2_tail_expected.root_generation) return;
    if (ftl->io.phase == SF_IO_DONE && io->kind == SF_IO_READ) {
        if (io->ppa == v2_tail_expected.source) {
            CHECK(io->result != FWLAB_SPINE_V0_OK && !io->read_valid &&
                  io->completion.reason == FWLAB_NFC_REASON_ECC_UNCORRECTABLE &&
                  io->completion.ecc_status == FWLAB_NFC_ECC_UNCORRECTABLE);
            v2_tail_seen |= 1u;
        } else if (io->ppa == v2_tail_expected.destination) {
            CHECK(io->result == FWLAB_SPINE_V0_OK && io->read_valid &&
                  io->completion.valid_region_mask == FWLAB_NFC_REGION_MASK);
            CHECK(sf_bytes_ff(ftl->io.main[io->frame], SF_PAGE_BYTES) &&
                  sf_bytes_ff(ftl->io.oob[io->frame], SF_OOB_BYTES));
            v2_tail_seen |= 2u;
        }
    }
    if (ftl->meta.phase == SF_M_REPLAY_PAIR &&
        ftl->root.layout.journal_base[ftl->root.bank][0] * ftl->config.geometry.pages_per_block +
            ftl->meta.ordinal == v2_tail_expected.source) {
        CHECK((v2_tail_seen & 3u) == 3u && ftl->meta.rail_class[0] == SF_READ_ECC &&
              ftl->meta.rail_class[1] == SF_READ_ERASED && !ftl->quarantined);
        v2_tail_seen |= 4u;
    }
}
static void v2_tail_child(struct fixture *f, int fd)
{
    struct v2_tail_hook hook = {0}; struct fwlab_nvme_command c = {0};
    struct j0_host_transfer transfer = {0}; struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_nvme_completion_intent intent; uint8_t input[8192]; uint32_t i;
    media_open(f, 0); runtime_start(f, 0, 0); wait_ready(f);
    hook.fixture = f; hook.original = f->media_v2->io;
    f->media_v2->io = (struct fnv2_io){&hook, v2_tail_read, v2_tail_write, v2_tail_sync,
                                     v2_tail_resize, v2_tail_size, v2_tail_close};
    c.version = FWLAB_NVME_COMMAND_VERSION; c.size = sizeof(c);
    c.handle.instance_nonce = UINT64_C(0x435052494d450000) + f->incarnation;
    c.handle.command_uid = ++f->uid; c.handle.controller_epoch = c.handle.generation = 1;
    c.origin.word[0] = c.handle.instance_nonce; c.origin.word[1] = f->uid;
    c.trace_cookie = f->uid; c.safety_generation = 1; c.namespace_id = 1;
    c.opcode = 1; c.queue_class = FWLAB_NVME_QUEUE_IO; c.fuse = FWLAB_NVME_FUSE_NONE;
    c.data_pointer_format = FWLAB_NVME_DATA_POINTER_PRP; c.data_address_present = 1;
    c.command_dword10_15[2] = 15u | (UINT32_C(1) << 30);
    memset(input, 0x9e, sizeof(input)); transfer.version = J0_RUNTIME_VERSION; transfer.size = sizeof(transfer);
    transfer.direction = FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER; transfer.exact_bytes = sizeof(input); transfer.input = input;
    CHECK(j0_runtime_admit_start(f->runtime, J0_PROFILE_LINUX_V1, &c, &transfer, &ticket) == FWLAB_SPINE_V0_OK);
    for (i = 0; i < STEP_LIMIT; ++i) {
        uint32_t used = 0; enum fwlab_spine_result_v0 r = j0_runtime_step(f->runtime, 3, &used);
        enum fwlab_spine_result_v0 seen = j0_runtime_intent_read(f->runtime, &ticket, &intent);
        CHECK(seen != FWLAB_SPINE_V0_OK || intent.status_code != 0 || intent.status_code_type != 0);
        if (cut_ftl(f)->quarantined) break;
        CHECK(r == FWLAB_SPINE_V0_OK && used == 3);
    }
    CHECK(i < STEP_LIMIT && hook.fired && f->media_v2->quarantined && cut_ftl(f)->quarantined);
    CHECK(cut_ftl(f)->map_sequence == hook.receipt.map_sequence &&
          cut_ftl(f)->parent.status.outcome != FWLAB_BLOCK_V0_SUCCEEDED);
    receipt_io(fd, &hook.receipt, 1); CHECK(close(fd) == 0);
    _exit(V2_TAIL_EXIT); /* Quarantined process interruption, not a zero-holder close. */
}
static void v2_tail_journey(struct fixture *f)
{
    uint8_t old[8192], output[8192], next[8192]; int channel[2], status; pid_t child, waited;
    CHECK(f->use_media_v2 && f->media_v2 && !f->cut_observer);
    memset(old, 0x6d, sizeof(old)); command(f, 1, 0, 16, old, NULL, 1, 0);
    command(f, 2, 0, 16, NULL, output, 0, 0); CHECK(!memcmp(old, output, sizeof(old)));
    runtime_close(f); media_close(f); CHECK(pipe(channel) == 0 && fflush(NULL) == 0);
    child = fork(); CHECK(child >= 0);
    if (!child) { CHECK(close(channel[0]) == 0); v2_tail_child(f, channel[1]); _exit(1); }
    CHECK(close(channel[1]) == 0);
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    CHECK(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == V2_TAIL_EXIT);
    memset(&v2_tail_expected, 0, sizeof(v2_tail_expected)); receipt_io(channel[0], &v2_tail_expected, 0);
    CHECK(close(channel[0]) == 0 && v2_tail_expected.kind == V2_TAIL_KIND && !v2_tail_expected.zero_holders);
    f->incarnation += 2; media_open(f, 0); /* Actual physical INTENT recovery installs ABORT_UNKNOWN A. */
    CHECK(media_sequence(f) > v2_tail_expected.media_sequence);
    v2_tail_seen = 0; f->cut_observer = v2_tail_recovery_observer;
    runtime_start(f, 0, 0); wait_ready(f); f->cut_observer = NULL;
    CHECK(v2_tail_seen == 7u && cut_ftl(f)->root.generation > v2_tail_expected.root_generation);
    command(f, 2, 0, 16, NULL, output, 0, 0); CHECK(!memcmp(old, output, sizeof(old)));
    memset(next, 0xae, sizeof(next)); command(f, 1, 0, 16, next, NULL, 1, 0);
    reopen(f); command(f, 2, 0, 16, NULL, output, 0, 0); CHECK(!memcmp(next, output, sizeof(next)));
    printf("SCALE_FTL_V2_TAIL_PASS|real_Linux_Write=1|intent_sync_error=1|A_ppa=%u|B_ppa=%u|A_C3_ECC=1|B_complete_erased=1|prior_ack_preserved=1|fresh_journal_epoch=1|SELF_continuation_reopen=1|no_powerloss_or_throughput_claim=1\n",
           v2_tail_expected.source, v2_tail_expected.destination);
}
