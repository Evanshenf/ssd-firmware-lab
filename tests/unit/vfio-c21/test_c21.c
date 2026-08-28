// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: BSD-3-Clause

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c21_state.h"
#include "fake_copy.h"
#include "fake_transition.h"
#include "golden_vectors.h"
#include "pthread_lock.h"

#define TEST_BASE_IOVA 0x10000ULL

_Static_assert(sizeof(fwlab_c21_request_golden) == FWLAB_C21_RECORD_SIZE,
	       "request golden must be a complete record");
_Static_assert(sizeof(fwlab_c21_result_golden) == FWLAB_C21_RECORD_SIZE,
	       "result golden must be a complete record");
_Static_assert(sizeof(fwlab_c21_state_golden) == FWLAB_C21_RECORD_SIZE,
	       "state golden must be a complete record");

#define CHECK(condition)                                                       \
	do {                                                                     \
		if (!(condition)) {                                                \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __func__,    \
				__LINE__, #condition);                              \
			return -1;                                                   \
		}                                                                \
	} while (0)

#define CHECK_INT(actual, expected)                                            \
	do {                                                                     \
		int check_actual = (actual);                                      \
		int check_expected = (expected);                                  \
		if (check_actual != check_expected) {                             \
			fprintf(stderr, "%s:%d: got %d, expected %d\n",          \
				__func__, __LINE__, check_actual, check_expected);   \
			return -1;                                                   \
		}                                                                \
	} while (0)

struct test_fixture {
	struct fwlab_c21_test_lock lock;
	struct fwlab_c21_fake_copy fake;
	struct fwlab_c21_fake_transition transition;
	struct fwlab_c21_copy_provider provider;
	struct fwlab_c21_device device;
};

static int fixture_init_at(struct test_fixture *fixture, c21_u64 base_iova)
{
	int ret;

	memset(fixture, 0, sizeof(*fixture));
	ret = fwlab_c21_test_lock_init(&fixture->lock);
	if (ret)
		return ret;
	ret = fwlab_c21_fake_copy_init(&fixture->fake, base_iova);
	if (ret) {
		fwlab_c21_test_lock_destroy(&fixture->lock);
		return ret;
	}
	ret = fwlab_c21_fake_transition_init(&fixture->transition);
	if (ret) {
		fwlab_c21_fake_copy_destroy(&fixture->fake);
		fwlab_c21_test_lock_destroy(&fixture->lock);
		return ret;
	}
	fixture->provider = fwlab_c21_fake_copy_provider(&fixture->fake);
	ret = fwlab_c21_device_init(&fixture->device,
				    &fwlab_c21_test_lock_ops,
				    &fixture->lock, &fixture->provider);
	if (ret) {
		fwlab_c21_fake_transition_destroy(&fixture->transition);
		fwlab_c21_fake_copy_destroy(&fixture->fake);
		fwlab_c21_test_lock_destroy(&fixture->lock);
	}
	return ret;
}

static int fixture_init(struct test_fixture *fixture)
{
	return fixture_init_at(fixture, TEST_BASE_IOVA);
}

static void fixture_destroy(struct test_fixture *fixture)
{
	if (fixture->device.state == FWLAB_C21_STATE_OPEN_UNATTACHED ||
	    fixture->device.state == FWLAB_C21_STATE_OPEN_ATTACHED)
		(void)fwlab_c21_device_close(&fixture->device);
	fwlab_c21_fake_transition_destroy(&fixture->transition);
	fwlab_c21_fake_copy_destroy(&fixture->fake);
	fwlab_c21_test_lock_destroy(&fixture->lock);
}

static int fixture_open_attach(struct test_fixture *fixture)
{
	int ret;

	ret = fwlab_c21_device_open(&fixture->device);
	if (ret)
		return ret;
	return fwlab_c21_device_transition(
		&fixture->device, FWLAB_C21_TRANSITION_ATTACH,
		fwlab_c21_fake_transition_call, &fixture->transition);
}

static int fixture_transition(struct test_fixture *fixture,
			      enum fwlab_c21_transition transition)
{
	return fwlab_c21_device_transition(
		&fixture->device, transition, fwlab_c21_fake_transition_call,
		&fixture->transition);
}

static int invalid_positive_transition(
	void *context, enum fwlab_c21_transition transition)
{
	(void)context;
	(void)transition;
	return 1;
}

static int invalid_large_errno_transition(
	void *context, enum fwlab_c21_transition transition)
{
	(void)context;
	(void)transition;
	return -(int)(FWLAB_C21_MAX_ERRNO + 1U);
}

static void make_request(unsigned char wire[FWLAB_C21_RECORD_SIZE],
			 c21_u16 operation, c21_u64 sequence,
			 c21_u64 generation, c21_u64 iova, c21_u32 length)
{
	struct fwlab_c21_request request = {
		.operation = operation,
		.sequence = sequence,
		.expected_generation = generation,
		.iova = iova,
		.length = length,
	};

	fwlab_c21_encode_request(wire, &request);
}

static int read_state(struct test_fixture *fixture,
		      struct fwlab_c21_state_snapshot *state)
{
	unsigned char wire[FWLAB_C21_RECORD_SIZE];
	int ret;

	ret = fwlab_c21_control_read(&fixture->device, FWLAB_C21_STATE_OFFSET,
				     wire, sizeof(wire));
	if (ret != FWLAB_C21_RECORD_SIZE)
		return ret < 0 ? ret : -EPROTO;
	return fwlab_c21_decode_state(wire, sizeof(wire), state);
}

static int read_result(struct test_fixture *fixture,
		       struct fwlab_c21_result *result)
{
	unsigned char wire[FWLAB_C21_RECORD_SIZE];
	int ret;

	ret = fwlab_c21_control_read(&fixture->device, FWLAB_C21_RESULT_OFFSET,
				     wire, sizeof(wire));
	if (ret != FWLAB_C21_RECORD_SIZE)
		return ret < 0 ? ret : -EPROTO;
	return fwlab_c21_decode_result(wire, sizeof(wire), result);
}

