// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: BSD-3-Clause

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include "c25_session.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/iommufd.h>
#include <linux/vfio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

_Static_assert(C25_PAGE_SIZE == FWLAB_C21_IOAS_PAGE_SIZE,
	       "C2.5 requires the frozen 4 KiB IOAS page envelope");
_Static_assert(C25_COPY_LENGTH == FWLAB_C21_MAX_COPY_LENGTH,
	       "C2.5 must exercise the full frozen copy window");
_Static_assert(FWLAB_C21_RECORD_SIZE == 64U,
	       "C2.5 requires the exact C2.1 record size");
_Static_assert(FWLAB_C21_REQ_RESERVED2 + 8U == FWLAB_C21_RECORD_SIZE,
	       "request offsets no longer describe one complete record");
_Static_assert(FWLAB_C21_RES_RESERVED2 + 8U == FWLAB_C21_RECORD_SIZE,
	       "result offsets no longer describe one complete record");
_Static_assert(FWLAB_C21_ST_RESERVED2 + 8U == FWLAB_C21_RECORD_SIZE,
	       "state offsets no longer describe one complete record");

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

static int decode_state(const unsigned char wire[FWLAB_C21_RECORD_SIZE],
			struct c25_state *state)
{
	uint16_t device_state;
	uint32_t flags;
	uint64_t last_sequence;
	uint64_t next_sequence;

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
	     (last_sequence == UINT64_MAX ||
	      next_sequence != last_sequence + 1U)) ||
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
			 struct c25_result *result)
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
	    result->iova > UINT64_MAX - (result->requested_length - 1U) ||
	    (result->iova & (FWLAB_C21_IOAS_PAGE_SIZE - 1U)) >
		    FWLAB_C21_IOAS_PAGE_SIZE - result->requested_length ||
	    result->op_errno > 0 || result->op_errno < -4095 ||
	    (!!(result->flags & FWLAB_C21_RES_F_DEST_MAY_HAVE_PARTIAL) !=
	     (result->operation == FWLAB_C21_OP_COPY_BUFFER_TO_IOAS &&
	      result->op_errno != 0)))
		return -EPROTO;
	return 0;
}

static int validate_region_layout(const struct c25_region regions[2])
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

