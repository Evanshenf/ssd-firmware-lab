/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _GNU_SOURCE

#include "../headless-j0/j0_internal.h"
#include "compact_nand.h"
#include "compact_nand_internal.h"
#include "m3p_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "SCALE media check failed at %s:%d: %s\n", \
            __FILE__, __LINE__, #expression); return 0; } } while (0)
#define STEP_LIMIT 400000u
#define ORPHAN_CUT_EXIT 87

struct image {
    int directory_fd;
    void *arena;
    struct fwlab_file_nand_v1 *media;
    struct fwlab_file_nand_v1_config config;
    struct fwlab_file_nand_holder_v1 holder;
    struct j0_media_binding binding;
};

static int media_open(struct image *image, int format)
{
    size_t alignment = fwlab_file_nand_v1_arena_alignment();
    size_t size = fwlab_file_nand_v1_arena_size();

    CHECK(alignment != 0 && size != 0 &&
          size <= SIZE_MAX - (alignment - 1u));
    size = (size + alignment - 1u) & ~(alignment - 1u);
    image->arena = aligned_alloc(alignment, size);
    CHECK(image->arena != NULL);
    if (format) {
        CHECK(fwlab_file_nand_v1_posix_format(
            image->arena, size, image->directory_fd, "nand.bin",
            &image->config, &image->media, &image->holder) == FWLAB_NFC_API_OK);
    } else {
        CHECK(fwlab_file_nand_v1_posix_restart(
            image->arena, size, image->directory_fd, "nand.bin",
            &image->config, &image->holder, &image->media) == FWLAB_NFC_API_OK);
    }
    image->binding.media = fwlab_file_nand_v1_media(image->media);
    image->binding.geometry = image->config.geometry;
    memcpy(image->binding.media_uuid, image->config.media_uuid, 16);
    return 1;
}

static int media_close(struct image *image)
{
    CHECK(fwlab_file_nand_v1_close(image->media) == FWLAB_NFC_API_OK);
    free(image->arena);
    image->arena = NULL;
    image->media = NULL;
    memset(&image->binding.media, 0, sizeof(image->binding.media));
    return 1;
}

static int second_open_rejected(const struct image *image)
{
    size_t alignment = fwlab_file_nand_v1_arena_alignment();
    size_t size = fwlab_file_nand_v1_arena_size();
    struct fwlab_file_nand_v1 *second = NULL;
    uint64_t sequence = fwlab_file_nand_v1_sequence(image->media);
    void *arena;

    CHECK(alignment != 0 && size <= SIZE_MAX - (alignment - 1u));
    size = (size + alignment - 1u) & ~(alignment - 1u);
    arena = aligned_alloc(alignment, size);
    CHECK(arena != NULL && arena != image->arena);
    CHECK(fwlab_file_nand_v1_posix_restart(arena, size, image->directory_fd,
        "nand.bin", &image->config, &image->holder, &second) ==
        FWLAB_NFC_API_INVALID_CONTRACT);
    CHECK(second == NULL);
    CHECK(fwlab_file_nand_v1_sequence(image->media) == sequence);
    free(arena);
    return 1;
}

static struct j0_runtime_config runtime_config(
    const struct image *image, uint32_t mode, uint64_t salt)
{
    struct j0_runtime_config config = {0};

    config.version = J0_RUNTIME_VERSION;
    config.size = (uint16_t)sizeof(config);
    memcpy(config.media_uuid, image->config.media_uuid, 16);
    config.media_binding = &image->binding;
    config.media_mode = mode;
    config.generation = 1;
    config.execution_epoch = 1;
    config.volatile_nonce_seed = salt;
    config.budget_profile = J0_BUDGET_LAB;
    return config;
}

