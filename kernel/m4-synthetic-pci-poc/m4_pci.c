// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/version.h>
#include <linux/iommu.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irq_work.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
#include <linux/irqchip/irq-msi-lib.h>
#endif
#include <linux/kthread.h>
#include <linux/msi.h>
#include <linux/module.h>
#include <linux/numa.h>
#include <linux/pci.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
#include <linux/unaligned.h>
#else
#include <asm/unaligned.h>
#endif

#include "m4_internal.h"
#include "m4_nvme.h"

static unsigned long long bar_start = FWLAB_M4_BAR_DEFAULT_START;
module_param(bar_start, ullong, 0444);
MODULE_PARM_DESC(bar_start, "Boot-reserved physical BAR0 start");

static unsigned long long bar_size = FWLAB_M4_BAR_SIZE;
module_param(bar_size, ullong, 0444);
MODULE_PARM_DESC(bar_size, "Boot-reserved physical BAR0 size");

static bool nvme_mode;
module_param(nvme_mode, bool, 0444);
MODULE_PARM_DESC(nvme_mode, "Expose the bounded native-NVMe transport fixture");

static void fwlab_m4_irq_work(struct irq_work *work)
{
	struct fwlab_m4_pci_ctx *ctx = container_of(
		work, struct fwlab_m4_pci_ctx, irq_work);
	unsigned int virq = READ_ONCE(ctx->pending_virq);

	if (virq) {
		int ret;

		pr_debug(FWLAB_M4_PCI_NAME ": irq_work enter virq=%u\n", virq);
		ret = generic_handle_irq(virq);
		pr_debug(FWLAB_M4_PCI_NAME
			": irq_work exit virq=%u ret=%d\n", virq, ret);
	}
}

void fwlab_m4_raise_msix(struct fwlab_m4_pci_ctx *ctx)
{
	unsigned long flags;
	unsigned int virq = 0;
	u32 vector_ctrl;
	u16 msix_flags;

	spin_lock_irqsave(&ctx->config_lock, flags);
	msix_flags = get_unaligned_le16(
		&ctx->config[FWLAB_M4_MSIX_CAP + PCI_MSIX_FLAGS]);
	vector_ctrl = readl(ctx->bar_mapping + FWLAB_M4_MSIX_TABLE_OFFSET +
			    PCI_MSIX_ENTRY_VECTOR_CTRL);
	if (ctx->pdev && ctx->pdev->msix_enabled &&
	    !(msix_flags & PCI_MSIX_FLAGS_MASKALL) &&
	    !(vector_ctrl & PCI_MSIX_ENTRY_CTRL_MASKBIT))
		virq = msi_get_virq(&ctx->pdev->dev, 0);
	spin_unlock_irqrestore(&ctx->config_lock, flags);

	if (virq) {
		WRITE_ONCE(ctx->pending_virq, virq);
		irq_work_queue(&ctx->irq_work);
	}
}

static bool fwlab_m4_bar_service_valid(struct fwlab_m4_pci_ctx *ctx,
				       u32 offset, size_t length)
{
	return ctx && ctx->bar_mapping && length &&
	       length <= FWLAB_M4_BAR_MAP_SIZE &&
	       offset <= FWLAB_M4_BAR_MAP_SIZE - length;
}

static u32 fwlab_m4_service_bar_read32(void *context, u32 offset)
{
	struct fwlab_m4_pci_ctx *ctx = context;

	if (WARN_ON_ONCE(!IS_ALIGNED(offset, sizeof(u32)) ||
			 !fwlab_m4_bar_service_valid(ctx, offset, sizeof(u32))))
		return 0;
	return readl(ctx->bar_mapping + offset);
}

static u64 fwlab_m4_service_bar_read64(void *context, u32 offset)
{
	struct fwlab_m4_pci_ctx *ctx = context;

	if (WARN_ON_ONCE(!IS_ALIGNED(offset, sizeof(u64)) ||
			 !fwlab_m4_bar_service_valid(ctx, offset, sizeof(u64))))
		return 0;
	return readq(ctx->bar_mapping + offset);
}

static void fwlab_m4_service_bar_write32(void *context, u32 offset, u32 value)
{
	struct fwlab_m4_pci_ctx *ctx = context;

	if (WARN_ON_ONCE(!IS_ALIGNED(offset, sizeof(u32)) ||
			 !fwlab_m4_bar_service_valid(ctx, offset, sizeof(u32))))
		return;
	writel(value, ctx->bar_mapping + offset);
}

