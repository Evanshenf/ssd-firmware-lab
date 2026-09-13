/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* N0 lower-binding evidence only: real physical-v2 on capped local tmpfs.
 * Not an FTL/native-NVMe, calibrated NAND or host-power-loss benchmark. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include "physical_nand.h"
#include "physical_nand_batch.h"
#include "fwlab/private/nfc_page_v2_lab.h"

#include <fcntl.h>
#include <limits.h>
#include <linux/magic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "NFC_LAB_MEDIA %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
#define MAIN_BYTES FWLAB_NFC_PAGE_V2_MAIN_BYTES
#define OOB_BYTES FWLAB_NFC_PAGE_V2_OOB_BYTES
#define GROUP 2u
#define RESOURCES 4u
#define NONCE UINT64_C(0x4c41424e46433031)

struct fixture {
    struct fwlab_file_nand_v2_config media_config;
    struct fwlab_file_nand_holder_v2 holder;
    struct fwlab_file_nand_v2 *media;
    void *media_arena, *lab_arena;
    struct fwlab_nfc_page_v2_lab_config config;
    struct fwlab_nfc_page_v2_lab *lab;
    struct fwlab_nfc_page_v2_provider port;
    char directory[PATH_MAX];
    int directory_fd;
    uint8_t main[GROUP * MAIN_BYTES], oob[GROUP * OOB_BYTES];
};

static uint8_t pattern(unsigned resource, unsigned page, size_t offset, bool oob)
{
    return (uint8_t)(resource * 53u + page * 19u + offset * 7u +
                     (offset >> 8) + (oob ? 0xb3u : 0x17u));
}

static void payload(struct fixture *f, unsigned resource)
{
    for (unsigned p = 0; p < GROUP; ++p) {
        for (size_t i = 0; i < MAIN_BYTES; ++i)
            f->main[p * MAIN_BYTES + i] = pattern(resource, p, i, false);
        for (size_t i = 0; i < OOB_BYTES; ++i)
            f->oob[p * OOB_BYTES + i] = pattern(resource, p, i, true);
    }
}

static void verify_payload(const struct fixture *f, unsigned resource)
{
    for (unsigned p = 0; p < GROUP; ++p) {
        for (size_t i = 0; i < MAIN_BYTES; ++i)
            CHECK(f->main[p * MAIN_BYTES + i] == pattern(resource, p, i, false));
        for (size_t i = 0; i < OOB_BYTES; ++i)
            CHECK(f->oob[p * OOB_BYTES + i] == pattern(resource, p, i, true));
    }
}

static void media_open(struct fixture *f, bool format)
{
    size_t bytes = fwlab_file_nand_v2_arena_size();
    size_t alignment = fwlab_file_nand_v2_arena_alignment();
    CHECK(bytes && bytes % alignment == 0);
    f->media_arena = aligned_alloc(alignment, bytes);
    CHECK(f->media_arena);
    CHECK((format ? fwlab_file_nand_v2_posix_format(f->media_arena, bytes,
        f->directory_fd, "nand.bin", &f->media_config, &f->media, &f->holder) :
        fwlab_file_nand_v2_posix_restart(f->media_arena, bytes, f->directory_fd,
        "nand.bin", &f->media_config, &f->holder, &f->media)) == FWLAB_NFC_API_OK);
}