static int binding_rejections(const struct image *image)
{
    struct j0_runtime *runtime = calloc(1, sizeof(*runtime));
    struct j0_runtime_config config;
    struct j0_media_binding binding;
    struct fwlab_nand_media_ops ops;
    uint64_t sequence = fwlab_file_nand_v1_sequence(image->media);
    unsigned int variant;

    CHECK(runtime != NULL);
    for (variant = 0; variant < 12; ++variant) {
        config = runtime_config(image, J0_MEDIA_FORMAT, 11);
        binding = image->binding;
        ops = *binding.media.ops;
        binding.media.ops = &ops;
        config.media_binding = &binding;
        switch (variant) {
        case 0: config.media_binding = NULL; break;
        case 1: config.file = (struct fwlab_file_nand_v0 *)image->arena; break;
        case 2: binding.media_uuid[0] ^= 1u; break;
        case 3: ++binding.geometry.blocks_per_plane; break;
        case 4: binding.media.context = NULL; break;
        case 5: binding.media.ops = NULL; break;
        case 6: ++ops.version; break;
        case 7: ops.read_page = NULL; break;
        case 8: ops.program = NULL; break;
        case 9: ops.erase = NULL; break;
        case 10: ops.mark_runtime_bad = NULL; break;
        default: ops.hash = NULL; break;
        }
        CHECK(j0_runtime_init(runtime, &config) == FWLAB_SPINE_V0_INVALID);
        CHECK(runtime->magic == 0);
        CHECK(fwlab_file_nand_v1_sequence(image->media) == sequence);
    }
    free(runtime);
    return 1;
}

static int runtime_open(struct image *image, uint32_t mode, uint64_t salt,
                        struct j0_runtime **output)
{
    struct j0_runtime_config config = runtime_config(image, mode, salt);
    struct j0_runtime *runtime = calloc(1, sizeof(*runtime));
    uint32_t iteration;

    CHECK(runtime != NULL);
    CHECK(j0_runtime_init(runtime, &config) == FWLAB_SPINE_V0_OK);
    for (iteration = 0; iteration < STEP_LIMIT; ++iteration) {
        uint32_t used;

        CHECK(j0_runtime_step(runtime, 3, &used) == FWLAB_SPINE_V0_OK);
        CHECK(used == 3);
        if (runtime->ready) {
            CHECK(runtime->config.file == NULL);
            CHECK(runtime->block.context == runtime->m3p);
            CHECK(runtime->block.ops == fwlab_m3p_block_service(runtime->m3p).ops);
            CHECK(runtime->m3p->nfc.context == runtime->nfc_provider.context);
            CHECK(runtime->m3p->nfc.ops == runtime->nfc_provider.ops);
            *output = runtime;
            return 1;
        }
    }
    CHECK(0 && "runtime readiness budget exhausted");
}

static int runtime_close(struct j0_runtime *runtime)
{
    struct j0_close_status status;
    uint32_t iteration;

    CHECK(j0_runtime_close_start(runtime) == FWLAB_SPINE_V0_OK);
    for (iteration = 0; iteration < STEP_LIMIT; ++iteration) {
        uint32_t used;

        CHECK(j0_runtime_close_query(runtime, &status) == FWLAB_SPINE_V0_OK);
        if (status.quiescent) break;
        CHECK(j0_runtime_step(runtime, 3, &used) == FWLAB_SPINE_V0_OK);
    }
    CHECK(iteration < STEP_LIMIT);
    for (iteration = 0; iteration < 256; ++iteration) {
        enum fwlab_spine_result_v0 result = j0_runtime_fini(runtime);

        if (result == FWLAB_SPINE_V0_OK) break;
        CHECK(result == FWLAB_SPINE_V0_IN_PROGRESS);
    }
    CHECK(iteration < 256);
    CHECK(j0_runtime_close_query(runtime, &status) == FWLAB_SPINE_V0_OK);
    CHECK(status.quiescent && status.profiles_retired);
    CHECK(status.host_authorities == 0 && status.dma_operations == 0 &&
          status.buffers == 0 && status.block_operations == 0 &&
          status.pending == 0 && status.pinned == 0 && status.nfc_operations == 0);
    free(runtime);
    return 1;
}

