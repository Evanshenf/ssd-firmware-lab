// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

/*
 * H0: exported-API feasibility probe for a synthetic PCI root bus.
 *
 * Deliberate limits:
 *   - one vendor-specific function at 00:00.0 in a dynamically allocated domain
 *   - immutable 256-byte conventional PCI configuration space
 *   - no BAR, DMA, IRQ, MSI/MSI-X, power management, reset, or driver binding
 *
 * This module must remain unable to expose an NVMe-class function. Later BAR
 * and transport experiments belong to separately reviewed milestones.
 */

#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/numa.h>
#include <linux/pci.h>
#include <linux/unaligned.h>

#include <asm/pci.h>

#define FWLAB_H0_NAME "ssd_fwlab_host_h0"
#define FWLAB_H0_VENDOR_ID 0xfffa
#define FWLAB_H0_DEVICE_ID 0x0001
#define FWLAB_H0_SUBSYSTEM_ID 0x0001
#define FWLAB_H0_CLASS_CODE 0xff0000U
#define FWLAB_H0_REVISION 0x01
#define FWLAB_H0_BUS_NR 0
#define FWLAB_H0_DEVFN PCI_DEVFN(0, 0)
#define FWLAB_H0_DOMAIN_MIN 0x10000U
#define FWLAB_H0_DOMAIN_MAX 0x7fffffffU

static uint domain_hint = FWLAB_H0_DOMAIN_MIN;
module_param(domain_hint, uint, 0444);
MODULE_PARM_DESC(domain_hint,
		 "Preferred synthetic PCI domain (range 0x10000..INT_MAX)");

struct fwlab_h0_ctx {
	/* Must be the object referenced by pci_bus::sysdata on x86. */
	struct pci_sysdata sysdata;
	struct pci_host_bridge *bridge;
	struct resource busn_res;
	u8 config[PCI_CFG_SPACE_SIZE];
};

static struct device *fwlab_root_dev;
static struct fwlab_h0_ctx *fwlab_ctx;

static bool fwlab_h0_target(const struct pci_bus *bus, unsigned int devfn)
{
	return bus->number == FWLAB_H0_BUS_NR && devfn == FWLAB_H0_DEVFN;
}

static bool fwlab_h0_cfg_access_valid(int where, int size)
{
	if (size != 1 && size != 2 && size != 4)
		return false;
	if (where < 0 || where > PCI_CFG_SPACE_SIZE - size)
		return false;
	return !(where & (size - 1));
}

