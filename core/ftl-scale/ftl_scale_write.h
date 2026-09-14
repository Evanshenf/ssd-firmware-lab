/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_FTL_SCALE_WRITE_H
#define FWLAB_FTL_SCALE_WRITE_H

/* The bounded write implementation owns this private layout. The selected
 * constructor will allocate it; existing constructors retain a NULL pointer. */
struct sf_write_pool;
size_t sf_write_pool_bytes(void);
bool sf_write_pool_init(struct fwlab_ftl_scale *, void *, size_t);
bool sf_write_pool_busy(const struct fwlab_ftl_scale *);
/* Scalar RMW/MAP may use control IO while write facts remain owned. This is
 * not a lower-provider idle or retirement certificate. DATA uses run IOs. */
bool sf_write_control_allowed(const struct fwlab_ftl_scale *);
enum fwlab_spine_result_v0 sf_write_parent_prepare(struct fwlab_ftl_scale *,
                                                  const struct sf_parent *);
bool sf_write_pool_step(struct fwlab_ftl_scale *);
bool sf_write_pool_runnable(const struct fwlab_ftl_scale *);
/* Extra committed LBAs in the active wave, not previously completed waves.
 * The parent failure path consumes this summary without group-success calls. */
uint32_t sf_write_known_prefix(const struct fwlab_ftl_scale *);

#endif
