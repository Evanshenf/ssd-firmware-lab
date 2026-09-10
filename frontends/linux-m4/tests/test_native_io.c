/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

/* Bounded native/owner journeys use the installed Linux nvme driver. The J3
 * case additionally uses a literal VFIO Admin-queue producer. Neither path
 * links a firmware executor or a synthetic media model into this client. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/fs.h>
#include <linux/nvme_ioctl.h>
#include <stdint.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef FWLAB_NATIVE_TEST_SCALED
#define FWLAB_NATIVE_TEST_SCALED 0
#endif
_Static_assert(FWLAB_NATIVE_TEST_SCALED == 0 || FWLAB_NATIVE_TEST_SCALED == 1,
               "native test profile must be legacy or scaled");
#if FWLAB_NATIVE_TEST_SCALED
#define NATIVE_TEST_BYTES UINT64_C(67108864)
#else
#define NATIVE_TEST_BYTES UINT64_C(1048576)
#endif

#ifndef FWLAB_NATIVE_TEST_LARGE
#define FWLAB_NATIVE_TEST_LARGE 0
#endif
_Static_assert(FWLAB_NATIVE_TEST_LARGE == 0 ||
               (FWLAB_NATIVE_TEST_LARGE == 1 && FWLAB_NATIVE_TEST_SCALED),
               "large client requires the explicit scaled profile");
#if FWLAB_NATIVE_TEST_LARGE
#define NATIVE_CLIENT_BUFFER_BYTES (1048576u + 4096u)
#else
#define NATIVE_CLIENT_BUFFER_BYTES 12288u
#endif

struct native_case { uint32_t lba, bytes, offset; uint8_t seed; };
int native_owner_host_journey(const char *directory, const char *bdf, int budget);
int native_owner_stale_journey(const char *directory, const char *bdf);
int native_owner_mq2_journey(const char *directory, const char *bdf, const char *worker_log);
int native_owner_qemu_journey(const char *directory, const char *bdf,
                              const char *kernel, const char *initrd, const char *workdir, unsigned cut);
int native_owner_postkill_journey(const char *directory, const char *bdf,
                                  const char *kernel, const char *initrd, const char *workdir);
static struct native_case cases[] = {
    { 128, 512, 0, 0x31 },
    { 129, 4096, 0, 0x52 },
    { 137, 8192, 512, 0x73 },
#if FWLAB_NATIVE_TEST_SCALED
    { (uint32_t)(NATIVE_TEST_BYTES / 512u - 16u), 8192, 0, 0x94 },
#endif
#if FWLAB_NATIVE_TEST_LARGE
    { 8192, 1048576, 0, 0xb5 },
    { 16384, 1048576, 512, 0xd6 },
#endif
};
#define NATIVE_CASE_COUNT (sizeof(cases) / sizeof(cases[0]))
static uint64_t expected_namespace_bytes = NATIVE_TEST_BYTES;
static uint8_t pattern_delta;

static int select_namespace_capacity(int *argc, char **argv)
{
    if (*argc < 3 || strcmp(argv[*argc - 2], "--namespace-mib"))
        return 1;
#if FWLAB_NATIVE_TEST_SCALED
    /* Only these finite standalone modes carry an explicit expectation.
     * Owner/guest subprocesses retain the existing default 64 MiB contract. */
    if (!((*argc == 4 && !strcmp(argv[1], "profile-plan")) ||
          (*argc == 6 && (!strcmp(argv[1], "write") ||
                         !strcmp(argv[1], "verify") ||
                         !strcmp(argv[1], "verify-b")))))
        return 0;
    if (!strcmp(argv[*argc - 1], "64"))
        expected_namespace_bytes = UINT64_C(64) << 20;
    else if (!strcmp(argv[*argc - 1], "256"))
        expected_namespace_bytes = UINT64_C(256) << 20;
    else if (!strcmp(argv[*argc - 1], "65536"))
        expected_namespace_bytes = UINT64_C(65536) << 20;
    else
        return 0;
    /* The fourth case is the scaled 8 KiB tail witness. Selected capacities
     * fit its existing 32-bit LBA field; no device-provided value is trusted. */
    cases[3].lba = (uint32_t)(expected_namespace_bytes / 512u - 16u);
    *argc -= 2;
    return 1;
#else
    (void)argv;
    return 0;
#endif
}

