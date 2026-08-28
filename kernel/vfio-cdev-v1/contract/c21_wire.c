// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: GPL-2.0-only

#include "c21_wire.h"

C21_STATIC_ASSERT(sizeof(struct fwlab_c21_wire_record) == 64,
		  "A-prime record must remain exactly 64 bytes");
C21_STATIC_ASSERT(FWLAB_C21_REQ_MAGIC == 0x00 &&
		  FWLAB_C21_REQ_ABI_MAJOR == 0x04 &&
		  FWLAB_C21_REQ_ABI_MINOR == 0x06 &&
		  FWLAB_C21_REQ_STRUCT_SIZE == 0x08 &&
		  FWLAB_C21_REQ_OPERATION == 0x0a &&
		  FWLAB_C21_REQ_FLAGS == 0x0c &&
		  FWLAB_C21_REQ_SEQUENCE == 0x10 &&
		  FWLAB_C21_REQ_EXPECTED_GENERATION == 0x18 &&
		  FWLAB_C21_REQ_IOVA == 0x20 &&
		  FWLAB_C21_REQ_LENGTH == 0x28 &&
		  FWLAB_C21_REQ_RESERVED0 == 0x2c &&
		  FWLAB_C21_REQ_RESERVED1 == 0x30 &&
		  FWLAB_C21_REQ_RESERVED2 == 0x38 &&
		  FWLAB_C21_REQ_RESERVED2 + 8 == FWLAB_C21_RECORD_SIZE,
		  "A-prime request offsets changed");
C21_STATIC_ASSERT(FWLAB_C21_RES_MAGIC == 0x00 &&
		  FWLAB_C21_RES_ABI_MAJOR == 0x04 &&
		  FWLAB_C21_RES_ABI_MINOR == 0x06 &&
		  FWLAB_C21_RES_STRUCT_SIZE == 0x08 &&
		  FWLAB_C21_RES_OPERATION == 0x0a &&
		  FWLAB_C21_RES_FLAGS == 0x0c &&
		  FWLAB_C21_RES_SEQUENCE == 0x10 &&
		  FWLAB_C21_RES_GENERATION == 0x18 &&
		  FWLAB_C21_RES_IOVA == 0x20 &&
		  FWLAB_C21_RES_REQUESTED_LENGTH == 0x28 &&
		  FWLAB_C21_RES_OP_ERRNO == 0x2c &&
		  FWLAB_C21_RES_RESERVED1 == 0x30 &&
		  FWLAB_C21_RES_RESERVED2 == 0x38 &&
		  FWLAB_C21_RES_RESERVED2 + 8 == FWLAB_C21_RECORD_SIZE,
		  "A-prime result offsets changed");
C21_STATIC_ASSERT(FWLAB_C21_ST_MAGIC == 0x00 &&
		  FWLAB_C21_ST_ABI_MAJOR == 0x04 &&
		  FWLAB_C21_ST_ABI_MINOR == 0x06 &&
		  FWLAB_C21_ST_STRUCT_SIZE == 0x08 &&
		  FWLAB_C21_ST_DEVICE_STATE == 0x0a &&
		  FWLAB_C21_ST_FLAGS == 0x0c &&
		  FWLAB_C21_ST_GENERATION == 0x10 &&
		  FWLAB_C21_ST_LAST_SEQUENCE == 0x18 &&
		  FWLAB_C21_ST_NEXT_SEQUENCE == 0x20 &&
		  FWLAB_C21_ST_MAX_COPY_LENGTH == 0x28 &&
		  FWLAB_C21_ST_DATA_REGION_SIZE == 0x2c &&
		  FWLAB_C21_ST_RESERVED1 == 0x30 &&
		  FWLAB_C21_ST_RESERVED2 == 0x38 &&
		  FWLAB_C21_ST_RESERVED2 + 8 == FWLAB_C21_RECORD_SIZE,
		  "A-prime state offsets changed");
C21_STATIC_ASSERT(FWLAB_C21_SUBMIT_OFFSET == 0x000 &&
		  FWLAB_C21_RESULT_OFFSET == 0x040 &&
		  FWLAB_C21_STATE_OFFSET == 0x080,
		  "A-prime control offsets changed");

