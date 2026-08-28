// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: BSD-3-Clause

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/iommufd.h>
#include <linux/vfio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "fwlab_c21_a1.h"

#define C2_PAGE_SIZE 4096U
#define C2_PRIMARY_IOVA UINT64_C(0x100000000)
#define C2_EMPTY_IOVA UINT64_C(0x200000000)
#define C2_DATA_WINDOW FWLAB_C21_MAX_COPY_LENGTH

_Static_assert(FWLAB_C21_RECORD_SIZE == 64U,
	       "C2.2 requires the exact C2.1 record size");
_Static_assert(FWLAB_C21_REQ_RESERVED2 + 8U == FWLAB_C21_RECORD_SIZE,
	       "request offsets no longer describe a complete record");
_Static_assert(FWLAB_C21_RES_RESERVED2 + 8U == FWLAB_C21_RECORD_SIZE,
	       "result offsets no longer describe a complete record");
_Static_assert(FWLAB_C21_ST_RESERVED2 + 8U == FWLAB_C21_RECORD_SIZE,
	       "state offsets no longer describe a complete record");

struct c2_region {
	uint32_t index;
	uint32_t flags;
	uint64_t offset;
	uint64_t size;
};

struct c2_state {
	uint16_t device_state;
	uint32_t flags;
	uint64_t generation;
	uint64_t last_sequence;
	uint64_t next_sequence;
};

struct c2_result {
	uint16_t operation;
	uint32_t flags;
	uint64_t sequence;
	uint64_t generation;
	uint64_t iova;
	uint32_t requested_length;
	int32_t op_errno;
};

struct c2_session {
	int device_fd;
	int iommu_fd;
	bool attached;
	bool mapped;
	bool ioas_created;
	uint32_t ioas_id;
	uint64_t iova;
	unsigned char *page;
	struct c2_region data_region;
	struct c2_region control_region;
};

static int validate_region_layout(const struct c2_region regions[2])
{
	const uint32_t exact_flags = VFIO_REGION_INFO_FLAG_READ |
				     VFIO_REGION_INFO_FLAG_WRITE;
	uint64_t end[2];
	unsigned int index;

	for (index = 0; index < 2; index++) {
		if (regions[index].size != FWLAB_C21_CONTROL_REGION_SIZE ||
		    regions[index].flags != exact_flags ||
		    regions[index].offset > UINT64_MAX - regions[index].size ||
		    regions[index].offset > (uint64_t)INT64_MAX ||
		    regions[index].size - 1U >
			    (uint64_t)INT64_MAX - regions[index].offset)
			return -ERANGE;
		end[index] = regions[index].offset + regions[index].size;
	}
	if (!(end[0] <= regions[1].offset || end[1] <= regions[0].offset))
		return -ERANGE;
	return 0;
}

static int selftest_region_layout(void)
{
	const uint32_t rw_flags = VFIO_REGION_INFO_FLAG_READ |
				  VFIO_REGION_INFO_FLAG_WRITE;
	struct c2_region regions[2] = {
		{ .index = 0, .flags = rw_flags, .offset = 0x1000,
		  .size = FWLAB_C21_CONTROL_REGION_SIZE },
		{ .index = 1, .flags = rw_flags, .offset = 0x2000,
		  .size = FWLAB_C21_CONTROL_REGION_SIZE },
	};

	if (validate_region_layout(regions))
		return -1;
	regions[1].offset = 0x1800;
	if (!validate_region_layout(regions))
		return -1;
	regions[1].offset = 0x2000;
	regions[0].size = FWLAB_C21_CONTROL_REGION_SIZE - 1U;
	if (!validate_region_layout(regions))
		return -1;
	regions[0].size = FWLAB_C21_CONTROL_REGION_SIZE;
	regions[0].flags = rw_flags | VFIO_REGION_INFO_FLAG_MMAP;
	if (!validate_region_layout(regions))
		return -1;
	regions[0].flags = rw_flags;
	regions[0].offset = UINT64_MAX - 2048U;
	if (!validate_region_layout(regions))
		return -1;
	regions[0].offset = (uint64_t)INT64_MAX - 2048U;
	if (!validate_region_layout(regions))
		return -1;
	printf("C2.2 pure region-layout selftest: PASS\n");
	return 0;
}

