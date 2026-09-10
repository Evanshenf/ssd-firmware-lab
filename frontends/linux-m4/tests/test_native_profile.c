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

static struct fwlab_nvme_command mq_command(uint64_t uid, uint8_t opcode,
                                            uint32_t dword10, uint32_t dword11)
{
    struct fwlab_nvme_command c = {0};
    c.version = FWLAB_NVME_COMMAND_VERSION; c.size = sizeof(c);
    c.handle.instance_nonce = 0x1000; c.handle.command_uid = uid;
    c.handle.controller_epoch = 5; c.handle.generation = 2;
    c.origin.word[0] = 0xa000 + uid; c.origin.word[1] = 0xb000 + uid;
    c.trace_cookie = 0xc000 + uid; c.safety_generation = 9;
    c.opcode = opcode; c.queue_class = FWLAB_NVME_QUEUE_ADMIN;
    c.data_pointer_format = FWLAB_NVME_DATA_POINTER_PRP;
    c.data_address_present = opcode == 5 || opcode == 1;
    c.command_dword10_15[0] = dword10; c.command_dword10_15[1] = dword11;
    return c;
}

static struct fwlab_host_action_status_v0 mq_success(
    struct fwlab_spine_profile_binding_v0 *binding,
    const struct fwlab_host_action_program_v0 *program, uint32_t value)
{
    struct fwlab_host_action_status_v0 s = {0};
    struct fwlab_nvme_completion_intent intent;
    CHECK(program->action_count == 1);
    s.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION; s.size = sizeof(s);
    s.token.version = FWLAB_HOST_ACTION_PROGRAM_V0_VERSION; s.token.size = sizeof(s.token);
    s.token.type_tag = FWLAB_HOST_ACTION_TOKEN_V0_TAG;
    s.token.command = program->command; s.token.origin = program->origin;
    s.token.action_uid = 100 + program->command.command_uid;
    s.token.generation = 1; s.token.kind = program->action[0].kind;
    s.state = FWLAB_HOST_ACTION_V0_STATE_TERMINAL;
    s.terminal_kind = FWLAB_HOST_ACTION_V0_SUCCEEDED;
    s.produced_witness_mask = FWLAB_HOST_WITNESS_V0_QUEUE_EFFECT;
    s.effect = FWLAB_HOST_ACTION_V0_EFFECT_FULL; s.units_completed = 1;
    CHECK(fwlab_host_action_status_v0_valid(&s));
    CHECK(binding->result_latch(binding->adapter.context, &program->action[0].argument,
          &s, FWLAB_SPINE_PROVIDER_V0_SUCCESS, value) == FWLAB_SPINE_V0_OK);
    CHECK(binding->adapter.ops->complete(binding->adapter.context, program, &s, 1,
          &intent) == FWLAB_SPINE_V0_OK);
    CHECK(!intent.status_code && intent.result_dword0 == value);
    return s;
}