static void fwlab_m4_service_bar_write64(void *context, u32 offset, u64 value)
{
	struct fwlab_m4_pci_ctx *ctx = context;

	if (WARN_ON_ONCE(!IS_ALIGNED(offset, sizeof(u64)) ||
			 !fwlab_m4_bar_service_valid(ctx, offset, sizeof(u64))))
		return;
	writeq(value, ctx->bar_mapping + offset);
}

static void fwlab_m4_service_bar_zero(void *context, u32 offset, size_t length)
{
	struct fwlab_m4_pci_ctx *ctx = context;

	if (WARN_ON_ONCE(!fwlab_m4_bar_service_valid(ctx, offset, length)))
		return;
	memset_io(ctx->bar_mapping + offset, 0, length);
}

static int fwlab_m4_service_dma(void *context, dma_addr_t iova, void *buffer,
				size_t length,
				enum fwlab_m4_dma_direction direction)
{
	struct fwlab_m4_pci_ctx *ctx = context;

	if (!ctx || !ctx->pdev)
		return -ENODEV;
	return fwlab_m4_dma_transfer(&ctx->pdev->dev, iova, buffer, length,
				     direction);
}

static void fwlab_m4_service_notify(void *context, unsigned int vector)
{
	struct fwlab_m4_pci_ctx *ctx = context;

	if (WARN_ON_ONCE(vector != 0))
		return;
	fwlab_m4_raise_msix(ctx);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
static void fwlab_m4_compose_msi_msg(struct irq_data *data,
				     struct msi_msg *msg)
{
	struct irq_data *parent = data->parent_data;
	struct irq_chip *chip = parent ? irq_data_get_irq_chip(parent) : NULL;

	memset(msg, 0, sizeof(*msg));
	if (chip && chip->irq_compose_msi_msg)
		chip->irq_compose_msi_msg(parent, msg);
}

static void fwlab_m4_irq_noop(struct irq_data *data)
{
	(void)data;
}

static void fwlab_m4_irq_set_mask(struct irq_data *data, bool masked)
{
	struct fwlab_m4_pci_ctx *ctx = irq_data_get_irq_chip_data(data);
	unsigned long flags;
	void __iomem *ctrl;
	u32 value;

	if (!ctx || data->hwirq >= 1)
		return;
	ctrl = ctx->bar_mapping + FWLAB_M4_MSIX_TABLE_OFFSET +
	       data->hwirq * PCI_MSIX_ENTRY_SIZE + PCI_MSIX_ENTRY_VECTOR_CTRL;
	spin_lock_irqsave(&ctx->config_lock, flags);
	value = readl(ctrl);
	if (masked)
		value |= PCI_MSIX_ENTRY_CTRL_MASKBIT;
	else
		value &= ~PCI_MSIX_ENTRY_CTRL_MASKBIT;
	writel(value, ctrl);
	wmb();
	spin_unlock_irqrestore(&ctx->config_lock, flags);
	pr_info(FWLAB_M4_PCI_NAME ": MSI-X hwirq=%lu mask=%u ctrl=%#x\n",
		(unsigned long)data->hwirq, masked, value);
}

static void fwlab_m4_irq_mask(struct irq_data *data)
{
	fwlab_m4_irq_set_mask(data, true);
}

static void fwlab_m4_irq_unmask(struct irq_data *data)
{
	fwlab_m4_irq_set_mask(data, false);
}

static void fwlab_m4_irq_mask_child(struct irq_data *data)
{
	if (data->parent_data)
		fwlab_m4_irq_mask(data->parent_data);
}

static void fwlab_m4_irq_unmask_child(struct irq_data *data)
{
	if (data->parent_data)
		fwlab_m4_irq_unmask(data->parent_data);
}

static struct irq_chip fwlab_m4_msi_chip = {
	.name = "SSD FWLab MSI",
	.irq_compose_msi_msg = fwlab_m4_compose_msi_msg,
	.irq_ack = fwlab_m4_irq_noop,
	.irq_eoi = fwlab_m4_irq_noop,
	.irq_mask = fwlab_m4_irq_mask,
	.irq_unmask = fwlab_m4_irq_unmask,
	.flags = IRQCHIP_SKIP_SET_WAKE,
};

static int fwlab_m4_irq_domain_alloc(struct irq_domain *domain,
				     unsigned int virq,
				     unsigned int nr_irqs, void *arg)
{
	unsigned int i;

	(void)arg;
	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_hwirq_and_chip(domain, virq + i, i,
					      &fwlab_m4_msi_chip,
					      domain->host_data);
		__irq_set_handler(virq + i, handle_simple_irq, 0,
				  "ssd-fwlab-simple");
	}
	return 0;
}

