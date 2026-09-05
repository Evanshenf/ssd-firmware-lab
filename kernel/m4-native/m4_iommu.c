// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

/* Platform-style software IOMMU for the native firmware endpoint. */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/iommu.h>
#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/pci.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/version.h>
#include <linux/xarray.h>

#include "m4_internal.h"
#include "m4_dma_api.h"

#define FWLAB_M4_PTE_READ BIT(0)
#define FWLAB_M4_PTE_WRITE BIT(1)
#define FWLAB_M4_PTE_SHIFT 2

struct fwlab_m4_domain {
	struct iommu_domain domain;
	struct xarray pages;
	struct xarray mapping_ids;
	struct rw_semaphore authority;
	u64 nonce;
	unsigned long next_mapping_uid;
};

struct fwlab_m4_endpoint {
	struct device *dev;
	struct mutex attach_lock;
	struct iommu_domain *attached_domain;
	u64 attach_generation;
	u32 requester_id;
};

struct fwlab_m4_iommu_ctx {
	struct device *root_dev;
	struct iommu_device iommu;
	bool sysfs_added;
	bool registered;
};

static struct fwlab_m4_iommu_ctx fwlab_m4_iommu;
static atomic64_t fwlab_m4_domain_nonce = ATOMIC64_INIT(0);
static const struct iommu_domain_ops fwlab_m4_domain_ops;

static bool fwlab_m4_is_endpoint(struct device *dev)
{
	struct pci_dev *pdev;
	int domain;

	if (!dev_is_pci(dev))
		return false;
	pdev = to_pci_dev(dev);
	domain = pci_domain_nr(pdev->bus);
	return domain >= FWLAB_M4_DOMAIN_MIN &&
	       domain <= FWLAB_M4_DOMAIN_MAX &&
	       pdev->bus->number == FWLAB_M4_BUS_NR &&
	       pdev->devfn == FWLAB_M4_DEVFN &&
	       pdev->vendor == FWLAB_M4_VENDOR_ID &&
	       pdev->device == FWLAB_M4_DEVICE_ID &&
	       (pdev->class == FWLAB_M4_CLASS_CODE ||
		pdev->class == FWLAB_M4_NVME_CLASS_CODE);
}

static struct fwlab_m4_domain *
fwlab_m4_to_domain(struct iommu_domain *domain)
{
	return container_of(domain, struct fwlab_m4_domain, domain);
}

static void *fwlab_m4_pte_encode(phys_addr_t paddr, int prot)
{
	unsigned long value = PHYS_PFN(paddr) << FWLAB_M4_PTE_SHIFT;

	if (prot & IOMMU_READ)
		value |= FWLAB_M4_PTE_READ;
	if (prot & IOMMU_WRITE)
		value |= FWLAB_M4_PTE_WRITE;
	return xa_mk_value(value);
}

static phys_addr_t fwlab_m4_pte_decode(void *entry)
{
	return PFN_PHYS(xa_to_value(entry) >> FWLAB_M4_PTE_SHIFT);
}

static int fwlab_m4_pte_prot(void *entry)
{
	unsigned long value = xa_to_value(entry);
	int prot = 0;

	if (value & FWLAB_M4_PTE_READ)
		prot |= IOMMU_READ;
	if (value & FWLAB_M4_PTE_WRITE)
		prot |= IOMMU_WRITE;
	return prot;
}

static int fwlab_m4_attach_common(struct iommu_domain *domain,
				  struct device *dev)
{
	struct fwlab_m4_endpoint *endpoint = dev_iommu_priv_get(dev);

	if (!endpoint || endpoint->dev != dev || !fwlab_m4_is_endpoint(dev))
		return -ENODEV;

