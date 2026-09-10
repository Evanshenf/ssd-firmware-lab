/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _GNU_SOURCE
#include "vfio_epoch.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/iommufd.h>
#include <linux/vfio.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define MQ_IOVA (J3_VFIO_IOVA + UINT64_C(0x10000))
#define MQ_BYTES 0x210000u

static void put16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t value)
{
    put16(bytes, (uint16_t)value);
    put16(bytes + 2, (uint16_t)(value >> 16));
}

static void put64(uint8_t *bytes, uint64_t value)
{
    put32(bytes, (uint32_t)value);
    put32(bytes + 4, (uint32_t)(value >> 32));
}

static uint16_t get16(const volatile uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | (uint16_t)bytes[1] << 8);
}

static int reg_write(struct j3_vfio_epoch *epoch, unsigned offset,
                     uint64_t value, unsigned length)
{
    uint8_t bytes[8];
    put64(bytes, value);
    return pwrite(epoch->device, bytes, length, (off_t)(epoch->bar + offset)) ==
           (ssize_t)length;
}

static int reg_wait(struct j3_vfio_epoch *epoch, unsigned offset, uint32_t value)
{
    unsigned iteration;
    for (iteration = 0; iteration < 4000; ++iteration) {
        uint8_t bytes[4];
        if (pread(epoch->device, bytes, sizeof(bytes), (off_t)(epoch->bar + offset)) !=
            sizeof(bytes)) return 0;
        if (((uint32_t)get16(bytes) | (uint32_t)get16(bytes + 2) << 16) == value)
            return 1;
        usleep(1000);
    }
    errno = ETIMEDOUT;
    return 0;
}

void j3_vfio_init(struct j3_vfio_epoch *epoch)
{
    memset(epoch, 0, sizeof(*epoch));
    epoch->iommu = epoch->device = epoch->irq = -1;
    epoch->extra_irq[0] = epoch->extra_irq[1] = -1;
}

static int cdev_open(const char *bdf)
{
    char directory[160], path[160];
    struct dirent *entry;
    DIR *dir;
    int fd = -1;
    snprintf(directory, sizeof(directory), "/sys/bus/pci/devices/%s/vfio-dev", bdf);
    dir = opendir(directory);
    if (!dir) return -1;
    while ((entry = readdir(dir))) {
        unsigned number;
        int used = 0;
        if (sscanf(entry->d_name, "vfio%u%n", &number, &used) != 1 || entry->d_name[used])
            continue;
        snprintf(path, sizeof(path), "/dev/vfio/devices/vfio%u", number);
        fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
        break;
    }
    closedir(dir);
    return fd;
}

