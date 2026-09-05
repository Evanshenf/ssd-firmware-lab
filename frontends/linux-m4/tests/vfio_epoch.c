/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#define _GNU_SOURCE
#include "vfio_epoch.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/iommufd.h>
#include <linux/vfio.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

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

int j3_vfio_open(struct j3_vfio_epoch *epoch, const char *bdf, int reused_irq_fd)
{
    struct iommu_ioas_alloc ioas = { .size = sizeof(ioas) };
    struct vfio_device_bind_iommufd bind = { .argsz = sizeof(bind) };
    struct vfio_device_attach_iommufd_pt attach = { .argsz = sizeof(attach) };
    struct vfio_region_info region = { .argsz = sizeof(region) };
    struct iommu_ioas_map map = { .size = sizeof(map) };
    struct vfio_irq_info info = { .argsz = sizeof(info), .index = VFIO_PCI_MSIX_IRQ_INDEX };
    struct { uint32_t argsz, flags, index, start, count; int32_t eventfd; } irq;

    _Static_assert(sizeof(irq) == sizeof(struct vfio_irq_set) + sizeof(int32_t),
                   "one eventfd follows the VFIO IRQ header");

    if (epoch->memory || epoch->device >= 0 || sysconf(_SC_PAGESIZE) != 4096) return 0;
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
    if (ioctl(epoch->device, VFIO_DEVICE_GET_IRQ_INFO, &info) || info.count != 1 ||
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
    irq.argsz = sizeof(irq);
    irq.flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
    irq.index = VFIO_PCI_MSIX_IRQ_INDEX;
    irq.count = 1;
    irq.eventfd = epoch->irq;
    if (ioctl(epoch->device, VFIO_DEVICE_SET_IRQS, &irq)) return 0;
    epoch->routed = 1;
    return 1;
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
    if (epoch->mapped && ioctl(epoch->iommu, IOMMU_IOAS_UNMAP, &unmap)) good = 0;
    epoch->mapped = 0;
    if (epoch->attached && ioctl(epoch->device, VFIO_DEVICE_DETACH_IOMMUFD_PT, &detach)) good = 0;
    epoch->attached = 0;
    if (epoch->irq >= 0 && close(epoch->irq)) good = 0;
    if (epoch->device >= 0 && close(epoch->device)) good = 0;
    if (epoch->ioas && ioctl(epoch->iommu, IOMMU_DESTROY, &destroy)) good = 0;
    epoch->ioas = 0;
    if (epoch->iommu >= 0 && close(epoch->iommu)) good = 0;
    epoch->irq = epoch->device = epoch->iommu = -1;
    return good;
}

void j3_vfio_memory_free(struct j3_vfio_epoch *epoch)
{
    if (epoch->memory) munmap(epoch->memory, J3_VFIO_MEMORY_BYTES);
    epoch->memory = NULL;
}