	mutex_lock(&endpoint->attach_lock);
	if (endpoint->attached_domain != domain) {
		if (endpoint->attach_generation == U64_MAX) {
			mutex_unlock(&endpoint->attach_lock);
			return -EOVERFLOW;
		}
		endpoint->attach_generation++;
	}
	endpoint->attached_domain = domain;
	mutex_unlock(&endpoint->attach_lock);
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
static int fwlab_m4_domain_attach(struct iommu_domain *domain,
				  struct device *dev,
				  struct iommu_domain *old)
{
	(void)old;
	return fwlab_m4_attach_common(domain, dev);
}
#else
static int fwlab_m4_domain_attach(struct iommu_domain *domain,
				  struct device *dev)
{
	return fwlab_m4_attach_common(domain, dev);
}
#endif

static int fwlab_m4_map_pages(struct iommu_domain *domain,
			      unsigned long iova, phys_addr_t paddr,
			      size_t pgsize, size_t pgcount, int prot,
			      gfp_t gfp, size_t *mapped)
{
	struct fwlab_m4_domain *fwdom = fwlab_m4_to_domain(domain);
	unsigned long first;
	unsigned long last;
	phys_addr_t paddr_last;
	size_t size;
	size_t done;
	int ret = 0;

	if (mapped)
		*mapped = 0;
	if (pgsize != PAGE_SIZE || !pgcount ||
	    !IS_ALIGNED(iova, PAGE_SIZE) || !IS_ALIGNED(paddr, PAGE_SIZE) ||
	    !(prot & (IOMMU_READ | IOMMU_WRITE)) ||
	    check_mul_overflow(pgsize, pgcount, &size) ||
	    check_add_overflow(iova, size - 1, &last) ||
	    check_add_overflow(paddr, size - 1, &paddr_last) ||
	    last > domain->geometry.aperture_end)
		return -EINVAL;

	down_write(&fwdom->authority);
	if (fwdom->next_mapping_uid >= LONG_MAX) {
		up_write(&fwdom->authority);
		return -EOVERFLOW;
	}
	fwdom->next_mapping_uid++;
	first = iova >> PAGE_SHIFT;
	for (done = 0; done < pgcount; done++) {
		void *entry = fwlab_m4_pte_encode(paddr + done * PAGE_SIZE,
						  prot);

		ret = xa_insert(&fwdom->pages, first + done, entry, gfp);
		if (ret)
			break;
		ret = xa_insert(&fwdom->mapping_ids, first + done,
				xa_mk_value(fwdom->next_mapping_uid), gfp);
		if (ret) {
			xa_erase(&fwdom->pages, first + done);
			break;
		}
	}
	if (ret) {
		while (done) {
			done--;
			xa_erase(&fwdom->pages, first + done);
			xa_erase(&fwdom->mapping_ids, first + done);
		}
		up_write(&fwdom->authority);
		return ret;
	}

	if (mapped)
		*mapped = size;
	up_write(&fwdom->authority);
	pr_info(FWLAB_M4_IOMMU_NAME
		": map iova=%#lx paddr=%pa size=%zu prot=%#x\n",
		iova, &paddr, size, prot);
	return 0;
}

static size_t fwlab_m4_unmap_pages(struct iommu_domain *domain,
				   unsigned long iova, size_t pgsize,
				   size_t pgcount,
				   struct iommu_iotlb_gather *gather)
{
	struct fwlab_m4_domain *fwdom = fwlab_m4_to_domain(domain);
	unsigned long first;
	size_t done;

	(void)gather;
	if (pgsize != PAGE_SIZE || !pgcount ||
	    !IS_ALIGNED(iova, PAGE_SIZE))
		return 0;

