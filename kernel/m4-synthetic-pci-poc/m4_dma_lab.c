// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

/* Private paging-domain and software-DMA observer for M4-B. */

#include <linux/highmem.h>
#include <linux/iommu.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/version.h>

#include "m4_internal.h"
#include "m4_dma_api.h"

#define FWLAB_DMA_LAB_NAME "ssd_fwlab_dma_lab"
#define FWLAB_DMA_IOVA_READ 0x00100000UL
#define FWLAB_DMA_IOVA_WRITE 0x00200000UL
#define FWLAB_DMA_PATTERN_A 0x31
#define FWLAB_DMA_PATTERN_B 0x72
#define FWLAB_DMA_PATTERN_WRITE 0xa6
#define FWLAB_DMA_CANARY 0xcc

static void fwlab_page_fill(struct page *page, u8 value)
{
	void *vaddr = kmap_local_page(page);

	memset(vaddr, value, PAGE_SIZE);
	kunmap_local(vaddr);
}

static bool fwlab_page_all(struct page *page, u8 value)
{
	void *vaddr = kmap_local_page(page);
	u8 *bytes = vaddr;
	size_t i;
	bool match = true;

	for (i = 0; i < PAGE_SIZE; i++) {
		if (bytes[i] != value) {
			match = false;
			break;
		}
	}
	kunmap_local(vaddr);
	return match;
}

static bool fwlab_page_prefix_only(struct page *page, size_t prefix,
				   u8 value)
{
	void *vaddr = kmap_local_page(page);
	u8 *bytes = vaddr;
	size_t i;
	bool match = true;

	for (i = 0; i < PAGE_SIZE; i++) {
		u8 expected = i < prefix ? value : 0;

		if (bytes[i] != expected) {
			match = false;
			break;
		}
	}
	kunmap_local(vaddr);
	return match;
}

static bool fwlab_bytes_all(const u8 *bytes, size_t length, u8 value)
{
	size_t i;

	for (i = 0; i < length; i++)
		if (bytes[i] != value)
			return false;
	return true;
}

static struct iommu_domain *fwlab_dma_domain_alloc(struct device *dev)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
	return iommu_paging_domain_alloc(dev);
#else
	struct iommu_domain *domain = iommu_domain_alloc(dev->bus);

	return domain ? domain : ERR_PTR(-ENOMEM);
#endif
}

