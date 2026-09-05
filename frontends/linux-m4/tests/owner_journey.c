/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _GNU_SOURCE
#include "../native_owner_rpc.h"
#include "vfio_epoch.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int packet_call(int fd, struct native_owner_packet *packet)
{
    uint32_t operation = packet->operation;
    packet->version = NATIVE_OWNER_RPC_VERSION;
    packet->size = (uint32_t)sizeof(*packet);
    packet->result = INT32_MIN;
    if (send(fd, packet, sizeof(*packet), MSG_NOSIGNAL) != sizeof(*packet) ||
        recv(fd, packet, sizeof(*packet), MSG_TRUNC) != sizeof(*packet) ||
        packet->version != NATIVE_OWNER_RPC_VERSION || packet->size != sizeof(*packet) ||
        packet->operation != operation)
        return -1;
    return packet->result;
}

static int sysfs_write(const char *bdf, const char *entry, const char *value)
{
    char path[PATH_MAX];
    int fd, result;
    size_t length = strlen(value);

    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/%s", bdf, entry);
    fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    result = write(fd, value, length) == (ssize_t)length;
    close(fd);
    return result;
}

static int bind_driver(const char *bdf, const char *driver)
{
    int fd, result;
    if (!sysfs_write(bdf, "driver_override", driver)) return 0;
    fd = open("/sys/bus/pci/drivers_probe", O_WRONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    result = write(fd, bdf, strlen(bdf)) == (ssize_t)strlen(bdf);
    close(fd);
    return result;
}

static int native_bind(const char *bdf) { return bind_driver(bdf, "nvme"); }

static int namespace_find(const char *bdf, char path[64])
{
    char directory[128];
    unsigned iteration;
    snprintf(directory, sizeof(directory), "/sys/bus/pci/devices/%s/nvme", bdf);
    for (iteration = 0; iteration < 200; ++iteration) {
        DIR *dir = opendir(directory);
        struct dirent *entry;
        if (dir) {
            while ((entry = readdir(dir))) {
                struct stat st;
                unsigned controller;
                int used = 0;
                if (sscanf(entry->d_name, "nvme%u%n", &controller, &used) != 1 || entry->d_name[used])
                    continue;
                snprintf(path, 64, "/dev/nvme%un1", controller);
                if (!stat(path, &st) && S_ISBLK(st.st_mode)) {
                    closedir(dir);
                    return 1;
                }
            }
            closedir(dir);
        }
        usleep(10000);
    }
    return 0;
}

static int run_native_io(const char *mode, const char *device, const char *bdf, int control)
{
    pid_t child = fork();
    int status;
    if (child < 0) return 0;
    if (!child) {
        close(control);
        execl("/proc/self/exe", "j1_native_io", mode, device, bdf, (char *)NULL);
        _exit(127);
    }
    return waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status);
}

static int revoke_and_drain(int fd, uint64_t uid, struct native_owner_packet *observed,
                            struct fwlab_owner_revoke_status_v0 *status)
{
    struct native_owner_packet packet = { 0 };
    struct fwlab_owner_revoke_key_v0 key = { 0 };
    unsigned iteration;

    packet.operation = NATIVE_OWNER_OBSERVE;
    if (packet_call(fd, &packet) || packet.current.phase != FWLAB_OWNER_V0_OWNED)
        return 0;
    *observed = packet;
    key.version = FWLAB_OWNER_CONTROL_V0_VERSION;
    key.size = (uint16_t)sizeof(key);
    key.expected_owner = packet.current;
    key.client_uid = uid;
    key.policy = FWLAB_OWNER_V0_DRAIN_ONLY;
    memset(&packet, 0, sizeof(packet));
    packet.operation = NATIVE_OWNER_REVOKE;
    packet.revoke_key = key;
    if (packet_call(fd, &packet)) return 0;
    for (iteration = 0; iteration < 4000; ++iteration) {
        memset(&packet, 0, sizeof(packet));
        packet.operation = NATIVE_OWNER_REVOKE_QUERY;
        packet.revoke_key = key;
        if (packet_call(fd, &packet)) return 0;
        if (packet.revoke_status.certificate_valid) {
            *status = packet.revoke_status;
            if (status->current.phase != FWLAB_OWNER_V0_NO_OWNER ||
                status->current.owner_kind || status->current.controller_epoch ||
                status->current.execution_epoch ||
                status->transition.no_owner_epoch != key.expected_owner.owner_epoch + 1)
                return 0;
            printf("OWNER_ZERO owner_epoch=%" PRIu64 " transition=%" PRIu64 " certificate=%" PRIu64 "\n",
                   status->current.owner_epoch, status->transition.transition_uid,
                   status->certificate.certificate_uid);
            return 1;
        }
        usleep(1000);
    }
    return 0;
}

