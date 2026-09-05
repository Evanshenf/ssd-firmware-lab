/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _GNU_SOURCE
#include "native_owner.h"
#include "native_owner_rpc.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

static void disconnect_peer(struct native_owner_server *server)
{
    if (server->peer >= 0)
        close(server->peer);
    server->peer = -1;
    server->disconnected = 1;
}

int native_owner_server_open(struct native_owner_server *server,
                             struct native_context *native,
                             struct native_media *media, const char *directory)
{
    struct sockaddr_un address;
    struct stat st;

    memset(server, 0, sizeof(*server));
    server->listener = server->peer = server->directory = -1;
    server->next_auto_uid = UINT64_C(0x8000000000000001);
    if (!native_owner_init(&server->owner, native, media))
        return 0;
    server->directory = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (server->directory < 0 || fstat(server->directory, &st) ||
        st.st_uid != geteuid() || (st.st_mode & 077))
        goto failed;
    if (!fstatat(server->directory, "owner.sock", &st, AT_SYMLINK_NOFOLLOW) || errno != ENOENT)
        goto failed;
    server->listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (server->listener < 0)
        goto failed;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "/proc/self/fd/%d/owner.sock", server->directory);
    if (bind(server->listener, (struct sockaddr *)&address, sizeof(address)) ||
        fchmodat(server->directory, "owner.sock", 0600, 0) ||
        fstatat(server->directory, "owner.sock", &st, AT_SYMLINK_NOFOLLOW))
        goto failed;
    server->socket_device = (uint64_t)st.st_dev;
    server->socket_inode = (uint64_t)st.st_ino;
    if (listen(server->listener, 1))
        goto failed;
    return 1;
failed:
    native_owner_server_close(server);
    return 0;
}

static int handle_request(struct native_owner_server *server)
{
    struct native_owner_packet request, response;
    struct native_owner *owner = &server->owner;
    const struct fwlab_owner_control_ops_v0 *ops = owner->port.ops;
    enum fwlab_spine_result_v0 result;
    ssize_t received = recv(server->peer, &request, sizeof(request), MSG_DONTWAIT | MSG_TRUNC);

    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return 1;
    if (received != sizeof(request) || request.version != NATIVE_OWNER_RPC_VERSION ||
        request.size != sizeof(request) || !j0_bytes_zero(request.reserved, sizeof(request.reserved))) {
        disconnect_peer(server);
        return 1;
    }
    memset(&response, 0, sizeof(response));
    response.version = NATIVE_OWNER_RPC_VERSION;
    response.size = (uint32_t)sizeof(response);
    response.operation = request.operation;
    switch (request.operation) {
    case NATIVE_OWNER_OBSERVE:
        result = ops->observe(owner, &response.current);
        break;
    case NATIVE_OWNER_REVOKE:
        result = ops->revoke_start(owner, &request.revoke_key, &response.revoke_status);
        break;
    case NATIVE_OWNER_REVOKE_QUERY:
        result = ops->revoke_query(owner, &request.revoke_key, &response.revoke_status);
        break;
    case NATIVE_OWNER_DRAIN:
        result = ops->drain_step(owner, &request.revoke_status.transition,
                                 request.budget, &response.step);
        break;
    case NATIVE_OWNER_GRANT:
        result = ops->grant_start(owner, &request.grant_key, &response.grant_status);
        break;
    case NATIVE_OWNER_GRANT_QUERY:
        result = ops->grant_query(owner, &request.grant_key, &response.grant_status);
        break;
    case NATIVE_OWNER_CANARY:
        response.canary = request.canary;
        result = native_canary_control(owner->native, &response.canary) == 0
                     ? FWLAB_SPINE_V0_OK : FWLAB_SPINE_V0_INVALID;
        break;
    default:
        result = FWLAB_SPINE_V0_INVALID;
        break;
    }
    response.result = (int32_t)result;
    response.stable = owner->port.stable;
    if (request.operation != NATIVE_OWNER_OBSERVE)
        (void)ops->observe(owner, &response.current);
    if (request.operation == NATIVE_OWNER_OBSERVE && owner->revoke_known)
        response.revoke_status = owner->revoke_status;
    if (send(server->peer, &response, sizeof(response), MSG_DONTWAIT | MSG_NOSIGNAL) != sizeof(response))
        disconnect_peer(server);
    return 1;
}

