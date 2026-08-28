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
#include <pthread.h>
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

#define C24_PAGE_SIZE 4096U
#define C24_IOVA_A UINT64_C(0x500000000)
#define C24_IOVA_B UINT64_C(0x500010000)
#define C24_IOVA_C UINT64_C(0x500020000)
#define C24_IOAS_COUNT 3U
#define C24_UNMAP_ROUNDS 24U
#define C24_TRANSITION_ROUNDS 12U
#define C24_COPY_LENGTH 64U

_Static_assert(FWLAB_C21_RECORD_SIZE == 64U,
	       "C2.4 requires exact 64-byte A-prime records");

enum c24_ioas_index {
	C24_IOAS_A = 0,
	C24_IOAS_B = 1,
	C24_IOAS_C = 2,
};

enum c24_action_kind {
	C24_ACTION_UNMAP,
	C24_ACTION_RESET,
	C24_ACTION_REPLACE,
	C24_ACTION_DETACH,
};

struct c24_region {
	uint32_t flags;
	uint64_t offset;
	uint64_t size;
};

struct c24_state {
	uint16_t device_state;
	uint32_t flags;
	uint64_t generation;
	uint64_t last_sequence;
	uint64_t next_sequence;
};

struct c24_result {
	uint16_t operation;
	uint32_t flags;
	uint64_t sequence;
	uint64_t generation;
	uint64_t iova;
	uint32_t length;
	int32_t op_errno;
};

struct c24_ioas {
	uint32_t id;
	bool created;
	bool mapped;
	uint64_t iova;
	uint64_t length;
};

struct c24_session {
	const char *device_path;
	int device_fd;
	int iommu_fd;
	bool attached;
	struct c24_region data_region;
	struct c24_region control_region;
	unsigned char *pages[C24_IOAS_COUNT];
	struct c24_ioas ioases[C24_IOAS_COUNT];
};

struct c24_observable {
	unsigned char state[FWLAB_C21_RECORD_SIZE];
	unsigned char result[FWLAB_C21_RECORD_SIZE];
	unsigned char data[FWLAB_C21_DATA_REGION_SIZE];
	bool result_valid;
};

struct c24_submit_call {
	pthread_barrier_t *barrier;
	int device_fd;
	off_t position;
	unsigned char request[FWLAB_C21_RECORD_SIZE];
	ssize_t result;
	int saved_errno;
};

struct c24_action_call {
	pthread_barrier_t *barrier;
	struct c24_session *session;
	enum c24_action_kind kind;
	struct c24_ioas *ioas;
	uint32_t replacement_id;
	int result;
	int saved_errno;
	uint64_t unmapped_length;
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

static int decode_state(const unsigned char wire[FWLAB_C21_RECORD_SIZE],
			struct c24_state *state)
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
			 struct c24_result *result)
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
	result->op_errno =
		decode_s32(get_le32(wire + FWLAB_C21_RES_OP_ERRNO));
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
			   uint64_t generation,
			   uint64_t iova, uint32_t length)
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