static uint64_t client_uid(void);
static int grant_owner(int fd, uint8_t target,
                        const struct fwlab_owner_revoke_status_v0 *revoked,
                        const struct fwlab_owner_stable_identity_v0 *stable);

int native_owner_host_journey(const char *directory, const char *bdf, int budget)
{
    struct sockaddr_un address;
    struct native_owner_packet before, packet;
    struct fwlab_owner_revoke_status_v0 revoked;
    struct fwlab_owner_grant_key_v0 grant = { 0 };
    char pci_path[128], resolved[PATH_MAX], device[64];
    int fd = -1, result = 1, used = 0;
    unsigned domain, bus, slot, function;

    if (sscanf(bdf, "%4x:%2x:%2x.%1x%n", &domain, &bus, &slot, &function, &used) != 4 ||
        bdf[used] || domain < 0x7000 || domain > 0x7fff || bus || slot || function)
        return 1;
    snprintf(pci_path, sizeof(pci_path), "/sys/bus/pci/devices/%s", bdf);
    if (!realpath(pci_path, resolved) || !strstr(resolved, "/ssd_fwlab_native_pci/"))
        return 1;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s/owner.sock", directory) >=
        (int)sizeof(address.sun_path)) return 1;
    fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&address, sizeof(address))) goto done;
    memset(&packet, 0, sizeof(packet));
    packet.operation = NATIVE_OWNER_OBSERVE;
    if (packet_call(fd, &packet)) goto done;
    if (packet.current.phase == FWLAB_OWNER_V0_NO_OWNER) {
        if (!packet.revoke_status.certificate_valid ||
            !grant_owner(fd, FWLAB_OWNER_V0_HOST_NATIVE, &packet.revoke_status, &packet.stable) ||
            !native_bind(bdf)) goto done;
    } else if (packet.current.phase != FWLAB_OWNER_V0_OWNED ||
               packet.current.owner_kind != FWLAB_OWNER_V0_HOST_NATIVE) goto done;
    if (!namespace_find(bdf, device) || !run_native_io("write", device, bdf, fd) ||
        !run_native_io("cut1", device, bdf, fd) || !run_native_io("cut2", device, bdf, fd) ||
        !run_native_io("cut3", device, bdf, fd) || !run_native_io("pba", device, bdf, fd)) goto done;
    if (!sysfs_write(bdf, "driver/unbind", bdf)) goto done;
    if (!revoke_and_drain(fd, client_uid(), &before, &revoked)) goto done;
    grant.version = FWLAB_OWNER_CONTROL_V0_VERSION;
    grant.size = (uint16_t)sizeof(grant);
    grant.transition = revoked.transition;
    grant.certificate = revoked.certificate;
    grant.client_uid = client_uid();
    grant.target_owner = FWLAB_OWNER_V0_HOST_NATIVE;
    memset(&packet, 0, sizeof(packet));
    packet.operation = NATIVE_OWNER_GRANT;
    packet.grant_key = grant;
    if (packet_call(fd, &packet)) goto done;
    if (packet.grant_status.current.owner_epoch != revoked.transition.no_owner_epoch ||
        packet.grant_status.current.controller_epoch != revoked.transition.old_controller_epoch + 1 ||
        packet.grant_status.current.execution_epoch != revoked.transition.old_execution_epoch + 1 ||
        packet.grant_status.current.function_instance_nonce != before.stable.function_instance_nonce ||
        memcmp(packet.stable.media_uuid, before.stable.media_uuid, 16) ||
        memcmp(packet.stable.binding_manifest_sha256, before.stable.binding_manifest_sha256, 32))
        goto done;
    if (!native_bind(bdf) || !namespace_find(bdf, device) ||
        !run_native_io("verify", device, bdf, fd) ||
        (budget && !run_native_io("budget", device, bdf, fd))) goto done;
    if (!sysfs_write(bdf, "driver/unbind", bdf) || !revoke_and_drain(fd, client_uid(), &before, &revoked))
        goto done;
    puts("OWNER_HOST_PATH_PASS same_function=1 same_media=1 cleanup=NO_OWNER guest=not_connected");
    result = 0;