#if FWLAB_NATIVE_TEST_LARGE
/* Workload size is not the Linux queue's per-command submission ceiling.
 * Only L1 may split, selected before I/O; the L2 large witness is strict. */
static uint32_t wire_io_bytes = 1048576;
static int wire_guest;

static int wire_limits(const char *namespace_path, int guest)
{
    char path[PATH_MAX];
    unsigned long kib = 0;
    FILE *file;
    int parsed;

    if (snprintf(path, sizeof(path), "%s/queue/max_hw_sectors_kb", namespace_path) >=
        (int)sizeof(path))
        return 0;
    file = fopen(path, "re");
    if (!file) return 0;
    parsed = fscanf(file, "%lu", &kib);
    if (fclose(file) || parsed != 1 || kib < 8 || kib % 4)
        return 0;
    wire_io_bytes = kib >= 1024 ? 1048576 : (uint32_t)kib * 1024;
    wire_guest = guest;
    if (guest && wire_io_bytes != 1048576) {
        fputs("L2 requires an unsplit 1 MiB command; refusing smaller queue limit\n", stderr);
        return 0;
    }
    printf("NATIVE_WIRE_LIMIT layer=%s max_hw_kib=%lu selected_bytes=%u split_allowed=%d\n",
           guest ? "L2" : "L1", kib, wire_io_bytes, !guest);
    return 1;
}
#endif

static int exchange(int fd, unsigned long operation,
                    struct nvme_passthru_cmd *command)
{
    int result;

    command->timeout_ms = 5000;
    result = ioctl(fd, operation, command);
    if (result)
        fprintf(stderr, "NVMe op=%02x result=%d errno=%d\n",
                command->opcode, result, result < 0 ? errno : 0);
    return result;
}

static int identity_guard(int fd, const char *bdf, int guest)
{
    struct stat st;
    struct nvme_passthru_cmd command = { 0 };
    char sysfs[128], resolved[PATH_MAX], component[64];
    uint8_t *identify = aligned_alloc(4096, 4096);
    uint64_t bytes = 0;
    unsigned domain, bus, device, function;
    int consumed = 0, valid = 0;
    struct statfs rootfs;

    if (!identify)
        return 0;
    if (sscanf(bdf, "%4x:%2x:%2x.%1x%n", &domain, &bus, &device,
               &function, &consumed) != 4 || bdf[consumed] ||
        domain > 0xffff || bus > 0xff || device > 31 || function > 7)
        goto done;
    snprintf(component, sizeof(component), "/%04x:%02x:%02x.%x/",
             domain, bus, device, function);
    if (fstat(fd, &st) || !S_ISBLK(st.st_mode))
        goto done;
    snprintf(sysfs, sizeof(sysfs), "/sys/dev/block/%u:%u",
             major(st.st_rdev), minor(st.st_rdev));
    if (!realpath(sysfs, resolved) || !strstr(resolved, component) ||
        (!guest && !strstr(resolved, "/ssd_fwlab_native_pci/")) ||
        (guest && (statfs("/", &rootfs) ||
                   (rootfs.f_type != 0x858458f6 && rootfs.f_type != 0x01021994))) ||
        ioctl(fd, BLKGETSIZE64, &bytes) || bytes != expected_namespace_bytes ||
        ioctl(fd, NVME_IOCTL_ID) != 1)
        goto done;
    memset(identify, 0, 4096);
    command.opcode = 6;
    command.cdw10 = 1;
    command.data_len = 4096;
    command.addr = (uintptr_t)identify;
    if (exchange(fd, NVME_IOCTL_ADMIN_CMD, &command) ||
        identify[0] != 0xfa || identify[1] != 0xff ||
        memcmp(identify + 4, "FWLABLINUXV1-0000001", 19) ||
        memcmp(identify + 24, "SSD Firmware Lab Linux-profile-v1", 32))
        goto done;
#if FWLAB_NATIVE_TEST_LARGE
    if (identify[77] != 8) goto done; /* matched 1 MiB / 4 KiB MDTS */
    if (!wire_limits(resolved, guest)) goto done;
#endif
    printf("IDENTITY bdf=%s namespace=1 bytes=%" PRIu64 " native_driver=1\n",
           bdf, bytes);
    valid = 1;
done:
    free(identify);
    if (!valid)
        fprintf(stderr, "refusing nonmatching or nonexclusive native test namespace\n");
    return valid;
}

