/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef FWLAB_C21_COPY_H
#define FWLAB_C21_COPY_H

#include "c21_compat.h"

/*
 * Synchronous, injected adjacent-layer seam.
 *
 * A provider returns zero only after the entire length completed, otherwise a
 * negative errno.  It must not retain src/dst or call back into the state
 * engine.  On an error, buffer_to_ioas may already have modified a prefix of
 * the external destination.  The state engine invokes providers while holding
 * its per-device mutex.
 */
struct fwlab_c21_copy_ops {
	int (*ioas_to_buffer)(void *context, c21_u64 iova, void *destination,
			      c21_u32 length);
	int (*buffer_to_ioas)(void *context, c21_u64 iova, const void *source,
			      c21_u32 length);
};

struct fwlab_c21_copy_provider {
	const struct fwlab_c21_copy_ops *ops;
	void *context;
};

#endif /* FWLAB_C21_COPY_H */
