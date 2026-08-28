/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef FWLAB_C21_WIRE_H
#define FWLAB_C21_WIRE_H

#include "c21_compat.h"
#include "../uapi/unstable/fwlab_c21_a1.h"

struct fwlab_c21_request {
	c21_u16 operation;
	c21_u32 flags;
	c21_u64 sequence;
	c21_u64 expected_generation;
	c21_u64 iova;
	c21_u32 length;
};

struct fwlab_c21_result {
	c21_u16 operation;
	c21_u32 flags;
	c21_u64 sequence;
	c21_u64 generation;
	c21_u64 iova;
	c21_u32 requested_length;
	c21_s32 op_errno;
};

struct fwlab_c21_state_snapshot {
	c21_u16 device_state;
	c21_u32 flags;
	c21_u64 generation;
	c21_u64 last_sequence;
	c21_u64 next_sequence;
	c21_u32 max_copy_length;
	c21_u32 data_region_size;
};

c21_u16 fwlab_c21_get_le16(const unsigned char *source);
c21_u32 fwlab_c21_get_le32(const unsigned char *source);
c21_u64 fwlab_c21_get_le64(const unsigned char *source);
void fwlab_c21_put_le16(unsigned char *destination, c21_u16 value);
void fwlab_c21_put_le32(unsigned char *destination, c21_u32 value);
void fwlab_c21_put_le64(unsigned char *destination, c21_u64 value);

int fwlab_c21_decode_request(const unsigned char *wire, size_t wire_size,
			     struct fwlab_c21_request *request);
void fwlab_c21_encode_request(unsigned char *wire,
			      const struct fwlab_c21_request *request);
int fwlab_c21_decode_result(const unsigned char *wire, size_t wire_size,
			    struct fwlab_c21_result *result);
void fwlab_c21_encode_result(unsigned char *wire,
			     const struct fwlab_c21_result *result);
int fwlab_c21_decode_state(const unsigned char *wire, size_t wire_size,
			   struct fwlab_c21_state_snapshot *state);
void fwlab_c21_encode_state(unsigned char *wire,
			    const struct fwlab_c21_state_snapshot *state);

#endif /* FWLAB_C21_WIRE_H */