static uint8_t pattern(uint32_t index, uint8_t seed)
{
    return (uint8_t)(seed + pattern_delta + index * 17u + (index >> 8));
}

static int aer_then_identify(int fd, const char *bdf)
{
    struct nvme_passthru_cmd command = { 0 };
    int result;
    command.opcode = 0x0c;
    result = exchange(fd, NVME_IOCTL_ADMIN_CMD, &command);
    if (result <= 0 || (result & 0x7ff) != 1 || !(result & 0x4000) ||
        !identity_guard(fd, bdf, 0))
        return 0;
    puts("NATIVE_AER_PASS unsupported_SCT0_SC1_DNR1=1 following_Identify_completed=1 no_long_lived_Admin=1");
    return 1;
}

static int transfer(int fd, uint8_t opcode, const struct native_case *test,
                    uint8_t *buffer, uint32_t control, uint32_t hint)
{
    struct nvme_passthru_cmd command = { 0 };
#if FWLAB_NATIVE_TEST_LARGE
    uint32_t done = 0, commands = 0, maximum = 0;

    while (done < test->bytes) {
        uint32_t bytes = test->bytes - done;
        int result;
        if (bytes > wire_io_bytes) bytes = wire_io_bytes;
        memset(&command, 0, sizeof(command));
        command.opcode = opcode;
        command.nsid = 1;
        command.addr = (uintptr_t)(buffer + done);
        command.data_len = bytes;
        command.cdw10 = test->lba + done / 512u;
        command.cdw12 = control | (bytes / 512u - 1u);
        command.cdw13 = hint;
        result = exchange(fd, NVME_IOCTL_IO_CMD, &command);
        if (result) return result; /* Never retry a failed command as smaller I/O. */
        done += bytes;
        commands++;
        if (bytes > maximum) maximum = bytes;
    }
    if (test->bytes == 1048576)
        printf("NATIVE_LARGE_TRANSFER layer=%s op=%02x logical_bytes=%u wire_max_bytes=%u commands=%u buffer_offset=%u\n",
               wire_guest ? "L2" : "L1", opcode, test->bytes, maximum, commands, test->offset);
    return 0;
#else

    command.opcode = opcode;
    command.nsid = 1;
    command.addr = (uintptr_t)buffer;
    command.data_len = test->bytes;
    command.cdw10 = test->lba;
    command.cdw12 = control | (test->bytes / 512u - 1u);
    command.cdw13 = hint;
    return exchange(fd, NVME_IOCTL_IO_CMD, &command);
#endif
}

static int read_compare(int fd, const struct native_case *test, uint8_t *buffer,
                        uint32_t control, uint32_t hint)
{
    uint32_t index;

    memset(buffer, 0xa5, test->bytes);
    if (transfer(fd, 2, test, buffer, control, hint))
        return 0;
    for (index = 0; index < test->bytes; ++index)
        if (buffer[index] != pattern(index, test->seed)) {
            fprintf(stderr, "read mismatch lba=%u byte=%u expected=%02x actual=%02x\n",
                    test->lba, index, pattern(index, test->seed), buffer[index]);
            return 0;
        }
    return 1;
}