	down_write(&fwdom->authority);
	first = iova >> PAGE_SHIFT;
	for (done = 0; done < pgcount; done++) {
		if (!xa_erase(&fwdom->pages, first + done))
			break;
		xa_erase(&fwdom->mapping_ids, first + done);
	}
	up_write(&fwdom->authority);
	if (done)
		pr_info(FWLAB_M4_IOMMU_NAME
			": unmap iova=%#lx size=%zu\n", iova,
			done * PAGE_SIZE);
	return done * PAGE_SIZE;
}

static phys_addr_t fwlab_m4_iova_to_phys(struct iommu_domain *domain,
					 dma_addr_t iova)
{
	struct fwlab_m4_domain *fwdom = fwlab_m4_to_domain(domain);
	void *entry;
	phys_addr_t paddr = 0;

	down_read(&fwdom->authority);
	rcu_read_lock();
	entry = xa_load(&fwdom->pages, iova >> PAGE_SHIFT);
	if (xa_is_value(entry))
		paddr = fwlab_m4_pte_decode(entry) + offset_in_page(iova);
	rcu_read_unlock();
	up_read(&fwdom->authority);
	return paddr;
}

int fwlab_m4_dma_transfer(struct device *dev, dma_addr_t iova, void *buffer,
			  size_t length,
			  enum fwlab_m4_dma_direction direction)
{
	struct fwlab_m4_endpoint *endpoint = dev_iommu_priv_get(dev);
	struct fwlab_m4_domain *fwdom;
	struct iommu_domain *domain;
	dma_addr_t cursor;
	size_t remaining;
	size_t copied;
	int required;
	int ret = 0;

	if (!endpoint || endpoint->dev != dev || !buffer || !length ||
	    length > FWLAB_M4_DMA_MAX_BYTES ||
	    check_add_overflow(iova, length - 1, &cursor))
		return -EINVAL;
	if (direction == FWLAB_M4_DMA_READ_HOST)
		required = IOMMU_READ;
	else if (direction == FWLAB_M4_DMA_WRITE_HOST)
		required = IOMMU_WRITE;
	else
		return -EINVAL;

	mutex_lock(&endpoint->attach_lock);
	domain = endpoint->attached_domain;
	if (!domain || domain->ops != &fwlab_m4_domain_ops) {
		ret = -EACCES;
		goto out_endpoint;
	}
	fwdom = fwlab_m4_to_domain(domain);
	down_read(&fwdom->authority);

	/* Preflight the whole range so a hole cannot cause a partial transfer. */
	cursor = iova;
	remaining = length;
	rcu_read_lock();
	while (remaining) {
		void *entry = xa_load(&fwdom->pages, cursor >> PAGE_SHIFT);
		phys_addr_t paddr;
		size_t chunk;

		if (!xa_is_value(entry)) {
			ret = -ENOENT;
			break;
		}
		if (!(fwlab_m4_pte_prot(entry) & required)) {
			ret = -EACCES;
			break;
		}
		paddr = fwlab_m4_pte_decode(entry);
		if (!pfn_valid(PHYS_PFN(paddr))) {
			ret = -ERANGE;
			break;
		}
		chunk = min_t(size_t, remaining,
				      PAGE_SIZE - offset_in_page(cursor));
		cursor += chunk;
		remaining -= chunk;
	}
	rcu_read_unlock();
	if (ret)
		goto out_authority;

