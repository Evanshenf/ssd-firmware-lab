// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/vfio.h>

#include "vfio_rw.h"

static int fwlab_c21_vfio_ioas_to_buffer(void *context, c21_u64 iova,
					 void *destination, c21_u32 length)
{
	struct fwlab_c21_vfio_rw *vfio_rw = context;
	dma_addr_t dma_iova = (dma_addr_t)iova;

	if ((c21_u64)dma_iova != iova)
		return -EOVERFLOW;
	return vfio_dma_rw(vfio_rw->vdev, dma_iova, destination, length, false);
}

static int fwlab_c21_vfio_buffer_to_ioas(void *context, c21_u64 iova,
					 const void *source, c21_u32 length)
{
	struct fwlab_c21_vfio_rw *vfio_rw = context;
	dma_addr_t dma_iova = (dma_addr_t)iova;

	if ((c21_u64)dma_iova != iova)
		return -EOVERFLOW;
	return vfio_dma_rw(vfio_rw->vdev, dma_iova, (void *)source, length, true);
}

static const struct fwlab_c21_copy_ops fwlab_c21_vfio_rw_ops = {
	.ioas_to_buffer = fwlab_c21_vfio_ioas_to_buffer,
	.buffer_to_ioas = fwlab_c21_vfio_buffer_to_ioas,
};

void fwlab_c21_vfio_rw_init(struct fwlab_c21_vfio_rw *vfio_rw,
			    struct vfio_device *vdev,
			    struct fwlab_c21_copy_provider *provider)
{
	vfio_rw->vdev = vdev;
	provider->ops = &fwlab_c21_vfio_rw_ops;
	provider->context = vfio_rw;
}
