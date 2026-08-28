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

#define C23_PAGE_SIZE 4096U
#define C23_BASE_IOVA UINT64_C(0x300000000)
#define C23_ALT_IOVA UINT64_C(0x300010000)
#define C23_UNMAPPED_IOVA UINT64_C(0x400000000)
#define C23_MAX_MAPS 4U
#define C23_PAGE_A_SIZE (2U * C23_PAGE_SIZE)

_Static_assert(FWLAB_C21_RECORD_SIZE == 64U,
	       "C2.3 requires exact 64-byte A-prime records");

struct c23_region {
	uint32_t index;
	uint32_t flags;
	uint64_t offset;
	uint64_t size;
};

struct c23_state {
	uint16_t device_state;
	uint32_t flags;
	uint64_t generation;
	uint64_t last_sequence;
	uint64_t next_sequence;
};

struct c23_result {
	uint16_t operation;
	uint32_t flags;
	uint64_t sequence;
	uint64_t generation;
	uint64_t iova;
	uint32_t length;
	int32_t op_errno;
};

struct c23_map {
	bool active;
	uint64_t iova;
	uint64_t length;
};

struct c23_session {
	int device_fd;
	int iommu_fd;
	bool attached;
	bool ioas_created;
	uint32_t ioas_id;
	unsigned char *page_a;
	unsigned char *page_b;
	struct c23_map maps[C23_MAX_MAPS];
	struct c23_region data_region;
	struct c23_region control_region;
};

struct c23_observable {
	unsigned char state[FWLAB_C21_RECORD_SIZE];
	unsigned char result[FWLAB_C21_RECORD_SIZE];
	unsigned char data[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char page_a[C23_PAGE_A_SIZE];
	unsigned char page_b[C23_PAGE_SIZE];
	bool result_valid;
};

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

static bool request_range_valid(uint64_t iova, uint32_t length)
{
	uint64_t page_offset;

	if (!iova || !length || length > FWLAB_C21_MAX_COPY_LENGTH ||
	    iova > UINT64_MAX - (length - 1U))
		return false;
	page_offset = iova & (FWLAB_C21_IOAS_PAGE_SIZE - 1U);
	return page_offset + length <= FWLAB_C21_IOAS_PAGE_SIZE;
}

static int checked_span(const struct c23_region *region, uint64_t relative,
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

static int expect_pread_errno(int fd, void *buffer, size_t length, off_t offset,
			      int expected_errno)
{
	ssize_t done;

	errno = 0;
	done = pread(fd, buffer, length, offset);
	return done == -1 && errno == expected_errno ? 0 : -EPROTO;
}

static int expect_pwrite_errno(int fd, const void *buffer, size_t length,
			       off_t offset, int expected_errno)
{
	ssize_t done;

	errno = 0;
	done = pwrite(fd, buffer, length, offset);
	return done == -1 && errno == expected_errno ? 0 : -EPROTO;
}

static int decode_state(const unsigned char wire[FWLAB_C21_RECORD_SIZE],
			struct c23_state *state)
{
	uint16_t device_state;
	uint32_t flags;
	uint64_t generation;
	uint64_t last_sequence;
	uint64_t next_sequence;

	if (get_le32(wire + FWLAB_C21_ST_MAGIC) != FWLAB_C21_STATE_MAGIC ||
	    get_le16(wire + FWLAB_C21_ST_ABI_MAJOR) !=
		    FWLAB_C21_A1_ABI_MAJOR ||
	    get_le16(wire + FWLAB_C21_ST_ABI_MINOR) !=
		    FWLAB_C21_A1_ABI_MINOR ||
	    get_le16(wire + FWLAB_C21_ST_STRUCT_SIZE) !=
		    FWLAB_C21_RECORD_SIZE ||
	    get_le32(wire + FWLAB_C21_ST_MAX_COPY_LENGTH) !=
		    FWLAB_C21_MAX_COPY_LENGTH ||
	    get_le32(wire + FWLAB_C21_ST_DATA_REGION_SIZE) !=
		    FWLAB_C21_DATA_REGION_SIZE ||
	    get_le64(wire + FWLAB_C21_ST_RESERVED1) ||
	    get_le64(wire + FWLAB_C21_ST_RESERVED2))
		return -EPROTO;
	device_state = get_le16(wire + FWLAB_C21_ST_DEVICE_STATE);
	flags = get_le32(wire + FWLAB_C21_ST_FLAGS);
	generation = get_le64(wire + FWLAB_C21_ST_GENERATION);
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
	      generation != UINT64_MAX)) ||
	    (device_state != FWLAB_C21_STATE_DEAD &&
	     (flags & FWLAB_C21_ST_F_DEAD)) ||
	    ((flags & FWLAB_C21_ST_F_SEQUENCE_EXHAUSTED) &&
	     (last_sequence != UINT64_MAX || next_sequence)) ||
	    (!(flags & FWLAB_C21_ST_F_SEQUENCE_EXHAUSTED) &&
	     (last_sequence == UINT64_MAX || next_sequence != last_sequence + 1U)) ||
	    ((flags & (FWLAB_C21_ST_F_RESULT_VALID |
		       FWLAB_C21_ST_F_SEQUENCE_EXHAUSTED)) &&
	     !(flags & FWLAB_C21_ST_F_OPEN)) ||
	    ((flags & FWLAB_C21_ST_F_OPEN) && !generation))
		return -EPROTO;
	state->device_state = device_state;
	state->flags = flags;
	state->generation = generation;
	state->last_sequence = last_sequence;
	state->next_sequence = next_sequence;
	return 0;
}

