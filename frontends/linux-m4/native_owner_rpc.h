/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_NATIVE_OWNER_RPC_H
#define FWLAB_NATIVE_OWNER_RPC_H

#include "fwlab/contracts/owner_control_v0.h"

/* Local native-ABI control packet, never a guest or firmware data command. */
#define NATIVE_OWNER_RPC_VERSION 1u
enum native_owner_rpc_operation {
    NATIVE_OWNER_OBSERVE = 1, NATIVE_OWNER_REVOKE = 2,
    NATIVE_OWNER_REVOKE_QUERY = 3, NATIVE_OWNER_DRAIN = 4,
    NATIVE_OWNER_GRANT = 5, NATIVE_OWNER_GRANT_QUERY = 6
};

struct native_owner_packet {
    uint32_t version, size, operation;
    int32_t result;
    uint32_t budget;
    uint32_t reserved[3];
    struct fwlab_owner_stable_identity_v0 stable;
    struct fwlab_owner_epoch_state_v0 current;
    struct fwlab_owner_revoke_key_v0 revoke_key;
    struct fwlab_owner_revoke_status_v0 revoke_status;
    struct fwlab_owner_grant_key_v0 grant_key;
    struct fwlab_owner_grant_status_v0 grant_status;
    struct fwlab_owner_step_result_v0 step;
};

#endif /* FWLAB_NATIVE_OWNER_RPC_H */