c21_u16 fwlab_c21_get_le16(const unsigned char *source)
{
	return (c21_u16)source[0] | ((c21_u16)source[1] << 8);
}

c21_u32 fwlab_c21_get_le32(const unsigned char *source)
{
	return (c21_u32)source[0] | ((c21_u32)source[1] << 8) |
	       ((c21_u32)source[2] << 16) | ((c21_u32)source[3] << 24);
}

c21_u64 fwlab_c21_get_le64(const unsigned char *source)
{
	return (c21_u64)fwlab_c21_get_le32(source) |
	       ((c21_u64)fwlab_c21_get_le32(source + 4) << 32);
}

void fwlab_c21_put_le16(unsigned char *destination, c21_u16 value)
{
	destination[0] = (unsigned char)value;
	destination[1] = (unsigned char)(value >> 8);
}

void fwlab_c21_put_le32(unsigned char *destination, c21_u32 value)
{
	destination[0] = (unsigned char)value;
	destination[1] = (unsigned char)(value >> 8);
	destination[2] = (unsigned char)(value >> 16);
	destination[3] = (unsigned char)(value >> 24);
}

void fwlab_c21_put_le64(unsigned char *destination, c21_u64 value)
{
	fwlab_c21_put_le32(destination, (c21_u32)value);
	fwlab_c21_put_le32(destination + 4, (c21_u32)(value >> 32));
}

static bool fwlab_c21_request_range_valid(c21_u64 iova, c21_u32 length)
{
	c21_u64 page_offset;

	if (!iova || !length || length > FWLAB_C21_MAX_COPY_LENGTH)
		return false;
	if (iova > C21_U64_MAX - (length - 1U))
		return false;
	page_offset = iova & (FWLAB_C21_IOAS_PAGE_SIZE - 1U);
	return page_offset + length <= FWLAB_C21_IOAS_PAGE_SIZE;
}

static int fwlab_c21_decode_errno(c21_u32 bits, c21_s32 *value)
{
	c21_u32 magnitude;

	if (!bits) {
		*value = 0;
		return 0;
	}
	magnitude = (~bits) + 1U;
	if (!magnitude || magnitude > FWLAB_C21_MAX_ERRNO)
		return -EPROTO;
	*value = -(c21_s32)magnitude;
	return 0;
}

int fwlab_c21_decode_request(const unsigned char *wire, size_t wire_size,
			     struct fwlab_c21_request *request)
{
	if (!wire || !request)
		return -EFAULT;
	if (wire_size != FWLAB_C21_RECORD_SIZE)
		return -EMSGSIZE;
	if (fwlab_c21_get_le32(wire + FWLAB_C21_REQ_MAGIC) !=
		    FWLAB_C21_REQUEST_MAGIC ||
	    fwlab_c21_get_le16(wire + FWLAB_C21_REQ_ABI_MAJOR) !=
		    FWLAB_C21_A1_ABI_MAJOR ||
	    fwlab_c21_get_le16(wire + FWLAB_C21_REQ_ABI_MINOR) !=
		    FWLAB_C21_A1_ABI_MINOR ||
	    fwlab_c21_get_le16(wire + FWLAB_C21_REQ_STRUCT_SIZE) !=
		    FWLAB_C21_RECORD_SIZE)
		return -EPROTO;

	request->operation =
		fwlab_c21_get_le16(wire + FWLAB_C21_REQ_OPERATION);
	request->flags = fwlab_c21_get_le32(wire + FWLAB_C21_REQ_FLAGS);
	request->sequence = fwlab_c21_get_le64(wire + FWLAB_C21_REQ_SEQUENCE);
	request->expected_generation =
		fwlab_c21_get_le64(wire + FWLAB_C21_REQ_EXPECTED_GENERATION);
	request->iova = fwlab_c21_get_le64(wire + FWLAB_C21_REQ_IOVA);
	request->length = fwlab_c21_get_le32(wire + FWLAB_C21_REQ_LENGTH);

	if ((request->operation != FWLAB_C21_OP_COPY_IOAS_TO_BUFFER &&
	     request->operation != FWLAB_C21_OP_COPY_BUFFER_TO_IOAS) ||
	    request->flags ||
	    fwlab_c21_get_le32(wire + FWLAB_C21_REQ_RESERVED0) ||
	    fwlab_c21_get_le64(wire + FWLAB_C21_REQ_RESERVED1) ||
	    fwlab_c21_get_le64(wire + FWLAB_C21_REQ_RESERVED2))
		return -EPROTO;
	if (!request->sequence || !request->expected_generation ||
	    !fwlab_c21_request_range_valid(request->iova, request->length))
		return -ERANGE;
	return 0;
}

