#define _POSIX_C_SOURCE 200809L

// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: BSD-3-Clause

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fake_transition.h"

int fwlab_c21_fake_transition_init(struct fwlab_c21_fake_transition *fake)
{
	int ret;

	memset(fake, 0, sizeof(*fake));
	fake->mode = FWLAB_C21_FAKE_TRANSITION_SUCCESS;
	fake->error = -EIO;
	ret = pthread_mutex_init(&fake->mutex, NULL);
	if (ret)
		return ret;
	ret = pthread_cond_init(&fake->condition, NULL);
	if (ret) {
		pthread_mutex_destroy(&fake->mutex);
		return ret;
	}
	return 0;
}

void fwlab_c21_fake_transition_destroy(struct fwlab_c21_fake_transition *fake)
{
	if (pthread_cond_destroy(&fake->condition) ||
	    pthread_mutex_destroy(&fake->mutex))
		abort();
}

void fwlab_c21_fake_transition_set_mode(
	struct fwlab_c21_fake_transition *fake,
	enum fwlab_c21_fake_transition_mode mode, int error)
{
	if (pthread_mutex_lock(&fake->mutex))
		abort();
	fake->mode = mode;
	fake->error = error < 0 ? error : -EIO;
	fake->entered = false;
	fake->released = false;
	if (pthread_mutex_unlock(&fake->mutex))
		abort();
}

int fwlab_c21_fake_transition_call(void *context,
				   enum fwlab_c21_transition transition)
{
	struct fwlab_c21_fake_transition *fake = context;
	enum fwlab_c21_fake_transition_mode mode;
	int ret;

	if (pthread_mutex_lock(&fake->mutex))
		abort();
	fake->calls++;
	fake->last_transition = transition;
	mode = fake->mode;
	if (mode == FWLAB_C21_FAKE_TRANSITION_DELAY_SUCCESS ||
	    mode == FWLAB_C21_FAKE_TRANSITION_DELAY_ERROR) {
		fake->entered = true;
		if (pthread_cond_broadcast(&fake->condition))
			abort();
		while (!fake->released)
			if (pthread_cond_wait(&fake->condition, &fake->mutex))
				abort();
	}
	ret = (mode == FWLAB_C21_FAKE_TRANSITION_ERROR ||
	       mode == FWLAB_C21_FAKE_TRANSITION_DELAY_ERROR) ?
		      fake->error : 0;
	if (pthread_mutex_unlock(&fake->mutex))
		abort();
	return ret;
}

static int fwlab_c21_fake_transition_deadline(struct timespec *deadline)
{
	if (clock_gettime(CLOCK_REALTIME, deadline))
		return -errno;
	deadline->tv_sec += 5;
	return 0;
}

int fwlab_c21_fake_transition_wait_entered(
	struct fwlab_c21_fake_transition *fake)
{
	struct timespec deadline;
	int ret;

	ret = fwlab_c21_fake_transition_deadline(&deadline);
	if (ret)
		return ret;
	if (pthread_mutex_lock(&fake->mutex))
		abort();
	while (!fake->entered) {
		ret = pthread_cond_timedwait(&fake->condition, &fake->mutex,
					     &deadline);
		if (ret == ETIMEDOUT) {
			pthread_mutex_unlock(&fake->mutex);
			return -ETIMEDOUT;
		}
		if (ret)
			abort();
	}
	if (pthread_mutex_unlock(&fake->mutex))
		abort();
	return 0;
}

void fwlab_c21_fake_transition_release(
	struct fwlab_c21_fake_transition *fake)
{
	if (pthread_mutex_lock(&fake->mutex))
		abort();
	fake->released = true;
	if (pthread_cond_broadcast(&fake->condition) ||
	    pthread_mutex_unlock(&fake->mutex))
		abort();
}

unsigned int fwlab_c21_fake_transition_calls(
	struct fwlab_c21_fake_transition *fake,
	enum fwlab_c21_transition *last_transition)
{
	unsigned int calls;

	if (pthread_mutex_lock(&fake->mutex))
		abort();
	calls = fake->calls;
	if (last_transition)
		*last_transition = fake->last_transition;
	if (pthread_mutex_unlock(&fake->mutex))
		abort();
	return calls;
}
