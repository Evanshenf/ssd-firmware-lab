/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

/* One bounded native journey. No firmware symbols or synthetic media model
 * are linked here: every operation uses the installed Linux nvme driver. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/fs.h>
#include <linux/nvme_ioctl.h>
#include <stdint.h>
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

struct native_case { uint32_t lba, bytes, offset; uint8_t seed; };
int native_owner_host_journey(const char *directory, const char *bdf);
int native_owner_qemu_journey(const char *directory, const char *bdf,
                              const char *kernel, const char *initrd, const char *workdir, unsigned cut);
int native_owner_postkill_journey(const char *directory, const char *bdf,
                                  const char *kernel, const char *initrd, const char *workdir);
static const struct native_case cases[] = {
    { 128, 512, 0, 0x31 },
    { 129, 4096, 0, 0x52 },
    { 137, 8192, 512, 0x73 },
};
static uint8_t pattern_delta;

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
        ioctl(fd, BLKGETSIZE64, &bytes) || bytes != UINT64_C(1048576) ||
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

static int transfer(int fd, uint8_t opcode, const struct native_case *test,
                    uint8_t *buffer, uint32_t control, uint32_t hint)
{
    struct nvme_passthru_cmd command = { 0 };

    command.opcode = opcode;
    command.nsid = 1;
    command.addr = (uintptr_t)buffer;
    command.data_len = test->bytes;
    command.cdw10 = test->lba;
    command.cdw12 = control | (test->bytes / 512u - 1u);
    command.cdw13 = hint;
    return exchange(fd, NVME_IOCTL_IO_CMD, &command);
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
        buffer[index] = point == 2 ? 0xa5 : pattern(index, test.seed);
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
        report.result = transfer(fd, point == 2 ? 2 : 1, &test, buffer,
                                 point == 3 ? UINT32_C(0x40000000) : 0, 0);
        report.untouched = 1;
        for (index = 0; index < test.bytes; ++index)
            if (buffer[index] != (point == 2 ? 0xa5 : pattern(index, test.seed)))
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

int main(int argc, char **argv)
{
    uint8_t *allocation;
    int fd, write_mode, result = 1, guest = 0, guest_phase = 0, guest_hold = 0, pba = 0;
    uint32_t index, iteration, cut = 0;

    if (argc == 7 && !strcmp(argv[1], "owner-qemu"))
        return native_owner_qemu_journey(argv[2], argv[3], argv[4], argv[5], argv[6], 0);
    if (argc == 7 && !strcmp(argv[1], "owner-prekill"))
        return native_owner_qemu_journey(argv[2], argv[3], argv[4], argv[5], argv[6], 1);
    if (argc == 7 && !strcmp(argv[1], "owner-postkill"))
        return native_owner_postkill_journey(argv[2], argv[3], argv[4], argv[5], argv[6]);
    if (argc == 4 && !strcmp(argv[1], "owner-host"))
        return native_owner_host_journey(argv[2], argv[3]);
    if (argc == 4 && strlen(argv[1]) == 4 && !strncmp(argv[1], "cut", 3) &&
        argv[1][3] >= '1' && argv[1][3] <= '3')
        cut = (uint32_t)(argv[1][3] - '0');
    guest_hold = argc == 4 && !strcmp(argv[1], "guest-hold");
    pba = argc == 4 && !strcmp(argv[1], "pba");
    guest = guest_hold || (argc == 4 && !strcmp(argv[1], "guest-ab"));
    if (argc != 4 || (!cut && !guest && !pba && strcmp(argv[1], "write") &&
                      strcmp(argv[1], "verify") && strcmp(argv[1], "verify-b"))) {
        fprintf(stderr, "usage: %s write|verify|verify-b|guest-ab|cut1|cut2|cut3 /dev/nvmeXn1 BDF\n", argv[0]);
        return 2;
    }
    pattern_delta = !strcmp(argv[1], "verify-b") ? 0x33 : 0;
    write_mode = cut != 0 || !strcmp(argv[1], "write");
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
    allocation = aligned_alloc(4096, 12288);
    if (!allocation) {
        close(fd);
        return 1;
    }
    if (cut) {
        result = cut_journey(fd, argv[2], argv[3], cut, allocation) ? 0 : 1;
        goto done;
    }
    if (pba) {
        result = masked_pba_journey(fd, argv[3], allocation) ? 0 : 1;
        goto done;
    }
guest_again:
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
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
        puts("NATIVE_GUEST_A_READ_OK shapes=3 data=exact");
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
    if (guest)
        puts("NATIVE_GUEST_AB_PASS host_A=exact guest_B=durable shapes=3");
    else
        printf("NATIVE_IO_PASS mode=%s shapes=3 continued_reads=64\n", argv[1]);
    result = 0;
done:
    free(allocation);
    close(fd);
    return result;
}
