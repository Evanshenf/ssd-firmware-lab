/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#include "../native_internal.h"
#include "../../../kernel/m4-native/m4_attach_identity.h"
#include "../../../kernel/m4-native/m4_prp_graph.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "PROFILE_CHECK %d %s\n", __LINE__, #x); exit(1); } } while (0)
_Static_assert(sizeof(struct fwlab_m4_attach_profile_message) == 160, "profile wire size");
_Static_assert(offsetof(struct fwlab_m4_attach_profile_message, limits) == 88, "profile limits offset");
_Static_assert(offsetof(struct fwlab_m4_attach_profile_message, reserved) == 128, "profile reserved offset");

struct graph_fixture {
    unsigned char lists[8192], scratch[4096];
    unsigned count, reads;
    __u32 total, last_bytes;
};

static int read_list(void *opaque, __u64 address, __u32 bytes, void *output)
{
    struct graph_fixture *f = opaque;
    if (address < 0x400000 || address > 0x402000 || bytes > 0x402000 - address)
        return -EINVAL;
    ++f->reads;
    memcpy(output, f->lists + (size_t)(address - 0x400000), bytes);
    return 0;
}

static int capture(void *opaque, __u64 address, __u32 bytes)
{
    struct graph_fixture *f = opaque;
    CHECK(address && bytes && bytes <= 4096);
    ++f->count; f->total += bytes; f->last_bytes = bytes;
    return 0; /* Snapshot-only fake; no DMA, mapping identity or kernel proof. */
}

static void put64(unsigned char *at, __u64 value)
{
    for (unsigned i = 0; i < 8; ++i) at[i] = (unsigned char)(value >> (i * 8));
}

static void graph_checks(void)
{
    struct graph_fixture *f = calloc(1, sizeof(*f));
    struct fwlab_m4_host_limits limits = fwlab_m4_host_limits_for(FWLAB_M4_HOST_PROFILE_LARGE_SERIAL);
    struct fwlab_m4_prp_walk walk = { .context = f, .read_list = read_list, .capture = capture };
    CHECK(f);
    walk.scratch = f->scratch; walk.scratch_bytes = sizeof(f->scratch);
    CHECK(fwlab_m4_prp_build(&walk, &limits, 0x100000, 0, 4096) == 0);
    CHECK(f->count == 1 && f->total == 4096 && !f->reads);
    memset(f, 0, sizeof(*f));
    CHECK(fwlab_m4_prp_build(&walk, &limits, 0x100200, 0x200000, 4096) == 0);
    CHECK(f->count == 2 && f->last_bytes == 512 && !f->reads);
    for (unsigned chained = 0; chained < 2; ++chained) {
        memset(f, 0, sizeof(*f));
        for (unsigned i = 0; i < 256; ++i)
            put64(f->lists + (chained ? 4096 : 0) + i * 8, 0x200000 + (__u64)i * 4096);
        if (chained) put64(f->lists + 4088, 0x401000);
        CHECK(fwlab_m4_prp_build(&walk, &limits, 0x100200,
              chained ? 0x400ff8 : 0x400000, 1048576) == 0);
        CHECK(f->count == 257 && f->total == 1048576 && f->last_bytes == 512);
        CHECK(walk.list_pages == chained + 1 && f->reads == chained + 1);
    }
    put64(f->lists + 4096 + 255 * 8, 0);
    f->count = f->reads = f->total = 0;
    CHECK(fwlab_m4_prp_build(&walk, &limits, 0x100200, 0x400ff8, 1048576) == -EINVAL);
    CHECK(f->count == 256); /* No accepted graph; capture callback has no data effect. */
    f->count = 0;
    CHECK(fwlab_m4_prp_build(&walk, &limits, 0x100002, 0, 4096) == -EINVAL);
    CHECK(!f->count);
    free(f);
}

