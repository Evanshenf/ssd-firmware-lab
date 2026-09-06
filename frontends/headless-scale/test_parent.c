/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#define _GNU_SOURCE
#include "ftl_scale_internal.h"
#include "compact_nand_internal.h"
#include "fwlab/private/nfc_scaled_model.h"
#include "fwlab/private/nfc_trace_window.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "PARENT %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define BUFFER_BYTES (1024u * 1024u)
#define GUARD_BYTES 32u
#define VOLUME_LBAS (64u * 2048u)
#define STEPS 20000000u
#define CLOSE_NONCE UINT64_C(0x504152454e54434c)

/* Adjacent controller-buffer owner only: no Host DMA or transport simulation.
 * The caller retains this lease and does not mutate Write input until retire. */
struct buffer_owner {
    uint8_t *allocation, *bytes;
    struct fwlab_controller_buffer_lease_v0 lease;
    uint64_t next_uid;
    uint32_t reads, writes, fail_write_offset;
    uint8_t active, closed, failed_write;
};
struct fixture {
    char directory[512];
    int directory_fd;
    struct fwlab_file_nand_v1_config media_config;
    struct fwlab_file_nand_holder_v1 holder;
    struct fwlab_file_nand_v1 *media;
    struct fwlab_ftl_scale *ftl;
    struct fwlab_nfc_model *nfc;
    void *media_arena, *ftl_arena, *nfc_arena;
    struct fwlab_block_service_v0 block;
    struct buffer_owner buffer;
    uint64_t uid, incarnation;
    uint32_t cp_with_parent, gc_with_parent;
    struct fnv1_io saved_io;
    uint32_t cut_syncs;
    uint8_t cut_fired;
};

static void guards(const struct buffer_owner *b)
{
    for (unsigned i = 0; i < GUARD_BYTES; ++i)
        CHECK(b->allocation[i] == 0xa5 && b->bytes[BUFFER_BYTES + i] == 0x5a);
}
static int span_ok(struct buffer_owner *b, const struct fwlab_controller_buffer_lease_v0 *l,
                   const struct fwlab_controller_buffer_span_v0 *s, size_t n, uint32_t right)
{
    guards(b);
    return b->active && l && s && !memcmp(l, &b->lease, sizeof(*l)) &&
        fwlab_controller_buffer_span_v0_valid_for_lease(s, l, right) && s->length == n;
}
static enum fwlab_controller_buffer_result_v0 buffer_acquire(void *p,
    const struct fwlab_controller_buffer_acquire_v0 *r, struct fwlab_controller_buffer_lease_v0 *l)
{
    struct buffer_owner *b = p;
    if (!l || !fwlab_controller_buffer_acquire_v0_valid(r)) return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    if (b->active || b->closed || r->capacity_bytes > BUFFER_BYTES) return FWLAB_CONTROLLER_BUFFER_V0_NO_CAPACITY;
    memset(&b->lease, 0, sizeof(b->lease));
    b->lease.version = FWLAB_CONTROLLER_BUFFER_V0_VERSION; b->lease.size = sizeof(b->lease);
    b->lease.type_tag = FWLAB_CONTROLLER_BUFFER_LEASE_V0_TAG;
    b->lease.issuer_nonce = UINT64_C(0x504152454e544255);
    b->lease.buffer_uid = 1; b->lease.lease_uid = ++b->next_uid;
    b->lease.generation = 1; b->lease.capacity_bytes = r->capacity_bytes;
    b->lease.rights = r->rights; b->active = 1; *l = b->lease;
    return FWLAB_CONTROLLER_BUFFER_V0_OK;
}
static enum fwlab_controller_buffer_result_v0 buffer_read(void *p,
    const struct fwlab_controller_buffer_lease_v0 *l, const struct fwlab_controller_buffer_span_v0 *s,
    void *out, size_t n)
{
    struct buffer_owner *b = p;
    if (!out || !span_ok(b, l, s, n, FWLAB_CONTROLLER_BUFFER_V0_READ)) return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    ++b->reads; memcpy(out, b->bytes + s->offset, n); return FWLAB_CONTROLLER_BUFFER_V0_OK;
}
static enum fwlab_controller_buffer_result_v0 buffer_write(void *p,
    const struct fwlab_controller_buffer_lease_v0 *l, const struct fwlab_controller_buffer_span_v0 *s,
    const void *in, size_t n)
{
    struct buffer_owner *b = p;
    if (!in || !span_ok(b, l, s, n, FWLAB_CONTROLLER_BUFFER_V0_WRITE)) return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    if (b->fail_write_offset && s->offset >= b->fail_write_offset) {
        b->failed_write = 1; return FWLAB_CONTROLLER_BUFFER_V0_POISONED;
    }
    ++b->writes; memcpy(b->bytes + s->offset, in, n); return FWLAB_CONTROLLER_BUFFER_V0_OK;
}
static enum fwlab_controller_buffer_result_v0 buffer_copy(void *p,
    const struct fwlab_controller_buffer_lease_v0 *d, const struct fwlab_controller_buffer_span_v0 *ds,
    const struct fwlab_controller_buffer_lease_v0 *s, const struct fwlab_controller_buffer_span_v0 *ss)
{
    struct buffer_owner *b = p;
    if (!ds || !ss || ds->length != ss->length ||
        !span_ok(b, d, ds, ds->length, FWLAB_CONTROLLER_BUFFER_V0_WRITE) ||
        !span_ok(b, s, ss, ss->length, FWLAB_CONTROLLER_BUFFER_V0_READ)) return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    memmove(b->bytes + ds->offset, b->bytes + ss->offset, ds->length); return FWLAB_CONTROLLER_BUFFER_V0_OK;
}
static enum fwlab_controller_buffer_result_v0 buffer_release(void *p,
    const struct fwlab_controller_buffer_lease_v0 *l)
{
    struct buffer_owner *b = p; guards(b);
    if (!b->active || !l || memcmp(l, &b->lease, sizeof(*l))) return FWLAB_CONTROLLER_BUFFER_V0_STALE;
    b->active = 0; return FWLAB_CONTROLLER_BUFFER_V0_OK;
}
static enum fwlab_controller_buffer_result_v0 buffer_close(void *p, uint64_t nonce, uint32_t epoch)
{
    if (nonce != CLOSE_NONCE || epoch != 1) return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    ((struct buffer_owner *)p)->closed = 1; return FWLAB_CONTROLLER_BUFFER_V0_OK;
}
static enum fwlab_controller_buffer_result_v0 buffer_quiet(void *p, uint64_t nonce, uint32_t epoch,
                                                         uint32_t *leases, uint8_t *quiet)
{
    struct buffer_owner *b = p;
    if (!b->closed || nonce != CLOSE_NONCE || epoch != 1 || !leases || !quiet) return FWLAB_CONTROLLER_BUFFER_V0_INVALID;
    *leases = b->active; *quiet = !b->active; guards(b); return FWLAB_CONTROLLER_BUFFER_V0_OK;
}
static const struct fwlab_controller_buffer_ops_v0 buffer_ops = {
    .version = FWLAB_CONTROLLER_BUFFER_V0_VERSION, .size = sizeof(buffer_ops),
    .acquire = buffer_acquire, .read = buffer_read, .write = buffer_write, .copy = buffer_copy,
    .release = buffer_release, .epoch_close = buffer_close, .epoch_quiescent = buffer_quiet,
};
static struct fwlab_controller_buffer_port_v0 buffer_port(struct fixture *f)
{
    struct fwlab_controller_buffer_port_v0 p = { .ops = &buffer_ops, .context = &f->buffer,
        .issuer_nonce = UINT64_C(0x504152454e544255), .generation = 1 };
    CHECK(fwlab_controller_buffer_port_v0_valid(&p)); return p;
}