static void fwlab_m4_irq_domain_free(struct irq_domain *domain,
				     unsigned int virq,
				     unsigned int nr_irqs)
{
	irq_domain_free_irqs_common(domain, virq, nr_irqs);
}

static const struct irq_domain_ops fwlab_m4_irq_domain_ops = {
	.alloc = fwlab_m4_irq_domain_alloc,
	.free = fwlab_m4_irq_domain_free,
};

static bool fwlab_m4_init_dev_msi_info(struct device *dev,
				       struct irq_domain *domain,
				       struct irq_domain *real_parent,
				       struct msi_domain_info *info)
{
	if (!msi_lib_init_dev_msi_info(dev, domain, real_parent, info))
		return false;
	info->chip->irq_disable = fwlab_m4_irq_mask_child;
	info->chip->irq_enable = fwlab_m4_irq_unmask_child;
	info->chip->irq_mask = fwlab_m4_irq_mask_child;
	info->chip->irq_unmask = fwlab_m4_irq_unmask_child;
	info->handler = handle_simple_irq;
	info->handler_name = "ssd-fwlab-simple";
	return true;
}

#define FWLAB_M4_MSI_REQUIRED_FLAGS \
	(MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS | \
	 MSI_FLAG_NO_AFFINITY)
#define FWLAB_M4_MSI_SUPPORTED_FLAGS \
	(MSI_FLAG_PCI_MSIX | MSI_FLAG_PCI_MSIX_ALLOC_DYN | \
	 MSI_FLAG_NO_AFFINITY | MSI_GENERIC_FLAGS_MASK)

static const struct msi_parent_ops fwlab_m4_msi_parent_ops = {
	.required_flags = FWLAB_M4_MSI_REQUIRED_FLAGS,
	.supported_flags = FWLAB_M4_MSI_SUPPORTED_FLAGS,
	.bus_select_token = DOMAIN_BUS_PCI_MSI,
	.prefix = "SSD-FWLab-",
	.init_dev_msi_info = fwlab_m4_init_dev_msi_info,
};

static int fwlab_m4_create_msi_domain(struct fwlab_m4_pci_ctx *ctx)
{
	struct irq_domain_info info;

	ctx->msi_fwnode = irq_domain_alloc_named_fwnode("ssd-fwlab-msi");
	if (!ctx->msi_fwnode)
		return -ENOMEM;

	memset(&info, 0, sizeof(info));
	info.fwnode = ctx->msi_fwnode;
	info.ops = &fwlab_m4_irq_domain_ops;
	info.host_data = ctx;
	info.parent = NULL;
	ctx->msi_domain = msi_create_parent_irq_domain(
		&info, &fwlab_m4_msi_parent_ops);
	if (!ctx->msi_domain) {
		irq_domain_free_fwnode(ctx->msi_fwnode);
		ctx->msi_fwnode = NULL;
		return -ENODEV;
	}
	ctx->msi_domain->flags |= IRQ_DOMAIN_FLAG_ISOLATED_MSI;
	dev_set_msi_domain(&ctx->bridge->dev, ctx->msi_domain);
	return 0;
}

static void fwlab_m4_destroy_msi_domain(struct fwlab_m4_pci_ctx *ctx)
{
	if (ctx->bridge)
		dev_set_msi_domain(&ctx->bridge->dev, NULL);
	if (ctx->msi_domain) {
		irq_domain_remove(ctx->msi_domain);
		ctx->msi_domain = NULL;
	}
	if (ctx->msi_fwnode) {
		irq_domain_free_fwnode(ctx->msi_fwnode);
		ctx->msi_fwnode = NULL;
	}
}
#else
static int fwlab_m4_create_msi_domain(struct fwlab_m4_pci_ctx *ctx)
{
	(void)ctx;
	return 0;
}

static void fwlab_m4_destroy_msi_domain(struct fwlab_m4_pci_ctx *ctx)
{
	(void)ctx;
}
#endif

