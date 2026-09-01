// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: 2026 Evanshenf

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/iommufd.h>
#include <linux/vfio.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define FWLAB_BAR_SIZE (16ULL * 1024ULL * 1024ULL)
#define FWLAB_REG_EPOCH 0x0008
#define FWLAB_REG_DOORBELL 0x0100
#define FWLAB_REG_ACK 0x0104
#define FWLAB_REG_IRQ_TRIGGER 0x0110
#define FWLAB_REG_IRQ_ACK 0x0114
#define FWLAB_BAR_ACK_XOR 0xa5a55a5aU
#define FWLAB_TEST_IOVA 0x40000000ULL
#define FWLAB_NVME_REG_VS 0x0008
#define FWLAB_NVME_REG_CC 0x0014
#define FWLAB_NVME_REG_CSTS 0x001c
#define FWLAB_NVME_MQES 31U
#define FWLAB_NVME_CAP_CQR (1U << 16)
#define FWLAB_NVME_VS_1_0 0x00010000U

struct fwlab_irq_eventfd {
	struct vfio_irq_set set;
	int32_t eventfd;
};

static int fail_errno(const char *operation)
{
	fprintf(stderr, "%s: %s\n", operation, strerror(errno));
	return 1;
}

static int wait_register(volatile uint32_t *reg, uint32_t expected,
			 unsigned int timeout_ms)
{
	unsigned int elapsed;

	for (elapsed = 0; elapsed < timeout_ms; elapsed++) {
		if (*reg == expected)
			return 0;
		usleep(1000);
	}
	errno = ETIMEDOUT;
	return -1;
}