static int test_wire_and_structural_rejection(void)
{
	struct fwlab_c21_request request = {
		.operation = FWLAB_C21_OP_COPY_BUFFER_TO_IOAS,
		.sequence = 0x0807060504030201ULL,
		.expected_generation = 0x1817161514131211ULL,
		.iova = 0x12080ULL,
		.length = 64,
	};
	struct fwlab_c21_request decoded;
	struct fwlab_c21_result golden_result = {
		.operation = FWLAB_C21_OP_COPY_BUFFER_TO_IOAS,
		.flags = FWLAB_C21_RES_F_VALID |
			 FWLAB_C21_RES_F_DEST_MAY_HAVE_PARTIAL,
		.sequence = 0x0807060504030201ULL,
		.generation = 0x1817161514131211ULL,
		.iova = 0x12080ULL,
		.requested_length = 64,
		.op_errno = -EIO,
	};
	struct fwlab_c21_result decoded_result;
	struct fwlab_c21_state_snapshot golden_state = {
		.device_state = FWLAB_C21_STATE_OPEN_ATTACHED,
		.flags = FWLAB_C21_ST_F_OPEN | FWLAB_C21_ST_F_ATTACHED |
			 FWLAB_C21_ST_F_RESULT_VALID,
		.generation = 0x1817161514131211ULL,
		.last_sequence = 1,
		.next_sequence = 2,
		.max_copy_length = FWLAB_C21_MAX_COPY_LENGTH,
		.data_region_size = FWLAB_C21_DATA_REGION_SIZE,
	};
	struct fwlab_c21_state_snapshot state;
	struct test_fixture fixture;
	unsigned char after[FWLAB_C21_RECORD_SIZE];
	unsigned char before[FWLAB_C21_RECORD_SIZE];
	unsigned char unaligned[FWLAB_C21_RECORD_SIZE + 1];
	unsigned char wire[FWLAB_C21_RECORD_SIZE];
	unsigned char valid[FWLAB_C21_RECORD_SIZE];
	unsigned char oversized[FWLAB_C21_RECORD_SIZE + 1];

	fwlab_c21_encode_request(wire, &request);
	CHECK(!memcmp(wire, fwlab_c21_request_golden, sizeof(wire)));
	memcpy(unaligned + 1, wire, sizeof(wire));
	CHECK_INT(fwlab_c21_decode_request(unaligned + 1, sizeof(wire),
					   &decoded),
		  0);
	CHECK(decoded.iova == request.iova && decoded.length == request.length);
	CHECK_INT(fwlab_c21_decode_request(wire, sizeof(wire) - 1, &decoded),
		  -EMSGSIZE);
	fwlab_c21_encode_result(wire, &golden_result);
	CHECK(!memcmp(wire, fwlab_c21_result_golden, sizeof(wire)));
	CHECK_INT(fwlab_c21_decode_result(fwlab_c21_result_golden,
					  sizeof(fwlab_c21_result_golden),
					  &decoded_result),
		  0);
	CHECK(decoded_result.sequence == golden_result.sequence &&
	      decoded_result.op_errno == -EIO);
	memcpy(wire, fwlab_c21_result_golden, sizeof(wire));
	fwlab_c21_put_le32(wire + FWLAB_C21_RES_OP_ERRNO, 1);
	CHECK_INT(fwlab_c21_decode_result(wire, sizeof(wire), &decoded_result),
		  -EPROTO);
	memcpy(wire, fwlab_c21_result_golden, sizeof(wire));
	fwlab_c21_put_le32(wire + FWLAB_C21_RES_OP_ERRNO,
			     (c21_u32)(-(int)(FWLAB_C21_MAX_ERRNO + 1U)));
	CHECK_INT(fwlab_c21_decode_result(wire, sizeof(wire), &decoded_result),
		  -EPROTO);
	memcpy(wire, fwlab_c21_result_golden, sizeof(wire));
	fwlab_c21_put_le32(wire + FWLAB_C21_RES_FLAGS,
			     FWLAB_C21_RES_F_VALID);
	CHECK_INT(fwlab_c21_decode_result(wire, sizeof(wire), &decoded_result),
		  -EPROTO);
	fwlab_c21_encode_state(wire, &golden_state);
	CHECK(!memcmp(wire, fwlab_c21_state_golden, sizeof(wire)));
	CHECK_INT(fwlab_c21_decode_state(fwlab_c21_state_golden,
					 sizeof(fwlab_c21_state_golden), &state),
		  0);
	CHECK(state.generation == golden_state.generation &&
	      state.flags == golden_state.flags);
	memcpy(wire, fwlab_c21_result_golden, sizeof(wire));
	fwlab_c21_put_le32(wire + FWLAB_C21_RES_OP_ERRNO, 0);
	CHECK_INT(fwlab_c21_decode_result(wire, sizeof(wire), &decoded_result),
		  -EPROTO);
	memcpy(wire, fwlab_c21_result_golden, sizeof(wire));
	fwlab_c21_put_le16(wire + FWLAB_C21_RES_OPERATION,
			     FWLAB_C21_OP_COPY_IOAS_TO_BUFFER);
	CHECK_INT(fwlab_c21_decode_result(wire, sizeof(wire), &decoded_result),
		  -EPROTO);
	memcpy(wire, fwlab_c21_result_golden, sizeof(wire));
	fwlab_c21_put_le32(wire + FWLAB_C21_RES_OP_ERRNO, 1);
	CHECK_INT(fwlab_c21_decode_result(wire, sizeof(wire), &decoded_result),
		  -EPROTO);
	memcpy(wire, fwlab_c21_state_golden, sizeof(wire));
	fwlab_c21_put_le32(wire + FWLAB_C21_ST_FLAGS,
			     FWLAB_C21_ST_F_ATTACHED);
	CHECK_INT(fwlab_c21_decode_state(wire, sizeof(wire), &state), -EPROTO);
	memcpy(wire, fwlab_c21_state_golden, sizeof(wire));
	fwlab_c21_put_le64(wire + FWLAB_C21_ST_NEXT_SEQUENCE, 3);
	CHECK_INT(fwlab_c21_decode_state(wire, sizeof(wire), &state), -EPROTO);
	memcpy(wire, fwlab_c21_state_golden, sizeof(wire));
	fwlab_c21_put_le32(wire + FWLAB_C21_ST_MAX_COPY_LENGTH, 255);
	CHECK_INT(fwlab_c21_decode_state(wire, sizeof(wire), &state), -EPROTO);

	CHECK_INT(fixture_init(&fixture), 0);
	CHECK_INT(fwlab_c21_device_open(&fixture.device), 0);
	CHECK_INT(fixture_transition(&fixture, FWLAB_C21_TRANSITION_ATTACH), 0);
	make_request(valid, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 1, 2,
		     TEST_BASE_IOVA, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET + 1, valid,
					  sizeof(valid)),
		  -EINVAL);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, valid,
					  sizeof(valid) - 1),
		  -EMSGSIZE);

