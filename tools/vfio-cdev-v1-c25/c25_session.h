/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef SSD_FWLAB_C25_SESSION_H
#define SSD_FWLAB_C25_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fwlab_c21_a1.h"

#define C25_PAGE_SIZE 4096U
#define C25_SHARED_IOVA UINT64_C(0x100000000)
#define C25_COPY_LENGTH 256U

struct c25_region {
	uint32_t index;
	uint32_t flags;
	uint64_t offset;
	uint64_t size;
};

struct c25_state {
	uint16_t device_state;
	uint32_t flags;
	uint64_t generation;
	uint64_t last_sequence;
	uint64_t next_sequence;
};

struct c25_result {
	uint16_t operation;
	uint32_t flags;
	uint64_t sequence;
	uint64_t generation;
	uint64_t iova;
	uint32_t requested_length;
	int32_t op_errno;
};

struct c25_owner {
	int iommu_fd;
};

struct c25_session {
	const char *label;
	struct c25_owner *owner;
	int device_fd;
	bool attached;
	bool mapped;
	bool ioas_created;
	uint32_t ioas_id;
	uint64_t iova;
	unsigned char *page;
	struct c25_region data_region;
	struct c25_region control_region;
};

struct c25_observation {
	unsigned char state_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char result_wire[FWLAB_C21_RECORD_SIZE];
	unsigned char data[FWLAB_C21_DATA_REGION_SIZE];
	unsigned char page[C25_PAGE_SIZE];
	bool result_valid;
};

int c25_selftest(void);
int c25_owner_open(struct c25_owner *owner);
int c25_owner_close(struct c25_owner *owner);
int c25_session_begin(struct c25_session *session, struct c25_owner *owner,
		      const char *label, const char *device_path,
		      uint64_t iova);
int c25_session_cleanup(struct c25_session *session);
int c25_session_map(struct c25_session *session);
int c25_session_unmap(struct c25_session *session);
int c25_session_attach(struct c25_session *session);
int c25_session_detach(struct c25_session *session);
int c25_session_destroy_ioas(struct c25_session *session);
int c25_session_close_device(struct c25_session *session);
int c25_session_reset(struct c25_session *session);
int c25_read_state(struct c25_session *session,
		   unsigned char wire[FWLAB_C21_RECORD_SIZE],
		   struct c25_state *state);
int c25_read_result(struct c25_session *session,
		    unsigned char wire[FWLAB_C21_RECORD_SIZE],
		    struct c25_result *result);
int c25_read_data(struct c25_session *session, unsigned char *buffer,
		  size_t length);
int c25_write_data(struct c25_session *session, const unsigned char *buffer,
		   size_t length);
int c25_submit(struct c25_session *session, uint16_t operation,
	       uint64_t iova, uint32_t length, int32_t expected_errno);
int c25_capture(struct c25_session *session,
		struct c25_observation *observation);
int c25_observation_equal(const struct c25_observation *left,
			  const struct c25_observation *right);
void c25_fill_pattern(unsigned char *buffer, size_t length,
		      unsigned int seed);

#endif