static int vfio_open_vectors(struct j3_vfio_epoch *epoch, const char *bdf,
                             int reused_irq_fd, unsigned vectors)
{
    struct iommu_ioas_alloc ioas = { .size = sizeof(ioas) };
    struct vfio_device_bind_iommufd bind = { .argsz = sizeof(bind) };
    struct vfio_device_attach_iommufd_pt attach = { .argsz = sizeof(attach) };
    struct vfio_region_info region = { .argsz = sizeof(region) };
    struct iommu_ioas_map map = { .size = sizeof(map) };
    struct vfio_irq_info info = { .argsz = sizeof(info), .index = VFIO_PCI_MSIX_IRQ_INDEX };
    struct { uint32_t argsz, flags, index, start, count; int32_t eventfd[3]; } irq;

    _Static_assert(sizeof(irq) == sizeof(struct vfio_irq_set) + 3 * sizeof(int32_t),
                   "three bounded eventfds follow the VFIO IRQ header");

    if ((vectors != 1 && vectors != 3) || epoch->memory || epoch->device >= 0 ||
        sysconf(_SC_PAGESIZE) != 4096) return 0;
    epoch->iommu = open("/dev/iommu", O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (epoch->iommu < 0 || ioctl(epoch->iommu, IOMMU_IOAS_ALLOC, &ioas)) return 0;
    epoch->ioas = ioas.out_ioas_id;
    epoch->device = cdev_open(bdf);
    if (epoch->device < 0) return 0;
    bind.iommufd = epoch->iommu;
    if (ioctl(epoch->device, VFIO_DEVICE_BIND_IOMMUFD, &bind)) return 0;
    attach.pt_id = epoch->ioas;
    if (ioctl(epoch->device, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &attach)) return 0;
    epoch->attached = 1;
    region.index = VFIO_PCI_BAR0_REGION_INDEX;
    if (ioctl(epoch->device, VFIO_DEVICE_GET_REGION_INFO, &region) || region.size != 16384 ||
        (region.flags & (VFIO_REGION_INFO_FLAG_READ | VFIO_REGION_INFO_FLAG_WRITE)) !=
        (VFIO_REGION_INFO_FLAG_READ | VFIO_REGION_INFO_FLAG_WRITE)) return 0;
    epoch->bar = region.offset;
    region.index = VFIO_PCI_CONFIG_REGION_INDEX;
    if (ioctl(epoch->device, VFIO_DEVICE_GET_REGION_INFO, &region)) return 0;
    epoch->config = region.offset;
    epoch->memory = mmap(NULL, J3_VFIO_MEMORY_BYTES, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (epoch->memory == MAP_FAILED) { epoch->memory = NULL; return 0; }
    memset(epoch->memory, 0, J3_VFIO_MEMORY_BYTES);
    memset(epoch->memory + 8192, 0x5a, 4096);
    map.flags = IOMMU_IOAS_MAP_FIXED_IOVA | IOMMU_IOAS_MAP_READABLE | IOMMU_IOAS_MAP_WRITEABLE;
    map.ioas_id = epoch->ioas;
    map.user_va = (uintptr_t)epoch->memory;
    map.length = J3_VFIO_MEMORY_BYTES;
    map.iova = J3_VFIO_IOVA;
    if (ioctl(epoch->iommu, IOMMU_IOAS_MAP, &map) || map.iova != J3_VFIO_IOVA) return 0;
    epoch->mapped = 1;
    if (ioctl(epoch->device, VFIO_DEVICE_GET_IRQ_INFO, &info) || info.count != vectors ||
        !(info.flags & VFIO_IRQ_INFO_EVENTFD)) return 0;
    epoch->irq = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (epoch->irq < 0) return 0;
    if (reused_irq_fd >= 0 && epoch->irq != reused_irq_fd) {
        if (fcntl(reused_irq_fd, F_GETFD) >= 0 || errno != EBADF ||
            dup3(epoch->irq, reused_irq_fd, O_CLOEXEC) < 0) return 0;
        close(epoch->irq);
        epoch->irq = reused_irq_fd;
    }
    memset(&irq, 0, sizeof(irq));
    irq.argsz = (uint32_t)(sizeof(struct vfio_irq_set) + vectors * sizeof(int32_t));
    irq.flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
    irq.index = VFIO_PCI_MSIX_IRQ_INDEX;
    irq.count = vectors;
    irq.eventfd[0] = epoch->irq;
    for (unsigned i = 1; i < vectors; ++i) {
        epoch->extra_irq[i - 1] = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (epoch->extra_irq[i - 1] < 0) return 0;
        irq.eventfd[i] = epoch->extra_irq[i - 1];
    }
    if (vectors == 3) epoch->routed = 1; /* Cleanup also covers partial setup errors. */
    if (ioctl(epoch->device, VFIO_DEVICE_SET_IRQS, &irq)) return 0;
    epoch->routed = 1;
    return 1;
}

int j3_vfio_open(struct j3_vfio_epoch *epoch, const char *bdf, int reused_irq_fd)
{
    return vfio_open_vectors(epoch, bdf, reused_irq_fd, 1);
}

int j3_vfio_open_mq2(struct j3_vfio_epoch *epoch, const char *bdf)
{
    return vfio_open_vectors(epoch, bdf, -1, 3);
}

int j3_vfio_identify(struct j3_vfio_epoch *epoch, uint16_t cid)
{
    uint8_t command[2] = { 6, 0 };
    uint8_t *sq = epoch->memory;
    if (pwrite(epoch->device, command, 2, (off_t)(epoch->config + 4)) != 2 ||
        !reg_write(epoch, 0x14, 0, 4) || !reg_wait(epoch, 0x1c, 0) ||
        !reg_write(epoch, 0x24, 0x001f001f, 4) ||
        !reg_write(epoch, 0x28, J3_VFIO_IOVA, 8) ||
        !reg_write(epoch, 0x30, J3_VFIO_IOVA + 4096, 8) ||
        !reg_write(epoch, 0x14, 0x00460001, 4) || !reg_wait(epoch, 0x1c, 1)) return 0;
    sq[0] = 6;
    put16(sq + 2, cid);
    put64(sq + 24, J3_VFIO_IOVA + 8192);
    put32(sq + 40, 1);
    __sync_synchronize();
    return reg_write(epoch, 0x1000, 1, 4);
}

int j3_vfio_data_valid(const struct j3_vfio_epoch *epoch)
{
    uint8_t expected[4096] = { 0 };
    const char *serial = "FWLABLINUXV1-0000001";
    const char *model = "SSD Firmware Lab Linux-profile-v1";
    put16(expected, 0xfffa);
    put16(expected + 2, 0xfffa);
    memset(expected + 4, ' ', 20);
    memcpy(expected + 4, serial, strlen(serial));
    memset(expected + 24, ' ', 40);
    memcpy(expected + 24, model, strlen(model));
    memset(expected + 64, ' ', 8);
    memcpy(expected + 64, "LNXV1", 5);
    expected[77] = 1;
    put16(expected + 78, 1);
    put32(expected + 80, 0x00010000);
    expected[512] = 0x66;
    expected[513] = 0x44;
    put32(expected + 516, 1);
    __sync_synchronize();
    return !memcmp(epoch->memory + 8192, expected, sizeof(expected));
}

int j3_vfio_complete(struct j3_vfio_epoch *epoch, uint16_t cid)
{
    volatile uint8_t *cq = epoch->memory + 4096;
    struct pollfd pollfd = { .fd = epoch->irq, .events = POLLIN };
    uint8_t expected[16] = { 0 }, observed[16];
    uint64_t count;
    unsigned iteration, byte;
    for (iteration = 0; iteration < 4000 && !(get16(cq + 14) & 1); ++iteration)
        usleep(1000);
    __sync_synchronize();
    for (byte = 0; byte < sizeof(observed); ++byte) observed[byte] = cq[byte];
    put16(expected + 8, 1);
    put16(expected + 12, cid);
    put16(expected + 14, 1);
    if (memcmp(observed, expected, sizeof(expected)) || !j3_vfio_data_valid(epoch) ||
        poll(&pollfd, 1, 2000) != 1 || !(pollfd.revents & POLLIN) ||
        read(epoch->irq, &count, sizeof(count)) != sizeof(count) || count != 1 ||
        !reg_write(epoch, 0x1004, 1, 4)) return 0;
    return 1;
}

int j3_vfio_quiet(const struct j3_vfio_epoch *epoch, int old_irq)
{
    struct pollfd fds[2] = { { .fd = epoch->irq, .events = POLLIN },
                            { .fd = old_irq, .events = POLLIN } };
    uint8_t pba[8];
    const uint8_t zero[8] = { 0 };
    return poll(fds, old_irq >= 0 ? 2 : 1, 10) == 0 &&
           pread(epoch->device, pba, sizeof(pba), (off_t)(epoch->bar + 0x3000)) == sizeof(pba) &&
           !memcmp(pba, zero, sizeof(pba));
}

int j3_vfio_close(struct j3_vfio_epoch *epoch)
{
    struct vfio_irq_set irq = { .argsz = sizeof(irq),
        .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_TRIGGER,
        .index = VFIO_PCI_MSIX_IRQ_INDEX };
    struct iommu_ioas_unmap unmap = { .size = sizeof(unmap), .ioas_id = epoch->ioas,
        .iova = J3_VFIO_IOVA, .length = J3_VFIO_MEMORY_BYTES };
    struct vfio_device_detach_iommufd_pt detach = { .argsz = sizeof(detach) };
    struct iommu_destroy destroy = { .size = sizeof(destroy), .id = epoch->ioas };
    int good = 1;
    if (epoch->routed && ioctl(epoch->device, VFIO_DEVICE_SET_IRQS, &irq)) good = 0;
    epoch->routed = 0;
    if (epoch->mq_mapped) {
        struct iommu_ioas_unmap mq = { .size = sizeof(mq), .ioas_id = epoch->ioas,
            .iova = MQ_IOVA, .length = MQ_BYTES };
        if (ioctl(epoch->iommu, IOMMU_IOAS_UNMAP, &mq)) good = 0;
        epoch->mq_mapped = 0;
    }
    if (epoch->mapped && ioctl(epoch->iommu, IOMMU_IOAS_UNMAP, &unmap)) good = 0;
    epoch->mapped = 0;
    if (epoch->attached && ioctl(epoch->device, VFIO_DEVICE_DETACH_IOMMUFD_PT, &detach)) good = 0;
    epoch->attached = 0;
    if (epoch->irq >= 0 && close(epoch->irq)) good = 0;
    for (unsigned i = 0; i < 2; ++i) {
        if (epoch->extra_irq[i] >= 0 && close(epoch->extra_irq[i])) good = 0;
        epoch->extra_irq[i] = -1;
    }
    if (epoch->device >= 0 && close(epoch->device)) good = 0;
    if (epoch->ioas && ioctl(epoch->iommu, IOMMU_DESTROY, &destroy)) good = 0;
    epoch->ioas = 0;
    if (epoch->iommu >= 0 && close(epoch->iommu)) good = 0;
    epoch->irq = epoch->device = epoch->iommu = -1;
    return good;
}

void j3_vfio_memory_free(struct j3_vfio_epoch *epoch)
{
    if (epoch->mq_memory) munmap(epoch->mq_memory, MQ_BYTES);
    epoch->mq_memory = NULL;
    if (epoch->memory) munmap(epoch->memory, J3_VFIO_MEMORY_BYTES);
    epoch->memory = NULL;
}

/* Fixed D195 Host producer. It does not execute commands or emulate storage. */
struct mq_ring {
    uint8_t *sq, *cq;
    uint64_t sq_iova, cq_iova;
    unsigned tail, head, phase;
};
struct mq_capture { uint64_t uid; unsigned qid, cid, retired; };
struct mq_host {
    struct j3_vfio_epoch *vfio;
    struct mq_ring ring[3];
    struct mq_capture capture[128];
    unsigned captures, epoch;
    uint64_t lost_uid, retry_uid;
    unsigned lost_epoch, retry_epoch;
    int pidfd, stopped, worker_pid;
    FILE *log;
};

static int mq_logs(struct mq_host *host)
{
    char line[512];
    clearerr(host->log);
    for (;;) {
        long position = ftell(host->log);
        unsigned epoch, qid, cid, op;
        uint64_t uid;
        char *field;
        if (!fgets(line, sizeof(line), host->log)) break;
        if (!strchr(line, '\n')) {
            if (!feof(host->log) || fseek(host->log, position, SEEK_SET)) return 0;
            break;
        }
        if (sscanf(line, "CAPTURE epoch=%u uid=%" SCNu64 " q=%u op=%x",
                   &epoch, &uid, &qid, &op) == 4) {
            field = strstr(line, " cid=");
            if (!field || sscanf(field, " cid=%u", &cid) != 1 || qid > 2 ||
                host->captures == 128 || !epoch || (host->epoch && host->epoch != epoch)) return 0;
            host->epoch = epoch;
            host->capture[host->captures++] = (struct mq_capture){ uid, qid, cid, 0 };
        } else if (sscanf(line, "COMPLETE epoch=%u uid=%" SCNu64, &epoch, &uid) == 2) {
            unsigned i;
            if (epoch != host->epoch) return 0;
            for (i = 0; i < host->captures && host->capture[i].uid != uid; ++i) {}
            if (i == host->captures || host->capture[i].retired) return 0;
            host->capture[i].retired = 1;
        } else if (sscanf(line, "QUEUE_DRAINING_REPLY_LOST uid=%" SCNu64 " epoch=%u cid=%u",
                          &uid, &epoch, &cid) == 3) {
            if (host->lost_uid || cid != 0xd195) return 0;
            host->lost_uid = uid; host->lost_epoch = epoch;
        } else if (sscanf(line, "QUEUE_REPLY_EXACT_RETRY uid=%" SCNu64 " epoch=%u",
                          &uid, &epoch) == 2) {
            if (host->retry_uid) return 0;
            host->retry_uid = uid; host->retry_epoch = epoch;
        } else if (strstr(line, "QUEUE_REPLY_RETRY_MISMATCH")) return 0;
    }
    return !ferror(host->log);
}

static int mq_retired(struct mq_host *host, unsigned qid, unsigned cid)
{
    for (unsigned wait = 0; wait < 4000; ++wait) {
        if (!mq_logs(host)) return 0;
        for (unsigned i = host->captures; i > 0; --i) {
            struct mq_capture *capture = &host->capture[i - 1];
            if (capture->qid == qid && capture->cid == cid) {
                if (capture->retired) return 1;
                break;
            }
        }
        usleep(1000);
    }
    return 0;
}

static int mq_resume(struct mq_host *host)
{
    if (host->stopped && syscall(SYS_pidfd_send_signal, host->pidfd, SIGCONT, NULL, 0)) return 0;
    host->stopped = 0;
    return 1;
}

static int mq_stop(struct mq_host *host)
{
    char path[64], line[256];
    if (syscall(SYS_pidfd_send_signal, host->pidfd, SIGSTOP, NULL, 0)) return 0;
    host->stopped = 1;
    snprintf(path, sizeof(path), "/proc/%d/status", host->worker_pid);
    for (unsigned wait = 0; wait < 1000; ++wait) {
        FILE *status = fopen(path, "r");
        int stopped = 0;
        if (!status) return 0;
        while (fgets(line, sizeof(line), status))
            if (!strncmp(line, "State:", 6) && strchr(line + 6, 'T')) stopped = 1;
        fclose(status);
        if (stopped) return 1;
        usleep(1000);
    }
    return 0;
}

static int mq_submit(struct mq_host *host, unsigned qid, const uint8_t sqe[64])
{
    struct mq_ring *ring = &host->ring[qid];
    memcpy(ring->sq + ring->tail * 64, sqe, 64);
    ring->tail = (ring->tail + 1) % 32;
    __sync_synchronize();
    return reg_write(host->vfio, 0x1000 + qid * 8, ring->tail, 4);
}

static int mq_ready(const struct mq_ring *ring)
{
    return (get16(ring->cq + ring->head * 16 + 14) & 1) == ring->phase;
}

static int mq_ack(struct mq_host *host, unsigned qid)
{
    return reg_write(host->vfio, 0x1004 + qid * 8, host->ring[qid].head, 4);
}

static int mq_complete(struct mq_host *host, unsigned qid, unsigned cid,
                        unsigned sqhd, uint32_t dword0, int acknowledge)
{
    struct mq_ring *ring = &host->ring[qid];
    uint8_t expected[16] = { 0 }, observed[16];
    volatile uint8_t *cq = ring->cq + ring->head * 16;
    unsigned wait;
    for (wait = 0; wait < 4000 && !mq_ready(ring); ++wait) usleep(1000);
    if (wait == 4000) { fprintf(stderr, "MQ_CQE_TIMEOUT q=%u cid=%u\n", qid, cid); return 0; }
    __sync_synchronize();
    for (unsigned byte = 0; byte < 16; ++byte) observed[byte] = cq[byte];
    put32(expected, dword0); put16(expected + 8, (uint16_t)sqhd);
    put16(expected + 10, (uint16_t)qid); put16(expected + 12, (uint16_t)cid);
    put16(expected + 14, (uint16_t)ring->phase);
    if (memcmp(expected, observed, 16)) {
        fprintf(stderr, "MQ_CQE_MISMATCH q=%u cid=%u status=%x sqhd=%u\n",
                qid, cid, get16(observed + 14), get16(observed + 8)); return 0;
    }
    printf("MQ_CQE q=%u cid=%u sqhd=%u head=%u phase=%u dw0=%x exact=1\n",
           qid, cid, sqhd, ring->head, ring->phase, dword0);
    if (++ring->head == 32) { ring->head = 0; ring->phase ^= 1; }
    return !acknowledge || mq_ack(host, qid);
}

static int mq_admin(struct mq_host *host, uint8_t opcode, uint16_t cid,
                     uint32_t cdw10, uint32_t cdw11, uint64_t prp, uint32_t result)
{
    uint8_t command[64] = { 0 };
    command[0] = opcode; put16(command + 2, cid); put64(command + 24, prp);
    put32(command + 40, cdw10); put32(command + 44, cdw11);
    return mq_submit(host, 0, command) &&
        mq_complete(host, 0, cid, host->ring[0].tail, result, 1) && mq_retired(host, 0, cid);
}

static int mq_read(struct mq_host *host, unsigned qid, uint16_t cid,
                    unsigned data_offset, int large)
{
    uint8_t command[64] = { 0 };
    command[0] = 2; put16(command + 2, cid); put32(command + 4, 1);
    put64(command + 24, MQ_IOVA + data_offset);
    put64(command + 40, large ? 8192 : 128);
    if (large) {
        for (unsigned i = 0; i < 255; ++i)
            put64(host->vfio->mq_memory + 0x4000 + i * 8,
                  MQ_IOVA + data_offset + (i + 1) * 4096);
        put64(command + 32, MQ_IOVA + 0x4000); put32(command + 48, 2047);
    }
    memset(host->vfio->mq_memory + data_offset, 0x5a, large ? 1048576 : 512);
    return mq_submit(host, qid, command);
}

static int mq_data(struct mq_host *host, unsigned offset, int large)
{
    const uint8_t *data = host->vfio->mq_memory + offset;
    unsigned length = large ? 1048576 : 512;
    unsigned seed = (large ? 0xb5 : 0x31) + 0x33;
    __sync_synchronize();
    for (unsigned i = 0; i < length; ++i)
        if (data[i] != (uint8_t)(seed + i * 17u + (i >> 8))) return 0;
    return 1;
}

static int mq_create_pair(struct mq_host *host, unsigned qid, uint16_t cid)
{
    struct mq_ring *ring = &host->ring[qid];
    return mq_admin(host, 5, cid, (31u << 16) | qid, (qid << 16) | 3,
                    ring->cq_iova, 0) &&
           mq_admin(host, 1, (uint16_t)(cid + 1), (31u << 16) | qid, (qid << 16) | 1,
                    ring->sq_iova, 0);
}

int j3_vfio_mq2_run(struct j3_vfio_epoch *epoch, int worker_pid,
                    const char *worker_log, uint32_t *tested_epoch)
{
    struct mq_host host = { .vfio = epoch, .pidfd = -1, .worker_pid = worker_pid };
    struct iommu_ioas_map map = { .size = sizeof(map), .ioas_id = epoch->ioas,
        .flags = IOMMU_IOAS_MAP_FIXED_IOVA | IOMMU_IOAS_MAP_READABLE | IOMMU_IOAS_MAP_WRITEABLE,
        .iova = MQ_IOVA, .length = MQ_BYTES };
    struct stat log_stat, stdout_stat;
    char path[64];
    uint8_t old_cq[4096], command[64] = { 0 };
    unsigned first, starts[3];
    int result = 0;
    const char *stage = "worker identity and mapping";
    host.pidfd = (int)syscall(SYS_pidfd_open, worker_pid, 0);
    host.log = fopen(worker_log, "r");
    snprintf(path, sizeof(path), "/proc/%d/fd/1", worker_pid);
    if (host.pidfd < 0 || !host.log || fstat(fileno(host.log), &log_stat) ||
        !S_ISREG(log_stat.st_mode) || stat(path, &stdout_stat) ||
        log_stat.st_dev != stdout_stat.st_dev || log_stat.st_ino != stdout_stat.st_ino ||
        fseek(host.log, 0, SEEK_END)) goto done;
    epoch->mq_memory = mmap(NULL, MQ_BYTES, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (epoch->mq_memory == MAP_FAILED) { epoch->mq_memory = NULL; goto done; }
    memset(epoch->mq_memory, 0, MQ_BYTES);
    map.user_va = (uintptr_t)epoch->mq_memory;
    if (ioctl(epoch->iommu, IOMMU_IOAS_MAP, &map)) goto done;
    epoch->mq_mapped = 1;
    if (map.iova != MQ_IOVA) goto done;
    host.ring[0] = (struct mq_ring){ epoch->memory, epoch->memory + 4096,
                                    J3_VFIO_IOVA, J3_VFIO_IOVA + 4096, 1, 0, 1 };
    for (unsigned q = 1; q < 3; ++q) {
        unsigned offset = (q - 1) * 8192;
        host.ring[q] = (struct mq_ring){ epoch->mq_memory + offset,
            epoch->mq_memory + offset + 4096, MQ_IOVA + offset,
            MQ_IOVA + offset + 4096, 0, 0, 1 };
    }
    stage = "one enable and paired NoQ";
    if (!j3_vfio_identify(epoch, 0xa000) || !mq_complete(&host, 0, 0xa000, 1, 0, 1) ||
        epoch->memory[8192 + 77] != 8 || !mq_retired(&host, 0, 0xa000) ||
        !mq_admin(&host, 9, 0xa001, 7, 0x00010001, 0, 0x00010001) ||
        !mq_create_pair(&host, 1, 0xa002) || !mq_create_pair(&host, 2, 0xa004)) goto done;
    stage = "full Q1 CQ skips to Q2";
    for (unsigned i = 0; i < 31; ++i)
        if (!mq_read(&host, 1, (uint16_t)(0x4000 + i), 0x10000, 0) ||
            !mq_complete(&host, 1, 0x4000 + i, (i + 1) % 32, 0, 0) ||
            !mq_data(&host, 0x10000, 0)) goto done;
    if (!mq_read(&host, 1, 0x401f, 0x10000, 0) ||
        !mq_read(&host, 2, 0x401f, 0x110000, 0) ||
        !mq_read(&host, 2, 0x4020, 0x111000, 0) ||
        !mq_complete(&host, 2, 0x401f, 1, 0, 1) || !mq_data(&host, 0x110000, 0) ||
        !mq_complete(&host, 2, 0x4020, 2, 0, 1) || !mq_data(&host, 0x111000, 0) ||
        mq_ready(&host.ring[1])) goto done;
    for (unsigned i = 0; i < 512; ++i) if (epoch->mq_memory[0x10000 + i] != 0x5a) goto done;
    if (!mq_ack(&host, 1) || !mq_complete(&host, 1, 0x401f, 0, 0, 1) ||
        !mq_data(&host, 0x10000, 0) || !mq_retired(&host, 1, 0x401f) ||
        !mq_retired(&host, 2, 0x4020)) goto done;
    puts("MQ_FULL_CQ_PASS Q1_occupied=31 Q2_completed=2 Q2_sameCID=complete Q1_after_ack=exact");
    stage = "actual grants with both IO queues eligible";
    first = host.captures;
    starts[1] = host.ring[1].tail; starts[2] = host.ring[2].tail;
    if (!mq_stop(&host)) goto done;
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned q = 1; q < 3; ++q)
            if (!mq_read(&host, q, (uint16_t)(0x5000 + i),
                         (q == 1 ? 0x10000 : 0x110000) + i * 4096, 0)) goto done;
    if (!mq_resume(&host)) goto done;
    for (unsigned i = 0; i < 4; ++i)
        for (unsigned q = 1; q < 3; ++q)
            if (!mq_complete(&host, q, 0x5000 + i, (starts[q] + i + 1) % 32, 0, 1) ||
                !mq_data(&host, (q == 1 ? 0x10000 : 0x110000) + i * 4096, 0) ||
                !mq_retired(&host, q, 0x5000 + i)) goto done;
    if (host.captures != first + 8) goto done;
    for (unsigned i = first; i < host.captures; ++i)
        if (!host.capture[i].qid || (i > first && host.capture[i].qid == host.capture[i - 1].qid)) goto done;
    puts("MQ_FAIR_GRANTS_PASS actual_captures=8 alternating=1 sameCID_per_pair=1");
    stage = "Delete drain and exact lost-reply retry";
    /* Actual Admin RETIRE fixes the service cursor; CQE alone is insufficient. */
    if (!mq_admin(&host, 6, 0xa006, 1, 0, J3_VFIO_IOVA + 8192, 0) || !mq_stop(&host)) goto done;
    command[0] = 0; put16(command + 2, 0xd195); put32(command + 40, 1);
    if (!mq_read(&host, 1, 0x6000, 0x10000, 1) || !mq_submit(&host, 0, command) ||
        !mq_resume(&host) ||
        !mq_complete(&host, 1, 0x6000, host.ring[1].tail, 0, 1) || !mq_data(&host, 0x10000, 1) ||
        !mq_complete(&host, 0, 0xd195, host.ring[0].tail, 0, 1) ||
        !mq_retired(&host, 0, 0xd195) || !mq_retired(&host, 1, 0x6000) ||
        !host.lost_uid || host.lost_uid != host.retry_uid ||
        host.lost_epoch != host.epoch || host.retry_epoch != host.epoch) goto done;
    for (unsigned i = 0; i < host.captures; ++i)
        if (host.capture[i].uid == host.lost_uid &&
            (host.capture[i].qid || host.capture[i].cid != 0xd195)) goto done;
    stage = "same epoch recreate on different queue IOVAs";
    if (!mq_admin(&host, 4, 0xa007, 1, 0, 0, 0)) goto done;
    memcpy(old_cq, epoch->mq_memory + 4096, sizeof(old_cq));
    host.ring[1] = (struct mq_ring){ epoch->mq_memory + 0x6000, epoch->mq_memory + 0x7000,
                                    MQ_IOVA + 0x6000, MQ_IOVA + 0x7000, 0, 0, 1 };
    if (!mq_create_pair(&host, 1, 0xa008) ||
        !mq_read(&host, 1, 0x6000, 0x10000, 0) || !mq_complete(&host, 1, 0x6000, 1, 0, 1) ||
        !mq_data(&host, 0x10000, 0) || !mq_retired(&host, 1, 0x6000) ||
        memcmp(old_cq, epoch->mq_memory + 4096, sizeof(old_cq))) goto done;
    for (unsigned q = 0; q < 3; ++q) {
        uint64_t count;
        int fd = q ? epoch->extra_irq[q - 1] : epoch->irq;
        if (read(fd, &count, sizeof(count)) != sizeof(count) || !count) goto done;
        printf("MQ_IRQ vector=%u eventfd_count=%" PRIu64 "\n", q, count);
    }
    *tested_epoch = host.epoch;
    printf("MQ_DELETE_RECREATE_PASS epoch=%u lost_uid=%" PRIu64
           " retry=exact large_read=exact reusedCID=24576 newCQ=exact oldCQ=unchanged\n",
           host.epoch, host.lost_uid);
    result = 1;
done:
    if (!mq_resume(&host)) result = 0;
    if (!result) fprintf(stderr, "MQ_LITERAL_FAIL stage=%s errno=%d\n", stage, errno);
    if (host.pidfd >= 0) close(host.pidfd);
    if (host.log) fclose(host.log);
    return result;
}
