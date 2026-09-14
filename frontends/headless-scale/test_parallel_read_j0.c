/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Reuse the existing J0/profile/lifecycle fixture; do not create a second
 * command driver or logical-file backend. Only these finite LAB cases run. */
#define main existing_scale_fixture_main
#include "test_ftl.c"
#undef main

struct read_lab_fixture {
    struct fixture f;
    struct fwlab_nfc_page_v2_lab_config lab;
    struct j0_storage_factory underlying;
    struct fwlab_controller_buffer_port_v0 buffer;
    struct fwlab_controller_buffer_ops_v0 buffer_ops;
    uint32_t published, publications;
    int watch_publication;
};
static struct read_lab_fixture *active_lab;

static enum fwlab_controller_buffer_result_v0 observe_write(void *context,
    const struct fwlab_controller_buffer_lease_v0 *lease,
    const struct fwlab_controller_buffer_span_v0 *span, const void *bytes, size_t count)
{
    struct read_lab_fixture *l = active_lab;
    CHECK(l && context == l->buffer.context);
    if (l->watch_publication) CHECK(span->offset == l->published && span->length == count);
    enum fwlab_controller_buffer_result_v0 result = l->buffer.ops->write(context, lease, span, bytes, count);
    if (l->watch_publication && result == FWLAB_CONTROLLER_BUFFER_V0_OK) {
        l->published += (uint32_t)count; ++l->publications;
    }
    return result;
}

static enum fwlab_spine_result_v0 observe_bind(void *opaque,
    const struct j0_runtime_config *config, const struct fwlab_controller_buffer_port_v0 *buffer,
    const struct fwlab_block_namespace_ref_v0 *namespace_ref, uint64_t lifecycle_nonce,
    uint64_t ftl_nonce, uint64_t nfc_nonce, struct j0_storage_runner *runner,
    struct fwlab_block_service_v0 *service)
{
    struct read_lab_fixture *l = opaque;
    struct fwlab_controller_buffer_port_v0 observed = *buffer;
    l->buffer = *buffer; l->buffer_ops = *buffer->ops;
    l->buffer_ops.write = observe_write; observed.ops = &l->buffer_ops;
    return l->underlying.bind(l->underlying.context, config, &observed, namespace_ref,
        lifecycle_nonce, ftl_nonce, nfc_nonce, runner, service);
}

static struct fwlab_nfc_page_v2_lab_stats lab_stats(struct fixture *f)
{
    struct fwlab_nfc_page_v2_lab_stats s;
    CHECK(scale_storage_lab_snapshot(f->runtime, &s) == FWLAB_SPINE_V0_OK);
    CHECK(!s.trace_dropped && !s.counters_saturated && !s.quarantined);
    return s;
}

static void one_tick(struct fixture *f)
{
    uint32_t used;
    CHECK(j0_runtime_step(f->runtime, 1, &used) == FWLAB_SPINE_V0_OK && used == 1);
}

