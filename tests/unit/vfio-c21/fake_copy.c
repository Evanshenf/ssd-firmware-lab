#define _POSIX_C_SOURCE 200809L

// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: BSD-3-Clause

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fake_copy.h"

/* Returns with gate_mutex held. */
static enum fwlab_c21_fake_mode
fwlab_c21_fake_enter_provider(struct fwlab_c21_fake_copy *fake)
{
	enum fwlab_c21_fake_mode mode;

	if (pthread_mutex_lock(&fake->gate_mutex))
		abort();
	mode = fake->mode;
	if (mode == FWLAB_C21_FAKE_DELAY_SUCCESS ||
	    mode == FWLAB_C21_FAKE_DELAY_EIO) {
		fake->delay_entered = true;
		if (pthread_cond_broadcast(&fake->gate_condition))
			abort();
		while (!fake->delay_released)
			if (pthread_cond_wait(&fake->gate_condition,
					      &fake->gate_mutex))
				abort();
	}
	return mode;
}

static void fwlab_c21_fake_leave_provider(struct fwlab_c21_fake_copy *fake)
{
	if (pthread_mutex_unlock(&fake->gate_mutex))
		abort();
}

static int fwlab_c21_fake_range(struct fwlab_c21_fake_copy *fake,
				c21_u64 iova, c21_u32 length,
				size_t *offset)
{
	c21_u64 relative;

	if (!fake->mapped || iova < fake->base_iova)
		return -ENXIO;
	relative = iova - fake->base_iova;
	if (relative > FWLAB_C21_IOAS_PAGE_SIZE ||
	    length > FWLAB_C21_IOAS_PAGE_SIZE - relative)
		return -ENXIO;
	*offset = (size_t)relative;
	return 0;
}

static int fwlab_c21_fake_ioas_to_buffer(void *context, c21_u64 iova,
					 void *destination, c21_u32 length)
{
	struct fwlab_c21_fake_copy *fake = context;
	enum fwlab_c21_fake_mode mode;
	size_t offset;
	size_t partial;
	int ret;

	mode = fwlab_c21_fake_enter_provider(fake);
	if (mode == FWLAB_C21_FAKE_FORCE_EACCES) {
		ret = -EACCES;
		goto out;
	}
	if (mode == FWLAB_C21_FAKE_FORCE_HOLE) {
		ret = -ENXIO;
		goto out;
	}
	if (mode == FWLAB_C21_FAKE_FORCE_EIO ||
	    mode == FWLAB_C21_FAKE_DELAY_EIO) {
		ret = -EIO;
		goto out;
	}
	ret = fwlab_c21_fake_range(fake, iova, length, &offset);
	if (ret)
		goto out;
	if (!fake->readable) {
		ret = -EACCES;
		goto out;
	}
	if (mode == FWLAB_C21_FAKE_PARTIAL_THEN_EIO) {
		partial = fake->partial_length;
		if (partial > length)
			partial = length;
		memcpy(destination, fake->page + offset, partial);
		ret = -EIO;
		goto out;
	}
	memcpy(destination, fake->page + offset, length);
	ret = 0;
out:
	fwlab_c21_fake_leave_provider(fake);
	return ret;
}

static int fwlab_c21_fake_buffer_to_ioas(void *context, c21_u64 iova,
					 const void *source, c21_u32 length)
{
	struct fwlab_c21_fake_copy *fake = context;
	enum fwlab_c21_fake_mode mode;
	size_t offset;
	size_t partial;
	int ret;

	mode = fwlab_c21_fake_enter_provider(fake);
	if (mode == FWLAB_C21_FAKE_FORCE_EACCES) {
		ret = -EACCES;
		goto out;
	}
	if (mode == FWLAB_C21_FAKE_FORCE_HOLE) {
		ret = -ENXIO;
		goto out;
	}
	if (mode == FWLAB_C21_FAKE_FORCE_EIO ||
	    mode == FWLAB_C21_FAKE_DELAY_EIO) {
		ret = -EIO;
		goto out;
	}
	ret = fwlab_c21_fake_range(fake, iova, length, &offset);
	if (ret)
		goto out;
	if (!fake->writable) {
		ret = -EACCES;
		goto out;
	}
	if (mode == FWLAB_C21_FAKE_PARTIAL_THEN_EIO) {
		partial = fake->partial_length;
		if (partial > length)
			partial = length;
		memcpy(fake->page + offset, source, partial);
		ret = -EIO;
		goto out;
	}
	memcpy(fake->page + offset, source, length);
	ret = 0;
out:
	fwlab_c21_fake_leave_provider(fake);
	return ret;
}

static const struct fwlab_c21_copy_ops fwlab_c21_fake_copy_ops = {
	.ioas_to_buffer = fwlab_c21_fake_ioas_to_buffer,
	.buffer_to_ioas = fwlab_c21_fake_buffer_to_ioas,
};