static void fwlab_m4_bar_reset_locked(struct fwlab_m4_pci_ctx *ctx)
{
	ctx->bar_epoch++;
	if (ctx->nvme_mode) {
		fwlab_m4_frontend_reset(&ctx->frontend, ctx->bar_epoch);
		return;
	}
	writel(FWLAB_M4_BAR_SIGNATURE,
	       ctx->bar_mapping + FWLAB_M4_REG_SIGNATURE);
	writel(FWLAB_M4_BAR_VERSION, ctx->bar_mapping + FWLAB_M4_REG_VERSION);
	writel(ctx->bar_epoch, ctx->bar_mapping + FWLAB_M4_REG_EPOCH);
	writel(0, ctx->bar_mapping + FWLAB_M4_REG_DOORBELL);
	writel(0, ctx->bar_mapping + FWLAB_M4_REG_ACK);
	writel(0, ctx->bar_mapping + FWLAB_M4_REG_IRQ_TRIGGER);
	writel(0, ctx->bar_mapping + FWLAB_M4_REG_IRQ_ACK);
	memset_io(ctx->bar_mapping + FWLAB_M4_MSIX_TABLE_OFFSET, 0,
		  FWLAB_M4_BAR_MAP_SIZE - FWLAB_M4_MSIX_TABLE_OFFSET);
	wmb();
}

static int fwlab_m4_bar_worker(void *data)
{
	struct fwlab_m4_pci_ctx *ctx = data;

	while (!kthread_should_stop()) {
		unsigned long flags;
		unsigned int virq = 0;
		u32 doorbell;
		u32 expected;
		u32 irq_trigger;
		u32 vector_ctrl;
		u16 msix_flags;

		if (ctx->nvme_mode) {
			fwlab_m4_frontend_poll(&ctx->frontend, ctx->bar_epoch);
			usleep_range(50, 100);
			continue;
		}

		spin_lock_irqsave(&ctx->config_lock, flags);
		doorbell = readl(ctx->bar_mapping + FWLAB_M4_REG_DOORBELL);
		expected = doorbell ^ FWLAB_M4_BAR_ACK_XOR;
		if (doorbell &&
		    readl(ctx->bar_mapping + FWLAB_M4_REG_ACK) != expected) {
			writel(expected, ctx->bar_mapping + FWLAB_M4_REG_ACK);
			wmb();
		}

		irq_trigger = readl(ctx->bar_mapping + FWLAB_M4_REG_IRQ_TRIGGER);
		msix_flags = get_unaligned_le16(
			&ctx->config[FWLAB_M4_MSIX_CAP + PCI_MSIX_FLAGS]);
		vector_ctrl = readl(ctx->bar_mapping +
				    FWLAB_M4_MSIX_TABLE_OFFSET +
				    PCI_MSIX_ENTRY_VECTOR_CTRL);
		if (irq_trigger &&
		    readl(ctx->bar_mapping + FWLAB_M4_REG_IRQ_ACK) != irq_trigger &&
		    ctx->pdev && ctx->pdev->msix_enabled &&
		    !(msix_flags & PCI_MSIX_FLAGS_MASKALL) &&
		    !(vector_ctrl & PCI_MSIX_ENTRY_CTRL_MASKBIT)) {
			virq = msi_get_virq(&ctx->pdev->dev, 0);
			if (virq)
				writel(irq_trigger,
				       ctx->bar_mapping + FWLAB_M4_REG_IRQ_ACK);
		}
		spin_unlock_irqrestore(&ctx->config_lock, flags);
		if (virq) {
			WRITE_ONCE(ctx->pending_virq, virq);
			irq_work_queue(&ctx->irq_work);
		}
		usleep_range(50, 100);
	}
	return 0;
}

static int fwlab_m4_prepare_aperture(struct fwlab_m4_pci_ctx *ctx)
{
	struct resource *probe;
	resource_size_t end;
	int ret;

	spin_lock_init(&ctx->config_lock);
	init_irq_work(&ctx->irq_work, fwlab_m4_irq_work);
	ctx->bar_start = (phys_addr_t)bar_start;
	ctx->bar_size = (resource_size_t)bar_size;
	if (ctx->bar_size != FWLAB_M4_BAR_SIZE ||
	    !is_power_of_2(ctx->bar_size) ||
	    !IS_ALIGNED(ctx->bar_start, ctx->bar_size) ||
	    ctx->bar_start + ctx->bar_size - 1 < ctx->bar_start)
		return -EINVAL;

	end = ctx->bar_start + ctx->bar_size - 1;
	probe = request_mem_region(ctx->bar_start, ctx->bar_size,
				   FWLAB_M4_PCI_NAME "-parent-probe");
	if (!probe)
		return -EBUSY;
	ctx->reserved_parent = probe->parent;
	if (!ctx->reserved_parent ||
	    ctx->reserved_parent->start != ctx->bar_start ||
	    ctx->reserved_parent->end != end ||
	    (ctx->reserved_parent->flags & IORESOURCE_BUSY)) {
		release_mem_region(ctx->bar_start, ctx->bar_size);
		ctx->reserved_parent = NULL;
		return -EINVAL;
	}
	release_mem_region(ctx->bar_start, ctx->bar_size);

	ctx->mem_window.name = FWLAB_M4_PCI_NAME "-bar-window";
	ctx->mem_window.start = ctx->bar_start;
	ctx->mem_window.end = end;
	ctx->mem_window.flags = IORESOURCE_MEM;
	ret = request_resource(ctx->reserved_parent, &ctx->mem_window);
	if (ret)
		return ret;
	ctx->mem_window_inserted = true;

	ctx->bar_mapping = ioremap(ctx->bar_start, FWLAB_M4_BAR_MAP_SIZE);
	if (!ctx->bar_mapping)
		return -ENOMEM;

	spin_lock_irq(&ctx->config_lock);
	fwlab_m4_bar_reset_locked(ctx);
	spin_unlock_irq(&ctx->config_lock);
	return 0;
}

