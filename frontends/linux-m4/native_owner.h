/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_NATIVE_OWNER_H
#define FWLAB_NATIVE_OWNER_H

#include "native_internal.h"
#include "fwlab/contracts/owner_control_v0.h"
#include "fwlab/unstable/m4_owner_native.h"

struct native_owner {
    struct native_context *native;
    struct native_media *media;
    struct fwlab_owner_control_port_v0 port;
    struct fwlab_owner_revoke_key_v0 revoke_key;
    struct fwlab_owner_revoke_status_v0 revoke_status;
    struct fwlab_owner_grant_key_v0 grant_key;
    struct fwlab_owner_grant_status_v0 grant_status;
    uint64_t expected_ftl_nonce;
    uint64_t expected_nfc_nonce;
    uint8_t revoke_pending;
    uint8_t revoke_known;
    uint8_t grant_pending;
    uint8_t grant_known;
    uint8_t quarantined;
};

struct native_owner_server {
    struct native_owner owner;
    int listener;
    int peer;
    int directory;
    uint64_t socket_device;
    uint64_t socket_inode;
    uint64_t next_auto_uid;
    uint8_t disconnected;
};

int native_owner_init(struct native_owner *owner, struct native_context *native,
                      struct native_media *media);
enum fwlab_spine_result_v0 native_owner_service(struct native_owner *owner);
int native_owner_blocks_commands(const struct native_owner *owner);
int native_owner_server_open(struct native_owner_server *server,
                             struct native_context *native,
                             struct native_media *media, const char *directory);
int native_owner_server_poll(struct native_owner_server *server);
void native_owner_server_close(struct native_owner_server *server);

#endif /* FWLAB_NATIVE_OWNER_H */