static int command_run_flags(struct j0_runtime *runtime, uint64_t uid,
                             uint8_t opcode, uint64_t lba, uint32_t lbas,
                             const uint8_t *input, uint8_t *output, int fua)
{
    struct fwlab_nvme_command command = {0};
    struct j0_host_transfer transfer = {0};
    struct fwlab_spine_command_ticket_v0 ticket;
    struct fwlab_nvme_completion_intent intent;
    struct fwlab_completion_lease_v0 lease;
    uint32_t iteration;

    command.version = FWLAB_NVME_COMMAND_VERSION;
    command.size = (uint16_t)sizeof(command);
    command.handle.instance_nonce = UINT64_C(0x5343414c45484f00) +
                                    runtime->config.volatile_nonce_seed;
    command.handle.command_uid = uid;
    command.handle.controller_epoch = 1;
    command.handle.generation = 1;
    command.origin.word[0] = command.handle.instance_nonce ^
                             UINT64_C(0x4f52494700000000);
    command.origin.word[1] = uid;
    command.trace_cookie = uid;
    command.safety_generation = 1;
    command.namespace_id = 1;
    command.opcode = opcode;
    command.queue_class = FWLAB_NVME_QUEUE_IO;
    command.fuse = FWLAB_NVME_FUSE_NONE;
    command.data_pointer_format = FWLAB_NVME_DATA_POINTER_PRP;
    command.data_address_present = (uint8_t)(opcode != 0);
    transfer.version = J0_RUNTIME_VERSION;
    transfer.size = (uint16_t)sizeof(transfer);
    if (opcode != 0) {
        command.command_dword10_15[0] = (uint32_t)lba;
        command.command_dword10_15[1] = (uint32_t)(lba >> 32);
        command.command_dword10_15[2] = lbas - 1u;
        transfer.direction = opcode == 1 ? FWLAB_HOST_DATA_V0_HOST_TO_CONTROLLER
                                         : FWLAB_HOST_DATA_V0_CONTROLLER_TO_HOST;
        transfer.exact_bytes = lbas * FWLAB_M3P_LBA_BYTES;
        transfer.input = input;
    }
    if (fua) {
        CHECK(opcode == 1);
        command.command_dword10_15[2] |= UINT32_C(1) << 30;
    }
    CHECK(j0_runtime_admit_start(runtime, J0_PROFILE_LINUX_V1, &command,
                                 &transfer, &ticket) == FWLAB_SPINE_V0_OK);
    CHECK(ticket.lifecycle_instance_nonce == runtime->lifecycle_instance_nonce);
    for (iteration = 0; iteration < STEP_LIMIT; ++iteration) {
        uint32_t used;
        enum fwlab_spine_result_v0 result =
            j0_runtime_intent_read(runtime, &ticket, &intent);

        if (result == FWLAB_SPINE_V0_OK) break;
        CHECK(result == FWLAB_SPINE_V0_IN_PROGRESS);
        CHECK(j0_runtime_step(runtime, 3, &used) == FWLAB_SPINE_V0_OK);
    }
    CHECK(iteration < STEP_LIMIT);
    CHECK(intent.status_code == 0 && intent.status_code_type == 0);
    if (fua) {
        const struct j0_admission_record *record = NULL;
        for (iteration = 0; iteration < J0_MAX_COMMANDS; ++iteration) {
            const struct j0_admission_record *candidate = &runtime->admission[iteration];
            if (candidate->occupied && fwlab_spine_command_ticket_v0_equal(
                    &candidate->ticket, &ticket)) {
                record = candidate;
                break;
            }
        }
        CHECK(record != NULL && record->program.action_count == 2);
        CHECK(record->action[1].token.kind == FWLAB_HOST_ACTION_V0_BLOCK_WRITE);
        CHECK(record->action[1].block_status.durability_witness ==
              FWLAB_BLOCK_V0_WITNESS_SELF_DURABLE);
        CHECK(runtime->m3p->pending_count == 0);
    }
    if (output != NULL) {
        CHECK(j0_runtime_host_read(runtime, &ticket, output,
                                  transfer.exact_bytes) == FWLAB_SPINE_V0_OK);
    }
    CHECK(j0_runtime_publication_acquire(runtime, &ticket, &lease, &intent) ==
          FWLAB_SPINE_V0_OK);
    CHECK(j0_runtime_publication_finish(runtime, &ticket, &lease,
          FWLAB_SPINE_PUBLICATION_V1_COMMITTED) == FWLAB_SPINE_V0_OK);
    return 1;
}

static int command_run(struct j0_runtime *runtime, uint64_t uid,
                       uint8_t opcode, uint64_t lba, uint32_t lbas,
                       const uint8_t *input, uint8_t *output)
{
    return command_run_flags(runtime, uid, opcode, lba, lbas, input, output, 0);
}

static int readback(struct j0_runtime *runtime, uint64_t *uid,
                    const uint8_t expected[16384])
{
    uint8_t output[8192];
    uint32_t part;

    for (part = 0; part < 2; ++part) {
        memset(output, 0xa5, sizeof(output));
        CHECK(command_run(runtime, (*uid)++, 2, part * 16u, 16, NULL, output));
        CHECK(memcmp(output, &expected[part * sizeof(output)], sizeof(output)) == 0);
    }
    return 1;
}