#define EXPECT_BAD(mutate)                                                     \
	do {                                                                     \
		memcpy(wire, valid, sizeof(wire));                                 \
		mutate;                                                            \
		CHECK(fwlab_c21_control_write(                                     \
			      &fixture.device, FWLAB_C21_SUBMIT_OFFSET, wire,       \
			      sizeof(wire)) < 0);                                    \
	} while (0)

	EXPECT_BAD(wire[FWLAB_C21_REQ_MAGIC] ^= 0x1U);
	EXPECT_BAD(fwlab_c21_put_le16(wire + FWLAB_C21_REQ_ABI_MAJOR, 2));
	EXPECT_BAD(fwlab_c21_put_le16(wire + FWLAB_C21_REQ_ABI_MINOR, 1));
	EXPECT_BAD(fwlab_c21_put_le16(wire + FWLAB_C21_REQ_STRUCT_SIZE, 63));
	EXPECT_BAD(fwlab_c21_put_le16(wire + FWLAB_C21_REQ_OPERATION, 99));
	EXPECT_BAD(fwlab_c21_put_le32(wire + FWLAB_C21_REQ_FLAGS, 1));
	EXPECT_BAD(fwlab_c21_put_le32(wire + FWLAB_C21_REQ_RESERVED0, 1));
	EXPECT_BAD(fwlab_c21_put_le64(wire + FWLAB_C21_REQ_RESERVED1, 1));
	EXPECT_BAD(fwlab_c21_put_le64(wire + FWLAB_C21_REQ_RESERVED2, 1));
	EXPECT_BAD(fwlab_c21_put_le32(wire + FWLAB_C21_REQ_LENGTH, 0));
	EXPECT_BAD(fwlab_c21_put_le32(wire + FWLAB_C21_REQ_LENGTH, 257));
	EXPECT_BAD(fwlab_c21_put_le64(wire + FWLAB_C21_REQ_IOVA, 0));
	EXPECT_BAD(fwlab_c21_put_le64(wire + FWLAB_C21_REQ_IOVA,
				      C21_U64_MAX - 31));
	EXPECT_BAD(fwlab_c21_put_le64(wire + FWLAB_C21_REQ_IOVA,
				      TEST_BASE_IOVA + 4090));
#undef EXPECT_BAD

	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.last_sequence == 0 &&
	      !(state.flags & FWLAB_C21_ST_F_RESULT_VALID));

	/* Establish an immutable old result, then reject without consuming it. */
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, valid,
					  sizeof(valid)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_RESULT_OFFSET, before,
					 sizeof(before)),
		  FWLAB_C21_RECORD_SIZE);
	make_request(valid, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 2, 2,
		     TEST_BASE_IOVA, 64);

#define EXPECT_BAD_PRESERVES(mutate)                                           \
	do {                                                                     \
		memcpy(wire, valid, sizeof(wire));                                 \
		mutate;                                                            \
		CHECK(fwlab_c21_control_write(                                     \
			      &fixture.device, FWLAB_C21_SUBMIT_OFFSET, wire,       \
			      sizeof(wire)) < 0);                                    \
		CHECK_INT(fwlab_c21_control_read(                                  \
				  &fixture.device, FWLAB_C21_RESULT_OFFSET, after,   \
				  sizeof(after)),                                   \
			  FWLAB_C21_RECORD_SIZE);                                     \
		CHECK(!memcmp(before, after, sizeof(before)));                    \
	} while (0)

	EXPECT_BAD_PRESERVES(
		fwlab_c21_put_le16(wire + FWLAB_C21_REQ_ABI_MINOR, 1));
	EXPECT_BAD_PRESERVES(
		fwlab_c21_put_le64(wire + FWLAB_C21_REQ_RESERVED1, 1));
	EXPECT_BAD_PRESERVES(
		fwlab_c21_put_le64(wire + FWLAB_C21_REQ_RESERVED2, 1));
#undef EXPECT_BAD_PRESERVES

	memcpy(oversized, valid, sizeof(valid));
	oversized[sizeof(valid)] = 0;
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, oversized,
					  sizeof(oversized)),
		  -EMSGSIZE);
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_RESULT_OFFSET + 1, after,
					 sizeof(after)),
		  -EINVAL);
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_STATE_OFFSET + 1, after,
					 sizeof(after)),
		  -EINVAL);
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_RESULT_OFFSET, after,
					 sizeof(after) - 1),
		  -EMSGSIZE);
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_RESULT_OFFSET, oversized,
					 sizeof(oversized)),
		  -EMSGSIZE);
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_STATE_OFFSET, after,
					 sizeof(after) - 1),
		  -EMSGSIZE);
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_STATE_OFFSET, oversized,
					 sizeof(oversized)),
		  -EMSGSIZE);
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_RESULT_OFFSET, after,
					 sizeof(after)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK(!memcmp(before, after, sizeof(before)));
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.last_sequence == 1 && state.next_sequence == 2 &&
	      (state.flags & FWLAB_C21_ST_F_RESULT_VALID));
	fixture_destroy(&fixture);
	return 0;
}