static int fwlab_h0_cfg_read(struct pci_bus *bus, unsigned int devfn,
			     int where, int size, u32 *value)
{
	struct fwlab_h0_ctx *ctx;

	*value = ~0U;
	if (!fwlab_h0_target(bus, devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (!fwlab_h0_cfg_access_valid(where, size))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	ctx = container_of(to_pci_sysdata(bus), struct fwlab_h0_ctx,
			   sysdata);

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
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int fwlab_h0_cfg_write(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 value)
{
	(void)value;

	if (!fwlab_h0_target(bus, devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (!fwlab_h0_cfg_access_valid(where, size))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	/*
	 * H0 is intentionally read-only. In particular, BAR sizing writes and
	 * attempts to enable I/O, memory decoding, or bus mastering have no
	 * effect. Returning success matches harmless configuration probing while
	 * preserving the fail-closed state.
	 */
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops fwlab_h0_pci_ops = {
	.read = fwlab_h0_cfg_read,
	.write = fwlab_h0_cfg_write,
};

static void fwlab_h0_init_config(struct fwlab_h0_ctx *ctx)
{
	memset(ctx->config, 0, sizeof(ctx->config));
	put_unaligned_le16(FWLAB_H0_VENDOR_ID,
			   &ctx->config[PCI_VENDOR_ID]);
	put_unaligned_le16(FWLAB_H0_DEVICE_ID,
			   &ctx->config[PCI_DEVICE_ID]);
	put_unaligned_le32((FWLAB_H0_CLASS_CODE << 8) | FWLAB_H0_REVISION,
			   &ctx->config[PCI_CLASS_REVISION]);
	ctx->config[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL;
	put_unaligned_le16(FWLAB_H0_VENDOR_ID,
			   &ctx->config[PCI_SUBSYSTEM_VENDOR_ID]);
	put_unaligned_le16(FWLAB_H0_SUBSYSTEM_ID,
			   &ctx->config[PCI_SUBSYSTEM_ID]);
}

static void fwlab_h0_remove_bridge(void)
{
	struct pci_host_bridge *bridge;
	struct pci_bus *bus;

	if (!fwlab_ctx)
		return;

	bridge = fwlab_ctx->bridge;
	bus = bridge->bus;
	if (bus) {
		pci_lock_rescan_remove();
		pci_stop_root_bus(bus);
		pci_remove_root_bus(bus);
		pci_unlock_rescan_remove();
	}

	/* Also releases the dynamically allocated emulated domain. */
	pci_free_host_bridge(bridge);
	fwlab_ctx = NULL;
}

static int __init fwlab_h0_init(void)
{
	struct pci_host_bridge *bridge;
	struct fwlab_h0_ctx *ctx;
	struct pci_dev *pdev;
	int domain_nr;
	int ret;

	if (domain_hint < FWLAB_H0_DOMAIN_MIN ||
	    domain_hint > FWLAB_H0_DOMAIN_MAX)
		return -EINVAL;

	fwlab_root_dev = root_device_register(FWLAB_H0_NAME);
	if (IS_ERR(fwlab_root_dev))
		return PTR_ERR(fwlab_root_dev);

	bridge = pci_alloc_host_bridge(sizeof(*ctx));
	if (!bridge) {
		ret = -ENOMEM;
		goto err_root;
	}

	ctx = pci_host_bridge_priv(bridge);
	ctx->bridge = bridge;
	fwlab_h0_init_config(ctx);

	domain_nr = pci_bus_find_emul_domain_nr(domain_hint,
						FWLAB_H0_DOMAIN_MIN,
						FWLAB_H0_DOMAIN_MAX);
	if (domain_nr < 0) {
		ret = domain_nr;
		goto err_bridge;
	}

	ctx->sysdata.domain = domain_nr;
	ctx->sysdata.node = NUMA_NO_NODE;

	ctx->busn_res.name = FWLAB_H0_NAME "-bus";
	ctx->busn_res.start = FWLAB_H0_BUS_NR;
	ctx->busn_res.end = FWLAB_H0_BUS_NR;
	ctx->busn_res.flags = IORESOURCE_BUS | IORESOURCE_PCI_FIXED;

	bridge->dev.parent = fwlab_root_dev;
	bridge->sysdata = &ctx->sysdata;
	bridge->ops = &fwlab_h0_pci_ops;
	bridge->busnr = FWLAB_H0_BUS_NR;
	bridge->domain_nr = domain_nr;
	bridge->msi_domain = false;
	pci_add_resource(&bridge->windows, &ctx->busn_res);

	/* Set only after bridge ownership and release metadata are complete. */
	fwlab_ctx = ctx;
	pci_lock_rescan_remove();
	ret = pci_scan_root_bus_bridge(bridge);
	if (ret) {
		pci_unlock_rescan_remove();
		goto err_scan;
	}

	/*
	 * Freeze driver ownership before devices become probe-visible.  H0 has
	 * no matching class/ID driver, but later milestones must not rely on a
	 * userspace driver_override race against asynchronous probing.
	 */
	pdev = pci_get_slot(bridge->bus, FWLAB_H0_DEVFN);
	if (!pdev) {
		ret = -ENODEV;
		goto err_registered_locked;
	}
	ret = device_set_driver_override(&pdev->dev, "none");
	pci_dev_put(pdev);
	if (ret)
		goto err_registered_locked;

	pci_bus_add_devices(bridge->bus);
	pci_unlock_rescan_remove();

	pr_info(FWLAB_H0_NAME
		": registered domain %04x, device %04x:%02x:%02x.%u, no BAR/DMA/IRQ\n",
		domain_nr, domain_nr, FWLAB_H0_BUS_NR,
		PCI_SLOT(FWLAB_H0_DEVFN), PCI_FUNC(FWLAB_H0_DEVFN));
	return 0;

err_registered_locked:
	pci_stop_root_bus(bridge->bus);
	pci_remove_root_bus(bridge->bus);
	pci_unlock_rescan_remove();
	fwlab_ctx = NULL;
	pci_free_host_bridge(bridge);
	goto err_root;
err_scan:
	/* A failed registration may leave bridge->bus stale; never dereference it. */
	fwlab_ctx = NULL;
err_bridge:
	pci_free_host_bridge(bridge);
err_root:
	root_device_unregister(fwlab_root_dev);
	fwlab_root_dev = NULL;
	return ret;
}

static void __exit fwlab_h0_exit(void)
{
	fwlab_h0_remove_bridge();
	if (fwlab_root_dev) {
		root_device_unregister(fwlab_root_dev);
		fwlab_root_dev = NULL;
	}
	pr_info(FWLAB_H0_NAME ": removed\n");
}

module_init(fwlab_h0_init);
module_exit(fwlab_h0_exit);

MODULE_DESCRIPTION("SSD FWLab H0 synthetic PCI enumeration probe");
MODULE_AUTHOR("Evanshenf and contributors");
MODULE_LICENSE("GPL");