/* One process cut in the private physical byte substrate. The normal POSIX
 * operations perform every byte and barrier; the wrapper only observes the
 * seal and terminates before the first home write. No production hook exists. */
struct orphan_witness {
    uint64_t media_sequence;
    uint32_t map_sequence;
    uint32_t data_sequence;
    struct fwlab_nfc_ppa ppa;
};

struct orphan_cut {
    struct fnv1_io original;
    struct j0_runtime *runtime;
    struct fwlab_file_nand_v1 *media;
    struct m3p_map_entry prior;
    uint64_t seal_offset;
    struct orphan_witness witness;
    int witness_fd;
    int seal_written;
    int seal_synced;
};

static struct orphan_cut *armed_orphan;

static enum fwlab_nfc_api_result orphan_sync(void *context)
{
    struct orphan_cut *cut = armed_orphan;
    enum fwlab_nfc_api_result result = cut->original.sync(context);

    if (result == FWLAB_NFC_API_OK && cut->seal_written)
        cut->seal_synced = 1;
    return result;
}

static enum fwlab_nfc_api_result orphan_write(
    void *context, uint64_t offset, const void *buffer, size_t size)
{
    struct orphan_cut *cut = armed_orphan;
    const uint8_t *bytes = buffer;
    enum fwlab_nfc_api_result result;

    if (offset >= FNV1_HOME_BASE && offset < cut->media->page_metadata_offset) {
        const struct fwlab_m3p *ftl = cut->runtime->m3p;
        const struct m3p_delta *delta = &ftl->operation.delta[0];
        size_t index;
        ssize_t sent;

        if (!cut->seal_synced || size != FNV1_SECTOR ||
            ftl->operation.request.operation != FWLAB_BLOCK_V0_WRITE ||
            ftl->operation.delta_count != 1 || delta->lpn != 0 ||
            ftl->map_sequence != cut->witness.map_sequence ||
            !m3p_map_entry_equal(&ftl->durable[0], &cut->prior) ||
            offset != FNV1_HOME_BASE + (uint64_t)m3p_physical_index(
                delta->target.block, delta->target.page) * FNV1_SECTOR)
            _exit(88);
        for (index = 0; index < size; ++index)
            if (bytes[index] != 0xd3) _exit(88);
        cut->witness.ppa.block = delta->target.block;
        cut->witness.ppa.page = delta->target.page;
        cut->witness.data_sequence = delta->target.data_record_sequence;
        do {
            sent = write(cut->witness_fd, &cut->witness, sizeof(cut->witness));
        } while (sent < 0 && errno == EINTR);
        _exit(sent == (ssize_t)sizeof(cut->witness) ? ORPHAN_CUT_EXIT : 88);
    }
    result = cut->original.write(context, offset, buffer, size);
    if (result == FWLAB_NFC_API_OK && offset == cut->seal_offset &&
        size == FNV1_SECTOR && m3p_get_le32(bytes) == UINT32_C(0x31434e46) &&
        ((uint64_t)m3p_get_le32(bytes + 8) |
         ((uint64_t)m3p_get_le32(bytes + 12) << 32)) == cut->witness.media_sequence)
        cut->seal_written = 1;
    return result;
}

static int orphan_child(struct image *image, int witness_fd)
{
    struct j0_runtime *runtime;
    struct orphan_cut cut = {0};
    uint8_t input[4096];

    CHECK(media_open(image, 0));
    CHECK(runtime_open(image, J0_MEDIA_RECOVER, 151, &runtime));
    CHECK(runtime->m3p->pending_count == 0);
    CHECK(runtime->m3p->work_kind == FWLAB_M3P_MAINTENANCE_NONE);
    cut.original = image->media->io;
    cut.runtime = runtime;
    cut.media = image->media;
    cut.prior = runtime->m3p->durable[0];
    cut.witness_fd = witness_fd;
    cut.witness.media_sequence = fwlab_file_nand_v1_sequence(image->media) + 1u;
    cut.witness.map_sequence = runtime->m3p->map_sequence;
    cut.seal_offset = FNV1_BANK_BASE +
        (cut.witness.media_sequence & 1u) * FNV1_BANK_BYTES + 4u * FNV1_SECTOR;
    armed_orphan = &cut;
    image->media->io.write = orphan_write;
    image->media->io.sync = orphan_sync;
    memset(input, 0xd3, sizeof(input));
    /* Plenty of ordinary data space remains: the next physical program is
     * this Write's new data page, before any FTL mapping-journal program. */
    (void)command_run(runtime, 1, 1, 0, 8, input, NULL);
    _exit(89); /* A normal completion never substitutes for the requested cut. */
}