done:
    if (result) fprintf(stderr, "owner Host path failed errno=%d\n", errno);
    if (fd >= 0) close(fd);
    return result;
}

static uint64_t client_uid(void)
{
    static uint64_t value;
    struct timespec now;
    if (!value) {
        if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
        value = (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
    }
    return ++value;
}

static int grant_owner(int fd, uint8_t target,
                        const struct fwlab_owner_revoke_status_v0 *revoked,
                        const struct fwlab_owner_stable_identity_v0 *stable)
{
    struct native_owner_packet packet = { 0 };
    struct fwlab_owner_grant_key_v0 key = { 0 };
    struct fwlab_owner_grant_status_v0 first;

    key.version = FWLAB_OWNER_CONTROL_V0_VERSION;
    key.size = (uint16_t)sizeof(key);
    key.transition = revoked->transition;
    key.certificate = revoked->certificate;
    key.client_uid = client_uid();
    key.target_owner = target;
    packet.operation = NATIVE_OWNER_GRANT;
    packet.grant_key = key;
    if (packet_call(fd, &packet)) return 0;
    first = packet.grant_status;
    if (first.current.owner_kind != target || first.current.phase != FWLAB_OWNER_V0_OWNED ||
        first.current.owner_epoch != key.transition.no_owner_epoch ||
        first.current.controller_epoch != key.transition.old_controller_epoch + 1 ||
        first.current.execution_epoch != key.transition.old_execution_epoch + 1 ||
        memcmp(&packet.stable, stable, sizeof(*stable))) return 0;
    memset(&packet, 0, sizeof(packet));
    packet.operation = NATIVE_OWNER_GRANT_QUERY;
    packet.grant_key = key;
    if (packet_call(fd, &packet) || memcmp(&packet.grant_status, &first, sizeof(first))) return 0;
    printf("OWNER_GRANT kind=%u owner_epoch=%" PRIu64 " controller_epoch=%u retry=exact\n",
           target, first.current.owner_epoch, first.current.controller_epoch);
    return 1;
}

static int qmp_control(int control, const char *script, const char *socket_path, const char *mode)
{
    pid_t child = fork();
    int status;
    if (child < 0) return 0;
    if (!child) {
        close(control);
        execl("/usr/bin/python3", "python3", script, socket_path, mode, (char *)NULL);
        _exit(127);
    }
    return waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status);
}

static int guest_log_passed(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[1024];
    int a = 0, b = 0, complete = 0, bad = 0;
    if (!file) return 0;
    while (fgets(line, sizeof(line), file)) {
        a |= strstr(line, "NATIVE_GUEST_A_READ_OK") != NULL;
        b |= strstr(line, "NATIVE_GUEST_AB_PASS") != NULL;
        complete |= strstr(line, "J2_L2_OWNER_PASS") != NULL;
        bad |= strstr(line, "J2_L2_PREFLIGHT_FAIL") != NULL || strstr(line, "Kernel panic") != NULL ||
               strstr(line, "BUG:") != NULL || strstr(line, "Oops:") != NULL;
    }
    if (ferror(file)) bad = 1;
    fclose(file);
    return a && b && complete && !bad;
}

static int guest_log_held(const char *path)
{
    FILE *file = fopen(path, "r");
    char line[1024];
    int a = 0, held = 0;
    if (!file) return 0;
    while (fgets(line, sizeof(line), file)) {
        a |= strstr(line, "NATIVE_GUEST_A_READ_OK") != NULL;
        held |= strstr(line, "NATIVE_GUEST_HOLD") != NULL;
    }
    fclose(file);
    return a && held;
}