static int fwlab_dma_lab_probe(struct pci_dev *pdev,
			       const struct pci_device_id *id)
{
	struct iommu_domain *domain;
	struct page *page_a = NULL;
	struct page *page_b = NULL;
	struct page *page_write = NULL;
	u8 buffer[64];
	bool attached = false;
	bool mapped_read = false;
	bool mapped_write = false;
	int ret;

	(void)id;
	domain = fwlab_dma_domain_alloc(&pdev->dev);
	if (IS_ERR(domain))
		return PTR_ERR(domain);

	page_a = alloc_page(GFP_KERNEL | __GFP_ZERO);
	page_b = alloc_page(GFP_KERNEL | __GFP_ZERO);
	page_write = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page_a || !page_b || !page_write) {
		ret = -ENOMEM;
		goto out;
	}
	fwlab_page_fill(page_a, FWLAB_DMA_PATTERN_A);
	fwlab_page_fill(page_b, FWLAB_DMA_PATTERN_B);
	fwlab_page_fill(page_write, 0);

	ret = iommu_attach_device(domain, &pdev->dev);
	if (ret)
		goto out;
	attached = true;

	ret = iommu_map(domain, FWLAB_DMA_IOVA_READ, page_to_phys(page_a),
			PAGE_SIZE, IOMMU_READ, GFP_KERNEL);
	if (ret)
		goto out;
	mapped_read = true;

	memset(buffer, 0, sizeof(buffer));
	ret = fwlab_m4_dma_transfer(&pdev->dev, FWLAB_DMA_IOVA_READ,
				    buffer, sizeof(buffer),
				    FWLAB_M4_DMA_READ_HOST);
	if (ret || !fwlab_bytes_all(buffer, sizeof(buffer),
				    FWLAB_DMA_PATTERN_A)) {
		ret = ret ? ret : -EIO;
		goto out;
	}

	memset(buffer, FWLAB_DMA_PATTERN_WRITE, sizeof(buffer));
	ret = fwlab_m4_dma_transfer(&pdev->dev, FWLAB_DMA_IOVA_READ,
				    buffer, sizeof(buffer),
				    FWLAB_M4_DMA_WRITE_HOST);
	if (ret != -EACCES || !fwlab_page_all(page_a, FWLAB_DMA_PATTERN_A)) {
		ret = -EIO;
		goto out;
	}

	ret = iommu_map(domain, FWLAB_DMA_IOVA_WRITE,
			page_to_phys(page_write), PAGE_SIZE, IOMMU_WRITE,
			GFP_KERNEL);
	if (ret)
		goto out;
	mapped_write = true;

	ret = fwlab_m4_dma_transfer(&pdev->dev, FWLAB_DMA_IOVA_WRITE,
				    buffer, sizeof(buffer),
				    FWLAB_M4_DMA_WRITE_HOST);
	if (ret || !fwlab_page_prefix_only(page_write, sizeof(buffer),
					   FWLAB_DMA_PATTERN_WRITE)) {
		ret = ret ? ret : -EIO;
		goto out;
	}
	ret = fwlab_m4_dma_transfer(&pdev->dev, FWLAB_DMA_IOVA_WRITE,
				    buffer, sizeof(buffer),
				    FWLAB_M4_DMA_READ_HOST);
	if (ret != -EACCES) {
		ret = -EIO;
		goto out;
	}

	memset(buffer, FWLAB_DMA_CANARY, sizeof(buffer));
	ret = fwlab_m4_dma_transfer(&pdev->dev,
				    FWLAB_DMA_IOVA_READ + PAGE_SIZE - 16,
				    buffer, 32, FWLAB_M4_DMA_READ_HOST);
	if (ret != -ENOENT ||
	    !fwlab_bytes_all(buffer, sizeof(buffer), FWLAB_DMA_CANARY)) {
		ret = -EIO;
		goto out;
	}

	if (iommu_unmap(domain, FWLAB_DMA_IOVA_READ, PAGE_SIZE) != PAGE_SIZE) {
		ret = -EIO;
		goto out;
	}
	mapped_read = false;
	ret = fwlab_m4_dma_transfer(&pdev->dev, FWLAB_DMA_IOVA_READ,
				    buffer, sizeof(buffer),
				    FWLAB_M4_DMA_READ_HOST);
	if (ret != -ENOENT) {
		ret = -EIO;
		goto out;
	}

	ret = iommu_map(domain, FWLAB_DMA_IOVA_READ, page_to_phys(page_b),
			PAGE_SIZE, IOMMU_READ, GFP_KERNEL);
	if (ret)
		goto out;
	mapped_read = true;
	memset(buffer, 0, sizeof(buffer));
	ret = fwlab_m4_dma_transfer(&pdev->dev, FWLAB_DMA_IOVA_READ,
				    buffer, sizeof(buffer),
				    FWLAB_M4_DMA_READ_HOST);
	if (ret || !fwlab_bytes_all(buffer, sizeof(buffer),
				    FWLAB_DMA_PATTERN_B)) {
		ret = ret ? ret : -EIO;
		goto out;
	}

	ret = 0;
out:
	if (mapped_read &&
	    iommu_unmap(domain, FWLAB_DMA_IOVA_READ, PAGE_SIZE) != PAGE_SIZE)
		ret = ret ? ret : -EIO;
	if (mapped_write &&
	    iommu_unmap(domain, FWLAB_DMA_IOVA_WRITE, PAGE_SIZE) != PAGE_SIZE)
		ret = ret ? ret : -EIO;
	if (attached) {
		iommu_detach_device(domain, &pdev->dev);
		if (!ret) {
			int blocked_ret = fwlab_m4_dma_transfer(
				&pdev->dev, FWLAB_DMA_IOVA_READ, buffer,
				sizeof(buffer), FWLAB_M4_DMA_READ_HOST);

			if (blocked_ret != -ENOENT)
				ret = -EIO;
		}
	}
	iommu_domain_free(domain);
	if (page_write)
		__free_page(page_write);
	if (page_b)
		__free_page(page_b);
	if (page_a)
		__free_page(page_a);

	if (!ret)
		dev_info(&pdev->dev,
			 "M4-B paging/permission/hole/remap/revoke DMA: PASS\n");
	return ret;
}

static void fwlab_dma_lab_remove(struct pci_dev *pdev)
{
	(void)pdev;
}

static const struct pci_device_id fwlab_dma_lab_ids[] = {
	{ PCI_DEVICE(FWLAB_M4_VENDOR_ID, FWLAB_M4_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, fwlab_dma_lab_ids);

static struct pci_driver fwlab_dma_lab_driver = {
	.name = FWLAB_DMA_LAB_NAME,
	.id_table = fwlab_dma_lab_ids,
	.probe = fwlab_dma_lab_probe,
	.remove = fwlab_dma_lab_remove,
};
module_pci_driver(fwlab_dma_lab_driver);

MODULE_DESCRIPTION("SSD FWLab private software-IOMMU/DMA observer");
MODULE_AUTHOR("Evanshenf and contributors");
MODULE_LICENSE("GPL");