static int cut_journey(int fd, const char *device, const char *bdf,
                       unsigned int point, uint8_t *buffer)
{
    struct child_result { int result; int untouched; } report;
    struct native_case test = cases[0];
    char control_path[64], sysfs[128], resolved[PATH_MAX], component[64], text[32];
    struct stat st;
    int control = -1, parameter = -1, channel[2] = { -1, -1 };
    int consumed = 0, status = 0, fired = 0, success = 0;
    const int read_cut = point == 2 || point == 4;
    unsigned int controller, index;
    pid_t child = -1;

    if (sscanf(device, "/dev/nvme%un1%n", &controller, &consumed) != 1 ||
        device[consumed])
        return 0;
    snprintf(control_path, sizeof(control_path), "/dev/nvme%u", controller);
    control = open(control_path, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    if (control < 0 || fstat(control, &st) || !S_ISCHR(st.st_mode))
        goto done;
    snprintf(sysfs, sizeof(sysfs), "/sys/dev/char/%u:%u", major(st.st_rdev), minor(st.st_rdev));
    snprintf(component, sizeof(component), "/%s/", bdf);
    if (!realpath(sysfs, resolved) || !strstr(resolved, component) ||
        !strstr(resolved, "/ssd_fwlab_native_pci/"))
        goto done;
    if (!read_compare(fd, &test, buffer, 0, 0))
        goto done;
    test.seed = 0xd4;
    for (index = 0; index < test.bytes; ++index)
        buffer[index] = read_cut ? 0xa5 : pattern(index, test.seed);
    parameter = open("/sys/module/ssd_fwlab_native_pci/parameters/native_cut",
                     O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    if (parameter < 0 || pipe2(channel, O_CLOEXEC))
        goto done;
    text[0] = (char)('0' + point);
    text[1] = '\n';
    if (pwrite(parameter, text, 2, 0) != 2)
        goto done;
    child = fork();
    if (child < 0)
        goto done;
    if (!child) {
        close(channel[0]);
        report.result = transfer(fd, read_cut ? 2 : 1, &test, buffer,
                                 point == 3 ? UINT32_C(0x40000000) : 0, 0);
        report.untouched = 1;
        for (index = 0; index < test.bytes; ++index)
            if (buffer[index] != (read_cut ? 0xa5 : pattern(index, test.seed)))
                report.untouched = 0;
        _exit(write(channel[1], &report, sizeof(report)) == sizeof(report) ? 0 : 1);
    }
    close(channel[1]);
    channel[1] = -1;
    for (index = 0; index < 5000; ++index) {
        ssize_t length = pread(parameter, text, sizeof(text) - 1, 0);
        if (length <= 0)
            break;
        text[length] = 0;
        if (strtoul(text, NULL, 10) == 0) { fired = 1; break; }
        usleep(1000);
    }
    if (!fired || ioctl(control, NVME_IOCTL_RESET))
        goto done;
    if (read(channel[0], &report, sizeof(report)) != sizeof(report))
        goto done;
    if (waitpid(child, &status, 0) != child)
        goto done;
    child = -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) || report.result == 0 || !report.untouched)
        goto done;
    if (point != 3)
        test.seed = cases[0].seed;
    if (!read_compare(fd, &test, buffer, 0, 0))
        goto done;
    if (point <= 2) {
        int permission = open("/sys/module/ssd_fwlab_native_pci/parameters/native_cut_permission_result",
                              O_RDONLY | O_CLOEXEC);
        ssize_t length;
        if (permission < 0)
            goto done;
        length = read(permission, text, sizeof(text) - 1);
        close(permission);
        if (length <= 0)
            goto done;
        text[length] = 0;
        if (strtol(text, NULL, 10) != -ESTALE)
            goto done;
        permission = open("/sys/module/ssd_fwlab_native_pci/parameters/native_cut_permission_bit",
                          O_RDONLY | O_CLOEXEC);
        if (permission < 0)
            goto done;
        length = read(permission, text, sizeof(text) - 1);
        close(permission);
        if (length <= 0)
            goto done;
        text[length] = 0;
        if (strtoul(text, NULL, 10) != (point == 1 ? 4u : 2u))
            goto done;
        printf("NATIVE_PERMISSION_BLOCK bit=%u result=%d\n", point == 1 ? 4u : 2u, -ESTALE);
    }
    /* Restore the existing journey's baseline through the same NVMe path. */
    if (point == 3) {
        test.seed = cases[0].seed;
        for (index = 0; index < test.bytes; ++index)
            buffer[index] = pattern(index, test.seed);
        if (transfer(fd, 1, &test, buffer, UINT32_C(0x40000000), 0) ||
            !read_compare(fd, &test, buffer, 0, 0))
            goto done;
    }
    success = 1;
done:
    if (parameter >= 0) {
        if (pwrite(parameter, "0\n", 2, 0) != 2)
            success = 0;
        close(parameter);
    }
    if (child > 0) {
        kill(child, SIGTERM);
        waitpid(child, NULL, 0);
    }
    if (channel[0] >= 0) close(channel[0]);
    if (channel[1] >= 0) close(channel[1]);
    if (control >= 0) close(control);
    if (!success)
        fprintf(stderr, "native cut %u failed (fired=%d)\n", point, fired);
    else
        printf("NATIVE_CUT_PASS point=%u host_result=%d host_buffer_untouched=%d media=%s\n",
               point, report.result, report.untouched, point == 3 ? "durable-new" : "old");
    return success;
}