static bool fwlab_m4_target(const struct pci_bus *bus, unsigned int devfn)
{
	return bus->number == FWLAB_M4_BUS_NR && devfn == FWLAB_M4_DEVFN;
}

static bool fwlab_m4_cfg_access_valid(int where, int size)
{
	if (size != 1 && size != 2 && size != 4)
		return false;
	if (where < 0 || where > PCI_CFG_SPACE_SIZE - size)
		return false;
	return !(where & (size - 1));
}

static int fwlab_m4_cfg_read(struct pci_bus *bus, unsigned int devfn,
			     int where, int size, u32 *value)
{
	struct fwlab_m4_pci_ctx *ctx;
	unsigned long flags;

	*value = ~0U;
	if (!fwlab_m4_target(bus, devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (!fwlab_m4_cfg_access_valid(where, size))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	ctx = container_of(to_pci_sysdata(bus), struct fwlab_m4_pci_ctx,
			   sysdata);
	spin_lock_irqsave(&ctx->config_lock, flags);
	switch (size) {
	case 1:
		*value = ctx->config[where];
		break;
	case 2:
		*value = get_unaligned_le16(&ctx->config[where]);
		break;
	case 4:
		*value = get_unaligned_le32(&ctx->config[where]);
		break;
	default:
		spin_unlock_irqrestore(&ctx->config_lock, flags);
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}
	spin_unlock_irqrestore(&ctx->config_lock, flags);

	return PCIBIOS_SUCCESSFUL;
}

static int fwlab_m4_cfg_write(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 value)
{
	struct fwlab_m4_pci_ctx *ctx;
	unsigned long flags;
	u16 command;
	u16 devctl;
	u16 msix_flags;
	u64 size_mask;
	u32 fixed;

	if (!fwlab_m4_target(bus, devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (!fwlab_m4_cfg_access_valid(where, size))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	ctx = container_of(to_pci_sysdata(bus), struct fwlab_m4_pci_ctx,
			   sysdata);
	spin_lock_irqsave(&ctx->config_lock, flags);

	if (where == PCI_COMMAND && (size == 2 || size == 4)) {
		command = get_unaligned_le16(&ctx->config[PCI_COMMAND]);
		command &= ~(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
			     PCI_COMMAND_INTX_DISABLE);
		command |= value & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
				    PCI_COMMAND_INTX_DISABLE);
		put_unaligned_le16(command, &ctx->config[PCI_COMMAND]);
	} else if (where == PCI_BASE_ADDRESS_0 && size == 4) {
		size_mask = ~(ctx->bar_size - 1);
		fixed = lower_32_bits(ctx->bar_start) |
			PCI_BASE_ADDRESS_MEM_TYPE_64;
		put_unaligned_le32(value == ~0U ?
				   lower_32_bits(size_mask) |
					PCI_BASE_ADDRESS_MEM_TYPE_64 :
				   fixed,
				   &ctx->config[PCI_BASE_ADDRESS_0]);
	} else if (where == PCI_BASE_ADDRESS_1 && size == 4) {
		size_mask = ~(ctx->bar_size - 1);
		fixed = upper_32_bits(ctx->bar_start);
		put_unaligned_le32(value == ~0U ? upper_32_bits(size_mask) :
				   fixed,
				   &ctx->config[PCI_BASE_ADDRESS_1]);
	} else if (where == FWLAB_M4_PCIE_CAP + PCI_EXP_DEVCTL && size == 2) {
		devctl = value & ~PCI_EXP_DEVCTL_BCR_FLR;
		put_unaligned_le16(devctl,
				   &ctx->config[FWLAB_M4_PCIE_CAP +
						PCI_EXP_DEVCTL]);
		if (value & PCI_EXP_DEVCTL_BCR_FLR)
			fwlab_m4_bar_reset_locked(ctx);
	} else if (where == FWLAB_M4_MSIX_CAP + PCI_MSIX_FLAGS && size == 2) {
		msix_flags = get_unaligned_le16(
			&ctx->config[FWLAB_M4_MSIX_CAP + PCI_MSIX_FLAGS]);
		msix_flags &= PCI_MSIX_FLAGS_QSIZE;
		msix_flags |= value & (PCI_MSIX_FLAGS_ENABLE |
				       PCI_MSIX_FLAGS_MASKALL);
		put_unaligned_le16(
			msix_flags,
			&ctx->config[FWLAB_M4_MSIX_CAP + PCI_MSIX_FLAGS]);
	}

	spin_unlock_irqrestore(&ctx->config_lock, flags);
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops fwlab_m4_pci_ops = {
	.read = fwlab_m4_cfg_read,
	.write = fwlab_m4_cfg_write,
};

static void fwlab_m4_init_config(struct fwlab_m4_pci_ctx *ctx)
{
	u16 status;
	u16 pcie_flags;
	u32 bar_low;

	memset(ctx->config, 0, sizeof(ctx->config));
	put_unaligned_le16(FWLAB_M4_VENDOR_ID,
			   &ctx->config[PCI_VENDOR_ID]);
	put_unaligned_le16(FWLAB_M4_DEVICE_ID,
			   &ctx->config[PCI_DEVICE_ID]);
	put_unaligned_le32(((ctx->nvme_mode ? FWLAB_M4_NVME_CLASS_CODE :
			     FWLAB_M4_CLASS_CODE) << 8) | FWLAB_M4_REVISION,
			   &ctx->config[PCI_CLASS_REVISION]);
	ctx->config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL;
	put_unaligned_le16(FWLAB_M4_VENDOR_ID,
			   &ctx->config[PCI_SUBSYSTEM_VENDOR_ID]);
	put_unaligned_le16(FWLAB_M4_SUBSYSTEM_ID,
			   &ctx->config[PCI_SUBSYSTEM_ID]);

	bar_low = lower_32_bits(ctx->bar_start) |
		  PCI_BASE_ADDRESS_MEM_TYPE_64;
	put_unaligned_le32(bar_low, &ctx->config[PCI_BASE_ADDRESS_0]);
	put_unaligned_le32(upper_32_bits(ctx->bar_start),
			   &ctx->config[PCI_BASE_ADDRESS_1]);

	status = PCI_STATUS_CAP_LIST;
	put_unaligned_le16(status, &ctx->config[PCI_STATUS]);
	ctx->config[PCI_CAPABILITY_LIST] = FWLAB_M4_PCIE_CAP;
	ctx->config[FWLAB_M4_PCIE_CAP + PCI_CAP_LIST_ID] = PCI_CAP_ID_EXP;
	ctx->config[FWLAB_M4_PCIE_CAP + PCI_CAP_LIST_NEXT] = 0;
	pcie_flags = 2 | (PCI_EXP_TYPE_ENDPOINT << 4);
	put_unaligned_le16(pcie_flags,
			   &ctx->config[FWLAB_M4_PCIE_CAP + PCI_EXP_FLAGS]);
	put_unaligned_le32(PCI_EXP_DEVCAP_FLR,
			   &ctx->config[FWLAB_M4_PCIE_CAP + PCI_EXP_DEVCAP]);
	ctx->config[FWLAB_M4_PCIE_CAP + PCI_CAP_LIST_NEXT] =
		FWLAB_M4_MSIX_CAP;
	ctx->config[FWLAB_M4_MSIX_CAP + PCI_CAP_LIST_ID] = PCI_CAP_ID_MSIX;
	ctx->config[FWLAB_M4_MSIX_CAP + PCI_CAP_LIST_NEXT] = 0;
	put_unaligned_le16(0,
			   &ctx->config[FWLAB_M4_MSIX_CAP + PCI_MSIX_FLAGS]);
	put_unaligned_le32(FWLAB_M4_MSIX_TABLE_OFFSET,
			   &ctx->config[FWLAB_M4_MSIX_CAP + PCI_MSIX_TABLE]);
	put_unaligned_le32(FWLAB_M4_MSIX_PBA_OFFSET,
			   &ctx->config[FWLAB_M4_MSIX_CAP + PCI_MSIX_PBA]);
}

static void fwlab_m4_release_aperture(struct fwlab_m4_pci_ctx *ctx)
{
	if (ctx->bar_thread) {
		kthread_stop(ctx->bar_thread);
		ctx->bar_thread = NULL;
	}
	irq_work_sync(&ctx->irq_work);
	WRITE_ONCE(ctx->pending_virq, 0);
	if (ctx->bar_mapping) {
		iounmap(ctx->bar_mapping);
		ctx->bar_mapping = NULL;
	}
	if (ctx->mem_window_inserted) {
		remove_resource(&ctx->mem_window);
		ctx->mem_window_inserted = false;
	}
}

int fwlab_m4_pci_prepare(struct device *root_dev,
			 struct fwlab_m4_pci_ctx **out_ctx)
{
	struct pci_host_bridge *bridge;
	struct fwlab_m4_pci_ctx *ctx;
	struct fwlab_m4_frontend_services services;
	int domain_nr;
	int ret;

	if (!root_dev || !out_ctx)
		return -EINVAL;

	bridge = pci_alloc_host_bridge(sizeof(*ctx));
	if (!bridge)
		return -ENOMEM;

	ctx = pci_host_bridge_priv(bridge);
	ctx->bridge = bridge;
	ctx->root_dev = root_dev;
	ctx->iommu_group_id = -1;
	ctx->nvme_mode = nvme_mode;
	if (ctx->nvme_mode) {
		memset(&services, 0, sizeof(services));
		services.version = FWLAB_M4_FRONTEND_SERVICES_VERSION;
		services.size = sizeof(services);
		services.context = ctx;
		services.bar_read32 = fwlab_m4_service_bar_read32;
		services.bar_read64 = fwlab_m4_service_bar_read64;
		services.bar_write32 = fwlab_m4_service_bar_write32;
		services.bar_write64 = fwlab_m4_service_bar_write64;
		services.bar_zero = fwlab_m4_service_bar_zero;
		services.dma = fwlab_m4_service_dma;
		services.notify = fwlab_m4_service_notify;
		ret = fwlab_m4_frontend_bind(&ctx->frontend, &services,
					     &fwlab_m4_nvme_fixture_ops);
		if (ret) {
			pci_free_host_bridge(bridge);
			return ret;
		}
	}
	ret = fwlab_m4_prepare_aperture(ctx);
	if (ret) {
		fwlab_m4_release_aperture(ctx);
		fwlab_m4_frontend_unbind(&ctx->frontend);
		pci_free_host_bridge(bridge);
		return ret;
	}
	fwlab_m4_init_config(ctx);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)
	domain_nr = pci_bus_find_emul_domain_nr(FWLAB_M4_DOMAIN_MIN,
						FWLAB_M4_DOMAIN_MIN,
						FWLAB_M4_DOMAIN_MAX);
#else
	/* Build-only compatibility: this branch is never loaded by the PoC. */
	domain_nr = FWLAB_M4_DOMAIN_MIN;
	if (pci_find_bus(domain_nr, FWLAB_M4_BUS_NR))
		domain_nr = -EBUSY;
#endif
	if (domain_nr < 0) {
		fwlab_m4_release_aperture(ctx);
		fwlab_m4_frontend_unbind(&ctx->frontend);
		pci_free_host_bridge(bridge);
		return domain_nr;
	}

	ctx->sysdata.domain = domain_nr;
	ctx->sysdata.node = NUMA_NO_NODE;
	ret = fwlab_m4_create_msi_domain(ctx);
	if (ret) {
		fwlab_m4_release_aperture(ctx);
		fwlab_m4_frontend_unbind(&ctx->frontend);
		pci_free_host_bridge(bridge);
		return ret;
	}

	ctx->busn_res.name = FWLAB_M4_PCI_NAME "-bus";
	ctx->busn_res.start = FWLAB_M4_BUS_NR;
	ctx->busn_res.end = FWLAB_M4_BUS_NR;
	ctx->busn_res.flags = IORESOURCE_BUS | IORESOURCE_PCI_FIXED;

	bridge->dev.parent = root_dev;
	bridge->sysdata = &ctx->sysdata;
	bridge->ops = &fwlab_m4_pci_ops;
	bridge->busnr = FWLAB_M4_BUS_NR;
	bridge->domain_nr = domain_nr;
	bridge->msi_domain = ctx->msi_domain != NULL;
	pci_add_resource(&bridge->windows, &ctx->busn_res);
	pci_add_resource(&bridge->windows, &ctx->mem_window);

	*out_ctx = ctx;
	return 0;
}

int fwlab_m4_pci_scan(struct fwlab_m4_pci_ctx *ctx)
{
	struct pci_dev *pdev;
	struct resource *bar;
	int ret;

	if (!ctx || !ctx->bridge)
		return -EINVAL;

	pci_lock_rescan_remove();
	ret = pci_scan_root_bus_bridge(ctx->bridge);
	if (ret) {
		pci_unlock_rescan_remove();
		return ret;
	}
	ctx->bus_registered = true;

	pdev = pci_get_slot(ctx->bridge->bus, FWLAB_M4_DEVFN);
	if (!pdev) {
		ret = -ENODEV;
		goto err_registered_locked;
	}
	ctx->pdev = pdev;
	ctx->requester_id = pci_dev_id(pdev);
	bar = &pdev->resource[0];
	if (bar->start != ctx->bar_start || resource_size(bar) != ctx->bar_size ||
	    !(bar->flags & IORESOURCE_MEM) ||
	    !(bar->flags & IORESOURCE_MEM_64)) {
		ret = -EINVAL;
		goto err_put_locked;
	}
	if (bar->parent && bar->parent != &ctx->mem_window) {
		ret = -EBUSY;
		goto err_put_locked;
	}
	if (!bar->parent) {
		ret = request_resource(&ctx->mem_window, bar);
		if (ret)
			goto err_put_locked;
	}

	/* Prevent an unrelated driver from observing the feasibility function. */
	ret = device_set_driver_override(&pdev->dev,
					 ctx->nvme_mode ? "nvme" : "none");
	if (ret)
		goto err_put_locked;

	ctx->bar_thread = kthread_run(fwlab_m4_bar_worker, ctx,
				      "ssd-fwlab-bar");
	if (IS_ERR(ctx->bar_thread)) {
		ret = PTR_ERR(ctx->bar_thread);
		ctx->bar_thread = NULL;
		goto err_put_locked;
	}

	pci_dev_put(pdev);
	pci_unlock_rescan_remove();
	return 0;

err_put_locked:
	if (ctx->bar_thread) {
		kthread_stop(ctx->bar_thread);
		ctx->bar_thread = NULL;
	}
	ctx->pdev = NULL;
	pci_dev_put(pdev);
err_registered_locked:
	pci_stop_root_bus(ctx->bridge->bus);
	pci_remove_root_bus(ctx->bridge->bus);
	ctx->bus_registered = false;
	pci_unlock_rescan_remove();
	return ret;
}

int fwlab_m4_pci_publish(struct fwlab_m4_pci_ctx *ctx)
{
	struct iommu_group *group;

	if (!ctx || !ctx->bus_registered || !ctx->pdev)
		return -EINVAL;

	group = iommu_group_get(&ctx->pdev->dev);
	if (!group)
		return -ENODEV;
	ctx->iommu_group_id = iommu_group_id(group);
	iommu_group_put(group);

	pci_lock_rescan_remove();
	pci_bus_add_devices(ctx->bridge->bus);
	pci_unlock_rescan_remove();
	return 0;
}

void fwlab_m4_pci_remove(struct fwlab_m4_pci_ctx *ctx)
{
	if (!ctx || !ctx->bus_registered)
		return;

	pci_lock_rescan_remove();
	pci_stop_root_bus(ctx->bridge->bus);
	if (ctx->bar_thread) {
		kthread_stop(ctx->bar_thread);
		ctx->bar_thread = NULL;
	}
	fwlab_m4_frontend_stop(&ctx->frontend, ctx->bar_epoch);
	pci_remove_root_bus(ctx->bridge->bus);
	ctx->pdev = NULL;
	ctx->bus_registered = false;
	pci_unlock_rescan_remove();
}

void fwlab_m4_pci_free(struct fwlab_m4_pci_ctx *ctx)
{
	if (!ctx || !ctx->bridge)
		return;
	fwlab_m4_frontend_stop(&ctx->frontend, ctx->bar_epoch);
	pci_free_resource_list(&ctx->bridge->windows);
	fwlab_m4_release_aperture(ctx);
	fwlab_m4_destroy_msi_domain(ctx);
	fwlab_m4_frontend_unbind(&ctx->frontend);
	pci_free_host_bridge(ctx->bridge);
}
