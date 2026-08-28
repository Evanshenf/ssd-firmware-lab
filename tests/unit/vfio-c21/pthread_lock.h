/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C21_TEST_PTHREAD_LOCK_H
#define FWLAB_C21_TEST_PTHREAD_LOCK_H

#include <pthread.h>
#include <stdint.h>

#include "c21_state.h"

struct fwlab_c21_test_lock {
	pthread_mutex_t mutex;
	pthread_mutex_t contention_mutex;
	pthread_cond_t contention_condition;
	uint64_t contention_epoch;
};

extern const struct fwlab_c21_lock_ops fwlab_c21_test_lock_ops;

int fwlab_c21_test_lock_init(struct fwlab_c21_test_lock *lock);
void fwlab_c21_test_lock_destroy(struct fwlab_c21_test_lock *lock);
uint64_t
fwlab_c21_test_lock_contention_epoch(struct fwlab_c21_test_lock *lock);
int fwlab_c21_test_lock_wait_contention(struct fwlab_c21_test_lock *lock,
					uint64_t previous_epoch);

#endif /* FWLAB_C21_TEST_PTHREAD_LOCK_H */