static struct fwlab_nfc_model_config nfc_config(void)
{
    struct fwlab_nfc_model_config n = {0};
    n.version = FWLAB_NFC_CONTRACT_VERSION; n.size = sizeof(n);
    n.geometry = (struct fwlab_nfc_geometry){ .version = FWLAB_NFC_CONTRACT_VERSION,
        .size = sizeof(n.geometry), .channels = 1, .luns_per_channel = 1, .planes_per_lun = 1,
        .blocks_per_plane = 320, .pages_per_block = 64, .plane_parallelism_per_lun = 1,
        .main_bytes_per_page = 4096, .oob_bytes_per_page = 128,
        .max_programs_per_erase = 1, .program_order = FWLAB_NFC_PROGRAM_ASCENDING };
    n.ecc.version = FWLAB_NFC_CONTRACT_VERSION; n.ecc.size = sizeof(n.ecc);
    n.ecc.main_covered_bytes = 4096; n.ecc.oob_covered_bytes = 128;
    n.ecc.main_step_bytes = 512; n.ecc.oob_step_bytes = 16;
    n.ecc.main_strength_bits = 8; n.ecc.oob_strength_bits = 4; n.ecc.max_retry_step = 3;
    n.timing.version = FWLAB_NFC_CONTRACT_VERSION; n.timing.size = sizeof(n.timing);
    n.timing.command_ticks = n.timing.transfer_ticks_per_unit = 1; n.timing.read_array_ticks = 8;
    n.timing.program_setup_ticks = 2; n.timing.program_ticks_per_unit = 4; n.timing.program_status_ticks = 1;
    n.timing.erase_setup_ticks = 2; n.timing.erase_ticks_per_page = 2;
    n.timing.erase_status_ticks = n.timing.status_ticks = 1;
    n.fault.version = FWLAB_NFC_FAULT_PROFILE_VERSION; n.fault.size = sizeof(n.fault);
    n.fault.profile_version = 1; n.fault.seed = UINT64_C(0x504152454e544e46);
    n.capacity.version = FWLAB_NFC_CONTRACT_VERSION; n.capacity.size = sizeof(n.capacity);
    n.capacity.operations = n.capacity.request_registry = n.capacity.terminal_events = n.capacity.result_slots = 4;
    n.capacity.trace_entries = 4096; n.capacity.scratch_main_bytes = 4096; n.capacity.scratch_oob_bytes = 128;
    n.capacity.operation_generation_limit = n.capacity.cache_generation_limit = UINT32_MAX;
    n.capacity.controller_epoch_limit = n.capacity.submit_sequence_limit = UINT32_MAX;
    n.capacity.operation_uid_limit = UINT64_MAX; n.capacity.virtual_tick_limit = UINT64_MAX / 2;
    n.successful_erase_limit = UINT16_MAX; return n;
}
static void step(struct fixture *f, int may_fail)
{
    uint64_t cp = f->ftl->checkpoints, gc = f->ftl->garbage_collections;
    uint32_t used = 0, retired = 0;
    enum fwlab_spine_result_v0 r = fwlab_ftl_scale_step(f->ftl, 1, &used);
    CHECK(r == FWLAB_SPINE_V0_OK || (may_fail && r == FWLAB_SPINE_V0_QUARANTINED));
    CHECK(used <= 1);
    if (f->ftl->parent.owned && f->ftl->parent.status.state == FWLAB_BLOCK_V0_STATE_ACCEPTED &&
        f->ftl->parent.completed_lbas && f->ftl->parent.completed_lbas < f->ftl->parent.request.lba_count) {
        f->cp_with_parent += f->ftl->checkpoints > cp;
        f->gc_with_parent += f->ftl->garbage_collections > gc;
        CHECK(f->ftl->durable_frontier == f->ftl->parent.base_frontier ||
            (f->ftl->durable_frontier == f->ftl->parent.host_sequence &&
             f->ftl->work.kind == SF_WORK_HOST && f->ftl->work.phase == SF_W_HOST_MAP_WAIT &&
             f->ftl->parent.completed_lbas + f->ftl->work.request.lba_count ==
                 f->ftl->parent.request.lba_count));
    }
    if (fwlab_nfc_model_trace_count(f->nfc) >= 3072) {
        enum fwlab_nfc_api_result nr = fwlab_nfc_trace_window_retire(f->nfc, &retired);
        CHECK(nr == FWLAB_NFC_API_OK || nr == FWLAB_NFC_API_WRONG_STATE);
    }
    guards(&f->buffer);
}
static void open_lower(struct fixture *f, int format, int extended)
{
    struct fwlab_nfc_model_config n = nfc_config();
    struct fwlab_ftl_scale_extended_config e = {0}, invalid;
    struct fwlab_nfc_buffer_provider staging; struct fwlab_nfc_provider provider;
    struct fwlab_nand_media media; struct fwlab_controller_buffer_port_v0 buffer = buffer_port(f);
    struct fwlab_block_volume_binding_v0 volume; size_t fb, nb, mb = fwlab_file_nand_v1_arena_size();
    f->buffer.closed = 0; ++f->incarnation;
    f->media_arena = calloc(1, mb); CHECK(f->media_arena);
    CHECK((format ? fwlab_file_nand_v1_posix_format(f->media_arena, mb, f->directory_fd, "nand.bin",
        &f->media_config, &f->media, &f->holder) : fwlab_file_nand_v1_posix_restart(f->media_arena, mb,
        f->directory_fd, "nand.bin", &f->media_config, &f->holder, &f->media)) == FWLAB_NFC_API_OK);
    e.version = FWLAB_FTL_SCALE_EXTENDED_VERSION; e.size = sizeof(e); e.max_transfer_lbas = 2048;
    e.base.version = FWLAB_FTL_SCALE_VERSION; e.base.size = sizeof(e.base); e.base.geometry = n.geometry;
    memcpy(e.base.media_uuid, f->media_config.media_uuid, 16); e.base.namespace_ref.word[0] = 0x50415245;
    e.base.instance_nonce = UINT64_C(0x5041524500000000) + f->incarnation;
    e.base.provider_nonce = UINT64_C(0x5041524600000000) + f->incarnation;
    e.base.nfc_instance_nonce = UINT64_C(0x5041524700000000) + f->incarnation;
    e.base.nfc_operation_uid_limit = UINT64_MAX;
    e.base.host_sequence_limit = e.base.record_sequence_limit = UINT64_MAX - 1;
    e.base.mapping_slots = VOLUME_LBAS / 8; e.base.generation = e.base.execution_epoch = e.base.nfc_epoch = 1;
    CHECK(fwlab_ftl_scale_extended_config_valid(&e)); invalid = e; invalid.max_transfer_lbas = 2049;
    CHECK(!fwlab_ftl_scale_extended_config_valid(&invalid)); invalid.max_transfer_lbas = 0;
    CHECK(!fwlab_ftl_scale_extended_config_valid(&invalid));
    fb = extended ? fwlab_ftl_scale_extended_arena_size(&e) : fwlab_ftl_scale_arena_size(&e.base);
    nb = fwlab_nfc_scaled_arena_size(&n); CHECK(fb && nb);
    CHECK(fb < 1024u * 1024u); /* No per-parent 1 MiB payload inside the FTL arena. */
    f->ftl_arena = calloc(1, fb); f->nfc_arena = calloc(1, nb); CHECK(f->ftl_arena && f->nfc_arena);
    staging = fwlab_ftl_scale_staging_provider(f->ftl_arena, fb); media = fwlab_file_nand_v1_media(f->media);
    CHECK(fwlab_nfc_scaled_init(f->nfc_arena, nb, &n, e.base.nfc_instance_nonce, &staging, &media, &f->nfc) == FWLAB_NFC_API_OK);
    provider = fwlab_nfc_model_provider(f->nfc);
    CHECK((extended ? fwlab_ftl_scale_init_extended(f->ftl_arena, fb, &e, &buffer, &provider, &f->ftl) :
        fwlab_ftl_scale_init(f->ftl_arena, fb, &e.base, &buffer, &provider, &f->ftl)) == FWLAB_SPINE_V0_OK);
    f->block = fwlab_ftl_scale_block_service(f->ftl);
    CHECK((format ? fwlab_ftl_scale_format_start(f->ftl, VOLUME_LBAS) :
        fwlab_ftl_scale_recover_start(f->ftl, VOLUME_LBAS)) == FWLAB_SPINE_V0_OK);
    uint32_t i; for (i = 0; i < STEPS && !f->ftl->ready; ++i) step(f, 0);
    CHECK(i < STEPS && fwlab_ftl_scale_volume_query(f->ftl, &volume) == FWLAB_SPINE_V0_OK);
    CHECK(volume.volume.lba_count == VOLUME_LBAS && volume.service.context == f->block.context);
}
static void close_lower(struct fixture *f)
{
    struct fwlab_block_epoch_status_v0 s; uint32_t i, leases; uint8_t quiet;
    CHECK(f->block.ops->epoch_close(f->block.context, CLOSE_NONCE, 1) == FWLAB_SPINE_V0_OK);
    CHECK(buffer_close(&f->buffer, CLOSE_NONCE, 1) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    for (i = 0; i < STEPS; ++i) {
        CHECK(f->block.ops->epoch_quiescent(f->block.context, CLOSE_NONCE, 1, &s) == FWLAB_SPINE_V0_OK);
        CHECK(fwlab_block_epoch_status_v0_valid(&s)); if (s.quiescent) break; step(f, 0);
    }
    CHECK(i < STEPS && !s.aggregate_operations && sf_io_idle(f->ftl) && !f->ftl->parent.owned);
    CHECK(buffer_quiet(&f->buffer, CLOSE_NONCE, 1, &leases, &quiet) == FWLAB_CONTROLLER_BUFFER_V0_OK && !leases && quiet);
    CHECK(fwlab_ftl_scale_fini(f->ftl) == FWLAB_SPINE_V0_OK);
    CHECK(fwlab_file_nand_v1_close(f->media) == FWLAB_NFC_API_OK);
    free(f->ftl_arena); free(f->nfc_arena); free(f->media_arena);
    f->ftl = NULL; f->nfc = NULL; f->media = NULL;
}
static struct fixture *create(void)
{
    const char *root = getenv("FWLAB_TEST_MEDIA_DIR"); struct statfs fs; int fd;
    struct fixture *f = calloc(1, sizeof(*f)); CHECK(f);
    f->media_config.geometry = nfc_config().geometry;
    memcpy(f->media_config.media_uuid, "P2-A-PARENT-0001", 16);
    if (!root || root[0] != '/' || (fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)) < 0) {
        fputs("PARENT_PREFLIGHT_ERROR|absolute_existing_FWLAB_TEST_MEDIA_DIR_required|no_disk_fallback=1\n", stderr); exit(1);
    }
    CHECK(fstatfs(fd, &fs) == 0 && fs.f_bsize > 0);
    uint64_t available = (uint64_t)fs.f_bavail * (uint64_t)fs.f_bsize;
    uint64_t limit = (uint64_t)fs.f_blocks * (uint64_t)fs.f_bsize;
    uint64_t image = fwlab_file_nand_v1_image_bytes(&f->media_config);
    if (fs.f_type != TMPFS_MAGIC || limit > (UINT64_C(1) << 30) ||
        !image || image > (UINT64_C(256) << 20) || available < image + (UINT64_C(64) << 20)) {
        fprintf(stderr, "PARENT_PREFLIGHT_ERROR|tmpfs_1GiB_cap_and_space_required|type=%lx|available=%llu|limit=%llu|no_disk_fallback=1\n",
            (unsigned long)fs.f_type, (unsigned long long)available, (unsigned long long)limit); exit(1);
    }
    CHECK(close(fd) == 0);
    int n = snprintf(f->directory, sizeof(f->directory), "%s/fwlab-parent.XXXXXX", root);
    CHECK(n > 0 && (size_t)n < sizeof(f->directory) && mkdtemp(f->directory));
    f->directory_fd = open(f->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW); CHECK(f->directory_fd >= 0);
    CHECK(fstatfs(f->directory_fd, &fs) == 0 && fs.f_type == TMPFS_MAGIC);
    f->buffer.allocation = malloc(BUFFER_BYTES + 2 * GUARD_BYTES); CHECK(f->buffer.allocation);
    f->buffer.bytes = f->buffer.allocation + GUARD_BYTES;
    memset(f->buffer.allocation, 0xa5, GUARD_BYTES); memset(f->buffer.bytes + BUFFER_BYTES, 0x5a, GUARD_BYTES);
    printf("PARENT_PREFLIGHT_OK|logical_mib=64|image_bytes=%llu|adjacent_buffer_bytes=%u|directory=%s\n",
           (unsigned long long)image, BUFFER_BYTES, f->directory); return f;
}
static void destroy(struct fixture *f)
{
    CHECK(!f->ftl && !f->media && !f->buffer.active);
    CHECK(unlinkat(f->directory_fd, "nand.bin", 0) == 0 && close(f->directory_fd) == 0 && rmdir(f->directory) == 0);
    guards(&f->buffer); free(f->buffer.allocation); free(f);
}
static uint8_t pattern(uint64_t byte, uint8_t seed)
{ return (uint8_t)(seed ^ (uint8_t)byte ^ (uint8_t)(byte >> 9)); }
static void check_pattern(const uint8_t *p, uint64_t lba, uint32_t lbas, uint8_t seed)
{ for (uint32_t i = 0; i < lbas * 512u; ++i) CHECK(p[i] == pattern(lba * 512u + i, seed)); }
static struct fwlab_block_request_v0 request(struct fixture *f, uint32_t operation, uint64_t lba, uint32_t lbas, uint8_t seed)
{
    struct fwlab_block_request_v0 r = {0}; struct fwlab_controller_buffer_acquire_v0 a = {0};
    struct fwlab_host_action_token_v0 *t = &r.operation_token.action; ++f->uid;
    r.version = FWLAB_BLOCK_SERVICE_V0_VERSION; r.size = sizeof(r);
    r.operation_token.version = FWLAB_BLOCK_SERVICE_V0_VERSION; r.operation_token.size = sizeof(r.operation_token);
    r.operation_token.type_tag = FWLAB_BLOCK_OP_TOKEN_V0_TAG;
    r.operation_token.provider_nonce = f->block.provider_nonce; r.operation_token.generation = f->block.generation;
    t->version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION; t->size = sizeof(*t); t->type_tag = FWLAB_HOST_ACTION_TOKEN_V0_TAG;
    t->command.instance_nonce = CLOSE_NONCE; t->command.command_uid = f->uid;
    t->command.controller_epoch = t->command.generation = 1; t->origin.word[0] = CLOSE_NONCE; t->origin.word[1] = f->uid;
    t->action_uid = f->uid; t->generation = 1;
    t->kind = operation == FWLAB_BLOCK_V0_READ ? FWLAB_HOST_ACTION_V0_BLOCK_READ : FWLAB_HOST_ACTION_V0_BLOCK_WRITE;
    r.namespace_ref = f->ftl->config.namespace_ref; r.lba = lba; r.lba_count = lbas; r.operation = operation;
    r.durability = operation == FWLAB_BLOCK_V0_READ ? FWLAB_BLOCK_V0_DURABILITY_NONE : FWLAB_BLOCK_V0_DURABILITY_SELF;
    a.version = FWLAB_CONTROLLER_BUFFER_V0_VERSION; a.size = sizeof(a); a.command = t->command; a.origin = t->origin;
    a.client_uid = f->uid; a.execution_epoch = 1; a.capacity_bytes = BUFFER_BYTES;
    a.rights = operation == FWLAB_BLOCK_V0_WRITE ? FWLAB_CONTROLLER_BUFFER_V0_READ : FWLAB_CONTROLLER_BUFFER_V0_WRITE;
    CHECK(buffer_acquire(&f->buffer, &a, &r.buffer) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    r.buffer_present = 1; r.buffer_span.version = FWLAB_CONTROLLER_BUFFER_V0_VERSION;
    r.buffer_span.size = sizeof(r.buffer_span); r.buffer_span.length = lbas * 512u;
    CHECK(fwlab_block_request_v0_valid(&r));
    if (operation == FWLAB_BLOCK_V0_READ) memset(f->buffer.bytes, 0xcc, BUFFER_BYTES);
    else for (uint32_t i = 0; i < lbas * 512u; ++i) f->buffer.bytes[i] = pattern(lba * 512u + i, seed);
    return r;
}
static void admit(struct fixture *f, const struct fwlab_block_request_v0 *r)
{
    struct fwlab_block_submit_result_v0 s; uint32_t i;
    for (i = 0; i < STEPS; ++i) {
        CHECK(f->block.ops->submit(f->block.context, r, &s) == FWLAB_SPINE_V0_OK);
        CHECK(fwlab_block_submit_result_v0_matches_request(&s, r));
        if (s.disposition == FWLAB_HOST_ACTION_V0_ACCEPTED) break;
        CHECK(s.disposition == FWLAB_HOST_ACTION_V0_BACKPRESSURE); step(f, 0);
    }
    CHECK(i < STEPS);
}
static struct fwlab_block_status_v0 finish(struct fixture *f, const struct fwlab_block_request_v0 *r, int may_fail)
{
    struct fwlab_block_status_v0 s; uint32_t i;
    for (i = 0; i < STEPS; ++i) {
        CHECK(f->block.ops->query(f->block.context, &r->operation_token, &s) == FWLAB_SPINE_V0_OK);
        CHECK(fwlab_block_status_v0_matches_request(&s, r));
        if (s.state != FWLAB_BLOCK_V0_STATE_ACCEPTED) break;
        step(f, may_fail);
    }
    CHECK(i < STEPS); return s;
}
static void retire(struct fixture *f, const struct fwlab_block_request_v0 *r)
{
    struct fwlab_block_status_v0 s; uint32_t i;
    CHECK(f->buffer.active);
    CHECK(f->block.ops->retire_start(f->block.context, &r->operation_token) == FWLAB_SPINE_V0_OK);
    for (i = 0; i < 1000; ++i) {
        enum fwlab_spine_result_v0 rc = f->block.ops->retire_query(f->block.context, &r->operation_token, &s);
        if (rc == FWLAB_SPINE_V0_OK && s.state == FWLAB_BLOCK_V0_STATE_RETIRED) break;
        CHECK(rc == FWLAB_SPINE_V0_IN_PROGRESS); step(f, 0);
    }
    CHECK(i < 1000 && !f->ftl->parent.owned);
    CHECK(buffer_release(&f->buffer, &r->buffer) == FWLAB_CONTROLLER_BUFFER_V0_OK);
}
static void io(struct fixture *f, uint32_t op, uint64_t lba, uint32_t lbas, uint8_t seed)
{
    struct fwlab_block_request_v0 r = request(f, op, lba, lbas, seed);
    struct fwlab_block_status_v0 s; admit(f, &r); s = finish(f, &r, 0);
    CHECK(s.outcome == FWLAB_BLOCK_V0_SUCCEEDED && s.effect == FWLAB_BLOCK_V0_EFFECT_FULL && s.completed_lbas == lbas);
    if (op == FWLAB_BLOCK_V0_WRITE) CHECK(s.durability_witness == FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE);
    retire(f, &r);
}
static uint32_t between_groups(struct fixture *f)
{
    uint32_t i;
    for (i = 0; i < STEPS; ++i) {
        if (f->ftl->parent.owned && f->ftl->parent.status.state == FWLAB_BLOCK_V0_STATE_ACCEPTED &&
            f->ftl->parent.completed_lbas && sf_parent_clean_boundary(f->ftl)) break;
        step(f, 0);
    }
    CHECK(i < STEPS && f->ftl->parent.completed_lbas < f->ftl->parent.request.lba_count);
    CHECK(fwlab_ftl_scale_checkpoint_start(f->ftl) == FWLAB_SPINE_V0_WRONG_STATE);
    CHECK(fwlab_ftl_scale_gc_start(f->ftl, 3) == FWLAB_SPINE_V0_WRONG_STATE);
    return f->ftl->parent.completed_lbas;
}
static void positive(struct fixture *f)
{
    struct fwlab_block_request_v0 r; struct fwlab_block_submit_result_v0 submitted;
    open_lower(f, 1, 0); r = request(f, FWLAB_BLOCK_V0_WRITE, 0, 2048, 0x31);
    uint64_t sequence = fwlab_file_nand_v1_sequence(f->media);
    CHECK(f->block.ops->submit(f->block.context, &r, &submitted) == FWLAB_SPINE_V0_OK);
    CHECK(submitted.disposition == FWLAB_HOST_ACTION_V0_REJECTED && fwlab_file_nand_v1_sequence(f->media) == sequence);
    CHECK(buffer_release(&f->buffer, &r.buffer) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    close_lower(f); open_lower(f, 0, 1);
    io(f, FWLAB_BLOCK_V0_WRITE, 0, 2048, 0x31);
    io(f, FWLAB_BLOCK_V0_WRITE, 4096, 8, 0x55); io(f, FWLAB_BLOCK_V0_WRITE, 6144, 8, 0x55);
    io(f, FWLAB_BLOCK_V0_WRITE, 4097, 2048, 0x72);
    CHECK(f->cp_with_parent);
    close_lower(f); open_lower(f, 0, 1); /* No intervening Flush. */
    io(f, FWLAB_BLOCK_V0_READ, 0, 2048, 0); check_pattern(f->buffer.bytes, 0, 2048, 0x31);
    io(f, FWLAB_BLOCK_V0_READ, 4097, 2048, 0); check_pattern(f->buffer.bytes, 4097, 2048, 0x72);
    io(f, FWLAB_BLOCK_V0_READ, 4096, 8, 0); check_pattern(f->buffer.bytes, 4096, 1, 0x55);
    check_pattern(f->buffer.bytes + 512, 4097, 7, 0x72);
    io(f, FWLAB_BLOCK_V0_READ, 6144, 8, 0); check_pattern(f->buffer.bytes, 6144, 1, 0x72);
    check_pattern(f->buffer.bytes + 512, 6145, 7, 0x55);
    puts("PARENT_POSITIVE_PASS|legacy16_rejects2048=1|extended2048=1|aligned_unaligned_1MiB=1|rmw_neighbors=1|SELF_restart_noFlush=1|CP_while_owned=1");
}
static void cancellations(struct fixture *f)
{
    struct fwlab_block_request_v0 r = request(f, FWLAB_BLOCK_V0_WRITE, 8192, 2048, 0x81);
    struct fwlab_block_status_v0 s; admit(f, &r);
    uint64_t sequence = fwlab_file_nand_v1_sequence(f->media);
    CHECK(f->block.ops->cancel(f->block.context, &r.operation_token) == FWLAB_SPINE_V0_OK);
    s = finish(f, &r, 0); CHECK(s.outcome == FWLAB_BLOCK_V0_CANCELLED && s.effect == FWLAB_BLOCK_V0_EFFECT_NONE && !s.completed_lbas);
    CHECK(fwlab_file_nand_v1_sequence(f->media) == sequence); retire(f, &r);
    io(f, FWLAB_BLOCK_V0_WRITE, 8192, 2048, 0x81);
    r = request(f, FWLAB_BLOCK_V0_WRITE, 8192, 2048, 0x82); admit(f, &r);
    uint64_t frontier = f->ftl->parent.base_frontier; uint32_t prefix = between_groups(f);
    CHECK(f->block.ops->cancel(f->block.context, &r.operation_token) == FWLAB_SPINE_V0_OK);
    s = finish(f, &r, 0); CHECK(s.outcome == FWLAB_BLOCK_V0_CANCELLED && s.effect == FWLAB_BLOCK_V0_EFFECT_EXACT_PREFIX);
    CHECK(s.completed_lbas == prefix && !s.durability_witness && f->ftl->durable_frontier == frontier); retire(f, &r);
    io(f, FWLAB_BLOCK_V0_READ, 8192, 2048, 0); check_pattern(f->buffer.bytes, 8192, prefix, 0x82);
    check_pattern(f->buffer.bytes + prefix * 512u, 8192 + prefix, 2048 - prefix, 0x81);
    f->buffer.fail_write_offset = 8192;
    r = request(f, FWLAB_BLOCK_V0_READ, 8192, 2048, 0); admit(f, &r); s = finish(f, &r, 0);
    CHECK(f->buffer.failed_write && s.outcome == FWLAB_BLOCK_V0_FAILED && s.effect != FWLAB_BLOCK_V0_EFFECT_FULL);
    CHECK(s.completed_lbas == 16 && !f->ftl->quarantined);
    for (uint32_t i = 8192; i < BUFFER_BYTES; ++i) CHECK(f->buffer.bytes[i] == 0xcc);
    f->buffer.fail_write_offset = 0; retire(f, &r);
    puts("PARENT_CANCEL_READ_PASS|before_effect_none=1|between_groups_exact_prefix=1|frontier_unchanged=1|failed_Read_not_FULL=1|buffer_canaries=1|no_Host_DMA_claim=1");
}
static void garbage_collection(struct fixture *f)
{
    uint32_t changed = 0, moved = 0, minimum, tail;
    uint32_t ppb = f->ftl->config.geometry.pages_per_block;
    for (uint32_t lba = 0; lba < VOLUME_LBAS; lba += 2048) io(f, FWLAB_BLOCK_V0_WRITE, lba, 2048, 0xa7);
    minimum = f->ftl->victim_count ? f->ftl->blocks[f->ftl->victim_heap[0]].live_pages : SF_NONE;
    printf("PARENT_GC_PREP|stage=after_fill|free=%u|victims=%u|min_live=%u|gc=%llu|gc_while_parent=%u\n",
        f->ftl->free_count, f->ftl->victim_count, minimum,
        (unsigned long long)f->ftl->garbage_collections, f->gc_with_parent);
    /* Zero-live victims need only erase. Also leave exactly one Host page:
     * the unaligned parent's first 7 LBAs fit, then its retained next group
     * needs live GC rather than starting maintenance before parent admission. */
    for (;;) {
        minimum = f->ftl->victim_count ? f->ftl->blocks[f->ftl->victim_heap[0]].live_pages : SF_NONE;
        tail = f->ftl->host_head == SF_NONE ? 0 : ppb - f->ftl->blocks[f->ftl->host_head].disk.allocation_end;
        if (f->ftl->free_count == 1 && f->ftl->victim_count && minimum > 0 && tail == 1) break;
        CHECK(changed < VOLUME_LBAS / 16);
        io(f, FWLAB_BLOCK_V0_WRITE, (uint64_t)changed * 16, 8, 0xb1); ++changed;
    }
    CHECK(changed && minimum <= ppb - 3u);
    CHECK(f->ftl->map[98305 / 8].state == SF_VALUE &&
          f->ftl->blocks[f->ftl->map[98305 / 8].ppa / ppb].live_pages > 1);
    printf("PARENT_GC_PREP|stage=ready|free=%u|victims=%u|min_live=%u|host_tail=%u|interleaved_pages=%u|gc=%llu|gc_while_parent=%u\n",
        f->ftl->free_count, f->ftl->victim_count, minimum, tail, changed,
        (unsigned long long)f->ftl->garbage_collections, f->gc_with_parent);
    CHECK(fflush(stdout) == 0);
    struct sf_map_entry *before = malloc((VOLUME_LBAS / 8) * sizeof(*before)); CHECK(before);
    memcpy(before, f->ftl->map, (VOLUME_LBAS / 8) * sizeof(*before));
    uint32_t gc = f->gc_with_parent;
    io(f, FWLAB_BLOCK_V0_WRITE, 98305, 2048, 0xd3);
    minimum = f->ftl->victim_count ? f->ftl->blocks[f->ftl->victim_heap[0]].live_pages : SF_NONE;
    printf("PARENT_GC_PREP|stage=after_parent|free=%u|victims=%u|min_live=%u|gc=%llu|gc_while_parent=%u|prior_gc_while_parent=%u\n",
        f->ftl->free_count, f->ftl->victim_count, minimum,
        (unsigned long long)f->ftl->garbage_collections, f->gc_with_parent, gc);
    CHECK(fflush(stdout) == 0);
    CHECK(f->gc_with_parent > gc);
    io(f, FWLAB_BLOCK_V0_READ, 98305, 2048, 0); check_pattern(f->buffer.bytes, 98305, 2048, 0xd3);
    for (uint32_t lpn = 0; lpn < VOLUME_LBAS / 8; ++lpn) {
        if (lpn >= 98305 / 8 && lpn <= (98305 + 2047) / 8) continue;
        if (before[lpn].ppa == f->ftl->map[lpn].ppa) continue;
        io(f, FWLAB_BLOCK_V0_READ, (uint64_t)lpn * 8, 8, 0);
        check_pattern(f->buffer.bytes, (uint64_t)lpn * 8, 8, !(lpn % 2) && lpn / 2 < changed ? 0xb1 : 0xa7);
        ++moved;
    }
    CHECK(moved); free(before);
    printf("PARENT_GC_PASS|precondition_fill_mib=64|interleaved_pages=%u|relocated_pages_verified=%u|GC_between_owned_subgroups=1\n", changed, moved);
}
static void closing_parent(struct fixture *f)
{
    struct fwlab_block_request_v0 r = request(f, FWLAB_BLOCK_V0_WRITE, 16384, 2048, 0xd4);
    struct fwlab_block_status_v0 s; struct fwlab_block_epoch_status_v0 epoch; admit(f, &r);
    uint32_t prefix = between_groups(f);
    CHECK(f->block.ops->epoch_close(f->block.context, CLOSE_NONCE, 1) == FWLAB_SPINE_V0_OK);
    CHECK(buffer_close(&f->buffer, CLOSE_NONCE, 1) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    CHECK(f->block.ops->epoch_quiescent(f->block.context, CLOSE_NONCE, 1, &epoch) == FWLAB_SPINE_V0_OK);
    CHECK(!epoch.quiescent && epoch.aggregate_operations == 1 && f->buffer.active);
    s = finish(f, &r, 0); CHECK(s.outcome == FWLAB_BLOCK_V0_CANCELLED && s.effect == FWLAB_BLOCK_V0_EFFECT_EXACT_PREFIX && s.completed_lbas == prefix);
    retire(f, &r); close_lower(f); puts("PARENT_CLOSE_PASS|retained_parent_counted=1|prefix_cancel=1|Block_NFC_buffer_zero=1|media_closed=1");
}

static void cancel_staged(int closing)
{
    struct fixture *f = create(); struct fwlab_block_request_v0 r;
    struct fwlab_block_status_v0 s; uint32_t i;
    size_t map_bytes = (VOLUME_LBAS / 8u) * sizeof(struct sf_map_entry);
    struct sf_map_entry *before = malloc(map_bytes); CHECK(before);
    open_lower(f, 1, 1);
    io(f, FWLAB_BLOCK_V0_WRITE, 0, 16, 0x61); /* Ready head, no admission maintenance. */
    r = request(f, FWLAB_BLOCK_V0_WRITE, 0, 2048, 0x62); admit(f, &r);
    for (i = 0; i < STEPS; ++i) {
        if (f->ftl->work.phase == SF_W_HOST_PROGRAM_WAIT &&
            f->ftl->io.phase == SF_IO_SUBMIT_FIRST && f->ftl->io.result.kind == SF_IO_PROGRAM &&
            f->ftl->parent.completed_groups == 0 && f->ftl->work.page_index == 0) break;
        step(f, 0);
    }
    CHECK(i < STEPS && f->ftl->parent.owned && !f->ftl->parent.completed_lbas);
    CHECK(f->ftl->io.first.kind == FWLAB_NFC_PROGRAM_TRANSFER &&
          f->ftl->io.second.kind == FWLAB_NFC_PROGRAM_EXECUTE);
    /* Read-only phase observation: neither staged request has reached NFC. */
    uint64_t sequence = fwlab_file_nand_v1_sequence(f->media);
    uint64_t map_sequence = f->ftl->map_sequence, frontier = f->ftl->durable_frontier;
    unsigned staged_effect = f->ftl->work.effect_seen;
    memcpy(before, f->ftl->map, map_bytes);
    if (closing) {
        CHECK(f->block.ops->epoch_close(f->block.context, CLOSE_NONCE, 1) == FWLAB_SPINE_V0_OK);
        CHECK(buffer_close(&f->buffer, CLOSE_NONCE, 1) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    } else CHECK(f->block.ops->cancel(f->block.context, &r.operation_token) == FWLAB_SPINE_V0_OK);
    s = finish(f, &r, 1);
    int mapping_equal = memcmp(before, f->ftl->map, map_bytes) == 0;
    printf("P2A_R1_STAGED_CANCEL|case=%s|expected_media_sequence=%llu|actual_media_sequence=%llu|expected_prefix=0|actual_prefix=%u|expected_map_sequence=%llu|actual_map_sequence=%llu|mapping_equal=%d|outcome=%u|effect=%u|quarantined=%u|effect_seen_at_stage=%u|image=%s/nand.bin\n",
        closing ? "epoch_close" : "cancel", (unsigned long long)sequence,
        (unsigned long long)fwlab_file_nand_v1_sequence(f->media), s.completed_lbas,
        (unsigned long long)map_sequence, (unsigned long long)f->ftl->map_sequence,
        mapping_equal, s.outcome, s.effect, f->ftl->quarantined, staged_effect, f->directory);
    CHECK(fflush(stdout) == 0); /* Failed assertions intentionally retain the image. */
    CHECK(s.outcome == FWLAB_BLOCK_V0_CANCELLED && s.effect == FWLAB_BLOCK_V0_EFFECT_NONE &&
          !s.completed_lbas && !s.data_bytes && !s.durability_witness && !f->ftl->quarantined);
    CHECK(fwlab_file_nand_v1_sequence(f->media) == sequence && mapping_equal &&
          f->ftl->map_sequence == map_sequence && f->ftl->durable_frontier == frontier);
    free(before); retire(f, &r);
    if (!closing) {
        io(f, FWLAB_BLOCK_V0_READ, 0, 16, 0); check_pattern(f->buffer.bytes, 0, 16, 0x61);
    }
    close_lower(f); destroy(f);
    printf("PARENT_STAGED_CANCEL_PASS|case=%s|cut_before_NFC_submission=1|no_media_effect=1|mapping_unchanged=1|cancelled_none=1|zero_close=1\n",
           closing ? "epoch_close" : "cancel");
}

/* Actual POSIX substrate forwarding: inject a reported error only AFTER the
 * real third (seal) sync of a selected non-first MAP_GROUP rail A succeeds. */
static enum fwlab_nfc_api_result cut_read(void *p, uint64_t at, void *v, size_t n)
{ struct fixture *f = p; return f->saved_io.read(f->saved_io.context, at, v, n); }
static enum fwlab_nfc_api_result cut_write(void *p, uint64_t at, const void *v, size_t n)
{ struct fixture *f = p; return f->saved_io.write(f->saved_io.context, at, v, n); }
static enum fwlab_nfc_api_result cut_sync(void *p)
{
    struct fixture *f = p; enum fwlab_nfc_api_result r = f->saved_io.sync(f->saved_io.context);
    if (r == FWLAB_NFC_API_OK && !f->cut_fired && f->ftl->parent.completed_groups &&
        f->ftl->meta.phase == SF_M_JOURNAL_A && f->ftl->meta.record.kind == SF_MAP_GROUP && ++f->cut_syncs == 3) {
        f->cut_fired = 1; return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    return r;
}
static enum fwlab_nfc_api_result cut_resize(void *p, uint64_t n)
{ struct fixture *f = p; return f->saved_io.resize(f->saved_io.context, n); }
static enum fwlab_nfc_api_result cut_size(void *p, uint64_t *n)
{ struct fixture *f = p; return f->saved_io.size(f->saved_io.context, n); }
static enum fwlab_nfc_api_result cut_close(void *p)
{ struct fixture *f = p; return f->saved_io.close(f->saved_io.context); }
static void uncertain_map(void)
{
    struct fixture *f = create(); uint32_t receipt[2] = {0}; int pipefd[2], status;
    open_lower(f, 1, 1); io(f, FWLAB_BLOCK_V0_WRITE, 0, 2048, 0xe1); close_lower(f);
    CHECK(pipe(pipefd) == 0); fflush(NULL); pid_t child = fork(); CHECK(child >= 0);
    if (!child) {
        CHECK(close(pipefd[0]) == 0); open_lower(f, 0, 1);
        f->saved_io = f->media->io;
        f->media->io = (struct fnv1_io){f, cut_read, cut_write, cut_sync, cut_resize, cut_size, cut_close};
        struct fwlab_block_request_v0 r = request(f, FWLAB_BLOCK_V0_WRITE, 0, 2048, 0xe2);
        admit(f, &r); struct fwlab_block_status_v0 s = finish(f, &r, 1);
        CHECK(f->cut_fired && f->ftl->quarantined && f->media->quarantined);
        CHECK(s.effect == FWLAB_BLOCK_V0_EFFECT_UNKNOWN_PREFIX && s.outcome != FWLAB_BLOCK_V0_SUCCEEDED && !s.durability_witness);
        CHECK(s.completed_lbas && s.completed_lbas < 2048 && f->buffer.active && f->ftl->parent.owned);
        receipt[0] = s.completed_lbas; receipt[1] = f->ftl->work.request.lba_count;
        CHECK(write(pipefd[1], receipt, sizeof(receipt)) == (ssize_t)sizeof(receipt));
        CHECK(close(pipefd[1]) == 0); _exit(88); /* Process failure, not a false zero-close. */
    }
    CHECK(close(pipefd[1]) == 0 && read(pipefd[0], receipt, sizeof(receipt)) == (ssize_t)sizeof(receipt));
    CHECK(close(pipefd[0]) == 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 88);
    CHECK(receipt[0] && receipt[1] && receipt[0] + receipt[1] < 2048);
    open_lower(f, 0, 1); io(f, FWLAB_BLOCK_V0_READ, 0, 2048, 0);
    uint32_t recovered = receipt[0] + receipt[1];
    check_pattern(f->buffer.bytes, 0, recovered, 0xe2);
    check_pattern(f->buffer.bytes + recovered * 512u, recovered, 2048 - recovered, 0xe1);
    close_lower(f); destroy(f);
    puts("PARENT_UNCERTAIN_MAP_PASS|actual_POSIX_seal_sync_error=1|UNKNOWN_PREFIX=1|child_quarantine=1|recovered_committed_A=1|remaining_old_data=1|zero_close_after_recovery=1");
}
int main(int argc, char **argv)
{
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--cancel-staged"))) {
        fputs("usage: test_scale_parent [--cancel-staged]\n", stderr); return 2;
    }
    cancel_staged(0); cancel_staged(1);
    if (argc == 2) return 0;
    struct fixture *f = create(); positive(f); cancellations(f); garbage_collection(f); closing_parent(f); destroy(f);
    uncertain_map(); puts("PARENT_PASS|adjacent_Buffer_real_FTL_NFC_media=1|no_Host_DMA_or_throughput_claim=1"); return 0;
}
