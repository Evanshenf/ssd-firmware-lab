// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: 2026 Evanshenf

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <linux/nvme_ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define FWLAB_NVME_WRITE 0x01
#define FWLAB_NVME_READ 0x02
#define FWLAB_NVME_FLUSH 0x00
#define FWLAB_NSID 1U
#define FWLAB_LBA 64U
#define FWLAB_LBA_BYTES 512U
#define FWLAB_TRANSFER_BYTES 8192U
#define FWLAB_NLB (FWLAB_TRANSFER_BYTES / FWLAB_LBA_BYTES)

static int submit(int fd, uint8_t opcode, void *buffer)
{
	struct nvme_passthru_cmd cmd = {0};

	cmd.opcode = opcode;
	cmd.nsid = FWLAB_NSID;
	if (buffer) {
		cmd.addr = (uintptr_t)buffer;
		cmd.data_len = FWLAB_TRANSFER_BYTES;
		cmd.cdw10 = FWLAB_LBA;
		cmd.cdw11 = 0;
		cmd.cdw12 = FWLAB_NLB - 1U;
	}
	return ioctl(fd, NVME_IOCTL_IO_CMD, &cmd);
}

int main(int argc, char **argv)
{
	unsigned char *write_allocation = NULL;
	unsigned char *read_allocation = NULL;
	unsigned char *write_buffer;
	unsigned char *read_buffer;
	long page_size = sysconf(_SC_PAGESIZE);
	size_t allocation_size;
	int fd = -1;
	int status = 1;
	size_t i;

	if (argc != 2 || page_size <= 0) {
		fprintf(stderr, "usage: %s /dev/nvmeXn1\n", argv[0]);
		return 2;
	}
	allocation_size = (size_t)page_size * 3U;
	if (posix_memalign((void **)&write_allocation, (size_t)page_size,
			   allocation_size) ||
	    posix_memalign((void **)&read_allocation, (size_t)page_size,
			   allocation_size)) {
		fprintf(stderr, "aligned allocation failed\n");
		goto out;
	}
	write_buffer = write_allocation + 1;
	read_buffer = read_allocation + 1;
	for (i = 0; i < FWLAB_TRANSFER_BYTES; i++)
		write_buffer[i] = (unsigned char)(i * 37U + 0x5aU);
	memset(read_buffer, 0, FWLAB_TRANSFER_BYTES);

	fd = open(argv[1], O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", argv[1], strerror(errno));
		goto out;
	}
	if (submit(fd, FWLAB_NVME_WRITE, write_buffer) ||
	    submit(fd, FWLAB_NVME_FLUSH, NULL) ||
	    submit(fd, FWLAB_NVME_READ, read_buffer)) {
		fprintf(stderr, "NVMe passthrough: %s\n", strerror(errno));
		goto out;
	}
	if (memcmp(write_buffer, read_buffer, FWLAB_TRANSFER_BYTES)) {
		fprintf(stderr, "unaligned 8-KiB compare failed\n");
		goto out;
	}

	printf("native unaligned 8-KiB PRP-list write/read/flush: PASS\n");
	status = 0;

out:
	if (fd >= 0)
		close(fd);
	free(read_allocation);
	free(write_allocation);
	return status;
}