static int test_lifecycle_sequence_and_result(void)
{
	struct fwlab_c21_state_snapshot state;
	struct fwlab_c21_result result;
	struct test_fixture fixture;
	unsigned char before[FWLAB_C21_RECORD_SIZE];
	unsigned char after[FWLAB_C21_RECORD_SIZE];
	unsigned char fake_page[FWLAB_C21_IOAS_PAGE_SIZE];
	unsigned char observed[64];
	unsigned char wire[FWLAB_C21_RECORD_SIZE];
	size_t index;

	CHECK_INT(fixture_init(&fixture), 0);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.device_state == FWLAB_C21_STATE_CLOSED &&
	      state.generation == 0 && state.next_sequence == 1);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 1, 1,
		     TEST_BASE_IOVA, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  -ESHUTDOWN);
	CHECK_INT(fwlab_c21_device_open(&fixture.device), 0);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 1 &&
	      state.device_state == FWLAB_C21_STATE_OPEN_UNATTACHED);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 1, 1,
		     TEST_BASE_IOVA, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  -ENOTCONN);
	CHECK_INT(fixture_transition(&fixture, FWLAB_C21_TRANSITION_ATTACH), 0);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 2 && state.next_sequence == 1 &&
	      (state.flags & FWLAB_C21_ST_F_ATTACHED));
	for (index = 0; index < sizeof(fake_page); index++)
		fake_page[index] = (unsigned char)(index ^ 0xa5U);
	CHECK_INT(fwlab_c21_fake_copy_write_page(&fixture.fake, 0, fake_page,
						 sizeof(fake_page)),
		  0);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 1, 2,
		     TEST_BASE_IOVA + 16, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK_INT(fwlab_c21_data_read(&fixture.device, 0, observed,
				      sizeof(observed)),
		  (int)sizeof(observed));
	CHECK(!memcmp(observed, fake_page + 16, sizeof(observed)));
	CHECK_INT(read_result(&fixture, &result), 0);
	CHECK(result.sequence == 1 && result.generation == 2 &&
	      result.op_errno == 0 && result.flags == FWLAB_C21_RES_F_VALID);
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_RESULT_OFFSET, before,
					 sizeof(before)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  -EALREADY);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 3, 2,
		     TEST_BASE_IOVA, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  -ERANGE);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 2, 1,
		     TEST_BASE_IOVA, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  -ESTALE);
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_RESULT_OFFSET, after,
					 sizeof(after)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK(!memcmp(before, after, sizeof(before)));

	fwlab_c21_fake_copy_set_mode(&fixture.fake,
				      FWLAB_C21_FAKE_FORCE_EACCES, 0);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 2, 2,
		     TEST_BASE_IOVA, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK_INT(read_result(&fixture, &result), 0);
	CHECK(result.sequence == 2 && result.op_errno == -EACCES);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.last_sequence == 2 && state.next_sequence == 3);

	CHECK_INT(fwlab_c21_device_reset(&fixture.device), 0);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 3 && state.last_sequence == 0 &&
	      state.device_state == FWLAB_C21_STATE_OPEN_ATTACHED);
	CHECK_INT(read_result(&fixture, &result), -ENODATA);
	memset(observed, 0xff, sizeof(observed));
	CHECK_INT(fwlab_c21_data_read(&fixture.device, 0, observed,
				      sizeof(observed)),
		  (int)sizeof(observed));
	for (index = 0; index < sizeof(observed); index++)
		CHECK(observed[index] == 0);
	CHECK_INT(fixture_transition(&fixture, FWLAB_C21_TRANSITION_REPLACE), 0);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 4);
	CHECK_INT(fixture_transition(&fixture, FWLAB_C21_TRANSITION_DETACH), 0);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 5 &&
	      state.device_state == FWLAB_C21_STATE_OPEN_UNATTACHED);
	CHECK_INT(fwlab_c21_device_close(&fixture.device), 0);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 6 &&
	      state.device_state == FWLAB_C21_STATE_CLOSED);
	CHECK_INT(fwlab_c21_device_open(&fixture.device), 0);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 7);
	CHECK_INT(fwlab_c21_device_close(&fixture.device), 0);
	fixture_destroy(&fixture);
	return 0;
}