static int checked_span(const struct c25_region *region, uint64_t relative,
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

static int checked_record_position(const struct c25_region *region,
				   uint64_t relative, off_t *position)
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

static int pwrite_exact(int fd, const void *buffer, size_t length,
			off_t offset)
{
	ssize_t done;

	do {
		done = pwrite(fd, buffer, length, offset);
	} while (done < 0 && errno == EINTR);
	if (done < 0)
		return -errno;
	return done == (ssize_t)length ? 0 : -EIO;
}

int c25_read_state(struct c25_session *session,
		   unsigned char wire[FWLAB_C21_RECORD_SIZE],
		   struct c25_state *state)
{
	off_t position;
	int ret;

	ret = checked_record_position(&session->control_region,
				      FWLAB_C21_STATE_OFFSET, &position);
	if (ret)
		return ret;
	ret = pread_exact(session->device_fd, wire, FWLAB_C21_RECORD_SIZE,
			  position);
	if (ret)
		return ret;
	return decode_state(wire, state);
}

int c25_read_result(struct c25_session *session,
		    unsigned char wire[FWLAB_C21_RECORD_SIZE],
		    struct c25_result *result)
{
	off_t position;
	int ret;

	ret = checked_record_position(&session->control_region,
				      FWLAB_C21_RESULT_OFFSET, &position);
	if (ret)
		return ret;
	ret = pread_exact(session->device_fd, wire, FWLAB_C21_RECORD_SIZE,
			  position);
	if (ret)
		return ret;
	return decode_result(wire, result);
}

static int discover_regions(struct c25_session *session)
{
	struct vfio_device_info device_info = {
		.argsz = sizeof(device_info),
	};
	struct c25_region regions[2];
	unsigned char wire[FWLAB_C21_RECORD_SIZE];
	struct c25_state state;
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

		if (ioctl(session->device_fd, VFIO_DEVICE_GET_REGION_INFO,
			  &info))
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

		if (checked_record_position(&regions[index],
					    FWLAB_C21_STATE_OFFSET,
					    &position))
			return -EPROTO;
		do {
			done = pread(session->device_fd, wire, sizeof(wire),
				     position);
		} while (done < 0 && errno == EINTR);
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

int c25_read_data(struct c25_session *session, unsigned char *buffer,
		  size_t length)
{
	off_t position;
	int ret;

	ret = checked_span(&session->data_region, 0, length, &position);
	if (ret)
		return ret;
	return pread_exact(session->device_fd, buffer, length, position);
}

int c25_write_data(struct c25_session *session, const unsigned char *buffer,
		   size_t length)
{
	off_t position;
	int ret;

	ret = checked_span(&session->data_region, 0, length, &position);
	if (ret)
		return ret;
	return pwrite_exact(session->device_fd, buffer, length, position);
}

int c25_owner_open(struct c25_owner *owner)
{
	owner->iommu_fd = open("/dev/iommu", O_RDWR | O_CLOEXEC);
	return owner->iommu_fd < 0 ? -errno : 0;
}

int c25_owner_close(struct c25_owner *owner)
{
	int ret = 0;

	if (owner->iommu_fd >= 0 && close(owner->iommu_fd))
		ret = -errno;
	owner->iommu_fd = -1;
	return ret;
}

int c25_session_begin(struct c25_session *session, struct c25_owner *owner,
		      const char *label, const char *device_path,
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
	session->label = label;
	session->owner = owner;
	session->device_fd = -1;
	session->iova = iova;
	if (!owner || owner->iommu_fd < 0)
		return -EINVAL;
	page_size = sysconf(_SC_PAGESIZE);
	if (page_size != C25_PAGE_SIZE)
		return -EPROTO;
	session->device_fd = open(device_path, O_RDWR | O_CLOEXEC);
	if (session->device_fd < 0)
		return -errno;
	bind.iommufd = owner->iommu_fd;
	if (ioctl(session->device_fd, VFIO_DEVICE_BIND_IOMMUFD, &bind))
		return -errno;
	ret = discover_regions(session);
	if (ret)
		return ret;
	if (ioctl(owner->iommu_fd, IOMMU_IOAS_ALLOC, &ioas))
		return -errno;
	session->ioas_created = true;
	session->ioas_id = ioas.out_ioas_id;
	session->page = mmap(NULL, C25_PAGE_SIZE, PROT_READ | PROT_WRITE,
			     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (session->page == MAP_FAILED) {
		session->page = NULL;
		return -errno;
	}
	return 0;
}

int c25_session_map(struct c25_session *session)
{
	struct iommu_ioas_map map = {
		.size = sizeof(map),
		.flags = IOMMU_IOAS_MAP_FIXED_IOVA |
			 IOMMU_IOAS_MAP_READABLE | IOMMU_IOAS_MAP_WRITEABLE,
		.ioas_id = session->ioas_id,
		.user_va = (uintptr_t)session->page,
		.length = C25_PAGE_SIZE,
		.iova = session->iova,
	};

	if (session->mapped || !session->ioas_created || !session->page)
		return -EINVAL;
	if (ioctl(session->owner->iommu_fd, IOMMU_IOAS_MAP, &map))
		return -errno;
	if (map.iova != session->iova || map.length != C25_PAGE_SIZE)
		return -EPROTO;
	session->mapped = true;
	return 0;
}

int c25_session_unmap(struct c25_session *session)
{
	struct iommu_ioas_unmap unmap = {
		.size = sizeof(unmap),
		.ioas_id = session->ioas_id,
		.iova = session->iova,
		.length = C25_PAGE_SIZE,
	};

	if (!session->mapped)
		return 0;
	if (ioctl(session->owner->iommu_fd, IOMMU_IOAS_UNMAP, &unmap))
		return -errno;
	session->mapped = false;
	return unmap.length == C25_PAGE_SIZE ? 0 : -EPROTO;
}

int c25_session_attach(struct c25_session *session)
{
	struct vfio_device_attach_iommufd_pt attach = {
		.argsz = sizeof(attach),
		.pt_id = session->ioas_id,
	};

	if (session->attached || session->device_fd < 0)
		return -EINVAL;
	if (ioctl(session->device_fd, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &attach))
		return -errno;
	session->attached = true;
	return 0;
}

int c25_session_detach(struct c25_session *session)
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

int c25_session_destroy_ioas(struct c25_session *session)
{
	struct iommu_destroy destroy = {
		.size = sizeof(destroy),
		.id = session->ioas_id,
	};

	if (!session->ioas_created)
		return 0;
	if (session->mapped || session->attached)
		return -EBUSY;
	if (ioctl(session->owner->iommu_fd, IOMMU_DESTROY, &destroy))
		return -errno;
	session->ioas_created = false;
	return 0;
}

int c25_session_close_device(struct c25_session *session)
{
	int ret;

	if (session->device_fd < 0)
		return 0;
	ret = close(session->device_fd);
	session->device_fd = -1;
	session->attached = false;
	return ret ? -errno : 0;
}

int c25_session_reset(struct c25_session *session)
{
	if (ioctl(session->device_fd, VFIO_DEVICE_RESET))
		return -errno;
	return 0;
}

int c25_session_cleanup(struct c25_session *session)
{
	int first_error = 0;
	int ret;

	if (session->attached && session->device_fd >= 0) {
		ret = c25_session_detach(session);
		if (ret) {
			first_error = ret;
			ret = c25_session_close_device(session);
			if (!first_error && ret)
				first_error = ret;
		}
	}
	ret = c25_session_unmap(session);
	if (!first_error && ret)
		first_error = ret;
	ret = c25_session_destroy_ioas(session);
	if (!first_error && ret)
		first_error = ret;
	ret = c25_session_close_device(session);
	if (!first_error && ret)
		first_error = ret;
	if (session->page && munmap(session->page, C25_PAGE_SIZE) &&
	    !first_error)
		first_error = -errno;
	session->page = NULL;
	return first_error;
}

int c25_submit(struct c25_session *session, uint16_t operation,
	       uint64_t iova, uint32_t length, int32_t expected_errno)
{
	unsigned char request[FWLAB_C21_RECORD_SIZE];
	unsigned char result_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	struct c25_result result;
	struct c25_state before;
	struct c25_state after;
	off_t position;
	int ret;

	ret = c25_read_state(session, state_wire, &before);
	if (ret)
		return ret;
	if (before.device_state != FWLAB_C21_STATE_OPEN_ATTACHED ||
	    !(before.flags & FWLAB_C21_ST_F_ATTACHED) ||
	    !before.generation || !before.next_sequence)
		return -EPROTO;
	encode_request(request, operation, before.next_sequence,
		       before.generation, iova, length);
	ret = checked_record_position(&session->control_region,
				      FWLAB_C21_SUBMIT_OFFSET, &position);
	if (ret)
		return ret;
	ret = pwrite_exact(session->device_fd, request, sizeof(request),
			   position);
	if (ret)
		return ret;
	ret = c25_read_result(session, result_wire, &result);
	if (ret)
		return ret;
	if (result.operation != operation ||
	    result.sequence != before.next_sequence ||
	    result.generation != before.generation || result.iova != iova ||
	    result.requested_length != length ||
	    result.op_errno != expected_errno)
		return -EPROTO;
	ret = c25_read_state(session, state_wire, &after);
	if (ret)
		return ret;
	if (after.generation != before.generation ||
	    after.device_state != before.device_state ||
	    after.device_state != FWLAB_C21_STATE_OPEN_ATTACHED ||
	    !(after.flags & FWLAB_C21_ST_F_OPEN) ||
	    !(after.flags & FWLAB_C21_ST_F_ATTACHED) ||
	    after.last_sequence != before.next_sequence ||
	    after.next_sequence != before.next_sequence + 1U ||
	    !(after.flags & FWLAB_C21_ST_F_RESULT_VALID))
		return -EPROTO;
	return 0;
}

int c25_capture(struct c25_session *session,
		struct c25_observation *observation)
{
	struct c25_result result;
	struct c25_state state;
	int ret;

	memset(observation, 0, sizeof(*observation));
	ret = c25_read_state(session, observation->state_wire, &state);
	if (ret)
		return ret;
	ret = c25_read_data(session, observation->data,
			    sizeof(observation->data));
	if (ret)
		return ret;
	memcpy(observation->page, session->page, sizeof(observation->page));
	observation->result_valid =
		(state.flags & FWLAB_C21_ST_F_RESULT_VALID) != 0;
	if (!observation->result_valid)
		return 0;
	return c25_read_result(session, observation->result_wire, &result);
}

int c25_observation_equal(const struct c25_observation *left,
			  const struct c25_observation *right)
{
	return left->result_valid == right->result_valid &&
	       !memcmp(left->state_wire, right->state_wire,
		       sizeof(left->state_wire)) &&
	       !memcmp(left->result_wire, right->result_wire,
		       sizeof(left->result_wire)) &&
	       !memcmp(left->data, right->data, sizeof(left->data)) &&
	       !memcmp(left->page, right->page, sizeof(left->page));
}

void c25_fill_pattern(unsigned char *buffer, size_t length,
		      unsigned int seed)
{
	size_t index;

	for (index = 0; index < length; index++)
		buffer[index] = (unsigned char)(seed ^ (index * 29U) ^
						(index >> 1));
}

int c25_selftest(void)
{
	const uint32_t rw_flags = VFIO_REGION_INFO_FLAG_READ |
				  VFIO_REGION_INFO_FLAG_WRITE;
	struct c25_region regions[2] = {
		{ .index = 0, .flags = rw_flags, .offset = 0x1000,
		  .size = FWLAB_C21_CONTROL_REGION_SIZE },
		{ .index = 1, .flags = rw_flags, .offset = 0x2000,
		  .size = FWLAB_C21_CONTROL_REGION_SIZE },
	};
	struct c25_observation left;
	struct c25_observation right;
	unsigned char base_pattern[C25_PAGE_SIZE];
	unsigned char peer_pattern[C25_PAGE_SIZE];
	unsigned char request[FWLAB_C21_RECORD_SIZE];

	if (validate_region_layout(regions))
		return -1;
	regions[1].offset = 0x1800;
	if (!validate_region_layout(regions))
		return -1;
	regions[1].offset = 0x2000;
	regions[0].flags |= VFIO_REGION_INFO_FLAG_MMAP;
	if (!validate_region_layout(regions))
		return -1;
	encode_request(request, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 9, 7,
		       C25_SHARED_IOVA + 31U, C25_COPY_LENGTH);
	if (get_le32(request + FWLAB_C21_REQ_MAGIC) !=
		    FWLAB_C21_REQUEST_MAGIC ||
	    get_le16(request + FWLAB_C21_REQ_OPERATION) !=
		    FWLAB_C21_OP_COPY_IOAS_TO_BUFFER ||
	    get_le64(request + FWLAB_C21_REQ_SEQUENCE) != 9 ||
	    get_le64(request + FWLAB_C21_REQ_EXPECTED_GENERATION) != 7 ||
	    get_le64(request + FWLAB_C21_REQ_IOVA) !=
		    C25_SHARED_IOVA + 31U ||
	    get_le32(request + FWLAB_C21_REQ_LENGTH) != C25_COPY_LENGTH ||
	    get_le64(request + FWLAB_C21_REQ_RESERVED1) ||
	    get_le64(request + FWLAB_C21_REQ_RESERVED2))
		return -1;
	memset(&left, 0, sizeof(left));
	memset(left.state_wire, 0x5a, sizeof(left.state_wire));
	memset(left.result_wire, 0xa5, sizeof(left.result_wire));
	memset(left.data, 0x3c, sizeof(left.data));
	memset(left.page, 0xc3, sizeof(left.page));
	left.result_valid = true;
	right = left;
	if (!c25_observation_equal(&left, &right))
		return -1;
	right.data[17] ^= 1U;
	if (c25_observation_equal(&left, &right))
		return -1;
	right = left;
	right.page[3071] ^= 1U;
	if (c25_observation_equal(&left, &right))
		return -1;
	right = left;
	right.result_valid = !right.result_valid;
	if (c25_observation_equal(&left, &right))
		return -1;
	c25_fill_pattern(base_pattern, sizeof(base_pattern), 0x41U);
	c25_fill_pattern(peer_pattern, sizeof(peer_pattern), 0xb2U);
	if (!memcmp(base_pattern, peer_pattern, sizeof(base_pattern)))
		return -1;
	printf("C2.5 pure wire/layout/observation selftest: PASS\n");
	return 0;
}