static void frame_checks(void)
{
    struct j0_controller_buffer *buffer = calloc(1, sizeof(*buffer));
    struct fwlab_controller_buffer_acquire_v0 request = { 0 }, control;
    struct fwlab_controller_buffer_lease_v0 io_lease, control_lease;
    struct fwlab_controller_buffer_span_v0 span = { 0 };
    unsigned char input[16], output[16];
    uint32_t active;
    uint8_t quiet;
    CHECK(buffer);
    j0_controller_buffer_init(buffer, 42, 1);
    CHECK(j0_controller_buffer_large_init(buffer) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    request.version = FWLAB_CONTROLLER_BUFFER_V0_VERSION; request.size = sizeof(request);
    request.command.instance_nonce = 7; request.command.command_uid = 1;
    request.command.controller_epoch = request.command.generation = 1;
    request.origin.word[0] = 0x71; request.origin.word[1] = 0x91;
    request.client_uid = 1; request.execution_epoch = 1;
    request.capacity_bytes = 1048576; request.rights = FWLAB_CONTROLLER_BUFFER_RIGHT_V0_ALL;
    CHECK(buffer->port.ops->acquire(buffer, &request, &io_lease) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    control = request; ++control.command.command_uid; ++control.origin.word[1]; ++control.client_uid;
    control.capacity_bytes = 512;
    CHECK(buffer->port.ops->acquire(buffer, &control, &control_lease) == FWLAB_CONTROLLER_BUFFER_V0_NO_CAPACITY);
    control.capacity_bytes = 4096;
    CHECK(j0_controller_buffer_acquire_class(buffer, &control, &control_lease,
          J0_BUFFER_CLASS_CONTROL) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    CHECK(buffer->active_leases == 2 && buffer->frame_held[0] && buffer->frame_held[1]);
    CHECK(!j0_controller_buffer_storage_fini(buffer));
    span.version = FWLAB_CONTROLLER_BUFFER_V0_VERSION; span.size = sizeof(span);
    span.offset = 1048576 - 16; span.length = 16;
    memset(input, 0x5a, sizeof(input));
    CHECK(buffer->port.ops->write(buffer, &io_lease, &span, input, 16) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    CHECK(buffer->port.ops->read(buffer, &io_lease, &span, output, 16) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    CHECK(!memcmp(input, output, 16));
    CHECK(buffer->port.ops->release(buffer, &io_lease) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    CHECK(buffer->port.ops->release(buffer, &control_lease) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    CHECK(buffer->port.ops->epoch_close(buffer, 7, 1) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    CHECK(buffer->port.ops->epoch_quiescent(buffer, 7, 1, &active, &quiet) == FWLAB_CONTROLLER_BUFFER_V0_OK);
    CHECK(!active && quiet && j0_controller_buffer_storage_fini(buffer));
    free(buffer);
}

int main(void)
{
    struct fwlab_m4_attach_profile_message request = { 0 }, bad;
    request.version = FWLAB_M4_ATTACH_PROFILE_VERSION; request.size = sizeof(request);
    request.host_profile_id = FWLAB_M4_HOST_PROFILE_LARGE_SERIAL;
    request.producer_mode = FWLAB_M4_PRODUCER_PUMP;
    request.limits = fwlab_m4_host_limits_for(request.host_profile_id);
    CHECK(fwlab_m4_attach_profile_request_valid(&request));
    CHECK(FWLAB_M4_ATTACH_PROFILE != FWLAB_M4_ATTACH_MODE && FWLAB_M4_ATTACH_PROFILE != FWLAB_M4_ATTACH_IDENTITY);
    bad = request; bad.size = 128; CHECK(!fwlab_m4_attach_profile_request_valid(&bad));
    bad = request; --bad.limits.max_io_bytes; CHECK(!fwlab_m4_attach_profile_request_valid(&bad));
    bad = request; bad.reserved[3] = 1; CHECK(!fwlab_m4_attach_profile_request_valid(&bad));
    graph_checks(); frame_checks();
    puts("NATIVE_PROFILE_CHECK_PASS|wire160=1|actual_PRP_parser=1|actual_class_buffer_owner=1|not_kernel_DMA_or_native_proof=1");
    return 0;
}