static uint16_t get_le16(const unsigned char *source)
{
	return (uint16_t)source[0] | ((uint16_t)source[1] << 8);
}

static uint32_t get_le32(const unsigned char *source)
{
	return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
	       ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static uint64_t get_le64(const unsigned char *source)
{
	return (uint64_t)get_le32(source) |
	       ((uint64_t)get_le32(source + 4) << 32);
}

static void put_le16(unsigned char *destination, uint16_t value)
{
	destination[0] = (unsigned char)value;
	destination[1] = (unsigned char)(value >> 8);
}

static void put_le32(unsigned char *destination, uint32_t value)
{
	destination[0] = (unsigned char)value;
	destination[1] = (unsigned char)(value >> 8);
	destination[2] = (unsigned char)(value >> 16);
	destination[3] = (unsigned char)(value >> 24);
}

static void put_le64(unsigned char *destination, uint64_t value)
{
	put_le32(destination, (uint32_t)value);
	put_le32(destination + 4, (uint32_t)(value >> 32));
}

static int32_t decode_s32(uint32_t bits)
{
	int64_t value = bits;

	if (bits & UINT32_C(0x80000000))
		value -= INT64_C(0x100000000);
	return (int32_t)value;
}

static int checked_span(const struct c2_region *region, uint64_t relative,
			uint64_t length, off_t *position)
{
	uint64_t absolute;

	if (relative > region->size || length > region->size - relative ||
	    region->offset > UINT64_MAX - relative)
		return -ERANGE;
	absolute = region->offset + relative;
	if (absolute > (uint64_t)INT64_MAX ||
	    (length && length - 1U > (uint64_t)INT64_MAX - absolute))
		return -ERANGE;
	*position = (off_t)absolute;
	return 0;
}

static int checked_position(const struct c2_region *region, uint64_t relative,
			    off_t *position)
{
	return checked_span(region, relative, FWLAB_C21_RECORD_SIZE, position);
}

static int pread_exact(int fd, void *buffer, size_t length, off_t offset)
{
	ssize_t done;

	do {
		done = pread(fd, buffer, length, offset);
	} while (done < 0 && errno == EINTR);
	if (done < 0)
		return -errno;
	return done == (ssize_t)length ? 0 : -EIO;
}

static int pwrite_exact(int fd, const void *buffer, size_t length, off_t offset)
{
	ssize_t done;

	do {
		done = pwrite(fd, buffer, length, offset);
	} while (done < 0 && errno == EINTR);
	if (done < 0)
		return -errno;
	return done == (ssize_t)length ? 0 : -EIO;
}

static int decode_state(const unsigned char wire[FWLAB_C21_RECORD_SIZE],
			struct c2_state *state)
{
	uint32_t flags;
	uint64_t last_sequence;
	uint64_t next_sequence;
	uint16_t device_state;

	if (get_le32(wire + FWLAB_C21_ST_MAGIC) != FWLAB_C21_STATE_MAGIC ||
	    get_le16(wire + FWLAB_C21_ST_ABI_MAJOR) !=
		    FWLAB_C21_A1_ABI_MAJOR ||
	    get_le16(wire + FWLAB_C21_ST_ABI_MINOR) !=
		    FWLAB_C21_A1_ABI_MINOR ||
	    get_le16(wire + FWLAB_C21_ST_STRUCT_SIZE) !=
		    FWLAB_C21_RECORD_SIZE ||
	    get_le64(wire + FWLAB_C21_ST_RESERVED1) ||
	    get_le64(wire + FWLAB_C21_ST_RESERVED2) ||
	    get_le32(wire + FWLAB_C21_ST_MAX_COPY_LENGTH) !=
		    FWLAB_C21_MAX_COPY_LENGTH ||
	    get_le32(wire + FWLAB_C21_ST_DATA_REGION_SIZE) !=
		    FWLAB_C21_DATA_REGION_SIZE)
		return -EPROTO;
	device_state = get_le16(wire + FWLAB_C21_ST_DEVICE_STATE);
	flags = get_le32(wire + FWLAB_C21_ST_FLAGS);
	last_sequence = get_le64(wire + FWLAB_C21_ST_LAST_SEQUENCE);
	next_sequence = get_le64(wire + FWLAB_C21_ST_NEXT_SEQUENCE);
	if (device_state > FWLAB_C21_STATE_DEAD ||
	    (flags & ~FWLAB_C21_ST_F_ALL) ||
	    (device_state == FWLAB_C21_STATE_OPEN_UNATTACHED &&
	     ((flags & (FWLAB_C21_ST_F_OPEN | FWLAB_C21_ST_F_ATTACHED)) !=
		     FWLAB_C21_ST_F_OPEN)) ||
	    (device_state == FWLAB_C21_STATE_OPEN_ATTACHED &&
	     ((flags & (FWLAB_C21_ST_F_OPEN | FWLAB_C21_ST_F_ATTACHED)) !=
		     (FWLAB_C21_ST_F_OPEN | FWLAB_C21_ST_F_ATTACHED))) ||
	    ((device_state == FWLAB_C21_STATE_CLOSED ||
	      device_state == FWLAB_C21_STATE_CLOSING) && flags) ||
	    (device_state == FWLAB_C21_STATE_DEAD &&
	     ((flags & FWLAB_C21_ST_F_ALL) != FWLAB_C21_ST_F_DEAD ||
	      get_le64(wire + FWLAB_C21_ST_GENERATION) != UINT64_MAX)) ||
	    (device_state != FWLAB_C21_STATE_DEAD &&
	     (flags & FWLAB_C21_ST_F_DEAD)) ||
	    ((flags & FWLAB_C21_ST_F_SEQUENCE_EXHAUSTED) &&
	     (last_sequence != UINT64_MAX || next_sequence)) ||
	    (!(flags & FWLAB_C21_ST_F_SEQUENCE_EXHAUSTED) &&
	     (last_sequence == UINT64_MAX || next_sequence != last_sequence + 1U)) ||
	    ((flags & (FWLAB_C21_ST_F_RESULT_VALID |
		       FWLAB_C21_ST_F_SEQUENCE_EXHAUSTED)) &&
	     !(flags & FWLAB_C21_ST_F_OPEN)) ||
	    ((flags & FWLAB_C21_ST_F_OPEN) &&
	     !get_le64(wire + FWLAB_C21_ST_GENERATION)))
		return -EPROTO;
	state->device_state = device_state;
	state->flags = flags;
	state->generation = get_le64(wire + FWLAB_C21_ST_GENERATION);
	state->last_sequence = last_sequence;
	state->next_sequence = next_sequence;
	return 0;
}

static int decode_result(const unsigned char wire[FWLAB_C21_RECORD_SIZE],
			 struct c2_result *result)
{
	uint32_t errno_bits;

	if (get_le32(wire + FWLAB_C21_RES_MAGIC) != FWLAB_C21_RESULT_MAGIC ||
	    get_le16(wire + FWLAB_C21_RES_ABI_MAJOR) !=
		    FWLAB_C21_A1_ABI_MAJOR ||
	    get_le16(wire + FWLAB_C21_RES_ABI_MINOR) !=
		    FWLAB_C21_A1_ABI_MINOR ||
	    get_le16(wire + FWLAB_C21_RES_STRUCT_SIZE) !=
		    FWLAB_C21_RECORD_SIZE ||
	    get_le64(wire + FWLAB_C21_RES_RESERVED1) ||
	    get_le64(wire + FWLAB_C21_RES_RESERVED2))
		return -EPROTO;
	result->operation = get_le16(wire + FWLAB_C21_RES_OPERATION);
	result->flags = get_le32(wire + FWLAB_C21_RES_FLAGS);
	result->sequence = get_le64(wire + FWLAB_C21_RES_SEQUENCE);
	result->generation = get_le64(wire + FWLAB_C21_RES_GENERATION);
	result->iova = get_le64(wire + FWLAB_C21_RES_IOVA);
	result->requested_length =
		get_le32(wire + FWLAB_C21_RES_REQUESTED_LENGTH);
	errno_bits = get_le32(wire + FWLAB_C21_RES_OP_ERRNO);
	result->op_errno = decode_s32(errno_bits);
	if ((result->flags & ~FWLAB_C21_RES_F_ALL) ||
	    !(result->flags & FWLAB_C21_RES_F_VALID) ||
	    (result->operation != FWLAB_C21_OP_COPY_IOAS_TO_BUFFER &&
	     result->operation != FWLAB_C21_OP_COPY_BUFFER_TO_IOAS) ||
	    !result->sequence || !result->generation ||
	    !result->requested_length ||
	    result->requested_length > FWLAB_C21_MAX_COPY_LENGTH ||
	    result->op_errno > 0 || result->op_errno < -4095 ||
	    (!!(result->flags & FWLAB_C21_RES_F_DEST_MAY_HAVE_PARTIAL) !=
	     (result->operation == FWLAB_C21_OP_COPY_BUFFER_TO_IOAS &&
	      result->op_errno != 0)))
		return -EPROTO;
	return 0;
}

static void encode_request(unsigned char wire[FWLAB_C21_RECORD_SIZE],
			   uint16_t operation, uint64_t sequence,
			   uint64_t generation, uint64_t iova,
			   uint32_t length)
{
	memset(wire, 0, FWLAB_C21_RECORD_SIZE);
	put_le32(wire + FWLAB_C21_REQ_MAGIC, FWLAB_C21_REQUEST_MAGIC);
	put_le16(wire + FWLAB_C21_REQ_ABI_MAJOR, FWLAB_C21_A1_ABI_MAJOR);
	put_le16(wire + FWLAB_C21_REQ_ABI_MINOR, FWLAB_C21_A1_ABI_MINOR);
	put_le16(wire + FWLAB_C21_REQ_STRUCT_SIZE, FWLAB_C21_RECORD_SIZE);
	put_le16(wire + FWLAB_C21_REQ_OPERATION, operation);
	put_le64(wire + FWLAB_C21_REQ_SEQUENCE, sequence);
	put_le64(wire + FWLAB_C21_REQ_EXPECTED_GENERATION, generation);
	put_le64(wire + FWLAB_C21_REQ_IOVA, iova);
	put_le32(wire + FWLAB_C21_REQ_LENGTH, length);
}

static int read_state_record(struct c2_session *session,
			     unsigned char wire[FWLAB_C21_RECORD_SIZE],
			     struct c2_state *state)
{
	off_t position;
	int ret;

	ret = checked_position(&session->control_region,
			       FWLAB_C21_STATE_OFFSET, &position);
	if (ret)
		return ret;
	ret = pread_exact(session->device_fd, wire, FWLAB_C21_RECORD_SIZE,
			  position);
	if (ret)
		return ret;
	return decode_state(wire, state);
}

static int discover_regions(struct c2_session *session)
{
	struct vfio_device_info device_info = {
		.argsz = sizeof(device_info),
	};
	struct c2_region regions[2];
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	struct c2_state state;
	unsigned int control_count = 0;
	unsigned int control_index = 0;
	unsigned int index;

	if (ioctl(session->device_fd, VFIO_DEVICE_GET_INFO, &device_info))
		return -errno;
	if (device_info.num_regions != 2 || device_info.num_irqs != 0)
		return -EPROTO;
	for (index = 0; index < 2; index++) {
		struct vfio_region_info info = {
			.argsz = sizeof(info),
			.index = index,
		};

		if (ioctl(session->device_fd, VFIO_DEVICE_GET_REGION_INFO, &info))
			return -errno;
		if (info.index != index)
			return -EPROTO;
		regions[index].index = index;
		regions[index].flags = info.flags;
		regions[index].offset = info.offset;
		regions[index].size = info.size;
	}
	if (validate_region_layout(regions))
		return -EPROTO;
	for (index = 0; index < 2; index++) {
		off_t state_position;
		ssize_t done;

		if (checked_position(&regions[index], FWLAB_C21_STATE_OFFSET,
				     &state_position))
			return -EPROTO;
		do {
			done = pread(session->device_fd, state_wire,
				     sizeof(state_wire), state_position);
		} while (done < 0 && errno == EINTR);
		if (done == (ssize_t)sizeof(state_wire) &&
		    !decode_state(state_wire, &state)) {
			control_count++;
			control_index = index;
		}
	}
	if (control_count != 1)
		return -EPROTO;
	session->control_region = regions[control_index];
	session->data_region = regions[control_index ^ 1U];
	return 0;
}

static int read_data(struct c2_session *session, unsigned char *buffer,
		     size_t length)
{
	off_t position;
	int ret;

	ret = checked_span(&session->data_region, 0, length, &position);
	if (ret)
		return ret;
	return pread_exact(session->device_fd, buffer, length, position);
}

static int write_data(struct c2_session *session, const unsigned char *buffer,
		      size_t length)
{
	off_t position;
	int ret;

	ret = checked_span(&session->data_region, 0, length, &position);
	if (ret)
		return ret;
	return pwrite_exact(session->device_fd, buffer, length, position);
}

static int submit_operation(struct c2_session *session, uint16_t operation,
			    uint64_t iova, uint32_t length,
			    int32_t expected_errno)
{
	unsigned char request[FWLAB_C21_RECORD_SIZE];
	unsigned char result_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	struct c2_result result;
	struct c2_state state_before;
	struct c2_state state_after;
	off_t submit_position;
	off_t result_position;
	int ret;

	ret = read_state_record(session, state_wire, &state_before);
	if (ret)
		return ret;
	if (state_before.device_state != FWLAB_C21_STATE_OPEN_ATTACHED ||
	    !(state_before.flags & FWLAB_C21_ST_F_ATTACHED) ||
	    !state_before.generation || !state_before.next_sequence)
		return -EPROTO;
	encode_request(request, operation, state_before.next_sequence,
		       state_before.generation, iova, length);
	ret = checked_position(&session->control_region,
			       FWLAB_C21_SUBMIT_OFFSET, &submit_position);
	if (ret)
		return ret;
	ret = pwrite_exact(session->device_fd, request, sizeof(request),
			   submit_position);
	if (ret)
		return ret;
	ret = checked_position(&session->control_region,
			       FWLAB_C21_RESULT_OFFSET, &result_position);
	if (ret)
		return ret;
	ret = pread_exact(session->device_fd, result_wire, sizeof(result_wire),
			  result_position);
	if (ret)
		return ret;
	ret = decode_result(result_wire, &result);
	if (ret)
		return ret;
	if (result.operation != operation ||
	    result.sequence != state_before.next_sequence ||
	    result.generation != state_before.generation || result.iova != iova ||
	    result.requested_length != length ||
	    result.op_errno != expected_errno)
		return -EPROTO;
	ret = read_state_record(session, state_wire, &state_after);
	if (ret)
		return ret;
	if (state_after.generation != state_before.generation ||
	    state_after.last_sequence != state_before.next_sequence ||
	    state_after.next_sequence != state_before.next_sequence + 1U ||
	    !(state_after.flags & FWLAB_C21_ST_F_RESULT_VALID))
		return -EPROTO;
	return 0;
}

static int session_map_page(struct c2_session *session)
{
	struct iommu_ioas_map map = {
		.size = sizeof(map),
		.flags = IOMMU_IOAS_MAP_FIXED_IOVA |
			 IOMMU_IOAS_MAP_READABLE | IOMMU_IOAS_MAP_WRITEABLE,
		.ioas_id = session->ioas_id,
		.user_va = (uintptr_t)session->page,
		.length = C2_PAGE_SIZE,
		.iova = session->iova,
	};

	if (ioctl(session->iommu_fd, IOMMU_IOAS_MAP, &map))
		return -errno;
	if (map.iova != session->iova || map.length != C2_PAGE_SIZE)
		return -EPROTO;
	session->mapped = true;
	return 0;
}

static int session_unmap_page(struct c2_session *session)
{
	struct iommu_ioas_unmap unmap = {
		.size = sizeof(unmap),
		.ioas_id = session->ioas_id,
		.iova = session->iova,
		.length = C2_PAGE_SIZE,
	};

	if (!session->mapped)
		return 0;
	if (ioctl(session->iommu_fd, IOMMU_IOAS_UNMAP, &unmap))
		return -errno;
	/* The kernel accepted the unmap; never attempt the same unmap again. */
	session->mapped = false;
	if (unmap.length != C2_PAGE_SIZE)
		return -EPROTO;
	return 0;
}

static int session_attach_raw(struct c2_session *session)
{
	struct vfio_device_attach_iommufd_pt attach = {
		.argsz = sizeof(attach),
		.pt_id = session->ioas_id,
	};

	if (ioctl(session->device_fd, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &attach))
		return -errno;
	session->attached = true;
	return 0;
}

static int session_detach(struct c2_session *session)
{
	struct vfio_device_detach_iommufd_pt detach = {
		.argsz = sizeof(detach),
	};

	if (!session->attached)
		return 0;
	if (ioctl(session->device_fd, VFIO_DEVICE_DETACH_IOMMUFD_PT, &detach))
		return -errno;
	session->attached = false;
	return 0;
}

static int session_destroy_ioas(struct c2_session *session)
{
	struct iommu_destroy destroy = {
		.size = sizeof(destroy),
		.id = session->ioas_id,
	};

	if (!session->ioas_created)
		return 0;
	if (ioctl(session->iommu_fd, IOMMU_DESTROY, &destroy))
		return -errno;
	session->ioas_created = false;
	return 0;
}

static void session_cleanup(struct c2_session *session)
{
	int ret;

	if (session->attached) {
		ret = session_detach(session);
		if (ret && session->device_fd >= 0) {
			close(session->device_fd);
			session->device_fd = -1;
			session->attached = false;
		}
	}
	(void)session_unmap_page(session);
	ret = session_destroy_ioas(session);
	if (ret && session->device_fd >= 0) {
		close(session->device_fd);
		session->device_fd = -1;
		(void)session_destroy_ioas(session);
	}
	if (session->device_fd >= 0)
		close(session->device_fd);
	if (session->iommu_fd >= 0)
		close(session->iommu_fd);
	if (session->page)
		munmap(session->page, C2_PAGE_SIZE);
	session->device_fd = -1;
	session->iommu_fd = -1;
	session->page = NULL;
}

static int session_begin(struct c2_session *session, const char *device_path,
			 uint64_t iova)
{
	struct vfio_device_bind_iommufd bind = {
		.argsz = sizeof(bind),
	};
	struct iommu_ioas_alloc ioas = {
		.size = sizeof(ioas),
	};
	long page_size;
	int ret;

	memset(session, 0, sizeof(*session));
	session->device_fd = -1;
	session->iommu_fd = -1;
	session->iova = iova;
	page_size = sysconf(_SC_PAGESIZE);
	if (page_size != C2_PAGE_SIZE)
		return -EPROTO;
	session->device_fd = open(device_path, O_RDWR | O_CLOEXEC);
	if (session->device_fd < 0)
		return -errno;
	session->iommu_fd = open("/dev/iommu", O_RDWR | O_CLOEXEC);
	if (session->iommu_fd < 0)
		return -errno;
	bind.iommufd = session->iommu_fd;
	if (ioctl(session->device_fd, VFIO_DEVICE_BIND_IOMMUFD, &bind))
		return -errno;
	ret = discover_regions(session);
	if (ret)
		return ret;
	if (ioctl(session->iommu_fd, IOMMU_IOAS_ALLOC, &ioas))
		return -errno;
	session->ioas_created = true;
	session->ioas_id = ioas.out_ioas_id;
	session->page = mmap(NULL, C2_PAGE_SIZE, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (session->page == MAP_FAILED) {
		session->page = NULL;
		return -errno;
	}
	return 0;
}

static void fill_pattern(unsigned char *buffer, size_t length,
			 unsigned int seed)
{
	size_t index;

	for (index = 0; index < length; index++)
		buffer[index] = (unsigned char)(seed ^ (index * 29U) ^
						(index >> 1));
}

static int run_copy_matrix(struct c2_session *session)
{
	static const uint32_t lengths[] = { 1, 37, 64, 256 };
	unsigned char data_expected[C2_DATA_WINDOW];
	unsigned char data_observed[C2_DATA_WINDOW];
	unsigned char page_expected[C2_PAGE_SIZE];
	size_t case_index;
	size_t index;

	for (case_index = 0;
	     case_index < sizeof(lengths) / sizeof(lengths[0]); case_index++) {
		uint32_t length = lengths[case_index];
		size_t page_offset = 129U + case_index * 512U;

		memset(session->page, 0xc3, C2_PAGE_SIZE);
		memset(data_expected, 0x5d, sizeof(data_expected));
		fill_pattern(session->page + page_offset, length,
			     0x10U + (unsigned int)case_index);
		memcpy(page_expected, session->page, C2_PAGE_SIZE);
		if (write_data(session, data_expected, sizeof(data_expected)))
			return -EIO;
		if (submit_operation(session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
				     session->iova + page_offset, length, 0))
			return -EIO;
		if (read_data(session, data_observed, sizeof(data_observed)))
			return -EIO;
		if (memcmp(data_observed, session->page + page_offset, length) ||
		    memcmp(data_observed + length, data_expected + length,
			   sizeof(data_observed) - length) ||
		    memcmp(session->page, page_expected, C2_PAGE_SIZE))
			return -EIO;

		memset(session->page, 0xa7, C2_PAGE_SIZE);
		memset(data_expected, 0x6e, sizeof(data_expected));
		fill_pattern(data_expected, length,
			     0x80U + (unsigned int)case_index);
		if (write_data(session, data_expected, sizeof(data_expected)))
			return -EIO;
		if (submit_operation(session,
				     FWLAB_C21_OP_COPY_BUFFER_TO_IOAS,
				     session->iova + page_offset, length, 0))
			return -EIO;
		for (index = 0; index < C2_PAGE_SIZE; index++) {
			unsigned char expected =
				index >= page_offset && index < page_offset + length ?
					data_expected[index - page_offset] : 0xa7;

			if (session->page[index] != expected)
				return -EIO;
		}
		printf("C2.2 copy length=%" PRIu32 " both-directions: PASS\n",
		       length);
	}
	return 0;
}

static int verify_transition(const struct c2_state *before,
			     const struct c2_state *after, bool attached)
{
	if (after->generation != before->generation + 1U ||
	    after->last_sequence != 0 || after->next_sequence != 1 ||
	    !!(after->flags & FWLAB_C21_ST_F_ATTACHED) != attached ||
	    after->device_state !=
		    (attached ? FWLAB_C21_STATE_OPEN_ATTACHED :
				FWLAB_C21_STATE_OPEN_UNATTACHED))
		return -EPROTO;
	return 0;
}

static int run_primary(const char *device_path)
{
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char data_before[C2_DATA_WINDOW];
	unsigned char data_after[C2_DATA_WINDOW];
	struct c2_session session;
	struct c2_state before;
	struct c2_state after;
	int ret;

	ret = session_begin(&session, device_path, C2_PRIMARY_IOVA);
	if (ret)
		goto out;
	ret = session_map_page(&session);
	if (ret)
		goto out;
	ret = read_state_record(&session, state_wire, &before);
	if (ret || before.device_state != FWLAB_C21_STATE_OPEN_UNATTACHED)
		goto protocol_error;
	ret = session_attach_raw(&session);
	if (ret)
		goto out;
	ret = read_state_record(&session, state_wire, &after);
	if (ret || verify_transition(&before, &after, true))
		goto protocol_error;
	ret = run_copy_matrix(&session);
	if (ret)
		goto out;
	ret = read_data(&session, data_before, sizeof(data_before));
	if (ret)
		goto out;
	ret = session_unmap_page(&session);
	if (ret)
		goto out;
	ret = submit_operation(&session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			       session.iova, 1, -ENOENT);
	if (ret)
		goto out;
	ret = read_data(&session, data_after, sizeof(data_after));
	if (ret || memcmp(data_before, data_after, sizeof(data_before)))
		goto protocol_error;
	printf("C2.2 post-unmap result=-ENOENT: PASS\n");
	ret = read_state_record(&session, state_wire, &before);
	if (ret)
		goto out;
	ret = session_detach(&session);
	if (ret)
		goto out;
	ret = read_state_record(&session, state_wire, &after);
	if (ret || verify_transition(&before, &after, false))
		goto protocol_error;
	ret = session_destroy_ioas(&session);
	if (ret)
		goto out;
	printf("C2.2 primary MAP-before-ATTACH flow: PASS\n");
	goto out;

protocol_error:
	ret = -EPROTO;
out:
	session_cleanup(&session);
	return ret;
}

static int run_attach_before_map_order(const char *device_path)
{
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char data[C2_DATA_WINDOW];
	unsigned char expected[64];
	struct c2_session session;
	struct c2_state before;
	struct c2_state after;
	size_t index;
	int ret;

	ret = session_begin(&session, device_path, C2_EMPTY_IOVA);
	if (ret)
		goto out;
	ret = read_state_record(&session, state_wire, &before);
	if (ret || before.device_state != FWLAB_C21_STATE_OPEN_UNATTACHED)
		goto protocol_error;
	ret = session_attach_raw(&session);
	if (ret)
		goto out;
	ret = read_state_record(&session, state_wire, &after);
	if (ret || verify_transition(&before, &after, true))
		goto protocol_error;
	ret = submit_operation(&session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			       session.iova, 1, -ENOENT);
	if (ret)
		goto out;
	printf("C2.2 ATTACH empty IOAS then copy result=-ENOENT: PASS\n");

	for (index = 0; index < sizeof(expected); index++)
		expected[index] = (unsigned char)(0x31U ^ (index * 7U));
	memset(session.page, 0x9c, C2_PAGE_SIZE);
	memcpy(session.page + 17, expected, sizeof(expected));
	ret = session_map_page(&session);
	if (ret)
		goto out;
	ret = submit_operation(&session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			       session.iova + 17, sizeof(expected), 0);
	if (ret)
		goto out;
	memset(data, 0xff, sizeof(data));
	ret = read_data(&session, data, sizeof(data));
	if (ret || memcmp(data, expected, sizeof(expected)))
		goto protocol_error;
	for (index = sizeof(expected); index < sizeof(data); index++)
		if (data[index] != 0)
			goto protocol_error;
	printf("C2.2 MAP into attached IOAS then next-sequence copy: PASS\n");
	ret = session_unmap_page(&session);
	if (ret)
		goto out;
	ret = read_state_record(&session, state_wire, &before);
	if (ret)
		goto out;
	ret = session_detach(&session);
	if (ret)
		goto out;
	ret = read_state_record(&session, state_wire, &after);
	if (ret || verify_transition(&before, &after, false))
		goto protocol_error;
	ret = session_destroy_ioas(&session);
	if (ret)
		goto out;
	printf("C2.2 ATTACH-before-MAP alignment flow: PASS\n");
	goto out;

protocol_error:
	ret = -EPROTO;
out:
	session_cleanup(&session);
	return ret;
}

int main(int argc, char **argv)
{
	int ret;

	if (argc != 2) {
		fprintf(stderr,
			"usage: %s /dev/vfio/devices/vfioN | --selftest-layout\n",
			argv[0]);
		return EXIT_FAILURE;
	}
	if (!strcmp(argv[1], "--selftest-layout"))
		return selftest_region_layout() ? EXIT_FAILURE : EXIT_SUCCESS;
	ret = run_primary(argv[1]);
	if (ret) {
		errno = -ret;
		perror("C2.2 primary flow");
		return EXIT_FAILURE;
	}
	ret = run_attach_before_map_order(argv[1]);
	if (ret) {
		errno = -ret;
		perror("C2.2 attach-before-map flow");
		return EXIT_FAILURE;
	}
	printf("C2.2 synchronous CPU-mediated IOAS-copy oracle: PASS\n");
	return EXIT_SUCCESS;
}
