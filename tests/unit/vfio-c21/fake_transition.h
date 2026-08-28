/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C21_FAKE_TRANSITION_H
#define FWLAB_C21_FAKE_TRANSITION_H

#include <pthread.h>

#include "c21_state.h"

enum fwlab_c21_fake_transition_mode {
	FWLAB_C21_FAKE_TRANSITION_SUCCESS,
	FWLAB_C21_FAKE_TRANSITION_ERROR,
	FWLAB_C21_FAKE_TRANSITION_DELAY_SUCCESS,
	FWLAB_C21_FAKE_TRANSITION_DELAY_ERROR,
};

struct fwlab_c21_fake_transition {
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	enum fwlab_c21_fake_transition_mode mode;
	int error;
	bool entered;
	bool released;
	unsigned int calls;
	enum fwlab_c21_transition last_transition;
};

int fwlab_c21_fake_transition_init(struct fwlab_c21_fake_transition *fake);
void fwlab_c21_fake_transition_destroy(struct fwlab_c21_fake_transition *fake);
void fwlab_c21_fake_transition_set_mode(
	struct fwlab_c21_fake_transition *fake,
	enum fwlab_c21_fake_transition_mode mode, int error);
int fwlab_c21_fake_transition_call(void *context,
				   enum fwlab_c21_transition transition);
int fwlab_c21_fake_transition_wait_entered(
	struct fwlab_c21_fake_transition *fake);
void fwlab_c21_fake_transition_release(
	struct fwlab_c21_fake_transition *fake);
unsigned int fwlab_c21_fake_transition_calls(
	struct fwlab_c21_fake_transition *fake,
	enum fwlab_c21_transition *last_transition);

#endif /* FWLAB_C21_FAKE_TRANSITION_H */
