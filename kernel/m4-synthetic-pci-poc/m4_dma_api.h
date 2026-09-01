/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SSD_FWLAB_M4_DMA_API_H
#define SSD_FWLAB_M4_DMA_API_H

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>

#define FWLAB_M4_DMA_MAX_BYTES (64U * 1024U)

enum fwlab_m4_dma_direction {
	FWLAB_M4_DMA_READ_HOST = 1,
	FWLAB_M4_DMA_WRITE_HOST = 2,
};

int fwlab_m4_dma_transfer(struct device *dev, dma_addr_t iova, void *buffer,
			  size_t length,
			  enum fwlab_m4_dma_direction direction);

#endif /* SSD_FWLAB_M4_DMA_API_H */