static int masked_pba_journey(int fd, const char *bdf, uint8_t *buffer)
{
    char path[128];
    int config = -1, resource = -1, status, result = 0;
    uint8_t original[2], masked[2];
    int mask_written = 0;
    uint8_t *bar = MAP_FAILED;
    pid_t child = -1;
    unsigned iteration;

    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/config", bdf);
    config = open(path, O_RDWR | O_CLOEXEC);
    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource0", bdf);
    resource = open(path, O_RDONLY | O_CLOEXEC);
    if (config < 0 || resource < 0 || pread(config, original, 2, 0xa2) != 2 ||
        !(original[1] & 0x80) || (original[1] & 0x40)) goto done;
    bar = mmap(NULL, 16384, PROT_READ, MAP_SHARED, resource, 0);
    if (bar == MAP_FAILED || *(volatile uint64_t *)(bar + 0x3000)) goto done;
    memcpy(masked, original, 2);
    masked[1] |= 0x40;
    if (pwrite(config, masked, 2, 0xa2) != 2) goto done;
    mask_written = 1;
    child = fork();
    if (child < 0) goto done;
    if (!child)
        _exit(read_compare(fd, &cases[0], buffer, 0, 0) ? 0 : 1);
    for (iteration = 0; iteration < 5000; ++iteration) {
        if (*(volatile uint64_t *)(bar + 0x3000) & 1) break;
        usleep(100);
    }
    if (iteration == 5000) goto done;
    {
        pid_t reaped = waitpid(child, &status, WNOHANG);
        if (reaped == child || (reaped < 0 && errno == ECHILD)) child = -1;
        if (reaped != 0) goto done;
    }
    if (pwrite(config, original, 2, 0xa2) != 2) goto done;
    mask_written = 0;
    for (iteration = 0; iteration < 2000; ++iteration) {
        pid_t reaped = waitpid(child, &status, WNOHANG);
        if (reaped == child) { child = -1; break; }
        if (reaped < 0) goto done;
        usleep(1000);
    }
    if (child > 0 || !WIFEXITED(status) || WEXITSTATUS(status) ||
        *(volatile uint64_t *)(bar + 0x3000)) goto done;
    result = 1;
done:
    if (mask_written && pwrite(config, original, 2, 0xa2) != 2) result = 0;
    if (child > 0) { kill(child, SIGTERM); waitpid(child, NULL, 0); }
    if (bar != MAP_FAILED) munmap(bar, 16384);
    if (resource >= 0) close(resource);
    if (config >= 0) close(config);
    if (result) puts("NATIVE_PBA_PASS masked_pending=1 premature_completion=0 unmask_delivery=1 pending_cleared=1");
    else fputs("masked PBA journey failed\n", stderr);
    return result;
}

#if FWLAB_NATIVE_TEST_LARGE
static int mq_masked_pba_journey(int fd, const char *bdf, uint8_t *buffer,
                                  unsigned cpu1, unsigned cpu2)
{
    char path[128];
    cpu_set_t saved, selected;
    uint8_t flags[2];
    uint8_t *bar = MAP_FAILED;
    volatile uint32_t *mask = NULL;
    volatile uint64_t *pending = NULL;
    uint32_t original = 0;
    int config = -1, resource = -1, status = 0, result = 0;
    int masked = 0, affinity_changed = 0;
    pid_t child = -1;
    unsigned iteration;

    if (cpu1 == cpu2 || cpu1 >= CPU_SETSIZE || cpu2 >= CPU_SETSIZE ||
        sched_getaffinity(0, sizeof(saved), &saved) ||
        !CPU_ISSET(cpu1, &saved) || !CPU_ISSET(cpu2, &saved)) return 0;
    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/config", bdf);
    config = open(path, O_RDONLY | O_CLOEXEC);
    snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/resource0", bdf);
    resource = open(path, O_RDWR | O_CLOEXEC);
    if (config < 0 || resource < 0 || pread(config, flags, 2, 0xa2) != 2 ||
        (flags[0] | ((unsigned)(flags[1] & 7) << 8)) != 2 ||
        !(flags[1] & 0x80) || (flags[1] & 0x40)) goto done;
    bar = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED, resource, 0);
    if (bar == MAP_FAILED) goto done;
    mask = (volatile uint32_t *)(bar + 0x201c); /* MSI-X vector1, not MASKALL. */
    pending = (volatile uint64_t *)(bar + 0x3000);
    original = *mask;
    if ((original & 1) || *pending) goto done;
    *mask = original | 1; __sync_synchronize(); masked = 1;
    child = fork();
    if (child < 0) goto done;
    if (!child) {
        CPU_ZERO(&selected); CPU_SET(cpu1, &selected);
        if (sched_setaffinity(0, sizeof(selected), &selected)) _exit(2);
        _exit(read_compare(fd, &cases[0], buffer, 0, 0) ? 0 : 1);
    }
    for (iteration = 0; iteration < 5000 && !(*pending & 2); ++iteration) usleep(100);
    if (iteration == 5000) goto done;
    {
        pid_t reaped = waitpid(child, &status, WNOHANG);
        if (reaped == child || (reaped < 0 && errno == ECHILD)) child = -1;
        if (reaped != 0) goto done;
    }
    CPU_ZERO(&selected); CPU_SET(cpu2, &selected);
    if (sched_setaffinity(0, sizeof(selected), &selected)) goto done;
    affinity_changed = 1;
    if (!read_compare(fd, &cases[0], buffer, 0, 0) || *pending != 2) goto done;
    {
        pid_t reaped = waitpid(child, &status, WNOHANG);
        if (reaped == child || (reaped < 0 && errno == ECHILD)) child = -1;
        if (reaped != 0) goto done;
    }
    if (sched_setaffinity(0, sizeof(saved), &saved)) goto done;
    affinity_changed = 0;
    *mask = original; __sync_synchronize(); masked = 0;
    for (iteration = 0; iteration < 2000; ++iteration) {
        pid_t reaped = waitpid(child, &status, WNOHANG);
        if (reaped == child) { child = -1; break; }
        if (reaped < 0) goto done;
        usleep(1000);
    }
    if (child > 0 || !WIFEXITED(status) || WEXITSTATUS(status) || *pending) goto done;
    result = 1;