static int decode_result(const unsigned char wire[FWLAB_C21_RECORD_SIZE],
			 struct c23_result *result)
{
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
	result->length = get_le32(wire + FWLAB_C21_RES_REQUESTED_LENGTH);
	result->op_errno = decode_s32(get_le32(wire + FWLAB_C21_RES_OP_ERRNO));
	if ((result->flags & ~FWLAB_C21_RES_F_ALL) ||
	    !(result->flags & FWLAB_C21_RES_F_VALID) ||
	    (result->operation != FWLAB_C21_OP_COPY_IOAS_TO_BUFFER &&
	     result->operation != FWLAB_C21_OP_COPY_BUFFER_TO_IOAS) ||
	    !result->sequence || !result->generation ||
	    !request_range_valid(result->iova, result->length) ||
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

static int validate_region_layout(const struct c23_region regions[2])
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
	return end[0] <= regions[1].offset || end[1] <= regions[0].offset ?
		       0 : -ERANGE;
}

static int read_state_raw(struct c23_session *session,
			  unsigned char wire[FWLAB_C21_RECORD_SIZE],
			  struct c23_state *state)
{
	off_t position;
	int ret;

	ret = checked_span(&session->control_region, FWLAB_C21_STATE_OFFSET,
			   FWLAB_C21_RECORD_SIZE, &position);
	if (ret)
		return ret;
	ret = pread_exact(session->device_fd, wire, FWLAB_C21_RECORD_SIZE,
			  position);
	return ret ? ret : decode_state(wire, state);
}

static int read_result_raw(struct c23_session *session,
			   unsigned char wire[FWLAB_C21_RECORD_SIZE],
			   struct c23_result *result)
{
	off_t position;
	int ret;