static int owner_qemu_run(const char *directory, const char *bdf,
                          const char *kernel, const char *initrd, const char *workdir,
                          unsigned cut, int inherited_control)
{
    struct sockaddr_un address;
    struct native_owner_packet packet, observed;
    struct fwlab_owner_stable_identity_v0 stable;
    struct fwlab_owner_revoke_status_v0 revoked;
    char pci_path[128], resolved[PATH_MAX], device[64], run[PATH_MAX];
    char qmp_path[PATH_MAX], qmp_option[PATH_MAX + 32], log[PATH_MAX], script[PATH_MAX], vfio[128];
    unsigned domain, bus, slot, function, iteration;
    int used = 0, control = inherited_control, output = -1, result = 1, guest_status = 0;
    int vfio_bound = 0;
    pid_t guest = -1;
    struct stat st;

    setvbuf(stdout, NULL, _IOLBF, 0);
    if (sscanf(bdf, "%4x:%2x:%2x.%1x%n", &domain, &bus, &slot, &function, &used) != 4 ||
        bdf[used] || domain < 0x7000 || domain > 0x7fff || bus || slot || function) return 1;
    snprintf(pci_path, sizeof(pci_path), "/sys/bus/pci/devices/%s", bdf);
    if (!realpath(pci_path, resolved) || !strstr(resolved, "/ssd_fwlab_native_pci/") ||
        stat(kernel, &st) || !S_ISREG(st.st_mode) || stat(initrd, &st) || !S_ISREG(st.st_mode)) return 1;
    if (snprintf(run, sizeof(run), "%s/run.XXXXXX", workdir) >= (int)sizeof(run) || !mkdtemp(run)) return 1;
    if (snprintf(qmp_path, sizeof(qmp_path), "%s/qmp.sock", run) >= (int)sizeof(address.sun_path) ||
        snprintf(log, sizeof(log), "%s/qemu.log", run) >= (int)sizeof(log) ||
        snprintf(script, sizeof(script), "%s/lab/qmp-control.py", workdir) >= (int)sizeof(script)) goto done;
    printf("OWNER_QEMU_LOG %s\n", log);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s/owner.sock", directory) >=
        (int)sizeof(address.sun_path)) goto done;
    if (control < 0) {
        control = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (control < 0 || connect(control, (struct sockaddr *)&address, sizeof(address))) goto done;
    }
    memset(&packet, 0, sizeof(packet));
    packet.operation = NATIVE_OWNER_OBSERVE;
    if (packet_call(control, &packet)) goto done;
    stable = packet.stable;
    if (packet.current.phase == FWLAB_OWNER_V0_NO_OWNER) {
        if (!packet.revoke_status.certificate_valid ||
            !grant_owner(control, FWLAB_OWNER_V0_HOST_NATIVE, &packet.revoke_status, &stable) ||
            !native_bind(bdf)) goto done;
    } else if (packet.current.phase != FWLAB_OWNER_V0_OWNED ||
               packet.current.owner_kind != FWLAB_OWNER_V0_HOST_NATIVE) goto done;
    if (!namespace_find(bdf, device) || !run_native_io("write", device, bdf, control)) goto done;
    if (!sysfs_write(bdf, "driver/unbind", bdf) ||
        !revoke_and_drain(control, client_uid(), &observed, &revoked) ||
        !bind_driver(bdf, "vfio-pci")) goto done;
    vfio_bound = 1;
    output = open(log, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (output < 0) goto done;
    snprintf(vfio, sizeof(vfio), "vfio-pci,host=%s,iommufd=iommufd0,x-no-mmap=on", bdf);
    snprintf(qmp_option, sizeof(qmp_option), "unix:%s,server=on,wait=off", qmp_path);
    {
        pid_t parent = getpid();
        guest = fork();
        if (!guest) {
            close(control);
            if (prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != parent ||
                dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0) _exit(127);
            close(output);
            execlp("qemu-system-x86_64", "qemu-system-x86_64", "-enable-kvm", "-machine", "q35,accel=kvm",
                   "-cpu", "host", "-smp", "1", "-m", "512M", "-nodefaults", "-display", "none",
                   "-serial", "stdio", "-monitor", "none", "-no-reboot", "-S", "-qmp", qmp_option,
                   "-kernel", kernel, "-initrd", initrd, "-append",
                   cut == 2 ? "console=ttyS0 rdinit=/init panic=-1 fwlab_journey=owner-cut" :
                              "console=ttyS0 rdinit=/init panic=-1 fwlab_journey=owner",
                   "-object", "iommufd,id=iommufd0", "-device", vfio, (char *)NULL);
            _exit(127);
        }
    }
    close(output); output = -1;
    if (guest < 0 || !qmp_control(control, script, qmp_path, "paused")) goto done;
    if (cut == 1) {
        if (kill(guest, SIGKILL) || waitpid(guest, &guest_status, 0) != guest) goto done;
        guest = -1;
        memset(&packet, 0, sizeof(packet));
        packet.operation = NATIVE_OWNER_OBSERVE;
        if (!WIFSIGNALED(guest_status) || WTERMSIG(guest_status) != SIGKILL ||
            packet_call(control, &packet) || packet.current.phase != FWLAB_OWNER_V0_NO_OWNER ||
            packet.current.owner_epoch != revoked.current.owner_epoch ||
            memcmp(&packet.stable, &stable, sizeof(stable))) goto done;
        if (!sysfs_write(bdf, "driver/unbind", bdf)) goto done;
        vfio_bound = 0;
        if (!grant_owner(control, FWLAB_OWNER_V0_HOST_NATIVE, &revoked, &stable) ||
            !native_bind(bdf) || !namespace_find(bdf, device) ||
            !run_native_io("verify", device, bdf, control) ||
            !sysfs_write(bdf, "driver/unbind", bdf) ||
            !revoke_and_drain(control, client_uid(), &observed, &revoked)) goto done;
        puts("OWNER_PRE_GRANT_KILL_PASS guest=SIGKILL no_owner_preserved=1 A=exact cleanup=NO_OWNER");
        result = 0;
        goto done;
    }
    if (!grant_owner(control, FWLAB_OWNER_V0_VFIO, &revoked, &stable) ||
        !qmp_control(control, script, qmp_path, "cont")) goto done;
    for (iteration = 0; iteration < 4500; ++iteration) {
        pid_t done_pid = waitpid(guest, &guest_status, WNOHANG);
        if (done_pid == guest) { guest = -1; break; }
        if (done_pid < 0) goto done;
        if (cut == 2 && guest_log_held(log)) {
            puts("OWNER_POST_GRANT_COORDINATOR_KILL_POINT guest_A=exact guest=held");
            fflush(stdout);
            kill(getpid(), SIGKILL);
            _exit(127);
        }
        usleep(10000);
    }
    if (guest > 0 || !WIFEXITED(guest_status) || WEXITSTATUS(guest_status) || !guest_log_passed(log)) goto done;
    if (!revoke_and_drain(control, client_uid(), &observed, &revoked) ||
        !sysfs_write(bdf, "driver/unbind", bdf)) goto done;
    vfio_bound = 0;
    if (!grant_owner(control, FWLAB_OWNER_V0_HOST_NATIVE, &revoked, &stable) ||
        !native_bind(bdf) || !namespace_find(bdf, device) ||
        !run_native_io("verify-b", device, bdf, control) ||
        !sysfs_write(bdf, "driver/unbind", bdf) ||
        !revoke_and_drain(control, client_uid(), &observed, &revoked)) goto done;
    printf("OWNER_QEMU_PATH_PASS function=%" PRIu64 " L1_A_to_L2=exact L2_B_to_L1=exact cleanup=NO_OWNER\n",
           stable.function_instance_nonce);
    result = 0;
done:
    if (guest > 0) { kill(guest, SIGKILL); waitpid(guest, NULL, 0); }
    if (output >= 0) close(output);
    if (vfio_bound) (void)sysfs_write(bdf, "driver/unbind", bdf);
    if (control >= 0) close(control);
    if (result) fprintf(stderr, "owner QEMU path failed errno=%d\n", errno);
    return result;
}

