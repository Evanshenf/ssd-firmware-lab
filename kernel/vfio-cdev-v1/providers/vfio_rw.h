/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: 2026 Evanshenf */

#ifndef FWLAB_C21_VFIO_RW_H
#define FWLAB_C21_VFIO_RW_H

#include <linux/vfio.h>

#include "c21_copy.h"

struct fwlab_c21_vfio_rw {
	struct vfio_device *vdev;
};

void fwlab_c21_vfio_rw_init(struct fwlab_c21_vfio_rw *vfio_rw,
			    struct vfio_device *vdev,
			    struct fwlab_c21_copy_provider *provider);

#endif /* FWLAB_C21_VFIO_RW_H */
