// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

#include <linux/errno.h>
#include <linux/kernel.h>

#include "m4_frontend.h"

static bool fwlab_m4_services_valid(
	const struct fwlab_m4_frontend_services *services)
{
	return services &&
	       services->version == FWLAB_M4_FRONTEND_SERVICES_VERSION &&
	       services->size == sizeof(*services) && services->context &&
	       services->bar_read32 && services->bar_read64 &&
	       services->bar_write32 && services->bar_write64 &&
	       services->bar_zero && services->dma && services->notify;
}

static bool fwlab_m4_ops_valid(const struct fwlab_m4_frontend_ops *ops)
{
	return ops && ops->version == FWLAB_M4_FRONTEND_OPS_VERSION &&
	       ops->size == sizeof(*ops) && ops->create && ops->destroy &&
	       ops->reset && ops->poll && ops->stop && ops->quiescent;
}

int fwlab_m4_frontend_bind(struct fwlab_m4_frontend_binding *binding,
			   const struct fwlab_m4_frontend_services *services,
			   const struct fwlab_m4_frontend_ops *ops)
{
	int ret;

	if (!binding || binding->ops || binding->instance ||
	    !fwlab_m4_services_valid(services) || !fwlab_m4_ops_valid(ops))
		return -EINVAL;
	binding->services = *services;
	ret = ops->create(&binding->services, &binding->instance);
	if (ret) {
		memset(binding, 0, sizeof(*binding));
		return ret;
	}
	if (!binding->instance) {
		memset(binding, 0, sizeof(*binding));
		return -EINVAL;
	}
	binding->ops = ops;
	return 0;
}

void fwlab_m4_frontend_reset(struct fwlab_m4_frontend_binding *binding,
			     u32 controller_epoch)
{
	if (binding && binding->ops && binding->instance)
		binding->ops->reset(binding->instance, controller_epoch);
}

void fwlab_m4_frontend_poll(struct fwlab_m4_frontend_binding *binding,
			    u32 controller_epoch)
{
	if (binding && binding->ops && binding->instance)
		binding->ops->poll(binding->instance, controller_epoch);
}

void fwlab_m4_frontend_stop(struct fwlab_m4_frontend_binding *binding,
			    u32 controller_epoch)
{
	if (binding && binding->ops && binding->instance)
		binding->ops->stop(binding->instance, controller_epoch);
}

bool fwlab_m4_frontend_quiescent(struct fwlab_m4_frontend_binding *binding)
{
	return !binding || !binding->ops || !binding->instance ||
	       binding->ops->quiescent(binding->instance);
}

void fwlab_m4_frontend_unbind(struct fwlab_m4_frontend_binding *binding)
{
	if (!binding || !binding->ops || !binding->instance)
		return;
	WARN_ON_ONCE(!binding->ops->quiescent(binding->instance));
	binding->ops->destroy(binding->instance);
	memset(binding, 0, sizeof(*binding));
}
