/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C21_FAKE_COPY_H
#define FWLAB_C21_FAKE_COPY_H

#include <pthread.h>

#include "c21_copy.h"
#include "fwlab_c21_a1.h"

enum fwlab_c21_fake_mode {
	FWLAB_C21_FAKE_SUCCESS,
	FWLAB_C21_FAKE_FORCE_EACCES,
	FWLAB_C21_FAKE_FORCE_HOLE,
	FWLAB_C21_FAKE_FORCE_EIO,
	FWLAB_C21_FAKE_PARTIAL_THEN_EIO,
	FWLAB_C21_FAKE_DELAY_SUCCESS,
	FWLAB_C21_FAKE_DELAY_EIO,
};

struct fwlab_c21_fake_copy {
	c21_u64 base_iova;
	bool mapped;
	bool readable;
	bool writable;
	enum fwlab_c21_fake_mode mode;
	c21_u32 partial_length;
	unsigned char page[FWLAB_C21_IOAS_PAGE_SIZE];
	pthread_mutex_t gate_mutex;
	pthread_cond_t gate_condition;
	bool delay_entered;
	bool delay_released;
};

int fwlab_c21_fake_copy_init(struct fwlab_c21_fake_copy *fake,
			     c21_u64 base_iova);
void fwlab_c21_fake_copy_destroy(struct fwlab_c21_fake_copy *fake);
struct fwlab_c21_copy_provider
fwlab_c21_fake_copy_provider(struct fwlab_c21_fake_copy *fake);
void fwlab_c21_fake_copy_set_mode(struct fwlab_c21_fake_copy *fake,
				  enum fwlab_c21_fake_mode mode,
				  c21_u32 partial_length);
void fwlab_c21_fake_copy_set_permissions(struct fwlab_c21_fake_copy *fake,
					 bool readable, bool writable);
void fwlab_c21_fake_copy_set_mapped(struct fwlab_c21_fake_copy *fake,
				    bool mapped);
void fwlab_c21_fake_copy_fill_page(struct fwlab_c21_fake_copy *fake,
				   unsigned char value);
int fwlab_c21_fake_copy_write_page(struct fwlab_c21_fake_copy *fake,
				   size_t offset, const unsigned char *source,
				   size_t length);
int fwlab_c21_fake_copy_read_page(struct fwlab_c21_fake_copy *fake,
				  size_t offset, unsigned char *destination,
				  size_t length);
int fwlab_c21_fake_copy_wait_entered(struct fwlab_c21_fake_copy *fake);
void fwlab_c21_fake_copy_release(struct fwlab_c21_fake_copy *fake);

#endif /* FWLAB_C21_FAKE_COPY_H */
