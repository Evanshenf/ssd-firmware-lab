#define _POSIX_C_SOURCE 200809L

// SPDX-FileCopyrightText: 2026 Evanshenf
// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <errno.h>
#include <time.h>

#include "pthread_lock.h"

static void fwlab_c21_pthread_lock(void *context)
{
	struct fwlab_c21_test_lock *lock = context;
	int ret;

	ret = pthread_mutex_trylock(&lock->mutex);
	if (!ret)
		return;
	if (ret != EBUSY)
		abort();
	if (pthread_mutex_lock(&lock->contention_mutex))
		abort();
	lock->contention_epoch++;
	if (pthread_cond_broadcast(&lock->contention_condition) ||
	    pthread_mutex_unlock(&lock->contention_mutex))
		abort();
	if (pthread_mutex_lock(&lock->mutex))
		abort();
}

static void fwlab_c21_pthread_unlock(void *context)
{
	struct fwlab_c21_test_lock *lock = context;

	if (pthread_mutex_unlock(&lock->mutex))
		abort();
}

const struct fwlab_c21_lock_ops fwlab_c21_test_lock_ops = {
	.lock = fwlab_c21_pthread_lock,
	.unlock = fwlab_c21_pthread_unlock,
};

int fwlab_c21_test_lock_init(struct fwlab_c21_test_lock *lock)
{
	int ret;

	ret = pthread_mutex_init(&lock->mutex, NULL);
	if (ret)
		return ret;
	ret = pthread_mutex_init(&lock->contention_mutex, NULL);
	if (ret) {
		pthread_mutex_destroy(&lock->mutex);
		return ret;
	}
	ret = pthread_cond_init(&lock->contention_condition, NULL);
	if (ret) {
		pthread_mutex_destroy(&lock->contention_mutex);
		pthread_mutex_destroy(&lock->mutex);
		return ret;
	}
	lock->contention_epoch = 0;
	return 0;
}

void fwlab_c21_test_lock_destroy(struct fwlab_c21_test_lock *lock)
{
	if (pthread_cond_destroy(&lock->contention_condition) ||
	    pthread_mutex_destroy(&lock->contention_mutex) ||
	    pthread_mutex_destroy(&lock->mutex))
		abort();
}

uint64_t
fwlab_c21_test_lock_contention_epoch(struct fwlab_c21_test_lock *lock)
{
	uint64_t epoch;

	if (pthread_mutex_lock(&lock->contention_mutex))
		abort();
	epoch = lock->contention_epoch;
	if (pthread_mutex_unlock(&lock->contention_mutex))
		abort();
	return epoch;
}

int fwlab_c21_test_lock_wait_contention(struct fwlab_c21_test_lock *lock,
					uint64_t previous_epoch)
{
	struct timespec deadline;
	int ret;

	if (clock_gettime(CLOCK_REALTIME, &deadline))
		return -errno;
	deadline.tv_sec += 5;
	if (pthread_mutex_lock(&lock->contention_mutex))
		abort();
	while (lock->contention_epoch <= previous_epoch) {
		ret = pthread_cond_timedwait(&lock->contention_condition,
					     &lock->contention_mutex, &deadline);
		if (ret == ETIMEDOUT) {
			pthread_mutex_unlock(&lock->contention_mutex);
			return -ETIMEDOUT;
		}
		if (ret)
			abort();
	}
	if (pthread_mutex_unlock(&lock->contention_mutex))
		abort();
	return 0;
}