static int test_copy_directions_and_partial_effects(void)
{
	struct fwlab_c21_result result;
	struct test_fixture fixture;
	unsigned char expected[64];
	unsigned char observed[64];
	unsigned char fake_page[FWLAB_C21_IOAS_PAGE_SIZE];
	unsigned char page_observed[128];
	unsigned char wire[FWLAB_C21_RECORD_SIZE];
	size_t index;

	CHECK_INT(fixture_init(&fixture), 0);
	CHECK_INT(fixture_open_attach(&fixture), 0);
	for (index = 0; index < sizeof(expected); index++)
		expected[index] = (unsigned char)(0x5aU ^ index);
	fwlab_c21_fake_copy_fill_page(&fixture.fake, 0x77);
	CHECK_INT(fwlab_c21_data_write(&fixture.device, 0, expected,
				       sizeof(expected)),
		  (int)sizeof(expected));
	make_request(wire, FWLAB_C21_OP_COPY_BUFFER_TO_IOAS, 1, 2,
		     TEST_BASE_IOVA + 32, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK_INT(fwlab_c21_fake_copy_read_page(&fixture.fake, 0, page_observed,
					       sizeof(page_observed)),
		  0);
	CHECK(page_observed[31] == 0x77 && page_observed[96] == 0x77 &&
	      !memcmp(page_observed + 32, expected, sizeof(expected)));

	for (index = 0; index < sizeof(fake_page); index++)
		fake_page[index] = (unsigned char)(index + 3U);
	CHECK_INT(fwlab_c21_fake_copy_write_page(&fixture.fake, 0, fake_page,
						 sizeof(fake_page)),
		  0);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 2, 2,
		     TEST_BASE_IOVA + 64, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK_INT(fwlab_c21_data_read(&fixture.device, 0, observed,
				      sizeof(observed)),
		  (int)sizeof(observed));
	CHECK(!memcmp(observed, fake_page + 64, sizeof(observed)));

	memset(expected, 0xcc, sizeof(expected));
	CHECK_INT(fwlab_c21_data_write(&fixture.device, 0, expected,
				       sizeof(expected)),
		  (int)sizeof(expected));
	fwlab_c21_fake_copy_fill_page(&fixture.fake, 0x11);
	fwlab_c21_fake_copy_set_mode(&fixture.fake,
				      FWLAB_C21_FAKE_PARTIAL_THEN_EIO, 8);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 3, 2,
		     TEST_BASE_IOVA, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	memset(observed, 0, sizeof(observed));
	CHECK_INT(fwlab_c21_data_read(&fixture.device, 0, observed,
				      sizeof(observed)),
		  (int)sizeof(observed));
	CHECK(!memcmp(observed, expected, sizeof(observed)));
	CHECK_INT(read_result(&fixture, &result), 0);
	CHECK(result.op_errno == -EIO &&
	      !(result.flags & FWLAB_C21_RES_F_DEST_MAY_HAVE_PARTIAL));

	for (index = 0; index < sizeof(expected); index++)
		expected[index] = (unsigned char)(0x80U + index);
	CHECK_INT(fwlab_c21_data_write(&fixture.device, 0, expected,
				       sizeof(expected)),
		  (int)sizeof(expected));
	fwlab_c21_fake_copy_fill_page(&fixture.fake, 0x77);
	make_request(wire, FWLAB_C21_OP_COPY_BUFFER_TO_IOAS, 4, 2,
		     TEST_BASE_IOVA, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK_INT(fwlab_c21_fake_copy_read_page(&fixture.fake, 0, page_observed,
					       sizeof(expected)),
		  0);
	CHECK(!memcmp(page_observed, expected, 8));
	for (index = 8; index < sizeof(expected); index++)
		CHECK(page_observed[index] == 0x77);
	CHECK_INT(read_result(&fixture, &result), 0);
	CHECK(result.op_errno == -EIO &&
	      (result.flags & FWLAB_C21_RES_F_DEST_MAY_HAVE_PARTIAL));

	fwlab_c21_fake_copy_set_mode(&fixture.fake, FWLAB_C21_FAKE_FORCE_HOLE,
				      0);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 5, 2,
		     TEST_BASE_IOVA, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK_INT(read_result(&fixture, &result), 0);
	CHECK(result.op_errno == -ENXIO);
	fwlab_c21_fake_copy_set_mode(&fixture.fake, FWLAB_C21_FAKE_SUCCESS, 0);
	fwlab_c21_fake_copy_set_permissions(&fixture.fake, false, true);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 6, 2,
		     TEST_BASE_IOVA, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK_INT(read_result(&fixture, &result), 0);
	CHECK(result.op_errno == -EACCES);
	fwlab_c21_fake_copy_set_permissions(&fixture.fake, true, false);
	make_request(wire, FWLAB_C21_OP_COPY_BUFFER_TO_IOAS, 7, 2,
		     TEST_BASE_IOVA, 64);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK_INT(read_result(&fixture, &result), 0);
	CHECK(result.op_errno == -EACCES &&
	      (result.flags & FWLAB_C21_RES_F_DEST_MAY_HAVE_PARTIAL));

	CHECK_INT(fwlab_c21_data_write(&fixture.device,
				       FWLAB_C21_DATA_REGION_SIZE, expected,
				       sizeof(expected)),
		  0);
	CHECK_INT(fwlab_c21_data_write(&fixture.device,
				       FWLAB_C21_DATA_REGION_SIZE - 8, expected,
				       sizeof(expected)),
		  8);
	fixture_destroy(&fixture);
	return 0;
}

static int test_counter_wrap_fail_closed(void)
{
	struct fwlab_c21_state_snapshot state;
	struct test_fixture fixture;
	unsigned char wire[FWLAB_C21_RECORD_SIZE];

	CHECK_INT(fixture_init(&fixture), 0);
	CHECK_INT(pthread_mutex_lock(&fixture.lock.mutex), 0);
	fixture.device.generation = C21_U64_MAX;
	CHECK_INT(pthread_mutex_unlock(&fixture.lock.mutex), 0);
	CHECK_INT(fwlab_c21_device_open(&fixture.device), -EOVERFLOW);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.device_state == FWLAB_C21_STATE_DEAD &&
	      (state.flags & FWLAB_C21_ST_F_DEAD));
	fixture_destroy(&fixture);

	CHECK_INT(fixture_init(&fixture), 0);
	CHECK_INT(fixture_open_attach(&fixture), 0);
	CHECK_INT(pthread_mutex_lock(&fixture.lock.mutex), 0);
	fixture.device.last_sequence = C21_U64_MAX - 1;
	CHECK_INT(pthread_mutex_unlock(&fixture.lock.mutex), 0);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, C21_U64_MAX, 2,
		     TEST_BASE_IOVA, 1);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.last_sequence == C21_U64_MAX && state.next_sequence == 0 &&
	      (state.flags & FWLAB_C21_ST_F_SEQUENCE_EXHAUSTED));
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  -EOVERFLOW);
	CHECK_INT(fwlab_c21_device_reset(&fixture.device), 0);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 3 && state.last_sequence == 0 &&
	      state.next_sequence == 1);
	fixture_destroy(&fixture);
	return 0;
}

struct submit_thread_args {
	struct fwlab_c21_device *device;
	unsigned char wire[FWLAB_C21_RECORD_SIZE];
	atomic_bool done;
	int ret;
};

struct action_thread_args {
	struct fwlab_c21_device *device;
	bool close;
	atomic_bool done;
	int ret;
};

struct transition_thread_args {
	struct fwlab_c21_device *device;
	struct fwlab_c21_fake_transition *fake;
	enum fwlab_c21_transition transition;
	atomic_bool done;
	int ret;
};

static void *submit_thread(void *opaque)
{
	struct submit_thread_args *args = opaque;

	args->ret = fwlab_c21_control_write(args->device,
					    FWLAB_C21_SUBMIT_OFFSET, args->wire,
					    sizeof(args->wire));
	atomic_store(&args->done, true);
	return NULL;
}

static void *action_thread(void *opaque)
{
	struct action_thread_args *args = opaque;

	if (args->close)
		args->ret = fwlab_c21_device_close(args->device);
	else
		args->ret = fwlab_c21_device_reset(args->device);
	atomic_store(&args->done, true);
	return NULL;
}

static void *transition_thread(void *opaque)
{
	struct transition_thread_args *args = opaque;

	args->ret = fwlab_c21_device_transition(
		args->device, args->transition, fwlab_c21_fake_transition_call,
		args->fake);
	atomic_store(&args->done, true);
	return NULL;
}