static struct read_lab_fixture *lab_create(int shared_channel)
{
    struct read_lab_fixture *l = calloc(1, sizeof(*l));
    struct fixture *f;
    const char *root = getenv("FWLAB_TEST_MEDIA_DIR");
    int length;
    CHECK(l && !active_lab && root && root[0] == '/');
    f = &l->f;
    active_lab = l; f->started = now_seconds();
    f->use_media_v2 = f->use_window_v2 = 1; f->lbas = 2048;
    f->media_config.geometry = (struct fwlab_nfc_geometry){
        .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(struct fwlab_nfc_geometry),
        .channels = shared_channel ? 1 : 2, .luns_per_channel = shared_channel ? 2 : 1,
        .planes_per_lun = 1, .blocks_per_plane = 16, .pages_per_block = 64,
        .plane_parallelism_per_lun = 1, .main_bytes_per_page = 4096, .oob_bytes_per_page = 128,
        .max_programs_per_erase = 1, .program_order = FWLAB_NFC_PROGRAM_ASCENDING
    };
    memcpy(f->media_config.media_uuid, "N1-J0-NAND-00001", 16);
    media_preflight(root, 1, &f->media_config, 1);
    length = snprintf(f->directory, sizeof(f->directory), "%s/fwlab-n1-j0.XXXXXX", root);
    CHECK(length > 0 && (size_t)length < sizeof(f->directory) && mkdtemp(f->directory));
    f->directory_fd = open(f->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(f->directory_fd >= 0);
    printf("N1_J0_BEGIN|medium=local_tmpfs|shared_channel=%d|directory=%s|failure_preserves_image=1\n",
        shared_channel, f->directory); fflush(stdout);
    l->lab.version = FWLAB_NFC_PAGE_V2_LAB_VERSION; l->lab.size = sizeof(l->lab);
    l->lab.command_ns = 1000; l->lab.array_read_ns = 10000;
    l->lab.channel_bytes_per_second = UINT64_C(1000000000);
    l->lab.virtual_ns_limit = UINT64_C(1000000000);
    l->lab.lun[1].target = l->lab.lun[1].ce = (uint16_t)(shared_channel != 0);
    l->lab.lun[1].package = (uint16_t)(shared_channel == 0); l->lab.lun[1].die = 1;
    f->options.mapping_slots = 256; f->options.read_lab_config = &l->lab;
    scale_storage_parallel_read_lab_factory_init(&l->underlying, &f->options);
    f->factory.bind = observe_bind; f->factory.context = l;
    media_open(f, 1); runtime_start(f, 1, 0); wait_ready(f); identify(f);
    return l;
}

static void lab_destroy(struct read_lab_fixture *l)
{
    struct fixture *f = &l->f;
    struct stat image;
    if (f->runtime) {
        uint64_t before = media_sequence(f);
        runtime_close(f);
        CHECK(media_sequence(f) == before); /* no hidden close checkpoint */
    }
    media_close(f);
    CHECK(fstatat(f->directory_fd, "nand.bin", &image, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISREG(image.st_mode) && image.st_nlink == 1 &&
        (uint64_t)image.st_dev == f->holder_v2.device && (uint64_t)image.st_ino == f->holder_v2.inode);
    CHECK(unlinkat(f->directory_fd, "nand.bin", 0) == 0 && close(f->directory_fd) == 0 && rmdir(f->directory) == 0);
    free(l); active_lab = NULL;
}

static void write_pages(struct fixture *f, uint64_t lba, uint32_t lbas, uint8_t seed, int fua)
{
    uint8_t data[8192]; fill_pattern(data, lba, seed);
    command(f, 1, lba, lbas, data, NULL, fua, 0);
}

static void fill_volume(struct fixture *f)
{
    for (uint64_t lba = 0; lba < f->lbas; lba += 16) write_pages(f, lba, 16, 0x31, 0);
    command(f, 0, 0, 0, NULL, NULL, 0, 0);
}

static void arm_read(struct read_lab_fixture *l)
{
    struct fwlab_ftl_scale *ftl = l->f.runtime->block.context;
    wait_idle(&l->f);
    uint64_t uid = ftl->io.next_uid, sequence = media_sequence(&l->f);
    CHECK(scale_storage_begin_timed_read(l->f.runtime) == FWLAB_SPINE_V0_OK);
    CHECK(ftl->io.next_uid == uid && media_sequence(&l->f) == sequence && ftl->read_only);
    l->watch_publication = 1; l->published = l->publications = 0;
}

static struct fwlab_spine_command_ticket_v0 admit(struct fixture *f,
    uint8_t opcode, uint64_t lba, uint32_t lbas, const uint8_t *input)
{
    struct fwlab_nvme_command c = {0};
    struct j0_host_transfer t = {0};
    struct fwlab_spine_command_ticket_v0 ticket;
    c.version = FWLAB_NVME_COMMAND_VERSION; c.size = sizeof(c);
    c.handle.instance_nonce = UINT64_C(0x53464232484f0000) + f->incarnation;
    c.handle.command_uid = ++f->uid; c.handle.controller_epoch = c.handle.generation = 1;
    c.origin.word[0] = c.handle.instance_nonce ^ UINT64_C(0x4f52494700000000);
    c.origin.word[1] = f->uid; c.trace_cookie = f->uid; c.safety_generation = 1;
    c.namespace_id = 1; c.opcode = opcode; c.queue_class = FWLAB_NVME_QUEUE_IO;
    c.fuse = FWLAB_NVME_FUSE_NONE; c.data_pointer_format = FWLAB_NVME_DATA_POINTER_PRP;
    c.data_address_present = 1; c.command_dword10_15[0] = (uint32_t)lba;
    c.command_dword10_15[1] = (uint32_t)(lba >> 32); c.command_dword10_15[2] = lbas - 1u;
    t.version = J0_RUNTIME_VERSION; t.size = sizeof(t); t.exact_bytes = lbas * 512u; t.input = input;
    t.direction = opcode == 1 ? FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER : FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST;
    CHECK(j0_runtime_admit_start(f->runtime, J0_PROFILE_LINUX_V1, &c, &t, &ticket) == FWLAB_SPINE_V0_OK);
    return ticket;
}

static void finish(struct fixture *f, const struct fwlab_spine_command_ticket_v0 *ticket,
    uint8_t *output, size_t bytes, int error)
{
    struct fwlab_nvme_completion_intent intent;
    struct fwlab_completion_lease_v0 lease;
    unsigned i;
    for (i = 0; i < 100000; ++i) {
        enum fwlab_spine_result_v0 r = j0_runtime_intent_read(f->runtime, ticket, &intent);
        if (r == FWLAB_SPINE_V0_OK) break;
        CHECK(r == FWLAB_SPINE_V0_IN_PROGRESS); one_tick(f);
    }
    CHECK(i < 100000 && (error ? intent.status_code != 0 : intent.status_code == 0));
    if (output) CHECK(!error && j0_runtime_host_read(f->runtime, ticket, output, bytes) == FWLAB_SPINE_V0_OK);
    CHECK(j0_runtime_publication_acquire(f->runtime, ticket, &lease, &intent) == FWLAB_SPINE_V0_OK);
    CHECK(j0_runtime_publication_finish(f->runtime, ticket, &lease, FWLAB_SPINE_PUBLICATION_V1_COMMITTED) == FWLAB_SPINE_V0_OK);
}

static void parallel_and_reopen(int shared_channel)
{
    struct read_lab_fixture *l = lab_create(shared_channel);
    struct fixture *f = &l->f;
    uint8_t data[8192], expected[8192], replacement[8192];
    fill_volume(f); write_pages(f, 0, 8, 0x72, 1);
    struct fwlab_ftl_scale *ftl = f->runtime->block.context;
    struct fwlab_nfc_ppa a = sf_ppa(ftl, ftl->map[0].ppa), b = sf_ppa(ftl, ftl->map[1].ppa);
    CHECK(shared_channel ? a.channel == b.channel && a.lun != b.lun : a.channel != b.channel);
    /* A J0 command not yet at Block also prevents the phase transition. */
    struct fwlab_spine_command_ticket_v0 ticket = admit(f, 2, 0, 16, NULL);
    CHECK(scale_storage_begin_timed_read(f->runtime) == FWLAB_SPINE_V0_WRONG_STATE);
    finish(f, &ticket, data, sizeof(data), 0);
    arm_read(l);
    uint64_t sequence = media_sequence(f), record = ftl->record_sequence;
    ticket = admit(f, 2, 0, 16, NULL); finish(f, &ticket, data, sizeof(data), 0);
    fill_pattern(expected, 0, 0x31); fill_pattern(replacement, 0, 0x72);
    memcpy(expected, replacement, 4096); CHECK(!memcmp(data, expected, sizeof(data)));
    struct fwlab_nfc_page_v2_lab_stats s = lab_stats(f);
    CHECK(s.accepted_reads == 2 && s.materialized_pages == 2 && !s.active_slots &&
        s.now_ns == (shared_channel ? 19448u : 15224u) && l->published == 8192 && l->publications == 2);
    /* Valid write shape reaches Block rejection, not a malformed transfer. */
    ticket = admit(f, 1, 0, 8, replacement); finish(f, &ticket, NULL, 0, 1);
    CHECK(media_sequence(f) == sequence && ftl->record_sequence == record && lab_stats(f).accepted_reads == 2);
    printf("N1_J0_PARALLEL|shared_channel=%d|virtual_ns=%llu|children=2|payload_exact=1|readonly_before_mutation=1\n",
        shared_channel, (unsigned long long)s.now_ns);
    if (!shared_channel) {
        l->watch_publication = 0;
        runtime_close(f); CHECK(media_sequence(f) == sequence);
        media_close(f); media_open(f, 0); runtime_start(f, 0, f->lbas); wait_ready(f);
        arm_read(l); ticket = admit(f, 2, 0, 16, NULL);
        finish(f, &ticket, data, sizeof(data), 0);
        CHECK(!memcmp(data, expected, sizeof(data)));
        puts("N1_J0_REOPEN|same_format=1|new_construction_identity=1|normalization_in_PREP=1|payload_exact=1");
    }
    lab_destroy(l);
}

static void partial_and_hole(int hole)
{
    struct read_lab_fixture *l = lab_create(0);
    struct fixture *f = &l->f;
    uint8_t data[8192], expected[8192], replacement[8192];
    if (!hole) fill_volume(f); else write_pages(f, 0, 8, 0x31, 0);
    write_pages(f, 16, 8, 0x72, 1); command(f, 0, 0, 0, NULL, NULL, 0, 0);
    arm_read(l);
    struct fwlab_spine_command_ticket_v0 ticket = admit(f, 2, 7, 16, NULL);
    finish(f, &ticket, data, sizeof(data), 0);
    fill_pattern(expected, 7, 0x31); if (hole) memset(expected + 512, 0, 4096);
    fill_pattern(replacement, 16, 0x72); memcpy(expected + 9u * 512u, replacement, 7u * 512u);
    CHECK(!memcmp(data, expected, sizeof(data)) && l->published == 8192 && l->publications == 3);
    struct fwlab_nfc_page_v2_lab_stats s = lab_stats(f);
    CHECK(s.accepted_reads == (hole ? 2u : 3u) && !s.active_slots);
    if (!hole) {
        uint64_t uid[3] = {0}, terminal[3] = {0}; unsigned admitted = 0;
        for (uint32_t i = 0; i < s.trace_count; ++i) {
            struct fwlab_nfc_page_v2_lab_trace e;
            CHECK(scale_storage_lab_trace_at(f->runtime, i, &e) == FWLAB_SPINE_V0_OK);
            if (e.event == FWLAB_NFC_PAGE_V2_LAB_ADMIT) { CHECK(admitted < 3); uid[admitted++] = e.operation_uid; }
            if (e.event == FWLAB_NFC_PAGE_V2_LAB_TERMINAL)
                for (unsigned j = 0; j < admitted; ++j) if (uid[j] == e.operation_uid) terminal[j] = e.now_ns;
        }
        CHECK(admitted == 3 && terminal[2] < terminal[1]);
    }
    printf("N1_J0_PARTIAL|hole=%d|prefix_writes=3|issued_not_published=1|payload_exact=1|out_of_order=%d\n", hole, !hole);
    lab_destroy(l);
}

static void read_error(void)
{
    struct read_lab_fixture *l = lab_create(0);
    struct fixture *f = &l->f;
    fill_volume(f); write_pages(f, 0, 8, 0x72, 0);
    struct fwlab_ftl_scale *ftl = f->runtime->block.context;
    struct fwlab_nfc_ppa bad = sf_ppa(ftl, ftl->map[0].ppa); bad.page = 0;
    struct fwlab_nand_media media = fwlab_file_nand_v2_media(f->media_v2);
    /* Explicit fault preparation on this fresh medium, before timed admission;
     * normal NFC bad-block result, never direct map or logical-file mutation. */
    CHECK(media.ops->mark_runtime_bad(media.context, &bad) == FWLAB_NFC_API_OK);
    arm_read(l);
    uint64_t sequence = media_sequence(f);
    struct fwlab_spine_command_ticket_v0 ticket = admit(f, 2, 0, 16, NULL);
    finish(f, &ticket, NULL, 0, 1);
    struct fwlab_nfc_page_v2_lab_stats s = lab_stats(f);
    CHECK(s.accepted_reads == 2 && s.materialized_pages >= 1 && !s.active_slots &&
        !s.held_luns && !s.busy_channels && !l->published && !l->publications && media_sequence(f) == sequence);
    puts("N1_J0_READ_ERROR|legal_read_reached_NAND=1|no_prefix_publication=1|siblings_drained=1");
    lab_destroy(l);
}

static void close_cut(int started)
{
    struct read_lab_fixture *l = lab_create(0);
    struct fixture *f = &l->f;
    fill_volume(f); write_pages(f, 0, 8, 0x72, 0); arm_read(l);
    (void)admit(f, 2, 0, 16, NULL);
    unsigned i; bool reached = false;
    for (i = 0; i < 100000 && !reached; ++i) {
        struct fwlab_nfc_page_v2_lab_stats s = lab_stats(f);
        struct fwlab_ftl_scale *ftl = f->runtime->block.context;
        if (!started) reached = ftl->parent.owned && !s.accepted_reads;
        else if (s.held_luns) {
            for (uint32_t j = 0; j < s.trace_count; ++j) {
                struct fwlab_nfc_page_v2_lab_trace e;
                CHECK(scale_storage_lab_trace_at(f->runtime, j, &e) == FWLAB_SPINE_V0_OK);
                if (e.event == FWLAB_NFC_PAGE_V2_LAB_COMMAND_END) reached = true;
            }
        }
        if (!reached) one_tick(f);
    }
    CHECK(reached && !l->published);
    uint64_t sequence = media_sequence(f);
    runtime_close(f); /* Existing strict all-zero authority/buffer/NFC certificate. */
    CHECK(!l->published && media_sequence(f) == sequence);
    printf("N1_J0_CLOSE|started_array=%d|actual_cut_reached=1|no_late_publication=1|all_zero_certificate=1\n", started);
    lab_destroy(l);
}

int main(void)
{
    parallel_and_reopen(0); parallel_and_reopen(1);
    partial_and_hole(0); partial_and_hole(1);
    read_error(); close_cut(0); close_cut(1);
    puts("N1_J0_PASS|existing_Linux_profile_lifecycle_Block_FTL_NFC_physical_v2=1|bounded_six_groups=1|not_native_or_calibrated_NAND=1");
    return 0;
}
