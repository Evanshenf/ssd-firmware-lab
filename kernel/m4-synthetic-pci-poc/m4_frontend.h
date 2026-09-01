/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SSD_FWLAB_M4_FRONTEND_H
#define SSD_FWLAB_M4_FRONTEND_H

#include <linux/dma-mapping.h>
#include <linux/types.h>

#include "m4_dma_api.h"

#define FWLAB_M4_FRONTEND_SERVICES_VERSION 1U
#define FWLAB_M4_FRONTEND_OPS_VERSION 1U

/*
 * Internal PoC seam only.  This is deliberately not the portable B*-ABI.
 * Raw BAR/IOVA authority stays on the transport side of these callbacks.
 */
struct fwlab_m4_frontend_services {
	u32 version;
	u32 size;
	void *context;
	u32 (*bar_read32)(void *context, u32 offset);
	u64 (*bar_read64)(void *context, u32 offset);
	void (*bar_write32)(void *context, u32 offset, u32 value);
	void (*bar_write64)(void *context, u32 offset, u64 value);
	void (*bar_zero)(void *context, u32 offset, size_t length);
	int (*dma)(void *context, dma_addr_t iova, void *buffer, size_t length,
		   enum fwlab_m4_dma_direction direction);
	void (*notify)(void *context, unsigned int vector);
};

struct fwlab_m4_frontend_ops {
	u32 version;
	u32 size;
	int (*create)(const struct fwlab_m4_frontend_services *services,
		      void **instance);
	void (*destroy)(void *instance);
	void (*reset)(void *instance, u32 controller_epoch);
	void (*poll)(void *instance, u32 controller_epoch);
	void (*stop)(void *instance, u32 controller_epoch);
	bool (*quiescent)(void *instance);
};

struct fwlab_m4_frontend_binding {
	struct fwlab_m4_frontend_services services;
	const struct fwlab_m4_frontend_ops *ops;
	void *instance;
};

int fwlab_m4_frontend_bind(struct fwlab_m4_frontend_binding *binding,
			   const struct fwlab_m4_frontend_services *services,
			   const struct fwlab_m4_frontend_ops *ops);
void fwlab_m4_frontend_reset(struct fwlab_m4_frontend_binding *binding,
			     u32 controller_epoch);
void fwlab_m4_frontend_poll(struct fwlab_m4_frontend_binding *binding,
			    u32 controller_epoch);
void fwlab_m4_frontend_stop(struct fwlab_m4_frontend_binding *binding,
			    u32 controller_epoch);
bool fwlab_m4_frontend_quiescent(struct fwlab_m4_frontend_binding *binding);
void fwlab_m4_frontend_unbind(struct fwlab_m4_frontend_binding *binding);

#endif /* SSD_FWLAB_M4_FRONTEND_H */