static void lab_open(struct fixture *f, uint32_t epoch)
{
    struct fwlab_nand_batch_v2 media = fwlab_file_nand_v2_batch(f->media);
    size_t bytes = fwlab_nfc_page_v2_lab_arena_size();
    size_t alignment = fwlab_nfc_page_v2_lab_arena_alignment();
    memset(&f->config, 0, sizeof(f->config));
    f->config.version = FWLAB_NFC_PAGE_V2_LAB_VERSION;
    f->config.size = sizeof(f->config);
    f->config.base.version = FWLAB_NFC_PAGE_V2_VERSION;
    f->config.base.size = sizeof(f->config.base);
    f->config.base.profile = FWLAB_NFC_PAGE_V2_PROFILE_R0;
    f->config.base.geometry = f->media_config.geometry;
    memcpy(f->config.base.media_uuid, f->media_config.media_uuid, 16);
    f->config.base.instance_nonce = NONCE;
    f->config.base.operation_uid_limit = UINT64_MAX;
    f->config.base.controller_epoch = epoch;
    f->config.base.generation = 1;
    f->config.command_ns = 1000;
    f->config.array_read_ns = 10000;
    f->config.channel_bytes_per_second = UINT64_C(1000000000);
    f->config.virtual_ns_limit = UINT64_C(1000000000);
    for (unsigned r = 0; r < RESOURCES; ++r) {
        f->config.lun[r].target = f->config.lun[r].ce = (uint16_t)(r % 2);
        f->config.lun[r].package = (uint16_t)(r / 2);
        f->config.lun[r].die = (uint16_t)r;
    }
    CHECK(bytes && bytes % alignment == 0);
    f->lab_arena = aligned_alloc(alignment, bytes);
    CHECK(f->lab_arena);
    CHECK(fwlab_nfc_page_v2_lab_init(f->lab_arena, bytes, &f->config,
        &media, &f->lab) == FWLAB_NFC_API_OK);
    f->port = fwlab_nfc_page_v2_lab_provider(f->lab);
    CHECK(f->port.ops && f->port.context == f->lab);
}

