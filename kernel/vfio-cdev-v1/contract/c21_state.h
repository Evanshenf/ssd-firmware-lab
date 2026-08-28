/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef FWLAB_C21_STATE_H
#define FWLAB_C21_STATE_H

#include "c21_copy.h"
#include "c21_wire.h"

struct fwlab_c21_lock_ops {
	void (*lock)(void *context);
	void (*unlock)(void *context);
};

enum fwlab_c21_transition {
	FWLAB_C21_TRANSITION_ATTACH,
	FWLAB_C21_TRANSITION_REPLACE,
	FWLAB_C21_TRANSITION_DETACH,
};

/*
 * Called synchronously while the per-device mutex is held.  Zero means the
 * adjacent transition completed fully.  A negative errno must mean the
 * adjacent state is unchanged.  Positive returns are normalized to -EPROTO.
 */
typedef int (*fwlab_c21_transition_fn)(void *context,
				       enum fwlab_c21_transition transition);

/* Per-instance mutable state.  No member may become a process-wide global. */
struct fwlab_c21_device {
	const struct fwlab_c21_lock_ops *lock_ops;
	void *lock_context;
	struct fwlab_c21_copy_provider provider;
	bool initialized;
	bool result_valid;
	bool sequence_exhausted;
	c21_u16 state;
	c21_u64 generation;
	c21_u64 last_sequence;
	struct fwlab_c21_request current_request;
	struct fwlab_c21_result result;
	unsigned char data[FWLAB_C21_DATA_REGION_SIZE];
};

int fwlab_c21_device_init(struct fwlab_c21_device *device,
			  const struct fwlab_c21_lock_ops *lock_ops,
			  void *lock_context,
			  const struct fwlab_c21_copy_provider *provider);
int fwlab_c21_device_open(struct fwlab_c21_device *device);
int fwlab_c21_device_transition(struct fwlab_c21_device *device,
				enum fwlab_c21_transition transition,
				fwlab_c21_transition_fn transition_fn,
				void *transition_context);
int fwlab_c21_device_reset(struct fwlab_c21_device *device);
int fwlab_c21_device_close(struct fwlab_c21_device *device);

int fwlab_c21_control_write(struct fwlab_c21_device *device,
			    c21_u32 offset, const unsigned char *wire,
			    size_t wire_size);
int fwlab_c21_control_read(struct fwlab_c21_device *device, c21_u32 offset,
			   unsigned char *wire, size_t wire_size);
int fwlab_c21_data_write(struct fwlab_c21_device *device, c21_u32 offset,
			 const unsigned char *source, size_t count);
int fwlab_c21_data_read(struct fwlab_c21_device *device, c21_u32 offset,
			unsigned char *destination, size_t count);

#endif /* FWLAB_C21_STATE_H */