void fwlab_c21_encode_request(unsigned char *wire,
			      const struct fwlab_c21_request *request)
{
	memset(wire, 0, FWLAB_C21_RECORD_SIZE);
	fwlab_c21_put_le32(wire + FWLAB_C21_REQ_MAGIC,
			     FWLAB_C21_REQUEST_MAGIC);
	fwlab_c21_put_le16(wire + FWLAB_C21_REQ_ABI_MAJOR,
			     FWLAB_C21_A1_ABI_MAJOR);
	fwlab_c21_put_le16(wire + FWLAB_C21_REQ_ABI_MINOR,
			     FWLAB_C21_A1_ABI_MINOR);
	fwlab_c21_put_le16(wire + FWLAB_C21_REQ_STRUCT_SIZE,
			     FWLAB_C21_RECORD_SIZE);
	fwlab_c21_put_le16(wire + FWLAB_C21_REQ_OPERATION,
			     request->operation);
	fwlab_c21_put_le32(wire + FWLAB_C21_REQ_FLAGS, request->flags);
	fwlab_c21_put_le64(wire + FWLAB_C21_REQ_SEQUENCE, request->sequence);
	fwlab_c21_put_le64(wire + FWLAB_C21_REQ_EXPECTED_GENERATION,
			     request->expected_generation);
	fwlab_c21_put_le64(wire + FWLAB_C21_REQ_IOVA, request->iova);
	fwlab_c21_put_le32(wire + FWLAB_C21_REQ_LENGTH, request->length);
}

int fwlab_c21_decode_result(const unsigned char *wire, size_t wire_size,
			    struct fwlab_c21_result *result)
{
	c21_u32 errno_bits;

	if (!wire || !result)
		return -EFAULT;
	if (wire_size != FWLAB_C21_RECORD_SIZE)
		return -EMSGSIZE;
	if (fwlab_c21_get_le32(wire + FWLAB_C21_RES_MAGIC) !=
		    FWLAB_C21_RESULT_MAGIC ||
	    fwlab_c21_get_le16(wire + FWLAB_C21_RES_ABI_MAJOR) !=
		    FWLAB_C21_A1_ABI_MAJOR ||
	    fwlab_c21_get_le16(wire + FWLAB_C21_RES_ABI_MINOR) !=
		    FWLAB_C21_A1_ABI_MINOR ||
	    fwlab_c21_get_le16(wire + FWLAB_C21_RES_STRUCT_SIZE) !=
		    FWLAB_C21_RECORD_SIZE ||
	    fwlab_c21_get_le64(wire + FWLAB_C21_RES_RESERVED1) ||
	    fwlab_c21_get_le64(wire + FWLAB_C21_RES_RESERVED2))
		return -EPROTO;

	result->operation =
		fwlab_c21_get_le16(wire + FWLAB_C21_RES_OPERATION);
	result->flags = fwlab_c21_get_le32(wire + FWLAB_C21_RES_FLAGS);
	result->sequence = fwlab_c21_get_le64(wire + FWLAB_C21_RES_SEQUENCE);
	result->generation =
		fwlab_c21_get_le64(wire + FWLAB_C21_RES_GENERATION);
	result->iova = fwlab_c21_get_le64(wire + FWLAB_C21_RES_IOVA);
	result->requested_length =
		fwlab_c21_get_le32(wire + FWLAB_C21_RES_REQUESTED_LENGTH);
	errno_bits = fwlab_c21_get_le32(wire + FWLAB_C21_RES_OP_ERRNO);
	if (fwlab_c21_decode_errno(errno_bits, &result->op_errno))
		return -EPROTO;
	if ((result->flags & ~FWLAB_C21_RES_F_ALL) ||
	    !(result->flags & FWLAB_C21_RES_F_VALID) ||
	    (result->operation != FWLAB_C21_OP_COPY_IOAS_TO_BUFFER &&
	     result->operation != FWLAB_C21_OP_COPY_BUFFER_TO_IOAS) ||
	    !result->sequence || !result->generation ||
	    !fwlab_c21_request_range_valid(result->iova,
					   result->requested_length) ||
	    (!!(result->flags & FWLAB_C21_RES_F_DEST_MAY_HAVE_PARTIAL) !=
	     (result->operation == FWLAB_C21_OP_COPY_BUFFER_TO_IOAS &&
	      result->op_errno != 0)))
		return -EPROTO;
	return 0;
}