static void mq2_policy_checks(void)
{
    struct fwlab_host_profile_adapter_v0 adapter;
    struct fwlab_spine_profile_binding_v0 binding;
    struct fwlab_linux_profile_limits limits = fwlab_linux_profile_mq2_limits();
    struct fwlab_block_volume_desc_v0 volume = {0};
    struct fwlab_host_action_program_v0 p[8], repeated;
    struct fwlab_spine_profile_argument_v0 argument;
    struct fwlab_host_action_status_v0 first;
    struct fwlab_nvme_command c[8];
    void *arena = calloc(1, fwlab_linux_profile_v1_adapter_arena_size());
    CHECK(arena);
    volume.version = FWLAB_BLOCK_VOLUME_V0_VERSION; volume.size = sizeof(volume);
    volume.namespace_ref.word[0] = 1; volume.lba_count = 131072; volume.lba_bytes = 512;
    CHECK(fwlab_linux_profile_v1_adapter_init_limits(arena,
          fwlab_linux_profile_v1_adapter_arena_size(), 0x7100, 12, 1, &volume,
          &limits, &adapter) == FWLAB_SPINE_V0_OK);
    CHECK(fwlab_linux_profile_v1_binding_v0(&adapter, FWLAB_SPINE_ROLE_V0_NORMAL,
          &binding) == FWLAB_SPINE_V0_OK);
    c[0] = mq_command(1, 9, 7, 0x00070007);
    c[1] = mq_command(2, 5, 0x001f0002, 0x00020003);
    c[2] = mq_command(3, 1, 0x001f0002, 0x00020001);
    c[3] = mq_command(4, 4, 2, 0); /* CQ2 still has SQ2: policy rejection. */
    c[4] = mq_command(5, 0, 2, 0);
    c[5] = mq_command(6, 4, 2, 0);
    c[6] = mq_command(7, 9, 7, 0x00010000); /* SQ1/CQ2 requested: paired grant1. */
    c[7] = mq_command(8, 5, 0x001f0002, 0x00020003);
    for (unsigned i = 0; i < 8; ++i) {
        CHECK(adapter.ops->plan(adapter.context, &c[i], &p[i]) == FWLAB_SPINE_V0_OK);
        if (i == 3 || i == 7) { CHECK(!p[i].action_count); continue; }
        CHECK(binding.argument_read(adapter.context, &p[i].action[0].argument,
              &argument) == FWLAB_SPINE_V0_OK);
        if (i == 0) CHECK(argument.requested_sq_count == 8 && argument.requested_cq_count == 8);
        if (i == 1) CHECK(argument.queue_id == 2 && argument.interrupt_vector == 2);
        if (i == 2) CHECK(argument.queue_id == 2 && argument.associated_queue_id == 2);
        if (i == 6) CHECK(argument.requested_sq_count == 1 && argument.requested_cq_count == 2);
        if (!i) first = mq_success(&binding, &p[i], 0x00010001);
        else (void)mq_success(&binding, &p[i], 0);
        CHECK(adapter.ops->plan(adapter.context, &c[i], &repeated) == FWLAB_SPINE_V0_OK);
        CHECK(!memcmp(&p[i], &repeated, sizeof(repeated)));
    }
    /* Old NoQ replay must not undo the later one-pair grant. */
    CHECK(adapter.ops->plan(adapter.context, &c[0], &repeated) == FWLAB_SPINE_V0_OK);
    CHECK(!memcmp(&p[0], &repeated, sizeof(repeated)));
    CHECK(binding.result_latch(adapter.context, &p[0].action[0].argument, &first,
          FWLAB_SPINE_PROVIDER_V0_SUCCESS, 0x00010001) == FWLAB_SPINE_V0_OK);
    CHECK(binding.result_latch(adapter.context, &p[0].action[0].argument, &first,
          FWLAB_SPINE_PROVIDER_V0_SUCCESS, 0) == FWLAB_SPINE_V0_POISONED);
    c[7].handle.command_uid = 9; c[7].origin.word[1]++;
    CHECK(adapter.ops->plan(adapter.context, &c[7], &repeated) == FWLAB_SPINE_V0_OK);
    CHECK(!repeated.action_count);
    CHECK(adapter.ops->retire(adapter.context, &repeated) == FWLAB_SPINE_V0_OK);
    for (unsigned i = 0; i < 8; ++i)
        CHECK(adapter.ops->retire(adapter.context, &p[i]) == FWLAB_SPINE_V0_OK);
    free(arena);
    puts("MQ2_PROFILE_ADJACENT_PASS|NoQ_DW0=1|snapshot_retry=1|asymmetric_counts=1|paired_queue2=1|fake_queue_effects_not_kernel=1");
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
    bad = request; bad.host_profile_id = FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL;
    bad.limits = fwlab_m4_host_limits_for(bad.host_profile_id);
    CHECK(fwlab_m4_attach_profile_request_valid(&bad));
    CHECK(bad.limits.io_queue_pairs == 2 && bad.limits.vectors == 3 && bad.limits.io_ingress == 1);
    graph_checks(); frame_checks(); mq2_policy_checks();
    puts("NATIVE_PROFILE_CHECK_PASS|wire160=1|actual_PRP_parser=1|actual_class_buffer_owner=1|not_kernel_DMA_or_native_proof=1");
    return 0;
}