static int wait_eventfd(int eventfd)
{
	struct pollfd pfd = { .fd = eventfd, .events = POLLIN };
	uint64_t value;
	int ret;

	ret = poll(&pfd, 1, 1000);
	if (ret <= 0) {
		if (ret == 0)
			errno = ETIMEDOUT;
		return -1;
	}
	if (read(eventfd, &value, sizeof(value)) != (ssize_t)sizeof(value))
		return -1;
	if (value != 1) {
		errno = EIO;
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct iommu_ioas_alloc ioas = { .size = sizeof(ioas) };
	struct iommu_ioas_map map = {0};
	struct iommu_ioas_unmap unmap = {0};
	struct iommu_destroy destroy = {0};
	struct vfio_device_bind_iommufd bind = {0};
	struct vfio_device_attach_iommufd_pt attach = {0};
	struct vfio_device_detach_iommufd_pt detach = {0};
	struct vfio_device_info device_info = {0};
	struct vfio_region_info region = {0};
	struct vfio_irq_info irq_info = {0};
	struct fwlab_irq_eventfd irq_set = {0};
	struct vfio_irq_set irq_disable = {0};
	volatile uint32_t *bar = MAP_FAILED;
	void *dma_page = MAP_FAILED;
	long page_size = sysconf(_SC_PAGESIZE);
	int iommu_fd = -1;
	int device_fd = -1;
	int irq_fd = -1;
	int attached = 0;
	int mapped = 0;
	int irq_assigned = 0;
	int status = 1;
	uint32_t epoch;
	uint32_t token;
	int nvme_admission = 0;

	setvbuf(stderr, NULL, _IONBF, 0);
	if (argc == 3 && !strcmp(argv[2], "--nvme-admission"))
		nvme_admission = 1;
	if ((argc != 2 && !nvme_admission) || page_size <= 0) {
		fprintf(stderr,
			"usage: %s /dev/vfio/devices/vfioX [--nvme-admission]\n",
			argv[0]);
		return 2;
	}

	iommu_fd = open("/dev/iommu", O_RDWR | O_CLOEXEC);
	if (iommu_fd < 0) {
		fail_errno("open /dev/iommu");
		goto out;
	}
	if (ioctl(iommu_fd, IOMMU_IOAS_ALLOC, &ioas)) {
		fail_errno("IOMMU_IOAS_ALLOC");
		goto out;
	}
	fprintf(stderr, "stage: IOAS allocated id=%u\n", ioas.out_ioas_id);

	device_fd = open(argv[1], O_RDWR | O_CLOEXEC);
	if (device_fd < 0) {
		fail_errno("open VFIO cdev");
		goto out;
	}
	bind.argsz = sizeof(bind);
	bind.iommufd = iommu_fd;
	if (ioctl(device_fd, VFIO_DEVICE_BIND_IOMMUFD, &bind)) {
		fail_errno("VFIO_DEVICE_BIND_IOMMUFD");
		goto out;
	}
	fprintf(stderr, "stage: VFIO cdev bound devid=%u\n", bind.out_devid);
	attach.argsz = sizeof(attach);
	attach.pt_id = ioas.out_ioas_id;
	if (ioctl(device_fd, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &attach)) {
		fail_errno("VFIO_DEVICE_ATTACH_IOMMUFD_PT");
		goto out;
	}
	attached = 1;
	fprintf(stderr, "stage: IOAS attached pt=%u\n", attach.pt_id);

	device_info.argsz = sizeof(device_info);
	if (ioctl(device_fd, VFIO_DEVICE_GET_INFO, &device_info)) {
		fail_errno("VFIO_DEVICE_GET_INFO");
		goto out;
	}
	if (!(device_info.flags & VFIO_DEVICE_FLAGS_PCI) ||
	    !(device_info.flags & VFIO_DEVICE_FLAGS_RESET)) {
		fprintf(stderr, "VFIO device lacks PCI/reset flags: %#x\n",
			device_info.flags);
		goto out;
	}

	region.argsz = sizeof(region);
	region.index = VFIO_PCI_BAR0_REGION_INDEX;
	if (ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &region)) {
		fail_errno("VFIO_DEVICE_GET_REGION_INFO BAR0");
		goto out;
	}
	if (region.size != FWLAB_BAR_SIZE ||
	    (region.flags & (VFIO_REGION_INFO_FLAG_READ |
			     VFIO_REGION_INFO_FLAG_WRITE |
			     VFIO_REGION_INFO_FLAG_MMAP)) !=
		    (VFIO_REGION_INFO_FLAG_READ | VFIO_REGION_INFO_FLAG_WRITE |
		     VFIO_REGION_INFO_FLAG_MMAP)) {
		fprintf(stderr, "unexpected BAR0 size/flags: %#llx/%#x\n",
			(unsigned long long)region.size, region.flags);
		goto out;
	}
	fprintf(stderr, "stage: BAR0 region flags=%#x offset=%#llx\n",
		region.flags, (unsigned long long)region.offset);
	bar = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE, MAP_SHARED,
		   device_fd, (off_t)region.offset);
	if (bar == MAP_FAILED) {
		fail_errno("mmap BAR0");
		goto out;
	}
	fprintf(stderr, "stage: BAR0 mmap established\n");

	if (nvme_admission) {
		if ((bar[0] & (0xffffU | FWLAB_NVME_CAP_CQR)) !=
				(FWLAB_NVME_MQES | FWLAB_NVME_CAP_CQR) ||
		    bar[FWLAB_NVME_REG_VS / sizeof(*bar)] !=
				FWLAB_NVME_VS_1_0) {
			fprintf(stderr, "unexpected native-NVMe CAP/VS\n");
			goto out;
		}
		fprintf(stderr, "stage: native-NVMe CAP/VS observed\n");
	} else {
		token = 0x5a5a1234U;
		fprintf(stderr, "stage: BAR0 first MMIO write\n");
		bar[FWLAB_REG_DOORBELL / sizeof(*bar)] = token;
		__sync_synchronize();
		if (wait_register(&bar[FWLAB_REG_ACK / sizeof(*bar)],
				  token ^ FWLAB_BAR_ACK_XOR, 1000)) {
			fail_errno("VFIO BAR doorbell/ack");
			goto out;
		}
		fprintf(stderr, "stage: BAR doorbell acknowledged\n");
	}

	dma_page = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (dma_page == MAP_FAILED) {
		fail_errno("mmap DMA page");
		goto out;
	}
	memset(dma_page, 0x69, (size_t)page_size);
	map.size = sizeof(map);
	map.flags = IOMMU_IOAS_MAP_FIXED_IOVA | IOMMU_IOAS_MAP_READABLE |
		    IOMMU_IOAS_MAP_WRITEABLE;
	map.ioas_id = ioas.out_ioas_id;
	map.user_va = (uintptr_t)dma_page;
	map.length = (uint64_t)page_size;
	map.iova = FWLAB_TEST_IOVA;
	if (ioctl(iommu_fd, IOMMU_IOAS_MAP, &map)) {
		fail_errno("IOMMU_IOAS_MAP");
		goto out;
	}
	mapped = 1;
	fprintf(stderr, "stage: IOAS page mapped\n");

	irq_info.argsz = sizeof(irq_info);
	irq_info.index = VFIO_PCI_MSIX_IRQ_INDEX;
	if (ioctl(device_fd, VFIO_DEVICE_GET_IRQ_INFO, &irq_info)) {
		fail_errno("VFIO_DEVICE_GET_IRQ_INFO MSI-X");
		goto out;
	}
	if (irq_info.count != 1 || !(irq_info.flags & VFIO_IRQ_INFO_EVENTFD)) {
		fprintf(stderr, "unexpected MSI-X count/flags: %u/%#x\n",
			irq_info.count, irq_info.flags);
		goto out;
	}
	irq_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (irq_fd < 0) {
		fail_errno("eventfd");
		goto out;
	}
	irq_set.set.argsz = sizeof(irq_set);
	irq_set.set.flags = VFIO_IRQ_SET_DATA_EVENTFD |
			    VFIO_IRQ_SET_ACTION_TRIGGER;
	irq_set.set.index = VFIO_PCI_MSIX_IRQ_INDEX;
	irq_set.set.start = 0;
	irq_set.set.count = 1;
	irq_set.eventfd = irq_fd;
	if (ioctl(device_fd, VFIO_DEVICE_SET_IRQS, &irq_set)) {
		fail_errno("VFIO_DEVICE_SET_IRQS eventfd");
		goto out;
	}
	irq_assigned = 1;
	fprintf(stderr, "stage: MSI-X eventfd assigned\n");
	if (!nvme_admission) {
		token = 0x2468ace0U;
		bar[FWLAB_REG_IRQ_TRIGGER / sizeof(*bar)] = token;
		__sync_synchronize();
		if (wait_eventfd(irq_fd) ||
		    wait_register(&bar[FWLAB_REG_IRQ_ACK / sizeof(*bar)], token,
				  1000)) {
			fail_errno("VFIO MSI-X eventfd delivery");
			goto out;
		}
		fprintf(stderr, "stage: MSI-X eventfd delivered\n");
	} else {
		fprintf(stderr,
			"stage: MSI-X eventfd route admitted; delivery is L2-gated\n");
	}

	irq_disable.argsz = sizeof(irq_disable);
	irq_disable.flags = VFIO_IRQ_SET_DATA_NONE |
			    VFIO_IRQ_SET_ACTION_TRIGGER;
	irq_disable.index = VFIO_PCI_MSIX_IRQ_INDEX;
	irq_disable.start = 0;
	irq_disable.count = 0;
	if (ioctl(device_fd, VFIO_DEVICE_SET_IRQS, &irq_disable)) {
		fail_errno("VFIO_DEVICE_SET_IRQS disable");
		goto out;
	}
	irq_assigned = 0;
	fprintf(stderr, "stage: MSI-X eventfd revoked\n");

	epoch = bar[FWLAB_REG_EPOCH / sizeof(*bar)];
	fprintf(stderr, "stage: VFIO reset\n");
	if (ioctl(device_fd, VFIO_DEVICE_RESET)) {
		fail_errno("VFIO_DEVICE_RESET");
		goto out;
	}
	if (nvme_admission) {
		if ((bar[0] & (0xffffU | FWLAB_NVME_CAP_CQR)) !=
				(FWLAB_NVME_MQES | FWLAB_NVME_CAP_CQR) ||
		    bar[FWLAB_NVME_REG_VS / sizeof(*bar)] !=
				FWLAB_NVME_VS_1_0 ||
		    bar[FWLAB_NVME_REG_CC / sizeof(*bar)] != 0 ||
		    bar[FWLAB_NVME_REG_CSTS / sizeof(*bar)] != 0) {
			fprintf(stderr,
				"VFIO reset did not produce cold NVMe state\n");
			goto out;
		}
	} else if (bar[FWLAB_REG_EPOCH / sizeof(*bar)] == epoch ||
		   bar[FWLAB_REG_DOORBELL / sizeof(*bar)] != 0 ||
		   bar[FWLAB_REG_IRQ_TRIGGER / sizeof(*bar)] != 0) {
		fprintf(stderr, "VFIO reset did not produce cold BAR state\n");
		goto out;
	}

	status = 0;
	if (nvme_admission)
		printf("VFIO NVMe admission cdev/IOMMUFD/BAR/IOAS/MSI-X/reset: PASS devid=%u pt=%u\n",
		       bind.out_devid, attach.pt_id);
	else
		printf("VFIO cdev/IOMMUFD/BAR/IOAS/MSI-X/reset: PASS devid=%u pt=%u\n",
		       bind.out_devid, attach.pt_id);

