// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: BSD-3-Clause

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/iommufd.h>
#include <linux/vfio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define REGION_SIZE 4096U
#define PATTERN_SIZE 64U

static int check_all_zero(const uint8_t *buffer, size_t length)
{
	size_t index;

	for (index = 0; index < length; index++)
		if (buffer[index] != 0)
			return -1;
	return 0;
}

static void fill_pattern(uint8_t *buffer, size_t length)
{
	size_t index;

	for (index = 0; index < length; index++)
		buffer[index] = (uint8_t)(0x5aU ^ (uint8_t)index);
}

static int expect_ioctl_errno(int fd, unsigned long request, void *arg,
			      int expected_errno)
{
	errno = 0;
	if (ioctl(fd, request, arg) != -1 || errno != expected_errno) {
		fprintf(stderr, "ioctl %#lx: expected errno %d, got %d\n",
			request, expected_errno, errno);
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct vfio_device_detach_iommufd_pt detach = {
		.argsz = sizeof(detach),
	};
	struct vfio_device_attach_iommufd_pt attach = {
		.argsz = sizeof(attach),
	};
	struct vfio_device_bind_iommufd bind = {
		.argsz = sizeof(bind),
	};
	struct iommu_ioas_alloc ioas = {
		.size = sizeof(ioas),
	};
	struct vfio_device_info info = {
		.argsz = sizeof(info),
	};
	struct vfio_region_info region = {
		.argsz = sizeof(region),
		.index = 0,
	};
	struct vfio_region_info invalid_region = {
		.argsz = sizeof(invalid_region),
		.index = 1,
	};
	struct iommu_destroy destroy = {
		.size = sizeof(destroy),
	};
	uint8_t expected[PATTERN_SIZE];
	uint8_t observed[PATTERN_SIZE];
	bool attached = false;
	bool ioas_created = false;
	int iommu_fd = -1;
	int device_fd = -1;
	int status = EXIT_FAILURE;
	ssize_t done;

	if (argc != 2) {
		fprintf(stderr, "usage: %s /dev/vfio/devices/vfioN\n", argv[0]);
		return EXIT_FAILURE;
	}

	device_fd = open(argv[1], O_RDWR | O_CLOEXEC);
	if (device_fd < 0) {
		perror("open VFIO cdev");
		goto out;
	}

	if (expect_ioctl_errno(device_fd, VFIO_DEVICE_GET_INFO, &info,
			       EINVAL))
		goto out;

	iommu_fd = open("/dev/iommu", O_RDWR | O_CLOEXEC);
	if (iommu_fd < 0) {
		perror("open /dev/iommu");
		goto out;
	}

	bind.iommufd = iommu_fd;
	if (ioctl(device_fd, VFIO_DEVICE_BIND_IOMMUFD, &bind)) {
		perror("VFIO_DEVICE_BIND_IOMMUFD");
		goto out;
	}

	if (ioctl(iommu_fd, IOMMU_IOAS_ALLOC, &ioas)) {
		perror("IOMMU_IOAS_ALLOC");
		goto out;
	}
	ioas_created = true;
	destroy.id = ioas.out_ioas_id;

	attach.pt_id = ioas.out_ioas_id;
	if (ioctl(device_fd, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &attach)) {
		perror("VFIO_DEVICE_ATTACH_IOMMUFD_PT");
		goto out;
	}
	attached = true;

	memset(&info, 0, sizeof(info));
	info.argsz = sizeof(info);
	if (ioctl(device_fd, VFIO_DEVICE_GET_INFO, &info)) {
		perror("VFIO_DEVICE_GET_INFO");
		goto out;
	}
	if (info.num_regions != 1 || info.num_irqs != 0 ||
	    !(info.flags & VFIO_DEVICE_FLAGS_RESET)) {
		fprintf(stderr, "unexpected VFIO device info\n");
		goto out;
	}

	if (ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &region)) {
		perror("VFIO_DEVICE_GET_REGION_INFO");
		goto out;
	}
	if (region.size != REGION_SIZE || region.offset != 0 ||
	    region.flags != (VFIO_REGION_INFO_FLAG_READ |
			     VFIO_REGION_INFO_FLAG_WRITE)) {
		fprintf(stderr, "unexpected region contract\n");
		goto out;
	}
	if (expect_ioctl_errno(device_fd, VFIO_DEVICE_GET_REGION_INFO,
			       &invalid_region, EINVAL))
		goto out;

	memset(observed, 0xff, sizeof(observed));
	done = pread(device_fd, observed, sizeof(observed),
		     (off_t)region.offset);
	if (done != (ssize_t)sizeof(observed) ||
	    check_all_zero(observed, sizeof(observed))) {
		fprintf(stderr, "region was not initially zero\n");
		goto out;
	}

	fill_pattern(expected, sizeof(expected));
	done = pwrite(device_fd, expected, sizeof(expected),
		      (off_t)region.offset);
	if (done != (ssize_t)sizeof(expected)) {
		perror("pwrite region");
		goto out;
	}
	memset(observed, 0, sizeof(observed));
	done = pread(device_fd, observed, sizeof(observed),
		     (off_t)region.offset);
	if (done != (ssize_t)sizeof(observed) ||
	    memcmp(observed, expected, sizeof(observed))) {
		fprintf(stderr, "region read/write mismatch\n");
		goto out;
	}

	done = pread(device_fd, observed, sizeof(observed), REGION_SIZE);
	if (done != 0) {
		fprintf(stderr, "read beyond region did not return EOF\n");
		goto out;
	}
	done = pwrite(device_fd, expected, sizeof(expected), REGION_SIZE);
	if (done != 0) {
		fprintf(stderr, "write beyond region did not return zero\n");
		goto out;
	}
	done = pwrite(device_fd, expected, sizeof(expected), REGION_SIZE - 8);
	if (done != 8) {
		fprintf(stderr, "tail write was not bounded to 8 bytes\n");
		goto out;
	}
	memset(observed, 0, sizeof(observed));
	done = pread(device_fd, observed, sizeof(observed), REGION_SIZE - 8);
	if (done != 8 || memcmp(observed, expected, 8)) {
		fprintf(stderr, "tail read/write mismatch\n");
		goto out;
	}

	if (ioctl(device_fd, VFIO_DEVICE_RESET)) {
		perror("VFIO_DEVICE_RESET");
		goto out;
	}
	memset(observed, 0xff, sizeof(observed));
	done = pread(device_fd, observed, sizeof(observed),
		     (off_t)region.offset);
	if (done != (ssize_t)sizeof(observed) ||
	    check_all_zero(observed, sizeof(observed))) {
		fprintf(stderr, "reset did not clear region\n");
		goto out;
	}

	if (ioctl(device_fd, VFIO_DEVICE_DETACH_IOMMUFD_PT, &detach)) {
		perror("VFIO_DEVICE_DETACH_IOMMUFD_PT");
		goto out;
	}
	attached = false;

	if (ioctl(iommu_fd, IOMMU_DESTROY, &destroy)) {
		perror("IOMMU_DESTROY");
		goto out;
	}
	ioas_created = false;

	printf("VFIO cdev V0 BIND/ATTACH/region/reset/cleanup: PASS "
	       "devid=%u ioas=%u\n",
	       bind.out_devid, ioas.out_ioas_id);
	status = EXIT_SUCCESS;

out:
	if (attached) {
		if (ioctl(device_fd, VFIO_DEVICE_DETACH_IOMMUFD_PT,
			  &detach)) {
			perror("cleanup DETACH");
			/* close forces VFIO unbind before attempting IOAS destroy */
			close(device_fd);
			device_fd = -1;
		}
		attached = false;
	}
	if (ioas_created && ioctl(iommu_fd, IOMMU_DESTROY, &destroy))
		perror("cleanup DESTROY");
	if (device_fd >= 0)
		close(device_fd);
	if (iommu_fd >= 0)
		close(iommu_fd);
	return status;
}