static int cut_orphan_write(struct image *image, struct orphan_witness *witness)
{
    int pipe_fd[2];
    int status;
    pid_t child, waited;
    size_t received = 0;

    CHECK(image->media == NULL && image->arena == NULL);
    CHECK(pipe(pipe_fd) == 0);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        if (close(pipe_fd[0]) != 0) _exit(88);
        _exit(orphan_child(image, pipe_fd[1]) ? 89 : 88);
    }
    CHECK(close(pipe_fd[1]) == 0);
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    CHECK(waited == child && WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == ORPHAN_CUT_EXIT);
    while (received < sizeof(*witness)) {
        ssize_t count = read(pipe_fd[0], (uint8_t *)witness + received,
                             sizeof(*witness) - received);
        if (count < 0 && errno == EINTR) continue;
        CHECK(count > 0);
        received += (size_t)count;
    }
    CHECK(close(pipe_fd[0]) == 0);
    return 1;
}

static int physical_orphan_replayed(struct image *image,
                                    const struct orphan_witness *witness)
{
    uint8_t main[4096], oob[128];
    struct fwlab_nand_page_info page;
    struct fwlab_nand_block_info block;
    struct m3p_oob decoded;
    size_t index;

    CHECK(fwlab_file_nand_v1_sequence(image->media) == witness->media_sequence);
    CHECK(image->binding.media.ops->read_page(image->binding.media.context,
        &witness->ppa, main, sizeof(main), oob, sizeof(oob), &page, &block) ==
        FWLAB_NFC_API_OK);
    CHECK(page.state == FWLAB_NAND_PAGE_VALID && page.program_count == 1);
    for (index = 0; index < sizeof(main); ++index) CHECK(main[index] == 0xd3);
    CHECK(m3p_decode_oob(oob, &decoded));
    CHECK(decoded.page_type == M3P_PAGE_DATA && decoded.lpn == 0 &&
          decoded.record_sequence == witness->data_sequence);
    return 1;
}

static int force_gc(struct j0_runtime *runtime)
{
    struct fwlab_m3p_gc_status status;
    uint32_t iteration;

    CHECK(fwlab_m3p_force_gc_start(runtime->m3p) == FWLAB_SPINE_V0_OK);
    for (iteration = 0; iteration < STEP_LIMIT; ++iteration) {
        uint32_t used;

        CHECK(fwlab_m3p_force_gc_query(runtime->m3p, &status) == FWLAB_SPINE_V0_OK);
        if (status.state == FWLAB_M3P_MAINTENANCE_SUCCEEDED) break;
        CHECK(status.state == FWLAB_M3P_MAINTENANCE_RUNNING);
        CHECK(j0_runtime_step(runtime, 3, &used) == FWLAB_SPINE_V0_OK);
    }
    CHECK(iteration < STEP_LIMIT);
    CHECK(status.live_pages > 0 && status.reclaimable_pages > 0);
    CHECK(status.moved_pages == status.live_pages && status.successful_erase_count > 0);
    CHECK(status.victim_block != status.destination_block);
    printf("SCALE_MEDIA_GC|live=%u|moved=%u|reclaimable=%u|erases=%u\n",
           status.live_pages, status.moved_pages, status.reclaimable_pages,
           status.successful_erase_count);
    return 1;
}