static int run_delay_race(bool close)
{
	struct fwlab_c21_state_snapshot state;
	struct action_thread_args action;
	struct submit_thread_args submit;
	struct test_fixture fixture;
	pthread_t action_id;
	pthread_t submit_id;
	uint64_t contention_epoch;

	CHECK_INT(fixture_init(&fixture), 0);
	CHECK_INT(fixture_open_attach(&fixture), 0);
	fwlab_c21_fake_copy_set_mode(&fixture.fake,
				      FWLAB_C21_FAKE_DELAY_SUCCESS, 0);
	memset(&submit, 0, sizeof(submit));
	submit.device = &fixture.device;
	atomic_init(&submit.done, false);
	make_request(submit.wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 1, 2,
		     TEST_BASE_IOVA, 64);
	CHECK_INT(pthread_create(&submit_id, NULL, submit_thread, &submit), 0);
	CHECK_INT(fwlab_c21_fake_copy_wait_entered(&fixture.fake), 0);
	memset(&action, 0, sizeof(action));
	action.device = &fixture.device;
	action.close = close;
	atomic_init(&action.done, false);
	contention_epoch =
		fwlab_c21_test_lock_contention_epoch(&fixture.lock);
	CHECK_INT(pthread_create(&action_id, NULL, action_thread, &action), 0);
	CHECK_INT(fwlab_c21_test_lock_wait_contention(&fixture.lock,
					      contention_epoch),
		  0);
	CHECK(!atomic_load(&action.done));
	fwlab_c21_fake_copy_release(&fixture.fake);
	CHECK_INT(pthread_join(submit_id, NULL), 0);
	CHECK_INT(pthread_join(action_id, NULL), 0);
	CHECK_INT(submit.ret, FWLAB_C21_RECORD_SIZE);
	CHECK_INT(action.ret, 0);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 3);
	if (close)
		CHECK(state.device_state == FWLAB_C21_STATE_CLOSED);
	else
		CHECK(state.device_state == FWLAB_C21_STATE_OPEN_ATTACHED);
	CHECK(!(state.flags & FWLAB_C21_ST_F_RESULT_VALID));
	fixture_destroy(&fixture);
	return 0;
}

static int test_delay_reset_and_close_serialization(void)
{
	CHECK_INT(run_delay_race(false), 0);
	CHECK_INT(run_delay_race(true), 0);
	return 0;
}