int native_owner_qemu_journey(const char *directory, const char *bdf,
                              const char *kernel, const char *initrd, const char *workdir, unsigned cut)
{
    return owner_qemu_run(directory, bdf, kernel, initrd, workdir, cut, -1);
}

int native_owner_postkill_journey(const char *directory, const char *bdf,
                                  const char *kernel, const char *initrd, const char *workdir)
{
    pid_t coordinator;
    int status, control = -1, result = 1;
    unsigned iteration;
    struct sockaddr_un address;
    struct native_owner_packet packet, observed;
    struct fwlab_owner_revoke_status_v0 revoked;
    struct fwlab_owner_stable_identity_v0 stable, original;
    char device[64];

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s/owner.sock", directory) >=
        (int)sizeof(address.sun_path)) return 1;
    control = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    memset(&packet, 0, sizeof(packet));
    packet.operation = NATIVE_OWNER_OBSERVE;
    if (control < 0 || connect(control, (struct sockaddr *)&address, sizeof(address)) ||
        packet_call(control, &packet)) goto done;
    original = packet.stable;
    coordinator = fork();
    if (coordinator < 0) goto done;
    if (!coordinator)
        _exit(owner_qemu_run(directory, bdf, kernel, initrd, workdir, 2, control) ? 1 : 2);
    close(control); control = -1;
    if (waitpid(coordinator, &status, 0) != coordinator ||
        !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) goto done;
    for (iteration = 0; iteration < 800; ++iteration) {
        control = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (control < 0) goto done;
        memset(&packet, 0, sizeof(packet));
        packet.operation = NATIVE_OWNER_OBSERVE;
        if (!connect(control, (struct sockaddr *)&address, sizeof(address)) &&
            !packet_call(control, &packet) && packet.current.phase == FWLAB_OWNER_V0_NO_OWNER &&
            packet.revoke_status.certificate_valid) break;
        close(control); control = -1;
        usleep(10000);
    }
    if (control < 0 || iteration == 800) goto done;
    revoked = packet.revoke_status;
    stable = packet.stable;
    if (memcmp(&stable, &original, sizeof(stable))) goto done;
    if (!sysfs_write(bdf, "driver/unbind", bdf) ||
        !grant_owner(control, FWLAB_OWNER_V0_HOST_NATIVE, &revoked, &stable) ||
        !native_bind(bdf) || !namespace_find(bdf, device) ||
        !run_native_io("verify", device, bdf, control) ||
        !sysfs_write(bdf, "driver/unbind", bdf) ||
        !revoke_and_drain(control, client_uid(), &observed, &revoked)) goto done;
    puts("OWNER_POST_GRANT_KILL_PASS coordinator=SIGKILL automatic_revoke=1 A=exact cleanup=NO_OWNER");
    result = 0;