void fwlab_c21_encode_result(unsigned char *wire,
			     const struct fwlab_c21_result *result)
{
	memset(wire, 0, FWLAB_C21_RECORD_SIZE);
	fwlab_c21_put_le32(wire + FWLAB_C21_RES_MAGIC,
			     FWLAB_C21_RESULT_MAGIC);
	fwlab_c21_put_le16(wire + FWLAB_C21_RES_ABI_MAJOR,
			     FWLAB_C21_A1_ABI_MAJOR);
	fwlab_c21_put_le16(wire + FWLAB_C21_RES_ABI_MINOR,
			     FWLAB_C21_A1_ABI_MINOR);
	fwlab_c21_put_le16(wire + FWLAB_C21_RES_STRUCT_SIZE,
			     FWLAB_C21_RECORD_SIZE);
	fwlab_c21_put_le16(wire + FWLAB_C21_RES_OPERATION,
			     result->operation);
	fwlab_c21_put_le32(wire + FWLAB_C21_RES_FLAGS, result->flags);
	fwlab_c21_put_le64(wire + FWLAB_C21_RES_SEQUENCE, result->sequence);
	fwlab_c21_put_le64(wire + FWLAB_C21_RES_GENERATION,
			     result->generation);
	fwlab_c21_put_le64(wire + FWLAB_C21_RES_IOVA, result->iova);
	fwlab_c21_put_le32(wire + FWLAB_C21_RES_REQUESTED_LENGTH,
			     result->requested_length);
	fwlab_c21_put_le32(wire + FWLAB_C21_RES_OP_ERRNO,
			     (c21_u32)result->op_errno);
}

int fwlab_c21_decode_state(const unsigned char *wire, size_t wire_size,
			   struct fwlab_c21_state_snapshot *state)
{
	if (!wire || !state)
		return -EFAULT;
	if (wire_size != FWLAB_C21_RECORD_SIZE)
		return -EMSGSIZE;
	if (fwlab_c21_get_le32(wire + FWLAB_C21_ST_MAGIC) !=
		    FWLAB_C21_STATE_MAGIC ||
	    fwlab_c21_get_le16(wire + FWLAB_C21_ST_ABI_MAJOR) !=
		    FWLAB_C21_A1_ABI_MAJOR ||
	    fwlab_c21_get_le16(wire + FWLAB_C21_ST_ABI_MINOR) !=
		    FWLAB_C21_A1_ABI_MINOR ||
	    fwlab_c21_get_le16(wire + FWLAB_C21_ST_STRUCT_SIZE) !=
		    FWLAB_C21_RECORD_SIZE ||
	    fwlab_c21_get_le64(wire + FWLAB_C21_ST_RESERVED1) ||
	    fwlab_c21_get_le64(wire + FWLAB_C21_ST_RESERVED2))
		return -EPROTO;