	cursor = iova;
	remaining = length;
	copied = 0;
	while (remaining) {
		void *entry;
		struct page *page;
		void *vaddr;
		size_t offset = offset_in_page(cursor);
		size_t chunk = min_t(size_t, remaining, PAGE_SIZE - offset);

		rcu_read_lock();
		entry = xa_load(&fwdom->pages, cursor >> PAGE_SHIFT);
		page = pfn_to_page(PHYS_PFN(fwlab_m4_pte_decode(entry)));
		rcu_read_unlock();
		vaddr = kmap_local_page(page);
		if (direction == FWLAB_M4_DMA_READ_HOST)
			memcpy((u8 *)buffer + copied, (u8 *)vaddr + offset,
			       chunk);
		else
			memcpy((u8 *)vaddr + offset, (u8 *)buffer + copied,
			       chunk);
		kunmap_local(vaddr);

		cursor += chunk;
		remaining -= chunk;
		copied += chunk;
	}

out_authority:
	up_read(&fwdom->authority);
out_endpoint:
	mutex_unlock(&endpoint->attach_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(fwlab_m4_dma_transfer);

int fwlab_m4_mapping_capture(struct device *dev, dma_addr_t iova, u32 length,
			     enum fwlab_m4_dma_direction direction,
			     struct fwlab_m4_mapping *mapping)
{
	struct fwlab_m4_endpoint *endpoint = dev_iommu_priv_get(dev);
	struct fwlab_m4_mapping candidate = {};
	struct fwlab_m4_domain *domain;
	dma_addr_t last;
	unsigned long first;
	u32 index;
	int required;
	int ret = 0;

	if (!endpoint || endpoint->dev != dev || !mapping || !length ||
	    length > 8192 || check_add_overflow(iova, length - 1, &last))
		return -EINVAL;
	required = direction == FWLAB_M4_DMA_READ_HOST ? IOMMU_READ :
		   direction == FWLAB_M4_DMA_WRITE_HOST ? IOMMU_WRITE : 0;
	if (!required)
		return -EINVAL;
	candidate.pages = (last >> PAGE_SHIFT) - (iova >> PAGE_SHIFT) + 1;
	if (candidate.pages > FWLAB_M4_MAPPING_PAGES)
		return -E2BIG;
	mutex_lock(&endpoint->attach_lock);
	if (!endpoint->attached_domain ||
	    endpoint->attached_domain->ops != &fwlab_m4_domain_ops) {
		ret = -EACCES;
		goto out_endpoint;
	}
	domain = fwlab_m4_to_domain(endpoint->attached_domain);
	down_read(&domain->authority);
	candidate.domain_nonce = domain->nonce;
	candidate.attach_generation = endpoint->attach_generation;
	candidate.iova = iova;
	candidate.length = length;
	candidate.direction = direction;
	first = iova >> PAGE_SHIFT;
	for (index = 0; index < candidate.pages; index++) {
		void *pte = xa_load(&domain->pages, first + index);
		void *uid = xa_load(&domain->mapping_ids, first + index);

		if (!xa_is_value(pte) || !xa_is_value(uid) ||
		    !(fwlab_m4_pte_prot(pte) & required) ||
		    !pfn_valid(PHYS_PFN(fwlab_m4_pte_decode(pte)))) {
			ret = -EACCES;
			break;
		}
		candidate.pte[index] = xa_to_value(pte);
		candidate.mapping_uid[index] = xa_to_value(uid);
	}
	up_read(&domain->authority);
	if (!ret)
		*mapping = candidate;
out_endpoint:
	mutex_unlock(&endpoint->attach_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(fwlab_m4_mapping_capture);

int fwlab_m4_mapping_copy(struct device *dev,
			  const struct fwlab_m4_mapping *mapping, u32 offset,
			  void *buffer, u32 length,
			  const struct fwlab_m4_copy_guard *guard)
{
	struct fwlab_m4_endpoint *endpoint = dev_iommu_priv_get(dev);
	struct fwlab_m4_domain *domain;
	u64 cursor;
	unsigned long first;
	unsigned long flags;
	u32 index, copied = 0;
	int ret = 0;

	if (!endpoint || endpoint->dev != dev || !mapping || !buffer || !length ||
	    !guard || !guard->lock || !guard->valid_locked ||
	    !mapping->pages || mapping->pages > FWLAB_M4_MAPPING_PAGES ||
	    offset > mapping->length || length > mapping->length - offset ||
	    (mapping->direction != FWLAB_M4_DMA_READ_HOST &&
	     mapping->direction != FWLAB_M4_DMA_WRITE_HOST))
		return -EINVAL;
	mutex_lock(&endpoint->attach_lock);
	if (!endpoint->attached_domain ||
	    endpoint->attached_domain->ops != &fwlab_m4_domain_ops ||
	    endpoint->attach_generation != mapping->attach_generation) {
		ret = -ESTALE;
		goto out_endpoint;
	}
	domain = fwlab_m4_to_domain(endpoint->attached_domain);
	down_read(&domain->authority);
	if (domain->nonce != mapping->domain_nonce) {
		ret = -ESTALE;
		goto out_domain;
	}
	spin_lock_irqsave(guard->lock, flags);
	if (!guard->valid_locked(guard->context)) {
		ret = -ESTALE;
		goto out_guard;
	}
	first = mapping->iova >> PAGE_SHIFT;
	for (index = 0; index < mapping->pages; index++) {
		void *pte = xa_load(&domain->pages, first + index);
		void *uid = xa_load(&domain->mapping_ids, first + index);

		if (!xa_is_value(pte) || !xa_is_value(uid) ||
		    xa_to_value(pte) != mapping->pte[index] ||
		    xa_to_value(uid) != mapping->mapping_uid[index]) {
			ret = -ESTALE;
			goto out_guard;
		}
	}
	cursor = mapping->iova + offset;
	while (copied < length) {
		u32 chunk = min_t(u32, length - copied,
				 PAGE_SIZE - offset_in_page(cursor));
		void *pte = xa_load(&domain->pages, cursor >> PAGE_SHIFT);
		struct page *page = pfn_to_page(PHYS_PFN(fwlab_m4_pte_decode(pte)));
		void *address = kmap_local_page(page);

		if (mapping->direction == FWLAB_M4_DMA_READ_HOST)
			memcpy((u8 *)buffer + copied,
			       (u8 *)address + offset_in_page(cursor), chunk);
		else
			memcpy((u8 *)address + offset_in_page(cursor),
			       (u8 *)buffer + copied, chunk);
		kunmap_local(address);
		cursor += chunk;
		copied += chunk;
	}
out_guard:
	spin_unlock_irqrestore(guard->lock, flags);
out_domain:
	up_read(&domain->authority);
out_endpoint:
	mutex_unlock(&endpoint->attach_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(fwlab_m4_mapping_copy);

static bool fwlab_m4_enforce_cache_coherency(struct iommu_domain *domain)
{
	(void)domain;
	return true;
}

static void fwlab_m4_domain_free(struct iommu_domain *domain)
{
	struct fwlab_m4_domain *fwdom = fwlab_m4_to_domain(domain);

	xa_destroy(&fwdom->pages);
	xa_destroy(&fwdom->mapping_ids);
	kfree(fwdom);
}

static const struct iommu_domain_ops fwlab_m4_domain_ops = {
	.attach_dev = fwlab_m4_domain_attach,
	.map_pages = fwlab_m4_map_pages,
	.unmap_pages = fwlab_m4_unmap_pages,
	.iova_to_phys = fwlab_m4_iova_to_phys,
	.enforce_cache_coherency = fwlab_m4_enforce_cache_coherency,
	.free = fwlab_m4_domain_free,
};

static const struct iommu_domain_ops fwlab_m4_blocked_ops = {
	.attach_dev = fwlab_m4_domain_attach,
};

static struct iommu_domain fwlab_m4_blocked_domain = {
	.type = IOMMU_DOMAIN_BLOCKED,
	.ops = &fwlab_m4_blocked_ops,
};

static struct iommu_domain *
fwlab_m4_domain_alloc_paging(struct device *dev)
{
	struct fwlab_m4_domain *fwdom;

	if (!dev_iommu_priv_get(dev) || !fwlab_m4_is_endpoint(dev))
		return ERR_PTR(-ENODEV);

	fwdom = kzalloc(sizeof(*fwdom), GFP_KERNEL);
	if (!fwdom)
		return ERR_PTR(-ENOMEM);
	xa_init_flags(&fwdom->pages, XA_FLAGS_LOCK_IRQ);
	xa_init_flags(&fwdom->mapping_ids, XA_FLAGS_LOCK_IRQ);
	init_rwsem(&fwdom->authority);
	fwdom->nonce = atomic64_inc_return(&fwlab_m4_domain_nonce);
	fwdom->domain.ops = &fwlab_m4_domain_ops;
	fwdom->domain.pgsize_bitmap = PAGE_SIZE;
	fwdom->domain.geometry.aperture_start = 0;
	fwdom->domain.geometry.aperture_end = DMA_BIT_MASK(48);
	fwdom->domain.geometry.force_aperture = true;
	return &fwdom->domain;
}

static bool fwlab_m4_capable(struct device *dev, enum iommu_cap cap)
{
	if (!dev_iommu_priv_get(dev) || !fwlab_m4_is_endpoint(dev))
		return false;
	return cap == IOMMU_CAP_CACHE_COHERENCY;
}

static struct iommu_device *fwlab_m4_probe_device(struct device *dev)
{
	struct fwlab_m4_endpoint *endpoint;

	if (!fwlab_m4_is_endpoint(dev))
		return ERR_PTR(-ENODEV);

	endpoint = kzalloc(sizeof(*endpoint), GFP_KERNEL);
	if (!endpoint)
		return ERR_PTR(-ENOMEM);
	endpoint->dev = dev;
	endpoint->requester_id = pci_dev_id(to_pci_dev(dev));
	mutex_init(&endpoint->attach_lock);
	dev_iommu_priv_set(dev, endpoint);
	return &fwlab_m4_iommu.iommu;
}

static void fwlab_m4_release_device(struct device *dev)
{
	struct fwlab_m4_endpoint *endpoint = dev_iommu_priv_get(dev);

	if (!endpoint)
		return;
	mutex_lock(&endpoint->attach_lock);
	endpoint->attached_domain = NULL;
	mutex_unlock(&endpoint->attach_lock);
	dev_iommu_priv_set(dev, NULL);
	kfree(endpoint);
}

static const struct iommu_ops fwlab_m4_iommu_ops = {
	.capable = fwlab_m4_capable,
	.domain_alloc_paging = fwlab_m4_domain_alloc_paging,
	.probe_device = fwlab_m4_probe_device,
	.release_device = fwlab_m4_release_device,
	.device_group = pci_device_group,
	.owner = THIS_MODULE,
	.blocked_domain = &fwlab_m4_blocked_domain,
	.release_domain = &fwlab_m4_blocked_domain,
#if LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
	.pgsize_bitmap = PAGE_SIZE,
#endif
};

static int __init fwlab_m4_iommu_init(void)
{
	int ret;

	fwlab_m4_iommu.root_dev = root_device_register(FWLAB_M4_IOMMU_NAME);
	if (IS_ERR(fwlab_m4_iommu.root_dev))
		return PTR_ERR(fwlab_m4_iommu.root_dev);

	ret = iommu_device_sysfs_add(&fwlab_m4_iommu.iommu,
				     fwlab_m4_iommu.root_dev, NULL,
				     "%s", FWLAB_M4_IOMMU_NAME);
	if (ret)
		goto err_root;
	fwlab_m4_iommu.sysfs_added = true;

	/* NULL hwdev selects the standard platform-IOMMU (no-fwspec) path. */
	ret = iommu_device_register(&fwlab_m4_iommu.iommu,
				    &fwlab_m4_iommu_ops, NULL);
	if (ret)
		goto err_sysfs;
	fwlab_m4_iommu.registered = true;
	pr_info(FWLAB_M4_IOMMU_NAME ": registered platform software IOMMU\n");
	return 0;

err_sysfs:
	iommu_device_sysfs_remove(&fwlab_m4_iommu.iommu);
	fwlab_m4_iommu.sysfs_added = false;
err_root:
	root_device_unregister(fwlab_m4_iommu.root_dev);
	fwlab_m4_iommu.root_dev = NULL;
	return ret;
}

static void __exit fwlab_m4_iommu_exit(void)
{
	if (fwlab_m4_iommu.registered) {
		iommu_device_unregister(&fwlab_m4_iommu.iommu);
		fwlab_m4_iommu.registered = false;
	}
	if (fwlab_m4_iommu.sysfs_added) {
		iommu_device_sysfs_remove(&fwlab_m4_iommu.iommu);
		fwlab_m4_iommu.sysfs_added = false;
	}
	if (fwlab_m4_iommu.root_dev) {
		root_device_unregister(fwlab_m4_iommu.root_dev);
		fwlab_m4_iommu.root_dev = NULL;
	}
	pr_info(FWLAB_M4_IOMMU_NAME ": removed\n");
}

module_init(fwlab_m4_iommu_init);
module_exit(fwlab_m4_iommu_exit);

MODULE_DESCRIPTION("SSD FWLab native software IOMMU");
MODULE_AUTHOR("Evanshenf and contributors");
MODULE_LICENSE("GPL");
