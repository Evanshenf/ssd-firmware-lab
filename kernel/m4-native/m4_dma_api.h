/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SSD_FWLAB_M4_DMA_API_H
#define SSD_FWLAB_M4_DMA_API_H

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/types.h>
#include <linux/spinlock.h>

#define FWLAB_M4_DMA_MAX_BYTES (64U * 1024U)
#define FWLAB_M4_MAPPING_PAGES 3U

enum fwlab_m4_dma_direction {
	FWLAB_M4_DMA_READ_HOST = 1,
	FWLAB_M4_DMA_WRITE_HOST = 2,
};

/* Kernel HIF only: an immutable mapping snapshot, never a firmware token.
 * Copy revalidates every mapping identity under the IOMMU read lock. */
struct fwlab_m4_mapping {
	u64 domain_nonce;
	u64 attach_generation;
	u64 iova;
	u32 length;
	u32 direction;
	u32 pages;
	u32 reserved;
	u64 pte[FWLAB_M4_MAPPING_PAGES];
	u64 mapping_uid[FWLAB_M4_MAPPING_PAGES];
};

struct fwlab_m4_copy_guard {
	spinlock_t *lock;
	bool (*valid_locked)(void *context);
	void *context;
};

int fwlab_m4_mapping_capture(struct device *dev, dma_addr_t iova, u32 length,
			     enum fwlab_m4_dma_direction direction,
			     struct fwlab_m4_mapping *mapping);
int fwlab_m4_mapping_copy(struct device *dev,
			  const struct fwlab_m4_mapping *mapping, u32 offset,
			  void *buffer, u32 length,
			  const struct fwlab_m4_copy_guard *guard);

int fwlab_m4_dma_transfer(struct device *dev, dma_addr_t iova, void *buffer,
			  size_t length,
			  enum fwlab_m4_dma_direction direction);

#endif /* SSD_FWLAB_M4_DMA_API_H */