	state->device_state =
		fwlab_c21_get_le16(wire + FWLAB_C21_ST_DEVICE_STATE);
	state->flags = fwlab_c21_get_le32(wire + FWLAB_C21_ST_FLAGS);
	state->generation =
		fwlab_c21_get_le64(wire + FWLAB_C21_ST_GENERATION);
	state->last_sequence =
		fwlab_c21_get_le64(wire + FWLAB_C21_ST_LAST_SEQUENCE);
	state->next_sequence =
		fwlab_c21_get_le64(wire + FWLAB_C21_ST_NEXT_SEQUENCE);
	state->max_copy_length =
		fwlab_c21_get_le32(wire + FWLAB_C21_ST_MAX_COPY_LENGTH);
	state->data_region_size =
		fwlab_c21_get_le32(wire + FWLAB_C21_ST_DATA_REGION_SIZE);
	if (state->device_state > FWLAB_C21_STATE_DEAD ||
	    (state->flags & ~FWLAB_C21_ST_F_ALL) ||
	    state->max_copy_length != FWLAB_C21_MAX_COPY_LENGTH ||
	    state->data_region_size != FWLAB_C21_DATA_REGION_SIZE)
		return -EPROTO;
	if (state->device_state == FWLAB_C21_STATE_OPEN_UNATTACHED &&
	    ((state->flags & (FWLAB_C21_ST_F_OPEN |
			      FWLAB_C21_ST_F_ATTACHED)) !=
	     FWLAB_C21_ST_F_OPEN))
		return -EPROTO;
	if (state->device_state == FWLAB_C21_STATE_OPEN_ATTACHED &&
	    ((state->flags & (FWLAB_C21_ST_F_OPEN |
			      FWLAB_C21_ST_F_ATTACHED)) !=
	     (FWLAB_C21_ST_F_OPEN | FWLAB_C21_ST_F_ATTACHED)))
		return -EPROTO;
	if ((state->device_state == FWLAB_C21_STATE_CLOSED ||
	     state->device_state == FWLAB_C21_STATE_CLOSING) &&
	    (state->flags & (FWLAB_C21_ST_F_OPEN |
			     FWLAB_C21_ST_F_ATTACHED |
			     FWLAB_C21_ST_F_RESULT_VALID |
			     FWLAB_C21_ST_F_SEQUENCE_EXHAUSTED |
			     FWLAB_C21_ST_F_DEAD)))
		return -EPROTO;
	if (state->device_state == FWLAB_C21_STATE_DEAD &&
	    ((state->flags & FWLAB_C21_ST_F_ALL) != FWLAB_C21_ST_F_DEAD ||
	     state->generation != C21_U64_MAX))
		return -EPROTO;
	if (state->device_state != FWLAB_C21_STATE_DEAD &&
	    (state->flags & FWLAB_C21_ST_F_DEAD))
		return -EPROTO;
	if (state->flags & FWLAB_C21_ST_F_SEQUENCE_EXHAUSTED) {
		if (state->last_sequence != C21_U64_MAX || state->next_sequence)
			return -EPROTO;
	} else if (state->last_sequence == C21_U64_MAX ||
		   state->next_sequence != state->last_sequence + 1U) {
		return -EPROTO;
	}
	if ((state->flags & (FWLAB_C21_ST_F_RESULT_VALID |
			     FWLAB_C21_ST_F_SEQUENCE_EXHAUSTED)) &&
	    !(state->flags & FWLAB_C21_ST_F_OPEN))
		return -EPROTO;
	return 0;
}

void fwlab_c21_encode_state(unsigned char *wire,
			    const struct fwlab_c21_state_snapshot *state)
{
	memset(wire, 0, FWLAB_C21_RECORD_SIZE);
	fwlab_c21_put_le32(wire + FWLAB_C21_ST_MAGIC, FWLAB_C21_STATE_MAGIC);
	fwlab_c21_put_le16(wire + FWLAB_C21_ST_ABI_MAJOR,
			     FWLAB_C21_A1_ABI_MAJOR);
	fwlab_c21_put_le16(wire + FWLAB_C21_ST_ABI_MINOR,
			     FWLAB_C21_A1_ABI_MINOR);
	fwlab_c21_put_le16(wire + FWLAB_C21_ST_STRUCT_SIZE,
			     FWLAB_C21_RECORD_SIZE);
	fwlab_c21_put_le16(wire + FWLAB_C21_ST_DEVICE_STATE,
			     state->device_state);
	fwlab_c21_put_le32(wire + FWLAB_C21_ST_FLAGS, state->flags);
	fwlab_c21_put_le64(wire + FWLAB_C21_ST_GENERATION,
			     state->generation);
	fwlab_c21_put_le64(wire + FWLAB_C21_ST_LAST_SEQUENCE,
			     state->last_sequence);
	fwlab_c21_put_le64(wire + FWLAB_C21_ST_NEXT_SEQUENCE,
			     state->next_sequence);
	fwlab_c21_put_le32(wire + FWLAB_C21_ST_MAX_COPY_LENGTH,
			     state->max_copy_length);
	fwlab_c21_put_le32(wire + FWLAB_C21_ST_DATA_REGION_SIZE,
			     state->data_region_size);
}