static int checked_span(const struct c24_region *region, uint64_t relative,
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

static int validate_region_layout(const struct c24_region regions[2])
{
	const uint32_t exact_flags = VFIO_REGION_INFO_FLAG_READ |
				     VFIO_REGION_INFO_FLAG_WRITE;
	uint64_t ends[2];
	unsigned int index;

	for (index = 0; index < 2; index++) {
		if (regions[index].size != FWLAB_C21_CONTROL_REGION_SIZE ||
		    regions[index].flags != exact_flags ||
		    regions[index].offset > UINT64_MAX - regions[index].size ||
		    regions[index].offset > (uint64_t)INT64_MAX ||
		    regions[index].size - 1U >
			    (uint64_t)INT64_MAX - regions[index].offset)
			return -ERANGE;
		ends[index] = regions[index].offset + regions[index].size;
	}
	return ends[0] <= regions[1].offset ||
		       ends[1] <= regions[0].offset ?
		       0 : -ERANGE;
}

static int read_state_raw(struct c24_session *session,
			  unsigned char wire[FWLAB_C21_RECORD_SIZE],
			  struct c24_state *state)
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

static int read_result_raw(struct c24_session *session,
			   unsigned char wire[FWLAB_C21_RECORD_SIZE],
			   struct c24_result *result)
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

static int data_read(struct c24_session *session, unsigned char *buffer,
		     size_t length)
{
	off_t position;
	int ret = checked_span(&session->data_region, 0, length, &position);

	return ret ? ret : pread_exact(session->device_fd, buffer, length,
					position);
}

static int data_write(struct c24_session *session,
		      const unsigned char *buffer, size_t length)
{
	off_t position;
	int ret = checked_span(&session->data_region, 0, length, &position);

	return ret ? ret : pwrite_exact(session->device_fd, buffer, length,
					 position);
}

static int discover_regions(struct c24_session *session)
{
	struct vfio_device_info device_info = { .argsz = sizeof(device_info) };
	struct c24_region regions[2];
	unsigned char wire[FWLAB_C21_RECORD_SIZE];
	struct c24_state state;
	unsigned int control_count = 0;
	unsigned int control_index = 0;
	unsigned int index;

	if (ioctl(session->device_fd, VFIO_DEVICE_GET_INFO, &device_info))
		return -errno;
	if (device_info.num_regions != 2 || device_info.num_irqs != 0 ||
	    device_info.flags != VFIO_DEVICE_FLAGS_RESET)
		return -EPROTO;
	for (index = 0; index < 2; index++) {
		struct vfio_region_info info = {
			.argsz = sizeof(info), .index = index,
		};

		if (ioctl(session->device_fd, VFIO_DEVICE_GET_REGION_INFO, &info))
			return -errno;
		if (info.index != index)
			return -EPROTO;
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

static int open_and_bind_device(struct c24_session *session)
{
	struct vfio_device_bind_iommufd bind = {
		.argsz = sizeof(bind), .iommufd = session->iommu_fd,
	};

	session->device_fd = open(session->device_path, O_RDWR | O_CLOEXEC);
	if (session->device_fd < 0)
		return -errno;
	if (ioctl(session->device_fd, VFIO_DEVICE_BIND_IOMMUFD, &bind))
		return -errno;
	return discover_regions(session);
}

static int session_begin(struct c24_session *session, const char *device_path)
{
	unsigned int index;
	int ret;

	memset(session, 0, sizeof(*session));
	session->device_path = device_path;
	session->device_fd = -1;
	session->iommu_fd = -1;
	if (sysconf(_SC_PAGESIZE) != C24_PAGE_SIZE)
		return -EPROTO;
	session->iommu_fd = open("/dev/iommu", O_RDWR | O_CLOEXEC);
	if (session->iommu_fd < 0)
		return -errno;
	ret = open_and_bind_device(session);
	if (ret)
		return ret;
	for (index = 0; index < C24_IOAS_COUNT; index++) {
		session->pages[index] =
			mmap(NULL, C24_PAGE_SIZE, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (session->pages[index] == MAP_FAILED) {
			session->pages[index] = NULL;
			return -errno;
		}
	}
	return 0;
}

static int create_ioas(struct c24_session *session,
		       enum c24_ioas_index index)
{
	struct iommu_ioas_alloc allocation = { .size = sizeof(allocation) };
	struct c24_ioas *ioas = &session->ioases[index];

	if (ioas->created)
		return -EALREADY;
	if (ioctl(session->iommu_fd, IOMMU_IOAS_ALLOC, &allocation))
		return -errno;
	ioas->id = allocation.out_ioas_id;
	ioas->created = true;
	return 0;
}

static int map_ioas(struct c24_session *session, enum c24_ioas_index index,
		    uint64_t iova, bool misaligned)
{
	struct c24_ioas *ioas = &session->ioases[index];
	struct iommu_ioas_map map = {
		.size = sizeof(map),
		.flags = IOMMU_IOAS_MAP_FIXED_IOVA |
			 IOMMU_IOAS_MAP_READABLE |
			 IOMMU_IOAS_MAP_WRITEABLE,
		.ioas_id = ioas->id,
		.user_va = (uintptr_t)(session->pages[index] +
					  (misaligned ? 1U : 0U)),
		.length = C24_PAGE_SIZE - (misaligned ? 1U : 0U),
		.iova = iova + (misaligned ? 1U : 0U),
	};

	if (!ioas->created || ioas->mapped)
		return -EINVAL;
	if (ioctl(session->iommu_fd, IOMMU_IOAS_MAP, &map))
		return -errno;
	if (map.iova != iova + (misaligned ? 1U : 0U) ||
	    map.length != C24_PAGE_SIZE - (misaligned ? 1U : 0U))
		return -EPROTO;
	ioas->mapped = true;
	ioas->iova = map.iova;
	ioas->length = map.length;
	return 0;
}

static int unmap_ioas(struct c24_session *session, struct c24_ioas *ioas)
{
	uint64_t expected_length;
	struct iommu_ioas_unmap unmap;

	if (!ioas->mapped)
		return 0;
	expected_length = ioas->length;
	unmap = (struct iommu_ioas_unmap) {
		.size = sizeof(unmap), .ioas_id = ioas->id,
		.iova = ioas->iova, .length = ioas->length,
	};
	if (ioctl(session->iommu_fd, IOMMU_IOAS_UNMAP, &unmap))
		return -errno;
	if (unmap.length != expected_length)
		return -EPROTO;
	ioas->mapped = false;
	return 0;
}

static int destroy_ioas(struct c24_session *session, struct c24_ioas *ioas)
{
	struct iommu_destroy destroy = {
		.size = sizeof(destroy), .id = ioas->id,
	};

	if (!ioas->created)
		return 0;
	if (ioctl(session->iommu_fd, IOMMU_DESTROY, &destroy))
		return -errno;
	ioas->created = false;
	return 0;
}

static int attach_ioas(struct c24_session *session, struct c24_ioas *ioas)
{
	struct vfio_device_attach_iommufd_pt attach = {
		.argsz = sizeof(attach), .pt_id = ioas->id,
	};

	if (ioctl(session->device_fd, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &attach))
		return -errno;
	session->attached = true;
	return 0;
}

static int detach_ioas(struct c24_session *session)
{
	struct vfio_device_detach_iommufd_pt detach = {
		.argsz = sizeof(detach),
	};

	if (ioctl(session->device_fd, VFIO_DEVICE_DETACH_IOMMUFD_PT, &detach))
		return -errno;
	session->attached = false;
	return 0;
}

static int capture_observable(struct c24_session *session,
			      struct c24_observable *observable)
{
	struct c24_result result;
	struct c24_state state;
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
	return data_read(session, observable->data, sizeof(observable->data));
}

static bool observables_equal(const struct c24_observable *left,
			      const struct c24_observable *right)
{
	return left->result_valid == right->result_valid &&
	       !memcmp(left->state, right->state, sizeof(left->state)) &&
	       (!left->result_valid ||
		!memcmp(left->result, right->result, sizeof(left->result))) &&
	       !memcmp(left->data, right->data, sizeof(left->data));
}

static bool all_zero(const unsigned char *buffer, size_t length)
{
	size_t index;

	for (index = 0; index < length; index++)
		if (buffer[index])
			return false;
	return true;
}

static int expect_cleared_generation(struct c24_session *session,
				     uint64_t generation, bool attached)
{
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char result_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char data[FWLAB_C21_DATA_REGION_SIZE];
	struct c24_state state;
	struct c24_result result;
	uint32_t expected_flags = FWLAB_C21_ST_F_OPEN;
	int ret;

	if (attached)
		expected_flags |= FWLAB_C21_ST_F_ATTACHED;
	ret = read_state_raw(session, state_wire, &state);
	if (ret ||
	    state.device_state != (attached ? FWLAB_C21_STATE_OPEN_ATTACHED :
					       FWLAB_C21_STATE_OPEN_UNATTACHED) ||
	    state.flags != expected_flags || state.generation != generation ||
	    state.last_sequence || state.next_sequence != 1U)
		return -EPROTO;
	ret = read_result_raw(session, result_wire, &result);
	if (ret != -ENODATA)
		return -EPROTO;
	ret = data_read(session, data, sizeof(data));
	if (ret || !all_zero(data, sizeof(data)))
		return -EPROTO;
	return 0;
}

static int request_position(struct c24_session *session, off_t *position)
{
	return checked_span(&session->control_region, FWLAB_C21_SUBMIT_OFFSET,
			    FWLAB_C21_RECORD_SIZE, position);
}

static int submit_copy(struct c24_session *session, uint64_t iova,
		       int32_t expected_op_errno,
		       const unsigned char *expected_source)
{
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char result_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char request[FWLAB_C21_RECORD_SIZE];
	unsigned char data_before[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char data_after[FWLAB_C21_DATA_REGION_SIZE];
	struct c24_state before;
	struct c24_state after;
	struct c24_result result;
	off_t position;
	int ret;

	ret = read_state_raw(session, state_wire, &before);
	if (ret || before.device_state != FWLAB_C21_STATE_OPEN_ATTACHED)
		return -EPROTO;
	ret = data_read(session, data_before, sizeof(data_before));
	if (ret)
		return ret;
	encode_request(request, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
		       before.next_sequence, before.generation, iova,
		       C24_COPY_LENGTH);
	ret = request_position(session, &position);
	if (ret)
		return ret;
	ret = pwrite_exact(session->device_fd, request, sizeof(request), position);
	if (ret)
		return ret;
	ret = read_result_raw(session, result_wire, &result);
	if (ret || result.operation != FWLAB_C21_OP_COPY_IOAS_TO_BUFFER ||
	    result.flags != FWLAB_C21_RES_F_VALID ||
	    result.sequence != before.next_sequence ||
	    result.generation != before.generation || result.iova != iova ||
	    result.length != C24_COPY_LENGTH ||
	    result.op_errno != expected_op_errno)
		return -EPROTO;
	ret = read_state_raw(session, state_wire, &after);
	if (ret || after.device_state != FWLAB_C21_STATE_OPEN_ATTACHED ||
	    after.generation != before.generation ||
	    after.flags != (FWLAB_C21_ST_F_OPEN |
			    FWLAB_C21_ST_F_ATTACHED |
			    FWLAB_C21_ST_F_RESULT_VALID) ||
	    after.last_sequence != before.next_sequence ||
	    after.next_sequence != before.next_sequence + 1U)
		return -EPROTO;
	ret = data_read(session, data_after, sizeof(data_after));
	if (ret)
		return ret;
	if (!expected_op_errno) {
		if (!expected_source ||
		    memcmp(data_after, expected_source, C24_COPY_LENGTH) ||
		    memcmp(data_after + C24_COPY_LENGTH,
			   data_before + C24_COPY_LENGTH,
			   sizeof(data_after) - C24_COPY_LENGTH))
			return -EPROTO;
	} else if (memcmp(data_before, data_after, sizeof(data_before))) {
		return -EPROTO;
	}
	return 0;
}

static int expect_submit_reject(struct c24_session *session, uint64_t iova,
				int expected_errno)
{
	struct c24_observable before;
	struct c24_observable after;
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char request[FWLAB_C21_RECORD_SIZE];
	struct c24_state state;
	off_t position;
	ssize_t done;
	int ret;

	ret = read_state_raw(session, state_wire, &state);
	if (ret)
		return ret;
	ret = capture_observable(session, &before);
	if (ret)
		return ret;
	encode_request(request, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
		       state.next_sequence, state.generation, iova,
		       C24_COPY_LENGTH);
	ret = request_position(session, &position);
	if (ret)
		return ret;
	errno = 0;
	done = pwrite(session->device_fd, request, sizeof(request), position);
	if (done != -1 || errno != expected_errno)
		return -EPROTO;
	ret = capture_observable(session, &after);
	return ret || !observables_equal(&before, &after) ? -EPROTO : 0;
}

static int seed_ioases(struct c24_session *session, bool include_c)
{
	unsigned int count = include_c ? C24_IOAS_COUNT : 2U;
	unsigned int index;
	int ret;

	for (index = 0; index < C24_IOAS_COUNT; index++)
		memset(session->pages[index], (int)(0x31U + index * 0x22U),
		       C24_PAGE_SIZE);
	for (index = 0; index < count; index++) {
		ret = create_ioas(session, (enum c24_ioas_index)index);
		if (ret)
			return ret;
	}
	ret = map_ioas(session, C24_IOAS_A, C24_IOVA_A, false);
	if (!ret)
		ret = map_ioas(session, C24_IOAS_B, C24_IOVA_B, false);
	if (!ret && include_c)
		ret = map_ioas(session, C24_IOAS_C, C24_IOVA_C, true);
	return ret;
}

static void session_cleanup(struct c24_session *session)
{
	unsigned int index;
	int ret;

	if (session->device_fd >= 0 && session->attached) {
		ret = detach_ioas(session);
		if (ret) {
			close(session->device_fd);
			session->device_fd = -1;
			session->attached = false;
		}
	}
	for (index = 0; index < C24_IOAS_COUNT; index++)
		if (session->ioases[index].mapped)
			(void)unmap_ioas(session, &session->ioases[index]);
	for (index = 0; index < C24_IOAS_COUNT; index++) {
		if (!session->ioases[index].created)
			continue;
		ret = destroy_ioas(session, &session->ioases[index]);
		if (ret && session->device_fd >= 0) {
			close(session->device_fd);
			session->device_fd = -1;
			session->attached = false;
			(void)destroy_ioas(session, &session->ioases[index]);
		}
	}
	if (session->device_fd >= 0)
		close(session->device_fd);
	if (session->iommu_fd >= 0)
		close(session->iommu_fd);
	for (index = 0; index < C24_IOAS_COUNT; index++)
		if (session->pages[index])
			munmap(session->pages[index], C24_PAGE_SIZE);
}

static int test_replace_and_failed_replace(const char *device_path)
{
	struct c24_session session;
	struct c24_observable before;
	struct c24_observable after;
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	struct c24_state state;
	uint64_t generation;
	int ret = session_begin(&session, device_path);

	if (ret)
		goto out;
	ret = seed_ioases(&session, true);
	if (ret)
		goto out;
	ret = read_state_raw(&session, state_wire, &state);
	if (ret)
		goto out;
	generation = state.generation;
	ret = attach_ioas(&session, &session.ioases[C24_IOAS_A]);
	if (ret || expect_cleared_generation(&session, generation + 1U, true))
		goto protocol_error;
	ret = submit_copy(&session, C24_IOVA_A, 0,
			  session.pages[C24_IOAS_A]);
	if (ret)
		goto out;
	ret = read_state_raw(&session, state_wire, &state);
	if (ret)
		goto out;
	generation = state.generation;
	ret = attach_ioas(&session, &session.ioases[C24_IOAS_B]);
	if (ret || expect_cleared_generation(&session, generation + 1U, true))
		goto protocol_error;
	ret = submit_copy(&session, C24_IOVA_A, -ENOENT, NULL);
	if (!ret)
		ret = submit_copy(&session, C24_IOVA_B, 0,
				  session.pages[C24_IOAS_B]);
	if (ret)
		goto out;
	ret = capture_observable(&session, &before);
	if (ret)
		goto out;
	ret = attach_ioas(&session, &session.ioases[C24_IOAS_C]);
	if (ret != -EADDRINUSE)
		goto protocol_error;
	ret = capture_observable(&session, &after);
	if (ret || !observables_equal(&before, &after))
		goto protocol_error;
	ret = submit_copy(&session, C24_IOVA_A, -ENOENT, NULL);
	if (!ret)
		ret = submit_copy(&session, C24_IOVA_B, 0,
				  session.pages[C24_IOAS_B]);
	if (ret)
		goto out;
	printf("C2.4 A-to-B replace and failed misaligned-C preservation: PASS\n");
	goto out;

protocol_error:
	ret = -EPROTO;
out:
	session_cleanup(&session);
	return ret;
}

static int attach_with_read_only_argument(struct c24_session *session,
					  uint32_t ioas_id)
{
	struct vfio_device_attach_iommufd_pt *attach;
	void *page;
	int saved_errno;
	int result;

	page = mmap(NULL, C24_PAGE_SIZE, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (page == MAP_FAILED)
		return -errno;
	attach = page;
	memset(attach, 0, sizeof(*attach));
	attach->argsz = sizeof(*attach);
	attach->pt_id = ioas_id;
	if (mprotect(page, C24_PAGE_SIZE, PROT_READ)) {
		saved_errno = errno;
		munmap(page, C24_PAGE_SIZE);
		return -saved_errno;
	}
	errno = 0;
	result = ioctl(session->device_fd, VFIO_DEVICE_ATTACH_IOMMUFD_PT,
		       attach);
	saved_errno = errno;
	if (mprotect(page, C24_PAGE_SIZE, PROT_READ | PROT_WRITE)) {
		munmap(page, C24_PAGE_SIZE);
		return -errno;
	}
	munmap(page, C24_PAGE_SIZE);
	return result == -1 ? -saved_errno : -EPROTO;
}

static int test_attach_copyout_unwind(const char *device_path)
{
	struct c24_session session;
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	struct c24_state before;
	uint64_t detached_generation;
	int ret = session_begin(&session, device_path);

	if (ret)
		goto out;
	ret = seed_ioases(&session, false);
	if (!ret)
		ret = attach_ioas(&session, &session.ioases[C24_IOAS_A]);
	if (!ret)
		ret = submit_copy(&session, C24_IOVA_A, 0,
				  session.pages[C24_IOAS_A]);
	if (!ret)
		ret = read_state_raw(&session, state_wire, &before);
	if (ret)
		goto out;
	ret = attach_with_read_only_argument(
		&session, session.ioases[C24_IOAS_B].id);
	if (ret != -EFAULT)
		goto protocol_error;
	/* Core copyout unwind has detached the replacement; do not detach again. */
	session.attached = false;
	detached_generation = before.generation + 2U;
	if (expect_cleared_generation(&session, detached_generation, false))
		goto protocol_error;
	ret = expect_submit_reject(&session, C24_IOVA_A, ENOTCONN);
	if (ret)
		goto out;
	ret = unmap_ioas(&session, &session.ioases[C24_IOAS_A]);
	if (!ret)
		ret = destroy_ioas(&session, &session.ioases[C24_IOAS_A]);
	if (!ret)
		ret = unmap_ioas(&session, &session.ioases[C24_IOAS_B]);
	if (!ret)
		ret = destroy_ioas(&session, &session.ioases[C24_IOAS_B]);
	if (!ret)
		ret = create_ioas(&session, C24_IOAS_B);
	if (!ret)
		ret = map_ioas(&session, C24_IOAS_B, C24_IOVA_B, false);
	if (ret)
		goto out;
	ret = attach_ioas(&session, &session.ioases[C24_IOAS_B]);
	if (ret || expect_cleared_generation(&session,
					     detached_generation + 1U, true))
		goto protocol_error;
	ret = submit_copy(&session, C24_IOVA_B, 0,
			  session.pages[C24_IOAS_B]);
	if (ret)
		goto out;
	printf("C2.4 read-only REPLACE copyout EFAULT detach and recovery: PASS\n");
	goto out;

protocol_error:
	ret = -EPROTO;
out:
	session_cleanup(&session);
	return ret;
}

static int test_duplicate_detach(const char *device_path)
{
	struct c24_session session;
	struct c24_observable before_duplicate;
	struct c24_observable after_duplicate;
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	struct vfio_device_detach_iommufd_pt detach = {
		.argsz = sizeof(detach),
	};
	struct c24_state state;
	uint64_t generation;
	int ret = session_begin(&session, device_path);

	if (ret)
		goto out;
	ret = create_ioas(&session, C24_IOAS_A);
	if (!ret)
		ret = map_ioas(&session, C24_IOAS_A, C24_IOVA_A, false);
	if (!ret)
		ret = attach_ioas(&session, &session.ioases[C24_IOAS_A]);
	if (!ret)
		ret = submit_copy(&session, C24_IOVA_A, 0,
				  session.pages[C24_IOAS_A]);
	if (ret)
		goto out;
	ret = read_state_raw(&session, state_wire, &state);
	if (ret)
		goto out;
	generation = state.generation;
	ret = detach_ioas(&session);
	if (ret || expect_cleared_generation(&session, generation + 1U, false))
		goto protocol_error;
	ret = capture_observable(&session, &before_duplicate);
	if (ret)
		goto out;
	if (ioctl(session.device_fd, VFIO_DEVICE_DETACH_IOMMUFD_PT, &detach))
		goto protocol_error;
	ret = capture_observable(&session, &after_duplicate);
	if (ret || !observables_equal(&before_duplicate, &after_duplicate))
		goto protocol_error;
	ret = unmap_ioas(&session, &session.ioases[C24_IOAS_A]);
	if (!ret)
		ret = destroy_ioas(&session, &session.ioases[C24_IOAS_A]);
	if (ret)
		goto out;
	printf("C2.4 duplicate DETACH core no-op and stable engine state: PASS\n");
	goto out;

protocol_error:
	ret = -EPROTO;
out:
	session_cleanup(&session);
	return ret;
}

static int test_serial_close_attached(const char *device_path)
{
	struct c24_session session;
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	struct c24_state before_close;
	struct c24_state reopened;
	uint64_t reopened_generation;
	int ret = session_begin(&session, device_path);

	if (ret)
		goto out;
	ret = create_ioas(&session, C24_IOAS_A);
	if (!ret)
		ret = map_ioas(&session, C24_IOAS_A, C24_IOVA_A, false);
	if (!ret)
		ret = attach_ioas(&session, &session.ioases[C24_IOAS_A]);
	if (!ret)
		ret = submit_copy(&session, C24_IOVA_A, 0,
				  session.pages[C24_IOAS_A]);
	if (!ret)
		ret = read_state_raw(&session, state_wire, &before_close);
	if (ret)
		goto out;

	/* Deliberately serial: no thread may use this fd while close runs. */
	close(session.device_fd);
	session.device_fd = -1;
	session.attached = false;
	ret = unmap_ioas(&session, &session.ioases[C24_IOAS_A]);
	if (!ret)
		ret = destroy_ioas(&session, &session.ioases[C24_IOAS_A]);
	if (!ret)
		ret = open_and_bind_device(&session);
	if (!ret)
		ret = read_state_raw(&session, state_wire, &reopened);
	if (ret)
		goto out;
	if (reopened.device_state != FWLAB_C21_STATE_OPEN_UNATTACHED ||
	    reopened.flags != FWLAB_C21_ST_F_OPEN ||
	    reopened.generation != before_close.generation + 2U ||
	    reopened.last_sequence || reopened.next_sequence != 1U ||
	    expect_cleared_generation(&session, reopened.generation, false))
		goto protocol_error;
	reopened_generation = reopened.generation;
	ret = create_ioas(&session, C24_IOAS_B);
	if (!ret)
		ret = map_ioas(&session, C24_IOAS_B, C24_IOVA_B, false);
	if (!ret)
		ret = attach_ioas(&session, &session.ioases[C24_IOAS_B]);
	if (ret || expect_cleared_generation(&session,
					     reopened_generation + 1U, true))
		goto protocol_error;
	ret = submit_copy(&session, C24_IOVA_B, 0,
			  session.pages[C24_IOAS_B]);
	if (ret)
		goto out;
	printf("C2.4 serial close-attached, IOAS cleanup, and reopen generation: PASS\n");
	goto out;

protocol_error:
	ret = -EPROTO;
out:
	session_cleanup(&session);
	return ret;
}

static bool barrier_wait_ok(pthread_barrier_t *barrier)
{
	int ret = pthread_barrier_wait(barrier);

	return ret == 0 || ret == PTHREAD_BARRIER_SERIAL_THREAD;
}

static void *submit_thread(void *opaque)
{
	struct c24_submit_call *call = opaque;

	if (!barrier_wait_ok(call->barrier)) {
		call->result = -1;
		call->saved_errno = EIO;
		return NULL;
	}
	errno = 0;
	call->result = pwrite(call->device_fd, call->request,
			      sizeof(call->request), call->position);
	call->saved_errno = errno;
	return NULL;
}

static void *action_thread(void *opaque)
{
	struct c24_action_call *call = opaque;
	struct vfio_device_attach_iommufd_pt attach;
	struct vfio_device_detach_iommufd_pt detach;
	struct iommu_ioas_unmap unmap;

	if (!barrier_wait_ok(call->barrier)) {
		call->result = -1;
		call->saved_errno = EIO;
		return NULL;
	}
	errno = 0;
	switch (call->kind) {
	case C24_ACTION_UNMAP:
		unmap = (struct iommu_ioas_unmap) {
			.size = sizeof(unmap), .ioas_id = call->ioas->id,
			.iova = call->ioas->iova,
			.length = call->ioas->length,
		};
		call->result = ioctl(call->session->iommu_fd, IOMMU_IOAS_UNMAP,
				     &unmap);
		call->unmapped_length = unmap.length;
		break;
	case C24_ACTION_RESET:
		call->result = ioctl(call->session->device_fd, VFIO_DEVICE_RESET);
		break;
	case C24_ACTION_REPLACE:
		attach = (struct vfio_device_attach_iommufd_pt) {
			.argsz = sizeof(attach), .pt_id = call->replacement_id,
		};
		call->result = ioctl(call->session->device_fd,
				     VFIO_DEVICE_ATTACH_IOMMUFD_PT, &attach);
		break;
	case C24_ACTION_DETACH:
		detach = (struct vfio_device_detach_iommufd_pt) {
			.argsz = sizeof(detach),
		};
		call->result = ioctl(call->session->device_fd,
				     VFIO_DEVICE_DETACH_IOMMUFD_PT, &detach);
		break;
	default:
		call->result = -1;
		errno = EINVAL;
		break;
	}
	call->saved_errno = errno;
	return NULL;
}

static int run_race_pair(struct c24_session *session,
			 struct c24_submit_call *submit,
			 struct c24_action_call *action)
{
	pthread_barrier_t barrier;
	pthread_t submit_tid;
	pthread_t action_tid;
	int create_action;
	int destroy_ret;
	int ret;

	ret = pthread_barrier_init(&barrier, NULL, 2);
	if (ret)
		return -ret;
	submit->barrier = &barrier;
	action->barrier = &barrier;
	action->session = session;
	ret = pthread_create(&submit_tid, NULL, submit_thread, submit);
	if (ret) {
		destroy_ret = pthread_barrier_destroy(&barrier);
		return destroy_ret ? -destroy_ret : -ret;
	}
	create_action = pthread_create(&action_tid, NULL, action_thread, action);
	if (create_action) {
		/* Release the first thread without ever closing its fd concurrently. */
		if (!barrier_wait_ok(&barrier)) {
			fprintf(stderr, "C2.4 recovery barrier wait failed\n");
			abort();
		}
		ret = pthread_join(submit_tid, NULL);
		if (ret) {
			errno = ret;
			perror("C2.4 submit-thread join");
			abort();
		}
		destroy_ret = pthread_barrier_destroy(&barrier);
		return destroy_ret ? -destroy_ret : -create_action;
	}
	ret = pthread_join(submit_tid, NULL);
	if (ret) {
		errno = ret;
		perror("C2.4 submit-thread join");
		abort();
	}
	ret = pthread_join(action_tid, NULL);
	if (ret) {
		errno = ret;
		perror("C2.4 action-thread join");
		abort();
	}
	destroy_ret = pthread_barrier_destroy(&barrier);
	return destroy_ret ? -destroy_ret : 0;
}

static int prepare_submit_call(struct c24_session *session, uint16_t operation,
			       uint64_t iova,
			       const struct c24_state *state,
			       struct c24_submit_call *call)
{
	int ret;

	memset(call, 0, sizeof(*call));
	call->device_fd = session->device_fd;
	encode_request(call->request, operation, state->next_sequence,
		       state->generation, iova, C24_COPY_LENGTH);
	ret = request_position(session, &call->position);
	return ret;
}

static int validate_race_result(struct c24_session *session,
				const struct c24_state *before,
				uint64_t iova, int32_t first_errno,
				int32_t second_errno)
{
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char result_wire[FWLAB_C21_RECORD_SIZE];
	struct c24_state after;
	struct c24_result result;
	int ret;

	ret = read_result_raw(session, result_wire, &result);
	if (ret || result.operation != FWLAB_C21_OP_COPY_IOAS_TO_BUFFER ||
	    result.flags != FWLAB_C21_RES_F_VALID ||
	    result.sequence != before->next_sequence ||
	    result.generation != before->generation || result.iova != iova ||
	    result.length != C24_COPY_LENGTH ||
	    (result.op_errno != first_errno && result.op_errno != second_errno))
		return -EPROTO;
	ret = read_state_raw(session, state_wire, &after);
	if (ret || after.device_state != FWLAB_C21_STATE_OPEN_ATTACHED ||
	    after.generation != before->generation ||
	    after.flags != (FWLAB_C21_ST_F_OPEN |
			    FWLAB_C21_ST_F_ATTACHED |
			    FWLAB_C21_ST_F_RESULT_VALID) ||
	    after.last_sequence != before->next_sequence ||
	    after.next_sequence != before->next_sequence + 1U)
		return -EPROTO;
	return result.op_errno;
}

static int test_submit_unmap_race(const char *device_path)
{
	struct c24_session session;
	unsigned char data[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char sentinel[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char source_snapshot[C24_PAGE_SIZE];
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	struct c24_submit_call submit;
	struct c24_action_call action;
	struct c24_state before;
	unsigned int success_count = 0;
	unsigned int enoent_count = 0;
	unsigned int round;
	int op_errno;
	int ret = session_begin(&session, device_path);

	if (ret)
		goto out;
	ret = create_ioas(&session, C24_IOAS_A);
	if (!ret)
		ret = map_ioas(&session, C24_IOAS_A, C24_IOVA_A, false);
	if (!ret)
		ret = attach_ioas(&session, &session.ioases[C24_IOAS_A]);
	if (ret)
		goto out;
	memset(session.pages[C24_IOAS_A], 0xa7, C24_PAGE_SIZE);
	for (round = 0; round < C24_UNMAP_ROUNDS; round++) {
		memset(sentinel, (int)(0x40U + round), sizeof(sentinel));
		memcpy(source_snapshot, session.pages[C24_IOAS_A],
		       sizeof(source_snapshot));
		ret = data_write(&session, sentinel, sizeof(sentinel));
		if (!ret)
			ret = read_state_raw(&session, state_wire, &before);
		if (ret)
			goto out;
		ret = prepare_submit_call(&session,
					  FWLAB_C21_OP_COPY_IOAS_TO_BUFFER,
					  C24_IOVA_A, &before,
					  &submit);
		if (ret)
			goto out;
		memset(&action, 0, sizeof(action));
		action.kind = C24_ACTION_UNMAP;
		action.ioas = &session.ioases[C24_IOAS_A];
		ret = run_race_pair(&session, &submit, &action);
		if (ret)
			goto out;
		if (action.result || action.unmapped_length != C24_PAGE_SIZE ||
		    submit.result != FWLAB_C21_RECORD_SIZE)
			goto protocol_error;
		session.ioases[C24_IOAS_A].mapped = false;
		op_errno = validate_race_result(&session, &before, C24_IOVA_A,
					       0, -ENOENT);
		if (op_errno != 0 && op_errno != -ENOENT)
			goto protocol_error;
		if (op_errno)
			enoent_count++;
		else
			success_count++;
		ret = data_read(&session, data, sizeof(data));
		if (ret)
			goto out;
		if ((!op_errno &&
		     (memcmp(data, source_snapshot, C24_COPY_LENGTH) ||
		      memcmp(data + C24_COPY_LENGTH,
			     sentinel + C24_COPY_LENGTH,
			     sizeof(data) - C24_COPY_LENGTH))) ||
		    (op_errno && memcmp(data, sentinel, sizeof(data))) ||
		    memcmp(session.pages[C24_IOAS_A], source_snapshot,
			   sizeof(source_snapshot)))
			goto protocol_error;
		ret = submit_copy(&session, C24_IOVA_A, -ENOENT, NULL);
		if (ret)
			goto out;
		ret = map_ioas(&session, C24_IOAS_A, C24_IOVA_A, false);
		if (ret)
			goto out;
	}
	printf("C2.4 submit-vs-exact-UNMAP rounds=%u success=%u enoent=%u: PASS\n",
	       C24_UNMAP_ROUNDS, success_count, enoent_count);
	goto out;

protocol_error:
	ret = -EPROTO;
out:
	session_cleanup(&session);
	return ret;
}

static const char *action_name(enum c24_action_kind kind)
{
	switch (kind) {
	case C24_ACTION_RESET:
		return "RESET";
	case C24_ACTION_REPLACE:
		return "REPLACE";
	case C24_ACTION_DETACH:
		return "DETACH";
	default:
		return "UNKNOWN";
	}
}

static int check_unique_owner(struct c24_session *session,
			      enum c24_ioas_index owner,
			      enum c24_ioas_index nonowner)
{
	static const uint64_t iovas[C24_IOAS_COUNT] = {
		C24_IOVA_A, C24_IOVA_B, C24_IOVA_C,
	};
	int ret;

	ret = submit_copy(session, iovas[nonowner], -ENOENT, NULL);
	if (!ret)
		ret = submit_copy(session, iovas[owner], 0,
				  session->pages[owner]);
	return ret;
}

static int run_transition_race(const char *device_path,
			       enum c24_action_kind kind)
{
	struct c24_session session;
	unsigned char nonowner_snapshot[C24_PAGE_SIZE];
	unsigned char page_sentinel[C24_PAGE_SIZE];
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char dirty[FWLAB_C21_DATA_REGION_SIZE];
	struct c24_submit_call submit;
	struct c24_action_call action;
	struct c24_state before;
	enum c24_ioas_index owner = C24_IOAS_A;
	enum c24_ioas_index other = C24_IOAS_B;
	unsigned int accepted_count = 0;
	unsigned int rejected_count = 0;
	unsigned int round;
	size_t byte;
	bool submit_accepted;
	int expected_reject;
	int ret = session_begin(&session, device_path);

	if (ret)
		goto out;
	ret = seed_ioases(&session, false);
	if (!ret)
		ret = attach_ioas(&session, &session.ioases[owner]);
	if (ret)
		goto out;
	for (round = 0; round < C24_TRANSITION_ROUNDS; round++) {
		for (byte = 0; byte < sizeof(dirty); byte++)
			dirty[byte] = (unsigned char)(0x21U + byte * 13U +
						      round * 17U +
						      (unsigned int)kind * 29U);
		memset(page_sentinel, (int)(0x90U + round),
		       sizeof(page_sentinel));
		memcpy(session.pages[owner], page_sentinel,
		       sizeof(page_sentinel));
		memcpy(nonowner_snapshot, session.pages[other],
		       sizeof(nonowner_snapshot));
		ret = data_write(&session, dirty, sizeof(dirty));
		if (!ret)
			ret = read_state_raw(&session, state_wire, &before);
		if (!ret)
			ret = prepare_submit_call(
				&session, FWLAB_C21_OP_COPY_BUFFER_TO_IOAS,
				owner == C24_IOAS_A ? C24_IOVA_A : C24_IOVA_B,
				&before, &submit);
		if (ret)
			goto out;
		memset(&action, 0, sizeof(action));
		action.kind = kind;
		if (kind == C24_ACTION_REPLACE)
			action.replacement_id = session.ioases[other].id;
		ret = run_race_pair(&session, &submit, &action);
		if (ret)
			goto out;
		if (action.result)
			goto protocol_error;
		expected_reject = kind == C24_ACTION_DETACH ? ENOTCONN : ESTALE;
		if (submit.result == FWLAB_C21_RECORD_SIZE) {
			accepted_count++;
			submit_accepted = true;
		} else if (submit.result == -1 &&
			   submit.saved_errno == expected_reject) {
			rejected_count++;
			submit_accepted = false;
		} else {
			goto protocol_error;
		}
		if ((submit_accepted &&
		     (memcmp(session.pages[owner], dirty, C24_COPY_LENGTH) ||
		      memcmp(session.pages[owner] + C24_COPY_LENGTH,
			     page_sentinel + C24_COPY_LENGTH,
			     C24_PAGE_SIZE - C24_COPY_LENGTH))) ||
		    (!submit_accepted &&
		     memcmp(session.pages[owner], page_sentinel,
			    sizeof(page_sentinel))) ||
		    memcmp(session.pages[other], nonowner_snapshot,
			   sizeof(nonowner_snapshot)))
			goto protocol_error;
		if (expect_cleared_generation(&session, before.generation + 1U,
					      kind != C24_ACTION_DETACH))
			goto protocol_error;

		if (kind == C24_ACTION_RESET) {
			ret = check_unique_owner(&session, owner, other);
		} else if (kind == C24_ACTION_REPLACE) {
			enum c24_ioas_index old_owner = owner;

			owner = other;
			other = old_owner;
			ret = check_unique_owner(&session, owner, other);
		} else {
			uint64_t detached_generation = before.generation + 1U;
			enum c24_ioas_index old_owner = owner;

			session.attached = false;
			ret = expect_submit_reject(
				&session,
				old_owner == C24_IOAS_A ? C24_IOVA_A : C24_IOVA_B,
				ENOTCONN);
			owner = other;
			other = old_owner;
			if (!ret)
				ret = attach_ioas(&session,
						  &session.ioases[owner]);
			if (!ret)
				ret = expect_cleared_generation(
					&session, detached_generation + 1U, true);
			if (!ret)
				ret = check_unique_owner(&session, owner, other);
		}
		if (ret)
			goto out;
	}
	printf("C2.4 submit-vs-%s rounds=%u pwrite64=%u state-reject=%u: PASS\n",
	       action_name(kind), C24_TRANSITION_ROUNDS, accepted_count,
	       rejected_count);
	goto out;

protocol_error:
	ret = -EPROTO;
out:
	session_cleanup(&session);
	return ret;
}

static int selftest(void)
{
	struct c24_region regions[2] = {
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
	encode_request(request, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 3, 7,
		       C24_IOVA_A, C24_COPY_LENGTH);
	if (get_le32(request + FWLAB_C21_REQ_MAGIC) !=
		    FWLAB_C21_REQUEST_MAGIC ||
	    get_le16(request + FWLAB_C21_REQ_OPERATION) !=
		    FWLAB_C21_OP_COPY_IOAS_TO_BUFFER ||
	    get_le64(request + FWLAB_C21_REQ_SEQUENCE) != 3 ||
	    get_le64(request + FWLAB_C21_REQ_EXPECTED_GENERATION) != 7 ||
	    get_le64(request + FWLAB_C21_REQ_IOVA) != C24_IOVA_A ||
	    get_le32(request + FWLAB_C21_REQ_LENGTH) != C24_COPY_LENGTH ||
	    !request_range_valid(C24_IOVA_A, C24_COPY_LENGTH) ||
	    request_range_valid(C24_IOVA_A + C24_PAGE_SIZE - 32U,
				C24_COPY_LENGTH))
		return -1;
	printf("C2.4 pure LE/record/interval selftest: PASS\n");
	return 0;
}

static int hold_open(const char *device_path)
{
	struct c24_session session;
	unsigned char buffer[64];
	ssize_t done;
	int ret = session_begin(&session, device_path);

	if (ret)
		goto out;
	printf("C2.4 hold-open READY\n");
	if (fflush(stdout)) {
		ret = -errno;
		goto out;
	}
	do {
		done = read(STDIN_FILENO, buffer, sizeof(buffer));
	} while (done > 0 || (done < 0 && errno == EINTR));
	if (done < 0)
		ret = -errno;
out:
	session_cleanup(&session);
	return ret;
}

int main(int argc, char **argv)
{
	int ret;

	if (argc == 3 && !strcmp(argv[1], "--hold-open")) {
		ret = hold_open(argv[2]);
		if (ret) {
			errno = -ret;
			perror("C2.4 hold-open");
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
	}
	if (argc != 2) {
		fprintf(stderr,
			"usage: %s /dev/vfio/devices/vfioN | --selftest | "
			"--hold-open /dev/vfio/devices/vfioN\n",
			argv[0]);
		return EXIT_FAILURE;
	}
	if (!strcmp(argv[1], "--selftest"))
		return selftest() ? EXIT_FAILURE : EXIT_SUCCESS;
	ret = test_replace_and_failed_replace(argv[1]);
	if (!ret)
		ret = test_attach_copyout_unwind(argv[1]);
	if (!ret)
		ret = test_duplicate_detach(argv[1]);
	if (!ret)
		ret = test_serial_close_attached(argv[1]);
	if (!ret)
		ret = test_submit_unmap_race(argv[1]);
	if (!ret)
		ret = run_transition_race(argv[1], C24_ACTION_RESET);
	if (!ret)
		ret = run_transition_race(argv[1], C24_ACTION_REPLACE);
	if (!ret)
		ret = run_transition_race(argv[1], C24_ACTION_DETACH);
	if (ret) {
		errno = -ret;
		perror("C2.4 lifecycle/race oracle");
		return EXIT_FAILURE;
	}
	printf("C2.4 independent lifecycle and real-race oracle: PASS\n");
	return EXIT_SUCCESS;
}
