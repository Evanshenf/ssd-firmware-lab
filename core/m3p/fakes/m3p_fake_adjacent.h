/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_M3P_FAKE_ADJACENT_H
#define FWLAB_M3P_FAKE_ADJACENT_H

#include "../m3p.h"
#include "../../../media/file-nand-v0/file_nand.h"

#include <stddef.h>
#include <stdint.h>

#define M3P_FAKE_BUFFER_BYTES 8192u

struct m3p_fake_controller_buffer {
    uint8_t bytes[M3P_FAKE_BUFFER_BYTES];
    struct fwlab_controller_buffer_lease_v0 lease;
    uint64_t issuer_nonce;
    uint32_t generation;
    uint8_t acquired;
    uint8_t closed;
};

void m3p_fake_controller_buffer_init(
    struct m3p_fake_controller_buffer *buffer,
    uint64_t issuer_nonce,
    uint32_t generation
);
struct fwlab_controller_buffer_port_v0 m3p_fake_controller_buffer_port(
    struct m3p_fake_controller_buffer *buffer
);

struct m3p_fake_file_substrate {
    uint8_t *bytes;
    size_t capacity;
    uint64_t size;
    uint64_t device;
    uint64_t inode;
    uint32_t writes;
    uint32_t barriers;
    uint32_t page_candidates;
    uint32_t health_candidates;
    uint32_t wal_begin;
    uint32_t wal_applied;
    uint32_t wal_commit;
    uint32_t wal_rollback;
    uint32_t fail_write;
    uint32_t fail_barrier;
    uint8_t closed;
};

void m3p_fake_file_substrate_init(
    struct m3p_fake_file_substrate *substrate,
    uint8_t *bytes,
    size_t capacity,
    uint64_t device,
    uint64_t inode
);
enum fwlab_nfc_api_result m3p_fake_file_format(
    void *arena,
    size_t arena_size,
    struct m3p_fake_file_substrate *substrate,
    const uint8_t media_uuid[16],
    struct fwlab_file_nand_v0 **media,
    struct fwlab_file_nand_holder_v0 *holder
);
enum fwlab_nfc_api_result m3p_fake_file_restart(
    void *arena,
    size_t arena_size,
    struct m3p_fake_file_substrate *substrate,
    const struct fwlab_file_nand_holder_v0 *holder,
    struct fwlab_file_nand_v0 **media
);
int m3p_fake_file_corrupt(
    struct m3p_fake_file_substrate *substrate,
    uint64_t offset,
    uint8_t mask
);

struct m3p_fake_nfc {
    struct fwlab_nfc_buffer_provider buffers;
    struct fwlab_nand_media media;
    struct fwlab_nfc_request request;
    struct fwlab_nfc_completion completion;
    uint8_t main[4096];
    uint8_t oob[128];
    uint64_t instance_nonce;
    uint32_t epoch;
    uint32_t cache_generation;
    uint32_t submissions;
    uint32_t steps;
    uint32_t polls;
    uint32_t resets;
    uint8_t pending;
    uint8_t event_ready;
};

void m3p_fake_nfc_init(
    struct m3p_fake_nfc *fake,
    struct fwlab_nfc_buffer_provider buffers,
    struct fwlab_nand_media media,
    uint64_t instance_nonce,
    uint32_t epoch
);
struct fwlab_nfc_provider m3p_fake_nfc_provider(struct m3p_fake_nfc *fake);

#endif /* FWLAB_M3P_FAKE_ADJACENT_H */