out:
	if (irq_assigned && device_fd >= 0) {
		irq_disable.argsz = sizeof(irq_disable);
		irq_disable.flags = VFIO_IRQ_SET_DATA_NONE |
				    VFIO_IRQ_SET_ACTION_TRIGGER;
		irq_disable.index = VFIO_PCI_MSIX_IRQ_INDEX;
		irq_disable.count = 0;
		(void)ioctl(device_fd, VFIO_DEVICE_SET_IRQS, &irq_disable);
	}
	if (mapped) {
		unmap.size = sizeof(unmap);
		unmap.ioas_id = ioas.out_ioas_id;
		unmap.iova = FWLAB_TEST_IOVA;
		unmap.length = (uint64_t)page_size;
		if (ioctl(iommu_fd, IOMMU_IOAS_UNMAP, &unmap) && !status)
			status = fail_errno("IOMMU_IOAS_UNMAP");
	}
	if (attached) {
		detach.argsz = sizeof(detach);
		if (ioctl(device_fd, VFIO_DEVICE_DETACH_IOMMUFD_PT, &detach) &&
		    !status)
			status = fail_errno("VFIO_DEVICE_DETACH_IOMMUFD_PT");
	}
	if (bar != MAP_FAILED)
		munmap((void *)bar, (size_t)page_size);
	if (dma_page != MAP_FAILED)
		munmap(dma_page, (size_t)page_size);
	if (irq_fd >= 0)
		close(irq_fd);
	if (device_fd >= 0)
		close(device_fd);
	if (ioas.out_ioas_id && iommu_fd >= 0) {
		destroy.size = sizeof(destroy);
		destroy.id = ioas.out_ioas_id;
		(void)ioctl(iommu_fd, IOMMU_DESTROY, &destroy);
	}
	if (iommu_fd >= 0)
		close(iommu_fd);
	return status;
}