done:
    if (masked) { *mask = original; __sync_synchronize(); }
    if (affinity_changed && sched_setaffinity(0, sizeof(saved), &saved)) result = 0;
    if (child > 0) { kill(child, SIGTERM); waitpid(child, NULL, 0); }
    if (bar != MAP_FAILED) munmap(bar, 16384);
    if (resource >= 0) close(resource);
    if (config >= 0) close(config);
    if (result) puts("NATIVE_MQ_PBA_PASS Q1_masked=1 Q2_read_completed=1 Q1_still_waiting=1 independent_PBA=1 unmask_delivery=1 pending_cleared=1");
    else fputs("MQ per-vector masked PBA journey failed\n", stderr);
    return result;
}
#endif

static int budget_journey(int fd, uint8_t *buffer)
{
    const struct native_case *test = &cases[2];
    uint8_t expected[8192];
    uint32_t iteration, byte;
    int status;

    if (transfer(fd, 2, test, buffer, 0, 0)) return 0;
    memcpy(expected, buffer, sizeof(expected));
    for (iteration = 0; iteration < 12000; ++iteration) {
        memset(buffer, 0xa5, sizeof(expected));
        status = transfer(fd, 2, test, buffer, 0, 0);
        if (status != 0) {
            struct nvme_passthru_cmd flush = { 0 };
            /* Linux ioctl status has no CQ phase bit: SC=6, SCT=0, DNR=1. */
            if (status != 0x4006 || iteration < 4096) return 0;
            for (byte = 0; byte < sizeof(expected); ++byte)
                if (buffer[byte] != 0xa5) return 0;
            flush.nsid = 1;
            if (exchange(fd, NVME_IOCTL_IO_CMD, &flush)) return 0;
            printf("NATIVE_BUDGET_PASS reads=%u status=0x%x DNR=1 error_buffer=unchanged final_flush=success timeout_reset=not_used\n",
                   iteration + 1u, status);
            return 1;
        }
        if (memcmp(buffer, expected, sizeof(expected))) return 0;
        if ((iteration + 1u) % 1024u == 0)
            printf("NATIVE_BUDGET_PROGRESS reads=%u\n", iteration + 1u);
    }
    fputs("native budget boundary was not reached within its declared bound\n", stderr);
    return 0;
}

