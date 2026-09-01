// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

/* Native DMA-API/default-domain observer for M4-D admission. */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "m4_internal.h"
#include "m4_dma_api.h"

#define FWLAB_DMA_API_LAB_NAME "ssd_fwlab_dma_api_lab"
#define FWLAB_DMA_API_READ_PATTERN 0x4d
#define FWLAB_DMA_API_WRITE_PATTERN 0xb7

static bool fwlab_all(const u8 *bytes, size_t length, u8 value)
{
	size_t i;

	for (i = 0; i < length; i++)
		if (bytes[i] != value)
			return false;
	return true;
}

static int fwlab_dma_api_probe(struct pci_dev *pdev,
			       const struct pci_device_id *id)
{
	dma_addr_t dma_addr;
	void *cpu_addr;
	u8 buffer[64];
	int ret;

	(void)id;
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(48));
	if (ret)
		return ret;
	cpu_addr = dma_alloc_coherent(&pdev->dev, PAGE_SIZE, &dma_addr,
				      GFP_KERNEL);
	if (!cpu_addr)
		return -ENOMEM;

	memset(cpu_addr, FWLAB_DMA_API_READ_PATTERN, PAGE_SIZE);
	memset(buffer, 0, sizeof(buffer));
	ret = fwlab_m4_dma_transfer(&pdev->dev, dma_addr, buffer,
				    sizeof(buffer), FWLAB_M4_DMA_READ_HOST);
	if (ret || !fwlab_all(buffer, sizeof(buffer),
			      FWLAB_DMA_API_READ_PATTERN)) {
		ret = ret ? ret : -EIO;
		goto out_free;
	}

	memset(buffer, FWLAB_DMA_API_WRITE_PATTERN, sizeof(buffer));
	ret = fwlab_m4_dma_transfer(&pdev->dev, dma_addr + 128, buffer,
				    sizeof(buffer), FWLAB_M4_DMA_WRITE_HOST);
	if (ret || !fwlab_all((u8 *)cpu_addr + 128, sizeof(buffer),
			      FWLAB_DMA_API_WRITE_PATTERN)) {
		ret = ret ? ret : -EIO;
		goto out_free;
	}

	ret = 0;
out_free:
	dma_free_coherent(&pdev->dev, PAGE_SIZE, cpu_addr, dma_addr);
	if (!ret) {
		int stale = fwlab_m4_dma_transfer(
			&pdev->dev, dma_addr, buffer, sizeof(buffer),
			FWLAB_M4_DMA_READ_HOST);

		if (stale != -ENOENT)
			ret = -EIO;
	}
	if (!ret)
		dev_info(&pdev->dev,
			 "native DMA API/default-domain/revoke: PASS iova=%pad\n",
			 &dma_addr);
	return ret;
}

static void fwlab_dma_api_remove(struct pci_dev *pdev)
{
	(void)pdev;
}

static const struct pci_device_id fwlab_dma_api_ids[] = {
	{ PCI_DEVICE(FWLAB_M4_VENDOR_ID, FWLAB_M4_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, fwlab_dma_api_ids);

static struct pci_driver fwlab_dma_api_driver = {
	.name = FWLAB_DMA_API_LAB_NAME,
	.id_table = fwlab_dma_api_ids,
	.probe = fwlab_dma_api_probe,
	.remove = fwlab_dma_api_remove,
};
module_pci_driver(fwlab_dma_api_driver);

MODULE_DESCRIPTION("SSD FWLab native DMA API/default-domain observer");
MODULE_AUTHOR("Evanshenf and contributors");
MODULE_LICENSE("GPL");