static int drain_disconnected(struct native_owner_server *server)
{
    struct native_owner *owner = &server->owner;
    struct fwlab_owner_epoch_state_v0 current;
    struct fwlab_owner_revoke_key_v0 key;
    struct fwlab_owner_revoke_status_v0 status;
    enum fwlab_spine_result_v0 result;

    if (!server->disconnected || owner->revoke_pending || owner->grant_pending)
        return 1;
    result = owner->port.ops->observe(owner, &current);
    if (result == FWLAB_SPINE_V0_IN_PROGRESS)
        return 1;
    if (result != FWLAB_SPINE_V0_OK)
        return 0;
    if (current.phase == FWLAB_OWNER_V0_NO_OWNER && !owner->native->runtime) {
        server->disconnected = 0;
        printf("OWNER_PEER_CLOSED owner_epoch=%" PRIu64 " phase=NO_OWNER\n", current.owner_epoch);
        return 1;
    }
    if (current.phase != FWLAB_OWNER_V0_OWNED)
        return current.phase != FWLAB_OWNER_V0_QUARANTINED;
    if (!owner->native->runtime || owner->native->epoch != current.execution_epoch ||
        server->next_auto_uid == UINT64_MAX)
        return 0;
    memset(&key, 0, sizeof(key));
    key.version = FWLAB_OWNER_CONTROL_V0_VERSION;
    key.size = (uint16_t)sizeof(key);
    key.expected_owner = current;
    key.client_uid = server->next_auto_uid++;
    key.policy = FWLAB_OWNER_V0_DRAIN_ONLY;
    result = owner->port.ops->revoke_start(owner, &key, &status);
    if (result == FWLAB_SPINE_V0_OK)
        printf("OWNER_PEER_REVOKE transition=%" PRIu64 " owner_epoch=%" PRIu64 "\n",
               status.transition.transition_uid, status.current.owner_epoch);
    return result == FWLAB_SPINE_V0_OK || result == FWLAB_SPINE_V0_IN_PROGRESS ||
           result == FWLAB_SPINE_V0_STALE;
}

int native_owner_server_poll(struct native_owner_server *server)
{
    int peer;
    struct ucred credentials;
    socklen_t length = sizeof(credentials);
    enum fwlab_spine_result_v0 result;

    peer = accept4(server->listener, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (peer >= 0) {
        if (server->peer >= 0 || server->disconnected ||
            getsockopt(peer, SOL_SOCKET, SO_PEERCRED, &credentials, &length) ||
            length != sizeof(credentials) || credentials.uid != geteuid()) {
            close(peer);
        } else {
            server->peer = peer;
        }
    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        return 0;
    }
    if (server->peer >= 0 && !handle_request(server))
        return 0;
    result = native_owner_service(&server->owner);
    if (result != FWLAB_SPINE_V0_OK && result != FWLAB_SPINE_V0_IN_PROGRESS &&
        result != FWLAB_SPINE_V0_STALE)
        return 0;
    return drain_disconnected(server);
}

void native_owner_server_close(struct native_owner_server *server)
{
    struct stat st;

    if (server->peer >= 0) close(server->peer);
    if (server->listener >= 0) close(server->listener);
    if (server->directory >= 0) {
        if (server->socket_inode &&
            !fstatat(server->directory, "owner.sock", &st, AT_SYMLINK_NOFOLLOW) &&
            S_ISSOCK(st.st_mode) && (uint64_t)st.st_dev == server->socket_device &&
            (uint64_t)st.st_ino == server->socket_inode)
            (void)unlinkat(server->directory, "owner.sock", 0);
        close(server->directory);
    }
    server->listener = server->peer = server->directory = -1;
}