int fwlab_c21_fake_copy_init(struct fwlab_c21_fake_copy *fake,
			     c21_u64 base_iova)
{
	int ret;

	memset(fake, 0, sizeof(*fake));
	fake->base_iova = base_iova;
	fake->mapped = true;
	fake->readable = true;
	fake->writable = true;
	fake->mode = FWLAB_C21_FAKE_SUCCESS;
	fake->partial_length = 1;
	ret = pthread_mutex_init(&fake->gate_mutex, NULL);
	if (ret)
		return ret;
	ret = pthread_cond_init(&fake->gate_condition, NULL);
	if (ret) {
		pthread_mutex_destroy(&fake->gate_mutex);
		return ret;
	}
	return 0;
}

void fwlab_c21_fake_copy_destroy(struct fwlab_c21_fake_copy *fake)
{
	if (pthread_cond_destroy(&fake->gate_condition) ||
	    pthread_mutex_destroy(&fake->gate_mutex))
		abort();
}

struct fwlab_c21_copy_provider
fwlab_c21_fake_copy_provider(struct fwlab_c21_fake_copy *fake)
{
	struct fwlab_c21_copy_provider provider = {
		.ops = &fwlab_c21_fake_copy_ops,
		.context = fake,
	};

	return provider;
}

void fwlab_c21_fake_copy_set_mode(struct fwlab_c21_fake_copy *fake,
				  enum fwlab_c21_fake_mode mode,
				  c21_u32 partial_length)
{
	if (pthread_mutex_lock(&fake->gate_mutex))
		abort();
	fake->mode = mode;
	fake->partial_length = partial_length;
	fake->delay_entered = false;
	fake->delay_released = false;
	if (pthread_mutex_unlock(&fake->gate_mutex))
		abort();
}

void fwlab_c21_fake_copy_set_permissions(struct fwlab_c21_fake_copy *fake,
					 bool readable, bool writable)
{
	if (pthread_mutex_lock(&fake->gate_mutex))
		abort();
	fake->readable = readable;
	fake->writable = writable;
	if (pthread_mutex_unlock(&fake->gate_mutex))
		abort();
}

void fwlab_c21_fake_copy_set_mapped(struct fwlab_c21_fake_copy *fake,
				    bool mapped)
{
	if (pthread_mutex_lock(&fake->gate_mutex))
		abort();
	fake->mapped = mapped;
	if (pthread_mutex_unlock(&fake->gate_mutex))
		abort();
}

void fwlab_c21_fake_copy_fill_page(struct fwlab_c21_fake_copy *fake,
				   unsigned char value)
{
	if (pthread_mutex_lock(&fake->gate_mutex))
		abort();
	memset(fake->page, value, sizeof(fake->page));
	if (pthread_mutex_unlock(&fake->gate_mutex))
		abort();
}

int fwlab_c21_fake_copy_write_page(struct fwlab_c21_fake_copy *fake,
				   size_t offset, const unsigned char *source,
				   size_t length)
{
	if ((!source && length) || offset > sizeof(fake->page) ||
	    length > sizeof(fake->page) - offset)
		return -EINVAL;
	if (pthread_mutex_lock(&fake->gate_mutex))
		abort();
	if (length)
		memcpy(fake->page + offset, source, length);
	if (pthread_mutex_unlock(&fake->gate_mutex))
		abort();
	return 0;
}

int fwlab_c21_fake_copy_read_page(struct fwlab_c21_fake_copy *fake,
				  size_t offset, unsigned char *destination,
				  size_t length)
{
	if ((!destination && length) || offset > sizeof(fake->page) ||
	    length > sizeof(fake->page) - offset)
		return -EINVAL;
	if (pthread_mutex_lock(&fake->gate_mutex))
		abort();
	if (length)
		memcpy(destination, fake->page + offset, length);
	if (pthread_mutex_unlock(&fake->gate_mutex))
		abort();
	return 0;
}

static int fwlab_c21_fake_deadline(struct timespec *deadline)
{
	if (clock_gettime(CLOCK_REALTIME, deadline))
		return -errno;
	deadline->tv_sec += 5;
	return 0;
}

int fwlab_c21_fake_copy_wait_entered(struct fwlab_c21_fake_copy *fake)
{
	struct timespec deadline;
	int ret;

	ret = fwlab_c21_fake_deadline(&deadline);
	if (ret)
		return ret;
	if (pthread_mutex_lock(&fake->gate_mutex))
		abort();
	while (!fake->delay_entered) {
		ret = pthread_cond_timedwait(&fake->gate_condition,
					     &fake->gate_mutex, &deadline);
		if (ret == ETIMEDOUT) {
			pthread_mutex_unlock(&fake->gate_mutex);
			return -ETIMEDOUT;
		}
		if (ret)
			abort();
	}
	if (pthread_mutex_unlock(&fake->gate_mutex))
		abort();
	return 0;
}

void fwlab_c21_fake_copy_release(struct fwlab_c21_fake_copy *fake)
{
	if (pthread_mutex_lock(&fake->gate_mutex))
		abort();
	fake->delay_released = true;
	if (pthread_cond_broadcast(&fake->gate_condition) ||
	    pthread_mutex_unlock(&fake->gate_mutex))
		abort();
}