static struct fixture *create(void)
{
    const char *root = getenv("FWLAB_TEST_MEDIA_DIR");
    struct fixture *f = calloc(1, sizeof(*f));
    struct statfs fs;
    int root_fd, length;
    CHECK(f);
    if (!root || root[0] != '/') {
        fputs("NFC_LAB_MEDIA_PREFLIGHT_ERROR|FWLAB_TEST_MEDIA_DIR_required|no_disk_fallback=1\n", stderr);
        exit(1);
    }
    root_fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(root_fd >= 0 && fstatfs(root_fd, &fs) == 0 && fs.f_bsize > 0);
    CHECK(fs.f_type == TMPFS_MAGIC &&
        (uint64_t)fs.f_blocks * (uint64_t)fs.f_bsize <= (UINT64_C(1) << 30) &&
        (uint64_t)fs.f_bavail * (uint64_t)fs.f_bsize >= (UINT64_C(64) << 20));
    CHECK(close(root_fd) == 0);
    length = snprintf(f->directory, sizeof(f->directory), "%s/fwlab-nfc-read.XXXXXX", root);
    CHECK(length > 0 && (size_t)length < sizeof(f->directory) && mkdtemp(f->directory));
    f->directory_fd = open(f->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(f->directory_fd >= 0);
    f->media_config.geometry = (struct fwlab_nfc_geometry){
        .version = FWLAB_NFC_CONTRACT_VERSION, .size = sizeof(struct fwlab_nfc_geometry),
        .channels = 2, .luns_per_channel = 2, .planes_per_lun = 1,
        .blocks_per_plane = 2, .pages_per_block = 64, .plane_parallelism_per_lun = 1,
        .main_bytes_per_page = MAIN_BYTES, .oob_bytes_per_page = OOB_BYTES,
        .max_programs_per_erase = 1, .program_order = FWLAB_NFC_PROGRAM_ASCENDING
    };
    memcpy(f->media_config.media_uuid, "NFC-LAB-MEDIA001", 16);
    printf("NFC_LAB_MEDIA_PREFLIGHT|medium=local_tmpfs|image_bytes=%llu|directory=%s\n",
        (unsigned long long)fwlab_file_nand_v2_image_bytes(&f->media_config), f->directory);
    fflush(stdout);
    media_open(f, true);
    lab_open(f, 1);
    return f;
}

static struct fwlab_nfc_page_v2_request request(struct fixture *f,
    uint64_t uid, unsigned resource, uint16_t kind)
{
    struct fwlab_nfc_page_v2_request r = {0};
    r.version = FWLAB_NFC_PAGE_V2_VERSION; r.size = sizeof(r); r.kind = kind;
    r.operation = (struct fwlab_nfc_operation_token){
        NONCE, uid, f->config.base.controller_epoch, 1};
    r.first.channel = (uint16_t)(resource / 2);
    r.first.lun = (uint16_t)(resource % 2);
    r.page_count = GROUP;
    if (kind == FWLAB_NFC_PAGE_V2_PROGRAM_GROUP) {
        r.main = f->main; r.main_bytes = sizeof(f->main);
        r.oob = f->oob; r.oob_bytes = sizeof(f->oob);
    }
    return r;
}

static void step(struct fixture *f)
{
    struct fwlab_nfc_page_v2_step_result out;
    CHECK(f->port.ops->step(f->port.context, 1, &out) == FWLAB_NFC_API_OK && out.units_used <= 1);
}

static struct fwlab_nfc_page_v2_lab_stats snapshot(struct fixture *f)
{
    struct fwlab_nfc_page_v2_lab_stats s;
    CHECK(fwlab_nfc_page_v2_lab_snapshot(f->lab, &s) == FWLAB_NFC_API_OK);
    CHECK(!s.counters_saturated && !s.trace_dropped);
    return s;
}

static struct fwlab_nfc_page_v2_result take(struct fixture *f,
    const struct fwlab_nfc_page_v2_request *r, bool output)
{
    struct fwlab_nfc_page_v2_result result;
    struct fwlab_nfc_page_v2_result stale_output;
    struct fwlab_nfc_page_v2_output d = {f->main, sizeof(f->main), f->oob, sizeof(f->oob)};
    enum fwlab_nfc_api_result status;
    unsigned steps = 0;
    while ((status = f->port.ops->take_result(f->port.context, &r->operation,
        &result, output ? &d : NULL)) == FWLAB_NFC_API_WRONG_STATE) {
        CHECK(++steps < 256); step(f);
    }
    CHECK(status == FWLAB_NFC_API_OK &&
        result.operation.operation_uid == r->operation.operation_uid &&
        result.operation.controller_epoch == r->operation.controller_epoch);
    CHECK(f->port.ops->take_result(f->port.context, &r->operation, &stale_output, NULL) == FWLAB_NFC_API_STALE_TOKEN);
    return result;
}

static void resource_trace(struct fixture *f)
{
    struct fwlab_nfc_page_v2_lab_stats s = snapshot(f);
    unsigned bus[2] = {0}, reg[RESOURCES] = {0}, array[RESOURCES] = {0};
    unsigned peak_bus = 0, peak_array = 0;
    uint64_t last = 0;
    for (uint32_t i = 0; i < s.trace_count; ++i) {
        struct fwlab_nfc_page_v2_lab_trace e;
        CHECK(fwlab_nfc_page_v2_lab_trace_at(f->lab, i, &e) == FWLAB_NFC_API_OK);
        unsigned ch = e.ppa.channel, lun = ch * 2u + e.ppa.lun;
        CHECK(ch < 2 && lun < RESOURCES && e.now_ns >= last);
        last = e.now_ns;
        switch (e.event) {
        case FWLAB_NFC_PAGE_V2_LAB_COMMAND_BEGIN:
            CHECK(!bus[ch] && !reg[lun]); bus[ch] = reg[lun] = 1; break;
        case FWLAB_NFC_PAGE_V2_LAB_COMMAND_END:
            CHECK(bus[ch] && reg[lun] && !array[lun]); bus[ch] = 0; array[lun] = 1; break;
        case FWLAB_NFC_PAGE_V2_LAB_ARRAY_READY:
            CHECK(reg[lun] && array[lun]); array[lun] = 0; break;
        case FWLAB_NFC_PAGE_V2_LAB_DATA_BEGIN:
            CHECK(!bus[ch] && reg[lun] && !array[lun]); bus[ch] = 1; break;
        case FWLAB_NFC_PAGE_V2_LAB_DATA_END:
            CHECK(bus[ch] && reg[lun]); bus[ch] = reg[lun] = 0; break;
        default: break;
        }
        unsigned buses = bus[0] + bus[1];
        unsigned arrays = array[0] + array[1] + array[2] + array[3];
        if (buses > peak_bus) peak_bus = buses;
        if (arrays > peak_array) peak_array = arrays;
    }
    CHECK(!bus[0] && !bus[1] && !reg[0] && !reg[1] && !reg[2] && !reg[3]);
    CHECK(peak_bus == 2 && peak_array >= 2 && s.materialized_pages == RESOURCES * GROUP);
    printf("NFC_LAB_MEDIA_PARALLEL|materialized=%llu|virtual_ns=%llu|peak_channel=%u|peak_array=%u|physical_sequence=%llu\n",
        (unsigned long long)s.materialized_pages, (unsigned long long)s.now_ns,
        peak_bus, peak_array, (unsigned long long)fwlab_file_nand_v2_sequence(f->media));
}

static void close_instances(struct fixture *f)
{
    bool quiet = false;
    CHECK(f->port.ops->reset_begin(f->port.context, NONCE,
        f->config.base.controller_epoch) == FWLAB_NFC_API_OK);
    CHECK(f->port.ops->quiescent(f->port.context, NONCE,
        f->config.base.controller_epoch, &quiet) == FWLAB_NFC_API_OK && quiet);
    free(f->lab_arena); f->lab_arena = NULL; f->lab = NULL;
    CHECK(fwlab_file_nand_v2_close(f->media) == FWLAB_NFC_API_OK);
    free(f->media_arena); f->media_arena = NULL; f->media = NULL;
}

int main(void)
{
    struct fixture *f = create();
    struct fwlab_nfc_page_v2_request reads[RESOURCES], extra;
    struct fwlab_nfc_page_v2_result result;
    struct stat status;
    for (unsigned r = 0; r < RESOURCES; ++r) {
        payload(f, r); extra = request(f, r + 1u, r, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP);
        CHECK(f->port.ops->try_submit(f->port.context, &extra).disposition == FWLAB_NFC_ACCEPTED);
        CHECK(fwlab_nfc_page_v2_lab_begin_timed_read(f->lab) == FWLAB_NFC_API_WRONG_STATE);
        result = take(f, &extra, false);
        CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS &&
            result.effect == FWLAB_NFC_PAGE_V2_EFFECT_APPLIED_COMPLETE);
    }
    uint64_t sequence = fwlab_file_nand_v2_sequence(f->media);
    CHECK(sequence == RESOURCES);
    CHECK(fwlab_nfc_page_v2_lab_begin_timed_read(f->lab) == FWLAB_NFC_API_OK);
    for (unsigned r = 0; r < RESOURCES; ++r) {
        reads[r] = request(f, r + 5u, r, FWLAB_NFC_PAGE_V2_READ_GROUP);
        CHECK(f->port.ops->try_submit(f->port.context, &reads[r]).disposition == FWLAB_NFC_ACCEPTED);
    }
    extra = request(f, 9, 0, FWLAB_NFC_PAGE_V2_READ_GROUP);
    CHECK(f->port.ops->try_submit(f->port.context, &extra).disposition == FWLAB_NFC_BACKPRESSURE);
    unsigned steps = 0;
    while (snapshot(f).results_pending != RESOURCES) { CHECK(++steps < 256); step(f); }
    resource_trace(f);
    CHECK(snapshot(f).held_luns == 0 && snapshot(f).busy_channels == 0);
    for (unsigned r = RESOURCES; r-- > 0;) {
        memset(f->main, 0, sizeof(f->main)); memset(f->oob, 0, sizeof(f->oob));
        result = take(f, &reads[r], true);
        CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS && result.read_valid && result.delivered_pages == GROUP);
        verify_payload(f, r);
    }
    CHECK(fwlab_file_nand_v2_sequence(f->media) == sequence);
    CHECK(f->port.ops->try_submit(f->port.context, &extra).disposition == FWLAB_NFC_ACCEPTED);
    uint64_t materialized = snapshot(f).materialized_pages;
    CHECK(f->port.ops->cancel(f->port.context, &extra.operation) == FWLAB_NFC_API_OK);
    result = take(f, &extra, false);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_CANCELLED && !result.read_valid &&
        snapshot(f).materialized_pages == materialized);

    extra = request(f, 10, 0, FWLAB_NFC_PAGE_V2_READ_GROUP);
    CHECK(f->port.ops->try_submit(f->port.context, &extra).disposition == FWLAB_NFC_ACCEPTED);
    step(f); CHECK(snapshot(f).held_luns == 1);
    CHECK(f->port.ops->cancel(f->port.context, &extra.operation) == FWLAB_NFC_API_OK);
    result = take(f, &extra, false);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_CANCELLED && !result.read_valid &&
        snapshot(f).materialized_pages == materialized + 1u && !snapshot(f).held_luns);
    extra = request(f, 11, 0, FWLAB_NFC_PAGE_V2_PROGRAM_GROUP);
    CHECK(f->port.ops->try_submit(f->port.context, &extra).disposition == FWLAB_NFC_REJECTED);
    CHECK(fwlab_file_nand_v2_sequence(f->media) == sequence);
    puts("NFC_LAB_MEDIA_CANCEL|queued_no_effect=1|started_current_page_drained=1|timed_mutation_rejected=1");

    extra = request(f, 12, 0, FWLAB_NFC_PAGE_V2_READ_GROUP);
    CHECK(f->port.ops->try_submit(f->port.context, &extra).disposition == FWLAB_NFC_ACCEPTED);
    step(f);
    CHECK(f->port.ops->reset_begin(f->port.context, NONCE, 1) == FWLAB_NFC_API_OK);
    steps = 0;
    while (snapshot(f).results_pending != 1) { CHECK(++steps < 256); step(f); }
    bool quiet = true;
    CHECK(f->port.ops->quiescent(f->port.context, NONCE, 1, &quiet) == FWLAB_NFC_API_OK && !quiet);
    result = take(f, &extra, false);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_CANCELLED && !result.read_valid);
    close_instances(f);

    media_open(f, false); lab_open(f, 2);
    CHECK(fwlab_file_nand_v2_sequence(f->media) == sequence);
    CHECK(fwlab_nfc_page_v2_lab_begin_timed_read(f->lab) == FWLAB_NFC_API_OK);
    CHECK(f->port.ops->take_result(f->port.context, &reads[0].operation,
        &result, NULL) == FWLAB_NFC_API_STALE_TOKEN);
    extra = request(f, 1, 0, FWLAB_NFC_PAGE_V2_READ_GROUP);
    CHECK(f->port.ops->try_submit(f->port.context, &extra).disposition == FWLAB_NFC_ACCEPTED);
    result = take(f, &extra, true);
    CHECK(result.terminal == FWLAB_NFC_TERMINAL_SUCCESS && result.read_valid); verify_payload(f, 0);
    close_instances(f);
    CHECK(fstatat(f->directory_fd, "nand.bin", &status, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISREG(status.st_mode) && status.st_nlink == 1 &&
        (uint64_t)status.st_dev == f->holder.device && (uint64_t)status.st_ino == f->holder.inode);
    CHECK(unlinkat(f->directory_fd, "nand.bin", 0) == 0 && close(f->directory_fd) == 0 && rmdir(f->directory) == 0);
    free(f);
    puts("NFC_LAB_MEDIA_PASS|same_format_reopen=1|fresh_epoch=1|owned_temp_removed=1|N0_lower_binding_only=1|not_FTL_native_or_calibrated_rate=1");
    return 0;
}