done:
    if (control >= 0) close(control);
    if (result) fputs("post-grant coordinator kill recovery failed\n", stderr);
    return result;
}

static int canary_call(int control, uint32_t operation,
                       struct fwlab_m4_canary_message *result)
{
    struct native_owner_packet packet = { 0 };
    packet.operation = NATIVE_OWNER_CANARY;
    packet.canary.operation = operation;
    if (packet_call(control, &packet) || packet.canary.result) return 0;
    *result = packet.canary;
    return 1;
}

int native_owner_stale_journey(const char *directory, const char *bdf)
{
    struct sockaddr_un address;
    struct native_owner_packet packet, observed;
    struct fwlab_owner_stable_identity_v0 stable;
    struct fwlab_owner_revoke_status_v0 revoked;
    struct fwlab_m4_canary_message canary;
    struct j3_vfio_epoch old, fresh;
    uint8_t old_snapshot[J3_VFIO_MEMORY_BYTES], fresh_snapshot[J3_VFIO_MEMORY_BYTES];
    const uint8_t empty_cqe[16] = { 0 };
    char pci_path[128], resolved[PATH_MAX];
    unsigned domain, bus, slot, function, iteration;
    int used = 0, control = -1, old_irq = -1, reused_irq = -1, bound = 0, result = 1;
    const char *stage = "identity";

    j3_vfio_init(&old);
    j3_vfio_init(&fresh);
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (sscanf(bdf, "%4x:%2x:%2x.%1x%n", &domain, &bus, &slot, &function, &used) != 4 ||
        bdf[used] || domain < 0x7000 || domain > 0x7fff || bus || slot || function) return 1;
    snprintf(pci_path, sizeof(pci_path), "/sys/bus/pci/devices/%s", bdf);
    if (!realpath(pci_path, resolved) || !strstr(resolved, "/ssd_fwlab_native_pci/")) return 1;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s/owner.sock", directory) >=
        (int)sizeof(address.sun_path)) return 1;
    control = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (control < 0 || connect(control, (struct sockaddr *)&address, sizeof(address))) goto done;
    memset(&packet, 0, sizeof(packet));
    packet.operation = NATIVE_OWNER_OBSERVE;
    if (packet_call(control, &packet)) goto done;
    stable = packet.stable;
    if (packet.current.phase == FWLAB_OWNER_V0_OWNED) {
        /* No currently bound driver may be displaced by this fixed experiment. */
        char driver[160];
        struct stat st;
        snprintf(driver, sizeof(driver), "/sys/bus/pci/devices/%s/driver", bdf);
        if (!lstat(driver, &st) || errno != ENOENT ||
            !revoke_and_drain(control, client_uid(), &observed, &revoked)) goto done;
    } else if (packet.current.phase == FWLAB_OWNER_V0_NO_OWNER &&
               packet.revoke_status.certificate_valid) {
        revoked = packet.revoke_status;
    } else goto done;

    stage = "first VFIO attachment";
    if (!bind_driver(bdf, "vfio-pci")) goto done;
    bound = 1;
    if (!j3_vfio_open(&old, bdf, -1) ||
        !grant_owner(control, FWLAB_OWNER_V0_VFIO, &revoked, &stable) ||
        !canary_call(control, FWLAB_M4_CANARY_ARM, &canary)) goto done;
    stage = "first real Identify";
    if (!j3_vfio_identify(&old, 0x41) || !j3_vfio_complete(&old, 0x41) ||
        !j3_vfio_quiet(&old, -1) ||
        !canary_call(control, FWLAB_M4_CANARY_QUERY, &canary) || canary.flags != 7 ||
        !canary.old_origin || !canary.old_completion_uid || !canary.old_route_generation)
        goto done;
    printf("J3_OLD_CAPTURE origin=%" PRIu64 " owner=%" PRIu64 " completion=%" PRIu64
           " route=%" PRIu64 " eventfd=%d Identify=exact\n",
           (uint64_t)canary.old_origin, (uint64_t)canary.old_owner_epoch,
           (uint64_t)canary.old_completion_uid, (uint64_t)canary.old_route_generation, old.irq);
    reused_irq = old.irq;
    old_irq = fcntl(old.irq, F_DUPFD_CLOEXEC, 128);
    if (old_irq < 0) goto done;
    memcpy(old_snapshot, old.memory, sizeof(old_snapshot));
    stage = "old revoke and detach";
    if (!revoke_and_drain(control, client_uid(), &observed, &revoked) ||
        !j3_vfio_close(&old)) goto done;
    stage = "second VFIO attachment";
    if (!j3_vfio_open(&fresh, bdf, reused_irq) || fresh.memory == old.memory ||
        !grant_owner(control, FWLAB_OWNER_V0_VFIO, &revoked, &stable) ||
        !canary_call(control, FWLAB_M4_CANARY_HOLD, &canary)) goto done;
    stage = "second real Identify held before publication";
    if (!j3_vfio_identify(&fresh, 0x42)) goto done;
    for (iteration = 0; iteration < 4000; ++iteration) {
        if (!canary_call(control, FWLAB_M4_CANARY_QUERY, &canary)) goto done;
        if (canary.held) break;
        usleep(1000);
    }
    if (!canary.held || !j3_vfio_data_valid(&fresh) ||
        memcmp(fresh.memory + 4096, empty_cqe, sizeof(empty_cqe)) ||
        !j3_vfio_quiet(&fresh, old_irq) ||
        memcmp(old.memory, old_snapshot, sizeof(old_snapshot))) goto done;
    memcpy(fresh_snapshot, fresh.memory, sizeof(fresh_snapshot));
    stage = "four stale production entrypoints";
    if (!canary_call(control, FWLAB_M4_CANARY_PROBE, &canary) ||
        canary.dma_result != -ESTALE || canary.mapping_result != -ESTALE ||
        canary.publication_result != -ESTALE || canary.irq_result != -ESTALE ||
        canary.firmware_lease_result != FWLAB_SPINE_V0_STALE ||
        canary.old_owner_epoch >= canary.new_owner_epoch ||
        canary.old_controller >= canary.new_controller ||
        canary.old_domain == canary.new_domain ||
        canary.old_data_iova != canary.new_data_iova ||
        canary.new_data_iova != J3_VFIO_IOVA + 8192 ||
        canary.old_cq_iova != canary.new_cq_iova ||
        canary.new_cq_iova != J3_VFIO_IOVA + 4096 ||
        !canary.old_completion_uid || canary.old_completion_uid != canary.new_completion_uid ||
        canary.old_route_generation >= canary.new_route_generation ||
        memcmp(fresh.memory, fresh_snapshot, sizeof(fresh_snapshot)) ||
        memcmp(old.memory, old_snapshot, sizeof(old_snapshot)) ||
        !j3_vfio_quiet(&fresh, old_irq)) goto done;
    printf("J3_STALE_REJECTED dma=%d map=%d publication=%d firmware_lease=%d irq=%d "
           "old_owner=%" PRIu64 " new_owner=%" PRIu64 " old_domain=%" PRIu64
           " new_domain=%" PRIu64 " reused_completion=%" PRIu64 " old_route=%" PRIu64
           " new_route=%" PRIu64 " eventfd=%d vector=0 CQ_slot=0 bytes=unchanged\n",
           canary.dma_result, canary.mapping_result, canary.publication_result,
           canary.firmware_lease_result, canary.irq_result,
           (uint64_t)canary.old_owner_epoch, (uint64_t)canary.new_owner_epoch,
           (uint64_t)canary.old_domain, (uint64_t)canary.new_domain,
           (uint64_t)canary.new_completion_uid, (uint64_t)canary.old_route_generation,
           (uint64_t)canary.new_route_generation, fresh.irq);
    stage = "current positive completion";
    if (!canary_call(control, FWLAB_M4_CANARY_RELEASE, &canary) ||
        !j3_vfio_complete(&fresh, 0x42) || !j3_vfio_quiet(&fresh, old_irq) ||
        memcmp(old.memory, old_snapshot, sizeof(old_snapshot))) goto done;
    stage = "final zero and cleanup";
    if (!revoke_and_drain(control, client_uid(), &observed, &revoked) ||
        memcmp(&observed.stable, &stable, sizeof(stable)) ||
        !j3_vfio_close(&fresh) || !sysfs_write(bdf, "driver/unbind", bdf)) goto done;
    bound = 0;
    if (!canary_call(control, FWLAB_M4_CANARY_DISARM, &canary)) goto done;
    printf("J3_FOUR_AUTHORITY_PASS function=%" PRIu64 " cases=4 current_Identify=exact "
           "current_CQE=exact current_IRQ=1 old_IRQ=0 cleanup=NO_OWNER\n",
           stable.function_instance_nonce);
    result = 0;
done:
    if (result) {
        fprintf(stderr, "J3 failed stage=%s errno=%d\n", stage, errno);
        if (control >= 0) {
            memset(&packet, 0, sizeof(packet));
            packet.operation = NATIVE_OWNER_OBSERVE;
            if (!packet_call(control, &packet) && packet.current.phase == FWLAB_OWNER_V0_OWNED)
                (void)revoke_and_drain(control, client_uid(), &observed, &revoked);
        }
    }
    if (!j3_vfio_close(&fresh) || !j3_vfio_close(&old)) result = 1;
    if (bound && !sysfs_write(bdf, "driver/unbind", bdf)) result = 1;
    if (old_irq >= 0) close(old_irq);
    if (control >= 0) close(control);
    j3_vfio_memory_free(&fresh);
    j3_vfio_memory_free(&old);
    return result;
}