int main(int argc, char **argv)
{
    uint8_t *allocation;
    int fd, write_mode, result = 1, guest = 0, guest_phase = 0, guest_hold = 0, pba = 0;
    uint32_t index, iteration, cut = 0;
    int budget, aer, mq_pba;
    unsigned mq_cpu1 = 0, mq_cpu2 = 0;

    setvbuf(stdout, NULL, _IOLBF, 0);
    if (!select_namespace_capacity(&argc, argv)) {
        fputs("--namespace-mib requires scaled profile-plan/write/verify/verify-b and 64, 256 or 65536 MiB\n", stderr);
        return 2;
    }
    budget = argc == 4 && !strcmp(argv[1], "budget");
    aer = argc == 4 && !strcmp(argv[1], "aer");
    mq_pba = FWLAB_NATIVE_TEST_LARGE && argc == 6 && !strcmp(argv[1], "mq-pba-b");

    if (argc == 2 && !strcmp(argv[1], "profile-plan")) {
        printf("NATIVE_CLIENT_PLAN expected_bytes=%" PRIu64 " shapes=%zu no_device_open=1\n",
               expected_namespace_bytes, NATIVE_CASE_COUNT);
        for (index = 0; index < NATIVE_CASE_COUNT; ++index)
            printf("PLAN_CASE lba=%u bytes=%u buffer_offset=%u\n",
                   cases[index].lba, cases[index].bytes, cases[index].offset);
        return 0;
    }

    if (argc == 7 && !strcmp(argv[1], "owner-qemu"))
        return native_owner_qemu_journey(argv[2], argv[3], argv[4], argv[5], argv[6], 0);
    if (argc == 7 && !strcmp(argv[1], "owner-prekill"))
        return native_owner_qemu_journey(argv[2], argv[3], argv[4], argv[5], argv[6], 1);
    if (argc == 7 && !strcmp(argv[1], "owner-postkill"))
        return native_owner_postkill_journey(argv[2], argv[3], argv[4], argv[5], argv[6]);
    if (argc == 4 && !strcmp(argv[1], "owner-host"))
        return native_owner_host_journey(argv[2], argv[3], 0);
    if (argc == 4 && !strcmp(argv[1], "owner-budget"))
        return native_owner_host_journey(argv[2], argv[3], 1);
    if (argc == 4 && !strcmp(argv[1], "owner-stale"))
        return native_owner_stale_journey(argv[2], argv[3]);
#if FWLAB_NATIVE_TEST_LARGE
    if (argc == 5 && !strcmp(argv[1], "owner-mq2-b"))
        return native_owner_mq2_journey(argv[2], argv[3], argv[4]);
#endif
    if (argc == 4 && strlen(argv[1]) == 4 && !strncmp(argv[1], "cut", 3) &&
        argv[1][3] >= '1' && argv[1][3] <= '4')
        cut = (uint32_t)(argv[1][3] - '0');
    guest_hold = argc == 4 && !strcmp(argv[1], "guest-hold");
    pba = argc == 4 && !strcmp(argv[1], "pba");
    guest = guest_hold || (argc == 4 && !strcmp(argv[1], "guest-ab"));
    if ((!mq_pba && argc != 4) || (!mq_pba && !cut && !guest && !pba && !budget && !aer && strcmp(argv[1], "write") &&
                      strcmp(argv[1], "verify") && strcmp(argv[1], "verify-b"))) {
        fprintf(stderr, "usage: %s write|verify|verify-b|guest-ab|cut1|cut2|cut3|cut4|aer|budget /dev/nvmeXn1 BDF\n"
                        "       %s mq-pba-b /dev/nvmeXn1 BDF Q1_CPU Q2_CPU (LARGE client only)\n"
                        "       scaled profile-plan/write/verify/verify-b accept trailing --namespace-mib 64|256|65536\n", argv[0], argv[0]);
        return 2;
    }
    if (mq_pba) {
        char *end1, *end2;
        unsigned long a = strtoul(argv[4], &end1, 10), b = strtoul(argv[5], &end2, 10);
        if (!*argv[4] || !*argv[5] || *end1 || *end2 || a >= CPU_SETSIZE || b >= CPU_SETSIZE)
            return 2;
        mq_cpu1 = (unsigned)a; mq_cpu2 = (unsigned)b;
    }
    pattern_delta = (!strcmp(argv[1], "verify-b") || mq_pba) ? 0x33 : 0;
    write_mode = cut != 0 || budget || !strcmp(argv[1], "write");
    fd = open(argv[2], (write_mode || guest ? O_RDWR : O_RDONLY) |
                        O_EXCL | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        perror("exclusive namespace open");
        return 1;
    }
    if (!identity_guard(fd, argv[3], guest)) {
        close(fd);
        return 1;
    }
#if FWLAB_NATIVE_TEST_LARGE
    /* A 1 MiB transfer at +512 spans 257 base pages. Linux can limit a
     * passthrough request to 256 bio/SG entries. Prepare one 2 MiB folio for
     * the strict guest witness; NVMe still describes 257 distinct 4 KiB PRPs.
     * This is the client payload buffer, not the simulated NAND backing. */
    allocation = guest ? aligned_alloc(2097152, 2097152) :
                         aligned_alloc(4096, NATIVE_CLIENT_BUFFER_BYTES);
#else
    allocation = aligned_alloc(4096, NATIVE_CLIENT_BUFFER_BYTES);
#endif
    if (!allocation) {
        close(fd);
        return 1;
    }
#if FWLAB_NATIVE_TEST_LARGE
    if (guest) {
        if (madvise(allocation, 2097152, MADV_HUGEPAGE)) goto done;
        memset(allocation, 0, 2097152);
        if (madvise(allocation, 2097152, MADV_COLLAPSE)) {
            perror("strict guest payload requires a 2 MiB collapsed folio");
            goto done;
        }
        puts("NATIVE_GUEST_BUFFER bytes=2097152 alignment=2097152 MADV_COLLAPSE=success NAND_backend=unchanged");
    }
#endif
    if (cut) {
        result = cut_journey(fd, argv[2], argv[3], cut, allocation) ? 0 : 1;
        goto done;
    }
    if (aer) {
        result = aer_then_identify(fd, argv[3]) ? 0 : 1;
        goto done;
    }
    if (pba) {
        result = masked_pba_journey(fd, argv[3], allocation) ? 0 : 1;
        goto done;
    }
#if FWLAB_NATIVE_TEST_LARGE
    if (mq_pba) {
        result = mq_masked_pba_journey(fd, argv[3], allocation, mq_cpu1, mq_cpu2) ? 0 : 1;
        goto done;
    }
#else
    (void)mq_cpu1; (void)mq_cpu2;
#endif
    if (budget) {
        result = budget_journey(fd, allocation) ? 0 : 1;
        goto done;
    }
guest_again:
    for (index = 0; index < NATIVE_CASE_COUNT; ++index) {
        const struct native_case *test = &cases[index];
        uint8_t *buffer = allocation + test->offset;

        if (write_mode) {
            struct nvme_passthru_cmd flush = { 0 };
            uint32_t byte;
            for (byte = 0; byte < test->bytes; ++byte)
                buffer[byte] = pattern(byte, test->seed);
            if (transfer(fd, 1, test, buffer, index == 2 ? UINT32_C(0x40000000) : 0, 0))
                goto done;
            flush.nsid = 1;
            if (exchange(fd, NVME_IOCTL_IO_CMD, &flush))
                goto done;
        }
        if (!read_compare(fd, test, buffer, 0, 0) ||
            !read_compare(fd, test, buffer, UINT32_C(0x80000000), 7))
            goto done;
        printf("CASE bytes=%u lba=%u buffer_offset=%u data=exact readahead=exact\n",
               test->bytes, test->lba, test->offset);
    }
    /* More than the 32-command live capacity, in one controller epoch. */
    for (iteration = 0; iteration < 64; ++iteration)
        if (!read_compare(fd, &cases[0], allocation, 0, 0))
            goto done;
    if (guest && !guest_phase) {
        printf("NATIVE_GUEST_A_READ_OK shapes=%zu data=exact\n", NATIVE_CASE_COUNT);
        if (guest_hold) {
            puts("NATIVE_GUEST_HOLD");
            fflush(stdout);
            for (;;) pause();
        }
        guest_phase = 1;
        pattern_delta = 0x33;
        write_mode = 1;
        goto guest_again;
    }
    if (guest) {
#if FWLAB_NATIVE_TEST_LARGE
        puts("NATIVE_GUEST_LARGE_WIRE_PASS bytes=1048576 commands_per_transfer=1 aligned_and_offset=exact phases=A_read_B_write_read");
#endif
        printf("NATIVE_GUEST_AB_PASS host_A=exact guest_B=durable shapes=%zu\n", NATIVE_CASE_COUNT);
    } else
        printf("NATIVE_IO_PASS mode=%s shapes=%zu continued_reads=64\n", argv[1], NATIVE_CASE_COUNT);
    result = 0;
done:
    free(allocation);
    close(fd);
    return result;
}