static int test_transition_success_failure_atomicity(void)
{
	struct fwlab_c21_state_snapshot state;
	struct test_fixture fixture;
	unsigned char old_result[FWLAB_C21_RECORD_SIZE];
	unsigned char new_result[FWLAB_C21_RECORD_SIZE];
	unsigned char pattern[32];
	unsigned char observed[32];
	unsigned char wire[FWLAB_C21_RECORD_SIZE];
	size_t index;

	CHECK_INT(fixture_init(&fixture), 0);
	CHECK_INT(fwlab_c21_device_open(&fixture.device), 0);
	CHECK_INT(fwlab_c21_device_transition(
			  &fixture.device, FWLAB_C21_TRANSITION_ATTACH,
			  invalid_positive_transition, NULL),
		  -EPROTO);
	CHECK_INT(fwlab_c21_device_transition(
			  &fixture.device, FWLAB_C21_TRANSITION_ATTACH,
			  invalid_large_errno_transition, NULL),
		  -EPROTO);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 1 && state.last_sequence == 0 &&
	      state.device_state == FWLAB_C21_STATE_OPEN_UNATTACHED);
	fwlab_c21_fake_transition_set_mode(
		&fixture.transition, FWLAB_C21_FAKE_TRANSITION_ERROR, -EACCES);
	CHECK_INT(fixture_transition(&fixture, FWLAB_C21_TRANSITION_ATTACH),
		  -EACCES);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 1 && state.last_sequence == 0 &&
	      state.device_state == FWLAB_C21_STATE_OPEN_UNATTACHED &&
	      !(state.flags & FWLAB_C21_ST_F_RESULT_VALID));

	fwlab_c21_fake_transition_set_mode(
		&fixture.transition, FWLAB_C21_FAKE_TRANSITION_SUCCESS, 0);
	CHECK_INT(fixture_transition(&fixture, FWLAB_C21_TRANSITION_ATTACH), 0);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 1, 2,
		     TEST_BASE_IOVA, 16);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	for (index = 0; index < sizeof(pattern); index++)
		pattern[index] = (unsigned char)(0xd0U + index);
	CHECK_INT(fwlab_c21_data_write(&fixture.device, 0, pattern,
				       sizeof(pattern)),
		  (int)sizeof(pattern));
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_RESULT_OFFSET, old_result,
					 sizeof(old_result)),
		  FWLAB_C21_RECORD_SIZE);

	fwlab_c21_fake_transition_set_mode(
		&fixture.transition, FWLAB_C21_FAKE_TRANSITION_ERROR, -EBUSY);
	CHECK_INT(fixture_transition(&fixture, FWLAB_C21_TRANSITION_REPLACE),
		  -EBUSY);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 2 && state.last_sequence == 1 &&
	      state.device_state == FWLAB_C21_STATE_OPEN_ATTACHED &&
	      (state.flags & FWLAB_C21_ST_F_RESULT_VALID));
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_RESULT_OFFSET, new_result,
					 sizeof(new_result)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK(!memcmp(old_result, new_result, sizeof(old_result)));
	CHECK_INT(fwlab_c21_data_read(&fixture.device, 0, observed,
				      sizeof(observed)),
		  (int)sizeof(observed));
	CHECK(!memcmp(pattern, observed, sizeof(pattern)));

	fwlab_c21_fake_transition_set_mode(
		&fixture.transition, FWLAB_C21_FAKE_TRANSITION_SUCCESS, 0);
	CHECK_INT(fixture_transition(&fixture, FWLAB_C21_TRANSITION_REPLACE), 0);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 3 && state.last_sequence == 0 &&
	      !(state.flags & FWLAB_C21_ST_F_RESULT_VALID));
	memset(observed, 0xff, sizeof(observed));
	CHECK_INT(fwlab_c21_data_read(&fixture.device, 0, observed,
				      sizeof(observed)),
		  (int)sizeof(observed));
	for (index = 0; index < sizeof(observed); index++)
		CHECK(observed[index] == 0);

	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 1, 3,
		     TEST_BASE_IOVA, 16);
	CHECK_INT(fwlab_c21_control_write(&fixture.device,
					  FWLAB_C21_SUBMIT_OFFSET, wire,
					  sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_RESULT_OFFSET, old_result,
					 sizeof(old_result)),
		  FWLAB_C21_RECORD_SIZE);
	fwlab_c21_fake_transition_set_mode(
		&fixture.transition, FWLAB_C21_FAKE_TRANSITION_ERROR, -EIO);
	CHECK_INT(fixture_transition(&fixture, FWLAB_C21_TRANSITION_DETACH),
		  -EIO);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 3 && state.last_sequence == 1 &&
	      state.device_state == FWLAB_C21_STATE_OPEN_ATTACHED &&
	      (state.flags & FWLAB_C21_ST_F_RESULT_VALID));
	CHECK_INT(fwlab_c21_control_read(&fixture.device,
					 FWLAB_C21_RESULT_OFFSET, new_result,
					 sizeof(new_result)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK(!memcmp(old_result, new_result, sizeof(old_result)));

	fwlab_c21_fake_transition_set_mode(
		&fixture.transition, FWLAB_C21_FAKE_TRANSITION_SUCCESS, 0);
	CHECK_INT(fixture_transition(&fixture, FWLAB_C21_TRANSITION_DETACH), 0);
	CHECK_INT(read_state(&fixture, &state), 0);
	CHECK(state.generation == 4 && state.last_sequence == 0 &&
	      state.device_state == FWLAB_C21_STATE_OPEN_UNATTACHED &&
	      !(state.flags & FWLAB_C21_ST_F_RESULT_VALID));
	fixture_destroy(&fixture);
	return 0;
}

static bool transition_leaves_attached(enum fwlab_c21_transition transition,
				       bool transition_success)
{
	if (transition == FWLAB_C21_TRANSITION_ATTACH)
		return transition_success;
	if (transition == FWLAB_C21_TRANSITION_REPLACE)
		return true;
	return !transition_success;
}

static int run_transition_submit_race(enum fwlab_c21_transition transition_kind,
				      bool transition_success)
{
	struct fwlab_c21_state_snapshot state;
	struct transition_thread_args transition;
	struct submit_thread_args submit;
	struct test_fixture fixture;
	enum fwlab_c21_transition observed_transition;
	bool final_attached;
	c21_u64 expected_generation;
	c21_u64 pre_generation;
	pthread_t submit_id;
	pthread_t transition_id;
	uint64_t contention_epoch;

	CHECK_INT(fixture_init(&fixture), 0);
	CHECK_INT(fwlab_c21_device_open(&fixture.device), 0);
	pre_generation = 1;
	if (transition_kind != FWLAB_C21_TRANSITION_ATTACH) {
		CHECK_INT(fixture_transition(&fixture,
					     FWLAB_C21_TRANSITION_ATTACH),
			  0);
		pre_generation = 2;
	}
	final_attached = transition_leaves_attached(transition_kind,
						    transition_success);
	expected_generation = pre_generation + (transition_success ? 1U : 0U);
	fwlab_c21_fake_transition_set_mode(
		&fixture.transition,
		transition_success ? FWLAB_C21_FAKE_TRANSITION_DELAY_SUCCESS :
				     FWLAB_C21_FAKE_TRANSITION_DELAY_ERROR,
		-EIO);
	memset(&transition, 0, sizeof(transition));
	transition.device = &fixture.device;
	transition.fake = &fixture.transition;
	transition.transition = transition_kind;
	atomic_init(&transition.done, false);
	CHECK_INT(pthread_create(&transition_id, NULL, transition_thread,
				 &transition),
		  0);
	CHECK_INT(fwlab_c21_fake_transition_wait_entered(&fixture.transition), 0);

	memset(&submit, 0, sizeof(submit));
	submit.device = &fixture.device;
	atomic_init(&submit.done, false);
	make_request(submit.wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 1,
		     expected_generation, TEST_BASE_IOVA, 16);
	contention_epoch =
		fwlab_c21_test_lock_contention_epoch(&fixture.lock);
	CHECK_INT(pthread_create(&submit_id, NULL, submit_thread, &submit), 0);
	CHECK_INT(fwlab_c21_test_lock_wait_contention(&fixture.lock,
					      contention_epoch),
		  0);
	CHECK(!atomic_load(&submit.done) && !atomic_load(&transition.done));
	fwlab_c21_fake_transition_release(&fixture.transition);
	CHECK_INT(pthread_join(transition_id, NULL), 0);
	CHECK_INT(pthread_join(submit_id, NULL), 0);
	CHECK_INT(transition.ret, transition_success ? 0 : -EIO);
	CHECK_INT(submit.ret, final_attached ? (int)FWLAB_C21_RECORD_SIZE :
					    -ENOTCONN);
	CHECK(fwlab_c21_fake_transition_calls(&fixture.transition,
					      &observed_transition) >= 1 &&
	      observed_transition == transition_kind);
	CHECK_INT(read_state(&fixture, &state), 0);
	if (final_attached) {
		CHECK(state.generation == expected_generation &&
		      state.last_sequence == 1 &&
		      state.device_state == FWLAB_C21_STATE_OPEN_ATTACHED &&
		      (state.flags & FWLAB_C21_ST_F_RESULT_VALID));
	} else {
		CHECK(state.generation == expected_generation &&
		      state.last_sequence == 0 &&
		      state.device_state == FWLAB_C21_STATE_OPEN_UNATTACHED &&
		      !(state.flags & FWLAB_C21_ST_F_RESULT_VALID));
	}
	fixture_destroy(&fixture);
	return 0;
}

static int test_transition_delay_submit_serialization(void)
{
	enum fwlab_c21_transition transition;

	for (transition = FWLAB_C21_TRANSITION_ATTACH;
	     transition <= FWLAB_C21_TRANSITION_DETACH; transition++) {
		CHECK_INT(run_transition_submit_race(transition, true), 0);
		CHECK_INT(run_transition_submit_race(transition, false), 0);
	}
	return 0;
}

static int run_copy_transition_race(enum fwlab_c21_transition transition_kind,
				    bool transition_success)
{
	struct fwlab_c21_state_snapshot state;
	struct transition_thread_args transition;
	struct submit_thread_args submit;
	struct test_fixture fixture;
	c21_u16 expected_state;
	pthread_t submit_id;
	pthread_t transition_id;
	uint64_t contention_epoch;

	CHECK(transition_kind == FWLAB_C21_TRANSITION_REPLACE ||
	      transition_kind == FWLAB_C21_TRANSITION_DETACH);
	CHECK_INT(fixture_init(&fixture), 0);
	CHECK_INT(fixture_open_attach(&fixture), 0);
	fwlab_c21_fake_copy_set_mode(&fixture.fake,
				      FWLAB_C21_FAKE_DELAY_SUCCESS, 0);
	fwlab_c21_fake_transition_set_mode(
		&fixture.transition,
		transition_success ? FWLAB_C21_FAKE_TRANSITION_SUCCESS :
				     FWLAB_C21_FAKE_TRANSITION_ERROR,
		-EIO);

	memset(&submit, 0, sizeof(submit));
	submit.device = &fixture.device;
	atomic_init(&submit.done, false);
	make_request(submit.wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 1, 2,
		     TEST_BASE_IOVA, 16);
	CHECK_INT(pthread_create(&submit_id, NULL, submit_thread, &submit), 0);
	CHECK_INT(fwlab_c21_fake_copy_wait_entered(&fixture.fake), 0);

	memset(&transition, 0, sizeof(transition));
	transition.device = &fixture.device;
	transition.fake = &fixture.transition;
	transition.transition = transition_kind;
	atomic_init(&transition.done, false);
	contention_epoch =
		fwlab_c21_test_lock_contention_epoch(&fixture.lock);
	CHECK_INT(pthread_create(&transition_id, NULL, transition_thread,
				 &transition),
		  0);
	CHECK_INT(fwlab_c21_test_lock_wait_contention(&fixture.lock,
					      contention_epoch),
		  0);
	CHECK(!atomic_load(&transition.done) && !atomic_load(&submit.done));
	fwlab_c21_fake_copy_release(&fixture.fake);
	CHECK_INT(pthread_join(submit_id, NULL), 0);
	CHECK_INT(pthread_join(transition_id, NULL), 0);
	CHECK_INT(submit.ret, FWLAB_C21_RECORD_SIZE);
	CHECK_INT(transition.ret, transition_success ? 0 : -EIO);
	CHECK_INT(read_state(&fixture, &state), 0);
	if (transition_success) {
		expected_state = transition_kind == FWLAB_C21_TRANSITION_REPLACE ?
				 FWLAB_C21_STATE_OPEN_ATTACHED :
				 FWLAB_C21_STATE_OPEN_UNATTACHED;
		CHECK(state.generation == 3 && state.last_sequence == 0 &&
		      state.device_state == expected_state &&
		      !(state.flags & FWLAB_C21_ST_F_RESULT_VALID));
	} else {
		CHECK(state.generation == 2 && state.last_sequence == 1 &&
		      state.device_state == FWLAB_C21_STATE_OPEN_ATTACHED &&
		      (state.flags & FWLAB_C21_ST_F_RESULT_VALID));
	}
	fixture_destroy(&fixture);
	return 0;
}

static int test_copy_delay_transition_serialization(void)
{
	CHECK_INT(run_copy_transition_race(FWLAB_C21_TRANSITION_REPLACE, true),
		  0);
	CHECK_INT(run_copy_transition_race(FWLAB_C21_TRANSITION_REPLACE, false),
		  0);
	CHECK_INT(run_copy_transition_race(FWLAB_C21_TRANSITION_DETACH, true),
		  0);
	CHECK_INT(run_copy_transition_race(FWLAB_C21_TRANSITION_DETACH, false),
		  0);
	return 0;
}

static int test_two_device_isolation(void)
{
	struct fwlab_c21_state_snapshot state_a;
	struct fwlab_c21_state_snapshot state_b;
	struct fwlab_c21_result result_b;
	struct test_fixture a;
	struct test_fixture b;
	unsigned char data_a[32];
	unsigned char data_b[32];
	unsigned char wire[FWLAB_C21_RECORD_SIZE];

	CHECK_INT(fixture_init_at(&a, 0x10000), 0);
	CHECK_INT(fixture_init_at(&b, 0x20000), 0);
	CHECK_INT(fixture_open_attach(&a), 0);
	CHECK_INT(fixture_open_attach(&b), 0);
	fwlab_c21_fake_copy_fill_page(&a.fake, 0xaa);
	fwlab_c21_fake_copy_fill_page(&b.fake, 0xbb);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 1, 2, 0x10000,
		     sizeof(data_a));
	CHECK_INT(fwlab_c21_control_write(&a.device, FWLAB_C21_SUBMIT_OFFSET,
					  wire, sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	make_request(wire, FWLAB_C21_OP_COPY_IOAS_TO_BUFFER, 1, 2, 0x20000,
		     sizeof(data_b));
	CHECK_INT(fwlab_c21_control_write(&b.device, FWLAB_C21_SUBMIT_OFFSET,
					  wire, sizeof(wire)),
		  FWLAB_C21_RECORD_SIZE);
	CHECK_INT(fwlab_c21_data_read(&a.device, 0, data_a, sizeof(data_a)),
		  (int)sizeof(data_a));
	CHECK_INT(fwlab_c21_data_read(&b.device, 0, data_b, sizeof(data_b)),
		  (int)sizeof(data_b));
	CHECK(data_a[0] == 0xaa && data_b[0] == 0xbb);
	CHECK_INT(fwlab_c21_device_reset(&a.device), 0);
	CHECK_INT(read_state(&a, &state_a), 0);
	CHECK_INT(read_state(&b, &state_b), 0);
	CHECK(state_a.generation == 3 && state_b.generation == 2 &&
	      state_b.last_sequence == 1);
	CHECK_INT(read_result(&b, &result_b), 0);
	CHECK(result_b.sequence == 1 && result_b.generation == 2);
	fixture_destroy(&a);
	fixture_destroy(&b);
	return 0;
}

struct named_test {
	const char *name;
	int (*run)(void);
};

int main(void)
{
	static const struct named_test tests[] = {
		{ "wire-and-structural-rejection",
		  test_wire_and_structural_rejection },
		{ "lifecycle-sequence-and-result",
		  test_lifecycle_sequence_and_result },
		{ "copy-directions-and-partial-effects",
		  test_copy_directions_and_partial_effects },
		{ "counter-wrap-fail-closed", test_counter_wrap_fail_closed },
		{ "delay-reset-close-serialization",
		  test_delay_reset_and_close_serialization },
		{ "transition-success-failure-atomicity",
		  test_transition_success_failure_atomicity },
		{ "transition-delay-submit-serialization",
		  test_transition_delay_submit_serialization },
		{ "copy-delay-transition-serialization",
		  test_copy_delay_transition_serialization },
		{ "two-device-isolation", test_two_device_isolation },
	};
	size_t index;

	for (index = 0; index < sizeof(tests) / sizeof(tests[0]); index++) {
		if (tests[index].run()) {
			fprintf(stderr, "C2.1 FAIL: %s\n", tests[index].name);
			return EXIT_FAILURE;
		}
		printf("C2.1 PASS: %s\n", tests[index].name);
	}
	printf("C2.1 userspace fake-provider suite: PASS (%zu tests)\n",
	       sizeof(tests) / sizeof(tests[0]));
	return EXIT_SUCCESS;
}