static int journey(void)
{
    char directory[] = "/tmp/fwlab-scale-media.XXXXXX";
    struct image image = {0};
    struct j0_runtime *runtime;
    uint8_t expected[16384];
    uint8_t patch[1536];
    uint64_t uid = 1;
    uint64_t sequence;
    uint64_t old_lifecycle;
    uint64_t old_m3p;
    uint64_t old_nfc;
    struct orphan_witness orphan;
    struct stat holder;
    size_t index;

    CHECK(mkdtemp(directory) != NULL);
    fprintf(stderr, "SCALE media disposable image: %s/nand.bin\n", directory);
    image.directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(image.directory_fd >= 0);
    image.config.geometry = fwlab_file_nand_v0_geometry();
    memcpy(image.config.media_uuid, "SCALE-A-MEDIA-001", 16);
    CHECK(media_open(&image, 1));
    CHECK(binding_rejections(&image));
    CHECK(runtime_open(&image, J0_MEDIA_FORMAT, 101, &runtime));
    sequence = fwlab_file_nand_v1_sequence(image.media);
    for (index = 0; index < sizeof(expected); ++index)
        expected[index] = (uint8_t)(index * 17u + index / 512u + 3u);
    CHECK(command_run(runtime, uid++, 1, 0, 16, expected, NULL));
    CHECK(command_run(runtime, uid++, 0, 0, 0, NULL, NULL));
    CHECK(command_run(runtime, uid++, 1, 16, 16, &expected[8192], NULL));
    CHECK(command_run(runtime, uid++, 0, 0, 0, NULL, NULL));
    memset(patch, 0x6b, sizeof(patch));
    CHECK(command_run(runtime, uid++, 1, 7, 3, patch, NULL));
    memcpy(&expected[7u * 512u], patch, sizeof(patch));
    CHECK(command_run(runtime, uid++, 0, 0, 0, NULL, NULL));
    CHECK(second_open_rejected(&image));
    /* This read still uses the original live binding after the refused open. */
    CHECK(readback(runtime, &uid, expected));
    CHECK(fwlab_file_nand_v1_sequence(image.media) > sequence);
    CHECK(runtime->m3p->child_starts > 0);
    old_lifecycle = runtime->lifecycle_instance_nonce;
    old_m3p = runtime->m3p_instance_nonce;
    old_nfc = runtime->nfc_instance_nonce;
    CHECK(runtime_close(runtime));
    CHECK(media_close(&image));

    CHECK(cut_orphan_write(&image, &orphan));
    CHECK(media_open(&image, 0));
    CHECK(physical_orphan_replayed(&image, &orphan));
    CHECK(fstatat(image.directory_fd, "nand.bin", &holder, AT_SYMLINK_NOFOLLOW) == 0);
    CHECK((uint64_t)holder.st_dev == image.holder.device &&
          (uint64_t)holder.st_ino == image.holder.inode);
    CHECK(runtime_open(&image, J0_MEDIA_RECOVER, 202, &runtime));
    CHECK(runtime->m3p->map_sequence == orphan.map_sequence);
    CHECK(runtime->m3p->p2l[m3p_physical_index(
        (uint8_t)orphan.ppa.block, (uint8_t)orphan.ppa.page)] == M3P_P2L_ORPHAN);
    CHECK(runtime->lifecycle_instance_nonce != old_lifecycle &&
          runtime->m3p_instance_nonce != old_m3p && runtime->nfc_instance_nonce != old_nfc);
    CHECK(readback(runtime, &uid, expected));
    puts("SCALE_MEDIA_ORPHAN|sealed_no_reply=1|physical_replayed=1|prior_logical_data=1");
    sequence = fwlab_file_nand_v1_sequence(image.media);
    CHECK(force_gc(runtime));
    CHECK(fwlab_file_nand_v1_sequence(image.media) > sequence);
    CHECK(readback(runtime, &uid, expected));
    memset(patch, 0xc7, 1024);
    CHECK(command_run_flags(runtime, uid++, 1, 21, 2, patch, NULL, 1));
    memcpy(&expected[21u * 512u], patch, 1024);
    /* No Flush follows this FUA Write: its own durable witness must survive. */
    CHECK(readback(runtime, &uid, expected));
    CHECK(runtime_close(runtime));
    CHECK(media_close(&image));

    CHECK(media_open(&image, 0));
    CHECK(runtime_open(&image, J0_MEDIA_RECOVER, 303, &runtime));
    CHECK(readback(runtime, &uid, expected));
    puts("SCALE_MEDIA_FUA|self_witness=1|no_later_flush=1|recovered_readback=1");
    CHECK(runtime_close(runtime));
    CHECK(media_close(&image));
    CHECK(unlinkat(image.directory_fd, "nand.bin", 0) == 0);
    CHECK(close(image.directory_fd) == 0);
    CHECK(rmdir(directory) == 0);
    puts("SCALE_MEDIA_PASS|namespace_bytes=1048576|write_flush_read=1|rmw=1|reopens=3|sealed_orphan=1|live_gc=1|post_gc_write=1|fua=1|exclusive_open=1|holders_zero=1");
    return 1;
}

int main(void)
{
    return journey() ? EXIT_SUCCESS : EXIT_FAILURE;
}