	ret = checked_span(&session->control_region, FWLAB_C21_RESULT_OFFSET,
			   FWLAB_C21_RECORD_SIZE, &position);
	if (ret)
		return ret;
	ret = pread_exact(session->device_fd, wire, FWLAB_C21_RECORD_SIZE,
			  position);
	return ret ? ret : decode_result(wire, result);
}

static int discover_regions(struct c23_session *session)
{
	struct vfio_device_info device_info = { .argsz = sizeof(device_info) };
	struct c23_region regions[2];
	unsigned char wire[FWLAB_C21_RECORD_SIZE];
	struct c23_state state;
	unsigned int control_count = 0;
	unsigned int control_index = 0;
	unsigned int index;

	if (ioctl(session->device_fd, VFIO_DEVICE_GET_INFO, &device_info))
		return -errno;
	if (device_info.num_regions != 2 || device_info.num_irqs != 0)
		return -EPROTO;
	for (index = 0; index < 2; index++) {
		struct vfio_region_info info = {
			.argsz = sizeof(info), .index = index,
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
		off_t position;
		ssize_t done;

		if (checked_span(&regions[index], FWLAB_C21_STATE_OFFSET,
				  FWLAB_C21_RECORD_SIZE, &position))
			return -EPROTO;
		done = pread(session->device_fd, wire, sizeof(wire), position);
		if (done == (ssize_t)sizeof(wire) && !decode_state(wire, &state)) {
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

static int data_read(struct c23_session *session, unsigned char *buffer,
		     size_t length)
{
	off_t position;
	int ret = checked_span(&session->data_region, 0, length, &position);

	return ret ? ret : pread_exact(session->device_fd, buffer, length,
					position);
}

static int data_write(struct c23_session *session, const unsigned char *buffer,
		      size_t length)
{
	off_t position;
	int ret = checked_span(&session->data_region, 0, length, &position);

	return ret ? ret : pwrite_exact(session->device_fd, buffer, length,
					 position);
}

static int capture_observable(struct c23_session *session,
			      struct c23_observable *observable)
{
	struct c23_result result;
	struct c23_state state;
	off_t position;
	int ret;

	ret = read_state_raw(session, observable->state, &state);
	if (ret)
		return ret;
	ret = checked_span(&session->control_region, FWLAB_C21_RESULT_OFFSET,
			   FWLAB_C21_RECORD_SIZE, &position);
	if (ret)
		return ret;
	ret = pread_exact(session->device_fd, observable->result,
			  sizeof(observable->result), position);
	if (ret == -ENODATA) {
		observable->result_valid = false;
		memset(observable->result, 0, sizeof(observable->result));
	} else {
		if (ret || decode_result(observable->result, &result))
			return ret ? ret : -EPROTO;
		observable->result_valid = true;
	}
	ret = data_read(session, observable->data, sizeof(observable->data));
	if (ret)
		return ret;
	memcpy(observable->page_a, session->page_a,
	       sizeof(observable->page_a));
	memcpy(observable->page_b, session->page_b,
	       sizeof(observable->page_b));
	return 0;
}

static bool observables_equal(const struct c23_observable *left,
			      const struct c23_observable *right)
{
	return left->result_valid == right->result_valid &&
	       !memcmp(left->state, right->state, sizeof(left->state)) &&
	       (!left->result_valid ||
		!memcmp(left->result, right->result, sizeof(left->result))) &&
	       !memcmp(left->data, right->data, sizeof(left->data)) &&
	       !memcmp(left->page_a, right->page_a, sizeof(left->page_a)) &&
	       !memcmp(left->page_b, right->page_b, sizeof(left->page_b));
}

static int session_attach(struct c23_session *session)
{
	struct vfio_device_attach_iommufd_pt attach = {
		.argsz = sizeof(attach), .pt_id = session->ioas_id,
	};

	if (ioctl(session->device_fd, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &attach))
		return -errno;
	session->attached = true;
	return 0;
}

static int session_detach(struct c23_session *session)
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

static int session_map_range(struct c23_session *session, void *memory,
			     uint64_t length, uint64_t iova,
			     uint32_t permissions, struct c23_map **out_map)
{
	struct iommu_ioas_map map = {
		.size = sizeof(map),
		.flags = IOMMU_IOAS_MAP_FIXED_IOVA | permissions,
		.ioas_id = session->ioas_id,
		.user_va = (uintptr_t)memory,
		.length = length,
		.iova = iova,
	};
	unsigned int index;

	for (index = 0; index < C23_MAX_MAPS; index++)
		if (!session->maps[index].active)
			break;
	if (index == C23_MAX_MAPS)
		return -ENOSPC;
	if (ioctl(session->iommu_fd, IOMMU_IOAS_MAP, &map))
		return -errno;
	session->maps[index].active = true;
	session->maps[index].iova = map.iova;
	session->maps[index].length = map.length;
	*out_map = &session->maps[index];
	if (map.iova != iova || map.length != length)
		return -EPROTO;
	return 0;
}

static int session_map(struct c23_session *session, void *memory, uint64_t iova,
		       uint32_t permissions, struct c23_map **out_map)
{
	return session_map_range(session, memory, C23_PAGE_SIZE, iova,
				 permissions, out_map);
}

static int session_unmap(struct c23_session *session, struct c23_map *mapping)
{
	uint64_t expected_length = mapping->length;
	struct iommu_ioas_unmap unmap = {
		.size = sizeof(unmap), .ioas_id = session->ioas_id,
		.iova = mapping->iova, .length = mapping->length,
	};

	if (!mapping->active)
		return 0;
	if (ioctl(session->iommu_fd, IOMMU_IOAS_UNMAP, &unmap))
		return -errno;
	mapping->active = false;
	return unmap.length == expected_length ? 0 : -EPROTO;
}

static int expect_map_errno_unchanged(struct c23_session *session,
				      const struct iommu_ioas_map *template,
				      int expected_errno, const char *name)
{
	struct c23_observable before;
	struct c23_observable after;
	struct iommu_ioas_map map = *template;
	int ret;

	ret = capture_observable(session, &before);
	if (ret)
		return ret;
	errno = 0;
	if (ioctl(session->iommu_fd, IOMMU_IOAS_MAP, &map) != -1) {
		struct iommu_ioas_unmap unmap = {
			.size = sizeof(unmap),
			.ioas_id = session->ioas_id,
			.iova = map.iova,
			.length = map.length,
		};

		(void)ioctl(session->iommu_fd, IOMMU_IOAS_UNMAP, &unmap);
		return -EPROTO;
	}
	if (errno != expected_errno)
		return -EPROTO;
	ret = capture_observable(session, &after);
	if (ret || !observables_equal(&before, &after))
		return -EPROTO;
	printf("C2.3 MAP reject %-27s errno=%d stable: PASS\n", name,
	       expected_errno);
	return 0;
}

static int session_destroy_ioas(struct c23_session *session)
{
	struct iommu_destroy destroy = {
		.size = sizeof(destroy), .id = session->ioas_id,
	};

	if (!session->ioas_created)
		return 0;
	if (ioctl(session->iommu_fd, IOMMU_DESTROY, &destroy))
		return -errno;
	session->ioas_created = false;
	return 0;
}

static void session_cleanup(struct c23_session *session)
{
	int destroy_ret;
	unsigned int index;

	if (session->attached && session_detach(session) &&
	    session->device_fd >= 0) {
		close(session->device_fd);
		session->device_fd = -1;
		session->attached = false;
	}
	for (index = 0; index < C23_MAX_MAPS; index++)
		if (session->maps[index].active)
			(void)session_unmap(session, &session->maps[index]);
	destroy_ret = session_destroy_ioas(session);
	if (destroy_ret && session->device_fd >= 0) {
		close(session->device_fd);
		session->device_fd = -1;
		session->attached = false;
		(void)session_destroy_ioas(session);
	}
	if (session->device_fd >= 0)
		close(session->device_fd);
	if (session->iommu_fd >= 0)
		close(session->iommu_fd);
	if (session->page_a)
		munmap(session->page_a, C23_PAGE_A_SIZE);
	if (session->page_b)
		munmap(session->page_b, C23_PAGE_SIZE);
}

static int session_begin(struct c23_session *session, const char *device_path)
{
	struct vfio_device_bind_iommufd bind = { .argsz = sizeof(bind) };
	struct iommu_ioas_alloc ioas = { .size = sizeof(ioas) };

	memset(session, 0, sizeof(*session));
	session->device_fd = -1;
	session->iommu_fd = -1;
	if (sysconf(_SC_PAGESIZE) != C23_PAGE_SIZE)
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
	if (discover_regions(session))
		return -EPROTO;
	if (ioctl(session->iommu_fd, IOMMU_IOAS_ALLOC, &ioas))
		return -errno;
	session->ioas_created = true;
	session->ioas_id = ioas.out_ioas_id;
	session->page_a = mmap(NULL, C23_PAGE_A_SIZE, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (session->page_a == MAP_FAILED) {
		session->page_a = NULL;
		return -errno;
	}
	session->page_b = mmap(NULL, C23_PAGE_SIZE, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (session->page_b == MAP_FAILED) {
		session->page_b = NULL;
		return -errno;
	}
	return 0;
}

static int submit_accepted(struct c23_session *session, uint16_t operation,
			   uint64_t iova, uint32_t length,
			   int32_t expected_errno)
{
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char result_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char request[FWLAB_C21_RECORD_SIZE];
	struct c23_state before;
	struct c23_state after;
	struct c23_result result;
	off_t position;
	uint32_t expected_flags = FWLAB_C21_RES_F_VALID;
	int ret;

	ret = read_state_raw(session, state_wire, &before);
	if (ret || before.device_state != FWLAB_C21_STATE_OPEN_ATTACHED)
		return -EPROTO;
	encode_request(request, operation, before.next_sequence, before.generation,
		       iova, length);
	ret = checked_span(&session->control_region, FWLAB_C21_SUBMIT_OFFSET,
			   sizeof(request), &position);
	if (ret)
		return ret;
	ret = pwrite_exact(session->device_fd, request, sizeof(request), position);
	if (ret)
		return ret;
	ret = read_result_raw(session, result_wire, &result);
	if (ret)
		return ret;
	if (operation == FWLAB_C21_OP_COPY_BUFFER_TO_IOAS && expected_errno)
		expected_flags |= FWLAB_C21_RES_F_DEST_MAY_HAVE_PARTIAL;
	if (result.operation != operation || result.sequence != before.next_sequence ||
	    result.generation != before.generation || result.iova != iova ||
	    result.length != length || result.op_errno != expected_errno ||
	    result.flags != expected_flags)
		return -EPROTO;
	ret = read_state_raw(session, state_wire, &after);
	if (ret || after.generation != before.generation ||
	    after.device_state != FWLAB_C21_STATE_OPEN_ATTACHED ||
	    after.flags != (before.flags | FWLAB_C21_ST_F_RESULT_VALID) ||
	    after.last_sequence != before.next_sequence ||
	    after.next_sequence != before.next_sequence + 1U)
		return -EPROTO;
	return 0;
}

static int expect_rejected_unchanged(struct c23_session *session,
				     const unsigned char *request,
				     size_t request_size, uint64_t relative_offset,
				     int expected_errno, const char *name)
{
	struct c23_observable before;
	struct c23_observable after;
	off_t position;
	int ret;

	ret = capture_observable(session, &before);
	if (ret)
		return ret;
	ret = checked_span(&session->control_region, relative_offset,
			   request_size, &position);
	if (ret)
		return ret;
	ret = expect_pwrite_errno(session->device_fd, request, request_size,
				  position, expected_errno);
	if (ret)
		return ret;
	ret = capture_observable(session, &after);
	if (ret || !observables_equal(&before, &after))
		return -EPROTO;
	printf("C2.3 reject %-28s errno=%d stable: PASS\n", name,
	       expected_errno);
	return 0;
}

static int test_pre_bind(const char *device_path)
{
	struct vfio_device_info info = { .argsz = sizeof(info) };
	unsigned char wire[FWLAB_C21_RECORD_SIZE] = { 0 };
	int fd = open(device_path, O_RDWR | O_CLOEXEC);
	int ret = 0;

	if (fd < 0)
		return -errno;
	errno = 0;
	if (ioctl(fd, VFIO_DEVICE_GET_INFO, &info) != -1 || errno != EINVAL)
		ret = -EPROTO;
	if (!ret)
		ret = expect_pread_errno(fd, wire, sizeof(wire), 0, EINVAL);
	if (!ret)
		ret = expect_pwrite_errno(fd, wire, sizeof(wire), 0, EINVAL);
	close(fd);
	if (!ret)
		printf("C2.3 pre-BIND GET_INFO/read/write rejection: PASS\n");
	return ret;
}

static int test_bound_unattached(const char *device_path)
{
	struct c23_session session;
	struct c23_observable observable_before;
	struct c23_observable observable_after;
	unsigned char data_pattern[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char result[FWLAB_C21_RECORD_SIZE];
	unsigned char request[FWLAB_C21_RECORD_SIZE];
	struct c23_state state;
	off_t submit_position;
	off_t result_position;
	int ret = session_begin(&session, device_path);

	if (ret)
		goto out;
	memset(data_pattern, 0x35, sizeof(data_pattern));
	memset(session.page_a, 0x46, C23_PAGE_A_SIZE);
	memset(session.page_b, 0x57, C23_PAGE_SIZE);
	ret = data_write(&session, data_pattern, sizeof(data_pattern));
	if (ret)
		goto out;
	ret = read_state_raw(&session, state_wire, &state);
	if (ret || state.device_state != FWLAB_C21_STATE_OPEN_UNATTACHED)
		goto protocol_error;
	ret = capture_observable(&session, &observable_before);
	if (ret || observable_before.result_valid)
		goto protocol_error;
	ret = checked_span(&session.control_region, FWLAB_C21_RESULT_OFFSET,
			   sizeof(result), &result_position);
	if (ret)
		goto out;
	ret = expect_pread_errno(session.device_fd, result, sizeof(result),
				 result_position, ENODATA);
	if (ret)
		goto out;
	encode_request(request, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
		       state.next_sequence, state.generation, C23_BASE_IOVA, 1);
	ret = checked_span(&session.control_region, FWLAB_C21_SUBMIT_OFFSET,
			   sizeof(request), &submit_position);
	if (ret)
		goto out;
	ret = expect_pwrite_errno(session.device_fd, request, sizeof(request),
				  submit_position, ENOTCONN);
	if (ret)
		goto out;
	ret = capture_observable(&session, &observable_after);
	if (ret || !observables_equal(&observable_before, &observable_after))
		goto protocol_error;
	ret = expect_pread_errno(session.device_fd, result, sizeof(result),
				 result_position, ENODATA);
	if (ret)
		goto out;
	printf("C2.3 bound-unattached -ENOTCONN stable state/result: PASS\n");
	goto out;

protocol_error:
	ret = -EPROTO;
out:
	session_cleanup(&session);
	return ret;
}

static int run_malformed_matrix(struct c23_session *session)
{
	unsigned char request[FWLAB_C21_RECORD_SIZE + 1];
	unsigned char valid[FWLAB_C21_RECORD_SIZE];
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	struct c23_state state;
	int ret;

	ret = read_state_raw(session, state_wire, &state);
	if (ret)
		return ret;
	encode_request(valid, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
		       state.next_sequence, state.generation, C23_BASE_IOVA, 64);

#define REJECT_MUTATION(label, expression, error_value)                        \
	do {                                                                     \
		memcpy(request, valid, sizeof(valid));                              \
		expression;                                                         \
		ret = expect_rejected_unchanged(                                   \
			session, request, sizeof(valid), FWLAB_C21_SUBMIT_OFFSET,   \
			error_value, label);                                         \
		if (ret)                                                           \
			return ret;                                                 \
	} while (0)

	REJECT_MUTATION("bad-magic", request[FWLAB_C21_REQ_MAGIC] ^= 1,
			EPROTO);
	REJECT_MUTATION("bad-major",
			put_le16(request + FWLAB_C21_REQ_ABI_MAJOR, 2), EPROTO);
	REJECT_MUTATION("bad-minor",
			put_le16(request + FWLAB_C21_REQ_ABI_MINOR, 1), EPROTO);
	REJECT_MUTATION("bad-struct-size",
			put_le16(request + FWLAB_C21_REQ_STRUCT_SIZE, 63), EPROTO);
	REJECT_MUTATION("bad-operation",
			put_le16(request + FWLAB_C21_REQ_OPERATION, 99), EPROTO);
	REJECT_MUTATION("bad-flags",
			put_le32(request + FWLAB_C21_REQ_FLAGS, 1), EPROTO);
	REJECT_MUTATION("reserved0",
			put_le32(request + FWLAB_C21_REQ_RESERVED0, 1), EPROTO);
	REJECT_MUTATION("reserved1",
			put_le64(request + FWLAB_C21_REQ_RESERVED1, 1), EPROTO);
	REJECT_MUTATION("reserved2",
			put_le64(request + FWLAB_C21_REQ_RESERVED2, 1), EPROTO);
	REJECT_MUTATION("zero-length",
			put_le32(request + FWLAB_C21_REQ_LENGTH, 0), ERANGE);
	REJECT_MUTATION("length-257",
			put_le32(request + FWLAB_C21_REQ_LENGTH, 257), ERANGE);
	REJECT_MUTATION("iova-overflow",
			put_le64(request + FWLAB_C21_REQ_IOVA, UINT64_MAX - 31),
			ERANGE);
	REJECT_MUTATION("cross-page",
			put_le64(request + FWLAB_C21_REQ_IOVA,
				 C23_BASE_IOVA + C23_PAGE_SIZE - 6),
			ERANGE);
	REJECT_MUTATION("stale-generation",
			put_le64(request + FWLAB_C21_REQ_EXPECTED_GENERATION,
				 state.generation - 1U),
			ESTALE);
	REJECT_MUTATION("replayed-sequence",
			put_le64(request + FWLAB_C21_REQ_SEQUENCE,
				 state.last_sequence),
			EALREADY);
	REJECT_MUTATION("sequence-gap",
			put_le64(request + FWLAB_C21_REQ_SEQUENCE,
				 state.next_sequence + 1U),
			ERANGE);
#undef REJECT_MUTATION

	memcpy(request, valid, sizeof(valid));
	request[sizeof(valid)] = 0;
	ret = expect_rejected_unchanged(session, request, 63,
					FWLAB_C21_SUBMIT_OFFSET, EMSGSIZE,
					"submit-63-bytes");
	if (ret)
		return ret;
	ret = expect_rejected_unchanged(session, request, 65,
					FWLAB_C21_SUBMIT_OFFSET, EMSGSIZE,
					"submit-65-bytes");
	if (ret)
		return ret;
	ret = expect_rejected_unchanged(session, valid, 32,
					FWLAB_C21_SUBMIT_OFFSET, EMSGSIZE,
					"fragment-first-32");
	if (ret)
		return ret;
	ret = expect_rejected_unchanged(session, valid + 32, 32,
					FWLAB_C21_SUBMIT_OFFSET + 32U,
					EMSGSIZE, "fragment-second-32");
	if (ret)
		return ret;
	return expect_rejected_unchanged(session, valid, sizeof(valid),
					 FWLAB_C21_SUBMIT_OFFSET + 1U, EINVAL,
					 "wrong-submit-offset");
}

static void fill_pattern(unsigned char *buffer, size_t length,
			 unsigned int seed)
{
	size_t index;

	for (index = 0; index < length; index++)
		buffer[index] = (unsigned char)(seed ^ index * 23U ^ (index >> 2));
}

static int test_premap_attach_recovery(const char *device_path)
{
	unsigned char data_pattern[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char copied[64];
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	struct c23_observable before;
	struct c23_observable after;
	struct c23_session session;
	struct c23_state state_before;
	struct c23_state state_after;
	struct c23_map *aligned = NULL;
	struct c23_map *unaligned = NULL;
	int ret = session_begin(&session, device_path);

	if (ret)
		goto out;
	fill_pattern(data_pattern, sizeof(data_pattern), 0x37);
	memset(session.page_a, 0xa4, C23_PAGE_A_SIZE);
	memset(session.page_b, 0xb5, C23_PAGE_SIZE);
	ret = data_write(&session, data_pattern, sizeof(data_pattern));
	if (ret)
		goto out;
	ret = capture_observable(&session, &before);
	if (ret || before.result_valid)
		goto protocol_error;
	ret = session_map_range(&session, session.page_a + 1,
				C23_PAGE_SIZE - 1U, C23_BASE_IOVA + 1U,
				IOMMU_IOAS_MAP_READABLE |
					IOMMU_IOAS_MAP_WRITEABLE,
				&unaligned);
	if (ret)
		goto out;
	ret = capture_observable(&session, &after);
	if (ret || !observables_equal(&before, &after))
		goto protocol_error;
	ret = session_attach(&session);
	if (ret != -EADDRINUSE)
		goto protocol_error;
	ret = capture_observable(&session, &after);
	if (ret || !observables_equal(&before, &after))
		goto protocol_error;
	ret = session_unmap(&session, unaligned);
	if (ret)
		goto out;
	ret = session_map(&session, session.page_a, C23_BASE_IOVA,
			  IOMMU_IOAS_MAP_READABLE | IOMMU_IOAS_MAP_WRITEABLE,
			  &aligned);
	if (ret)
		goto out;
	ret = read_state_raw(&session, state_wire, &state_before);
	if (ret)
		goto out;
	ret = session_attach(&session);
	if (ret)
		goto out;
	ret = read_state_raw(&session, state_wire, &state_after);
	if (ret || state_after.device_state != FWLAB_C21_STATE_OPEN_ATTACHED ||
	    state_after.flags !=
		    (FWLAB_C21_ST_F_OPEN | FWLAB_C21_ST_F_ATTACHED) ||
	    state_after.generation != state_before.generation + 1U ||
	    state_after.last_sequence || state_after.next_sequence != 1U)
		goto protocol_error;
	ret = submit_accepted(&session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			      C23_BASE_IOVA + 73U, sizeof(copied), 0);
	if (ret || data_read(&session, copied, sizeof(copied)) ||
	    memcmp(copied, session.page_a + 73U, sizeof(copied)))
		goto protocol_error;
	printf("C2.3 pre-attach unaligned map EADDRINUSE + recovery: PASS\n");
	goto out;

protocol_error:
	ret = -EPROTO;
out:
	session_cleanup(&session);
	return ret;
}

static int test_attached_map_matrix(const char *device_path)
{
	const uint32_t rw = IOMMU_IOAS_MAP_READABLE |
			    IOMMU_IOAS_MAP_WRITEABLE;
	struct c23_session session;
	struct c23_map *mapping = NULL;
	struct iommu_ioas_map map;
	unsigned char copied[64];
	int ret = session_begin(&session, device_path);

	if (ret)
		goto out;
	ret = session_attach(&session);
	if (ret)
		goto out;
	memset(session.page_a, 0x69, C23_PAGE_A_SIZE);
	memset(session.page_b, 0x7a, C23_PAGE_SIZE);

	map = (struct iommu_ioas_map) {
		.size = sizeof(map),
		.flags = IOMMU_IOAS_MAP_FIXED_IOVA,
		.ioas_id = session.ioas_id,
		.user_va = (uintptr_t)session.page_a,
		.length = C23_PAGE_SIZE,
		.iova = C23_ALT_IOVA,
	};
	ret = expect_map_errno_unchanged(&session, &map, EINVAL,
					 "no-read-or-write");
	if (ret)
		goto out;
	map.flags = IOMMU_IOAS_MAP_FIXED_IOVA | rw | (UINT32_C(1) << 31);
	ret = expect_map_errno_unchanged(&session, &map, EOPNOTSUPP,
					 "unknown-flag");
	if (ret)
		goto out;
	map.flags = IOMMU_IOAS_MAP_FIXED_IOVA | rw;
	map.__reserved = 1;
	ret = expect_map_errno_unchanged(&session, &map, EOPNOTSUPP,
					 "reserved-nonzero");
	if (ret)
		goto out;
	map.__reserved = 0;
	map.iova = C23_ALT_IOVA + 1U;
	ret = expect_map_errno_unchanged(&session, &map, EINVAL,
					 "unaligned-iova");
	if (ret)
		goto out;
	map.iova = C23_ALT_IOVA;
	map.length = C23_PAGE_SIZE - 1U;
	ret = expect_map_errno_unchanged(&session, &map, EINVAL,
					 "unaligned-length");
	if (ret)
		goto out;
	map.length = C23_PAGE_SIZE;
	map.user_va = (uintptr_t)(session.page_a + 1);
	ret = expect_map_errno_unchanged(&session, &map, EINVAL,
					 "unaligned-user-va");
	if (ret)
		goto out;

	ret = session_map(&session, session.page_a, C23_ALT_IOVA, rw,
			  &mapping);
	if (ret)
		goto out;
	ret = submit_accepted(&session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			      C23_ALT_IOVA + 53U, sizeof(copied), 0);
	if (ret || data_read(&session, copied, sizeof(copied)) ||
	    memcmp(copied, session.page_a + 53U, sizeof(copied)))
		goto protocol_error;
	map.user_va = (uintptr_t)session.page_b;
	map.length = C23_PAGE_SIZE;
	map.iova = C23_ALT_IOVA;
	ret = expect_map_errno_unchanged(&session, &map, EEXIST,
					 "overlap-existing-map");
	if (ret)
		goto out;
	ret = submit_accepted(&session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			      C23_ALT_IOVA + 149U, sizeof(copied), 0);
	if (ret || data_read(&session, copied, sizeof(copied)) ||
	    memcmp(copied, session.page_a + 149U, sizeof(copied)))
		goto protocol_error;
	printf("C2.3 attached MAP errno/alignment/overlap matrix: PASS\n");
	goto out;

protocol_error:
	ret = -EPROTO;
out:
	session_cleanup(&session);
	return ret;
}

static int test_permissions(struct c23_session *session)
{
	unsigned char data_before[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char data_after[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char page_before[C23_PAGE_A_SIZE];
	unsigned char page_expected[C23_PAGE_A_SIZE];
	unsigned char page_b_before[C23_PAGE_SIZE];
	unsigned char pattern[64];
	struct c23_map *mapping;
	int ret;

	fill_pattern(pattern, sizeof(pattern), 0x41);
	memset(session->page_a, 0x7a, C23_PAGE_SIZE);
	memcpy(session->page_a + 101, pattern, sizeof(pattern));
	ret = session_map(session, session->page_a, C23_BASE_IOVA,
			  IOMMU_IOAS_MAP_READABLE, &mapping);
	if (ret)
		return ret;
	ret = submit_accepted(session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			      C23_BASE_IOVA + 101, sizeof(pattern), 0);
	if (ret || data_read(session, data_after, sizeof(pattern)) ||
	    memcmp(data_after, pattern, sizeof(pattern)))
		return -EPROTO;
	memset(data_before, 0x55, sizeof(data_before));
	if (data_write(session, data_before, sizeof(data_before)))
		return -EIO;
	memcpy(page_before, session->page_a, sizeof(page_before));
	memcpy(page_b_before, session->page_b, sizeof(page_b_before));
	ret = submit_accepted(session, FWLAB_C21_OP_COPY_BUFFER_TO_IOAS,
			      C23_BASE_IOVA + 101, sizeof(pattern), -EPERM);
	if (ret || memcmp(page_before, session->page_a, sizeof(page_before)) ||
	    memcmp(page_b_before, session->page_b, sizeof(page_b_before)) ||
	    data_read(session, data_after, sizeof(data_after)) ||
	    memcmp(data_before, data_after, sizeof(data_before)))
		return -EPROTO;
	printf("C2.3 READABLE-only direction/EPERM matrix: PASS\n");
	ret = session_unmap(session, mapping);
	if (ret)
		return ret;

	memset(data_before, 0x63, sizeof(data_before));
	if (data_write(session, data_before, sizeof(data_before)))
		return -EIO;
	memset(session->page_a, 0x8b, C23_PAGE_SIZE);
	ret = session_map(session, session->page_a, C23_BASE_IOVA,
			  IOMMU_IOAS_MAP_WRITEABLE, &mapping);
	if (ret)
		return ret;
	ret = submit_accepted(session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			      C23_BASE_IOVA + 211, sizeof(pattern), -EPERM);
	if (ret)
		return ret;
	if (data_read(session, data_after, sizeof(data_after)) ||
	    memcmp(data_before, data_after, sizeof(data_before)))
		return -EPROTO;
	fill_pattern(data_before, sizeof(pattern), 0x92);
	if (data_write(session, data_before, sizeof(data_before)))
		return -EIO;
	memcpy(page_before, session->page_a, sizeof(page_before));
	memcpy(page_expected, page_before, sizeof(page_expected));
	memcpy(page_expected + 211, data_before, sizeof(pattern));
	memcpy(page_b_before, session->page_b, sizeof(page_b_before));
	ret = submit_accepted(session, FWLAB_C21_OP_COPY_BUFFER_TO_IOAS,
			      C23_BASE_IOVA + 211, sizeof(pattern), 0);
	if (ret || memcmp(session->page_a, page_expected,
			  sizeof(page_expected)) ||
	    memcmp(session->page_b, page_b_before, sizeof(page_b_before)))
		return -EPROTO;
	printf("C2.3 WRITEABLE-only EPERM/direction matrix: PASS\n");
	return session_unmap(session, mapping);
}

static int test_unmapped_errors(struct c23_session *session)
{
	unsigned char data_before[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char data_after[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char page_before[C23_PAGE_A_SIZE];
	unsigned char page_b_before[C23_PAGE_SIZE];
	int ret;

	memset(data_before, 0x51, sizeof(data_before));
	if (data_write(session, data_before, sizeof(data_before)))
		return -EIO;
	ret = submit_accepted(session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			      C23_UNMAPPED_IOVA, 64, -ENOENT);
	if (ret || data_read(session, data_after, sizeof(data_after)) ||
	    memcmp(data_before, data_after, sizeof(data_before)))
		return -EPROTO;
	memset(session->page_a, 0x6c, C23_PAGE_SIZE);
	memcpy(page_before, session->page_a, sizeof(page_before));
	memcpy(page_b_before, session->page_b, sizeof(page_b_before));
	ret = submit_accepted(session, FWLAB_C21_OP_COPY_BUFFER_TO_IOAS,
			      C23_UNMAPPED_IOVA, 64, -ENOENT);
	if (ret || memcmp(page_before, session->page_a, sizeof(page_before)) ||
	    memcmp(page_b_before, session->page_b, sizeof(page_b_before)))
		return -EPROTO;
	printf("C2.3 unmapped-start data/page no-commit semantics: PASS\n");
	return 0;
}

static int test_partial_unmap(struct c23_session *session)
{
	unsigned char data[64];
	unsigned char page_before[C23_PAGE_SIZE];
	struct c23_observable observable_before;
	struct c23_observable observable_after;
	struct iommu_ioas_unmap partial = {
		.size = sizeof(partial), .ioas_id = session->ioas_id,
		.iova = C23_BASE_IOVA, .length = C23_PAGE_SIZE / 2U,
	};
	struct c23_map *mapping;
	int ret;

	fill_pattern(session->page_a, C23_PAGE_SIZE, 0x4d);
	ret = session_map(session, session->page_a, C23_BASE_IOVA,
			  IOMMU_IOAS_MAP_READABLE | IOMMU_IOAS_MAP_WRITEABLE,
			  &mapping);
	if (ret)
		return ret;
	ret = capture_observable(session, &observable_before);
	if (ret)
		return ret;
	errno = 0;
	if (ioctl(session->iommu_fd, IOMMU_IOAS_UNMAP, &partial) != -1 ||
	    errno != ENOENT)
		return -EPROTO;
	ret = capture_observable(session, &observable_after);
	if (ret || !observables_equal(&observable_before, &observable_after))
		return -EPROTO;
	ret = submit_accepted(session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			      C23_BASE_IOVA + 33, 64, 0);
	if (ret || data_read(session, data, sizeof(data)) ||
	    memcmp(data, session->page_a + 33, sizeof(data)))
		return -EPROTO;
	fill_pattern(data, sizeof(data), 0x91);
	if (data_write(session, data, sizeof(data)))
		return -EIO;
	memcpy(page_before, session->page_a, sizeof(page_before));
	ret = submit_accepted(session, FWLAB_C21_OP_COPY_BUFFER_TO_IOAS,
			      C23_BASE_IOVA + 177, sizeof(data), 0);
	if (ret || memcmp(session->page_a + 177, data, sizeof(data)) ||
	    memcmp(session->page_a, page_before, 177) ||
	    memcmp(session->page_a + 177 + sizeof(data),
		   page_before + 177 + sizeof(data),
		   C23_PAGE_SIZE - 177 - sizeof(data)))
		return -EPROTO;
	printf("C2.3 partial exact-map UNMAP rejected; mapping usable: PASS "
	       "errno=%d\n", ENOENT);
	return session_unmap(session, mapping);
}

static int test_page_boundaries(struct c23_session *session)
{
	unsigned char request[FWLAB_C21_RECORD_SIZE];
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char copied[FWLAB_C21_MAX_COPY_LENGTH];
	struct c23_state state;
	struct c23_map *mapping;
	int ret;

	fill_pattern(session->page_a, C23_PAGE_SIZE, 0xc3);
	ret = session_map(session, session->page_a, C23_BASE_IOVA,
			  IOMMU_IOAS_MAP_READABLE | IOMMU_IOAS_MAP_WRITEABLE,
			  &mapping);
	if (ret)
		return ret;
	ret = submit_accepted(session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			      C23_BASE_IOVA + C23_PAGE_SIZE - 1U, 1, 0);
	if (ret || data_read(session, copied, 1) ||
	    copied[0] != session->page_a[C23_PAGE_SIZE - 1U])
		return -EPROTO;
	ret = submit_accepted(session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			      C23_BASE_IOVA + C23_PAGE_SIZE - sizeof(copied),
			      sizeof(copied), 0);
	if (ret || data_read(session, copied, sizeof(copied)) ||
	    memcmp(copied,
		   session->page_a + C23_PAGE_SIZE - sizeof(copied),
		   sizeof(copied)))
		return -EPROTO;
	ret = read_state_raw(session, state_wire, &state);
	if (ret)
		return ret;
	encode_request(request, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
		       state.next_sequence, state.generation,
		       C23_BASE_IOVA + C23_PAGE_SIZE - 1U, 2);
	ret = expect_rejected_unchanged(session, request, sizeof(request),
					FWLAB_C21_SUBMIT_OFFSET, ERANGE,
					"page-tail-two-bytes");
	if (ret)
		return ret;
	printf("C2.3 exact page-tail range boundaries: PASS\n");
	return session_unmap(session, mapping);
}

static int test_adjacent_cross_page(struct c23_session *session)
{
	unsigned char byte;
	unsigned char data[64];
	unsigned char request[FWLAB_C21_RECORD_SIZE];
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	struct c23_state state;
	struct c23_map *first;
	struct c23_map *second;
	int ret;

	fill_pattern(session->page_a, C23_PAGE_SIZE, 0x18);
	fill_pattern(session->page_b, C23_PAGE_SIZE, 0xe2);
	ret = session_map(session, session->page_a, C23_BASE_IOVA,
			  IOMMU_IOAS_MAP_READABLE | IOMMU_IOAS_MAP_WRITEABLE,
			  &first);
	if (ret)
		return ret;
	ret = session_map(session, session->page_b,
			  C23_BASE_IOVA + C23_PAGE_SIZE,
			  IOMMU_IOAS_MAP_READABLE | IOMMU_IOAS_MAP_WRITEABLE,
			  &second);
	if (ret)
		return ret;
	ret = submit_accepted(session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			      C23_BASE_IOVA + C23_PAGE_SIZE - 1U, 1, 0);
	if (ret || data_read(session, &byte, 1) ||
	    byte != session->page_a[C23_PAGE_SIZE - 1U])
		return -EPROTO;
	ret = submit_accepted(session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			      C23_BASE_IOVA + C23_PAGE_SIZE, 1, 0);
	if (ret || data_read(session, &byte, 1) || byte != session->page_b[0])
		return -EPROTO;
	ret = read_state_raw(session, state_wire, &state);
	if (ret)
		return ret;
	encode_request(request, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
		       state.next_sequence, state.generation,
		       C23_BASE_IOVA + C23_PAGE_SIZE - 128U, 256);
	ret = expect_rejected_unchanged(session, request, sizeof(request),
					FWLAB_C21_SUBMIT_OFFSET, ERANGE,
					"adjacent-cross-page");
	if (ret)
		return ret;
	ret = session_unmap(session, first);
	if (ret)
		return ret;
	ret = submit_accepted(session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			      C23_BASE_IOVA + C23_PAGE_SIZE + 91U,
			      sizeof(data), 0);
	if (ret || data_read(session, data, sizeof(data)) ||
	    memcmp(data, session->page_b + 91U, sizeof(data)))
		return -EPROTO;
	ret = session_unmap(session, second);
	if (!ret)
		printf("C2.3 adjacent exact maps cross-page engine reject: PASS\n");
	return ret;
}

static int test_negative_matrix(const char *device_path)
{
	struct c23_session session;
	struct c23_map *baseline_map;
	int ret = session_begin(&session, device_path);

	if (ret)
		goto out;
	memset(session.page_a, 0xa1, C23_PAGE_SIZE);
	ret = session_map(&session, session.page_a, C23_BASE_IOVA,
			  IOMMU_IOAS_MAP_READABLE | IOMMU_IOAS_MAP_WRITEABLE,
			  &baseline_map);
	if (ret)
		goto out;
	ret = session_attach(&session);
	if (ret)
		goto out;
	ret = submit_accepted(&session, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
			      C23_BASE_IOVA + 64, 64, 0);
	if (ret)
		goto out;
	ret = run_malformed_matrix(&session);
	if (ret)
		goto out;
	ret = session_unmap(&session, baseline_map);
	if (ret)
		goto out;
	ret = test_permissions(&session);
	if (ret)
		goto out;
	ret = test_unmapped_errors(&session);
	if (ret)
		goto out;
	ret = test_partial_unmap(&session);
	if (ret)
		goto out;
	ret = test_page_boundaries(&session);
	if (ret)
		goto out;
	ret = test_adjacent_cross_page(&session);
	if (ret)
		goto out;
	printf("C2.3 complete single-thread negative matrix: PASS\n");
out:
	session_cleanup(&session);
	return ret;
}

static int selftest(void)
{
	struct c23_region regions[2] = {
		{ .flags = VFIO_REGION_INFO_FLAG_READ |
			   VFIO_REGION_INFO_FLAG_WRITE,
		  .offset = 0x1000, .size = 4096 },
		{ .flags = VFIO_REGION_INFO_FLAG_READ |
			   VFIO_REGION_INFO_FLAG_WRITE,
		  .offset = 0x2000, .size = 4096 },
	};
	unsigned char request[FWLAB_C21_RECORD_SIZE];

	if (validate_region_layout(regions))
		return -1;
	regions[1].offset = 0x1800;
	if (!validate_region_layout(regions))
		return -1;
	encode_request(request, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 1, 2,
		       C23_BASE_IOVA, 64);
	if (get_le32(request + FWLAB_C21_REQ_MAGIC) !=
		    FWLAB_C21_REQUEST_MAGIC ||
	    get_le64(request + FWLAB_C21_REQ_SEQUENCE) != 1 ||
	    get_le64(request + FWLAB_C21_REQ_EXPECTED_GENERATION) != 2)
		return -1;
	printf("C2.3 pure LE/interval selftest: PASS\n");
	return 0;
}

int main(int argc, char **argv)
{
	int ret;

	if (argc != 2) {
		fprintf(stderr,
			"usage: %s /dev/vfio/devices/vfioN | --selftest\n",
			argv[0]);
		return EXIT_FAILURE;
	}
	if (!strcmp(argv[1], "--selftest"))
		return selftest() ? EXIT_FAILURE : EXIT_SUCCESS;
	ret = test_pre_bind(argv[1]);
	if (!ret)
		ret = test_bound_unattached(argv[1]);
	if (!ret)
		ret = test_premap_attach_recovery(argv[1]);
	if (!ret)
		ret = test_attached_map_matrix(argv[1]);
	if (!ret)
		ret = test_negative_matrix(argv[1]);
	if (ret) {
		errno = -ret;
		perror("C2.3 negative oracle");
		return EXIT_FAILURE;
	}
	printf("C2.3 independent negative IOAS-copy oracle: PASS\n");
	return EXIT_SUCCESS;
}
