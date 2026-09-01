// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

/* Private observer driver for the vendor-class M4-A BAR/reset PoC. */

#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "m4_internal.h"

#define FWLAB_BAR_LAB_NAME "ssd_fwlab_bar_lab"

struct fwlab_bar_lab {
	void __iomem *bar;
};

static int fwlab_bar_handshake(struct fwlab_bar_lab *lab, u32 doorbell)
{
	u32 observed;
	u32 expected = doorbell ^ FWLAB_M4_BAR_ACK_XOR;

	writel(doorbell, lab->bar + FWLAB_M4_REG_DOORBELL);
	wmb();
	return readl_poll_timeout(lab->bar + FWLAB_M4_REG_ACK, observed,
				 observed == expected, 50, 1000000);
}

static int fwlab_bar_verify_cold(struct fwlab_bar_lab *lab, u32 old_epoch)
{
	u32 epoch = readl(lab->bar + FWLAB_M4_REG_EPOCH);

	if (readl(lab->bar + FWLAB_M4_REG_SIGNATURE) !=
		    FWLAB_M4_BAR_SIGNATURE ||
	    readl(lab->bar + FWLAB_M4_REG_VERSION) != FWLAB_M4_BAR_VERSION ||
	    epoch == old_epoch ||
	    readl(lab->bar + FWLAB_M4_REG_DOORBELL) != 0 ||
	    readl(lab->bar + FWLAB_M4_REG_ACK) != 0)
		return -EIO;
	return 0;
}

static int fwlab_bar_lab_probe(struct pci_dev *pdev,
			       const struct pci_device_id *id)
{
	struct fwlab_bar_lab *lab;
	u32 epoch;
	int ret;

	(void)id;
	if (pci_resource_start(pdev, 0) != FWLAB_M4_BAR_DEFAULT_START ||
	    pci_resource_len(pdev, 0) != FWLAB_M4_BAR_SIZE ||
	    !(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) ||
	    !(pci_resource_flags(pdev, 0) & IORESOURCE_MEM_64))
		return -EINVAL;

	lab = kzalloc(sizeof(*lab), GFP_KERNEL);
	if (!lab)
		return -ENOMEM;

	ret = pci_enable_device_mem(pdev);
	if (ret)
		goto err_free;
	ret = pci_request_region(pdev, 0, FWLAB_BAR_LAB_NAME);
	if (ret)
		goto err_disable;

	lab->bar = pci_iomap_range(pdev, 0, 0, FWLAB_M4_BAR_MAP_SIZE);
	if (!lab->bar) {
		ret = -ENOMEM;
		goto err_region;
	}
	pci_set_drvdata(pdev, lab);

	if (readl(lab->bar + FWLAB_M4_REG_SIGNATURE) !=
		    FWLAB_M4_BAR_SIGNATURE ||
	    readl(lab->bar + FWLAB_M4_REG_VERSION) != FWLAB_M4_BAR_VERSION) {
		ret = -ENODEV;
		goto err_iounmap;
	}

	ret = fwlab_bar_handshake(lab, 0x12345678U);
	if (ret)
		goto err_iounmap;
	epoch = readl(lab->bar + FWLAB_M4_REG_EPOCH);
	ret = pci_reset_function_locked(pdev);
	if (ret)
		goto err_iounmap;
	ret = fwlab_bar_verify_cold(lab, epoch);
	if (ret)
		goto err_iounmap;

	ret = fwlab_bar_handshake(lab, 0x89abcdefU);
	if (ret)
		goto err_iounmap;
	epoch = readl(lab->bar + FWLAB_M4_REG_EPOCH);
	ret = pci_reset_function_locked(pdev);
	if (ret)
		goto err_iounmap;
	ret = fwlab_bar_verify_cold(lab, epoch);
	if (ret)
		goto err_iounmap;

	dev_info(&pdev->dev,
		 "M4-A BAR doorbell/ack and two cold FLR cycles: PASS\n");
	return 0;

err_iounmap:
	pci_set_drvdata(pdev, NULL);
	pci_iounmap(pdev, lab->bar);
err_region:
	pci_release_region(pdev, 0);
err_disable:
	pci_disable_device(pdev);
err_free:
	kfree(lab);
	return ret;
}

static void fwlab_bar_lab_remove(struct pci_dev *pdev)
{
	struct fwlab_bar_lab *lab = pci_get_drvdata(pdev);

	if (!lab)
		return;
	pci_set_drvdata(pdev, NULL);
	pci_iounmap(pdev, lab->bar);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	kfree(lab);
}

static const struct pci_device_id fwlab_bar_lab_ids[] = {
	{ PCI_DEVICE(FWLAB_M4_VENDOR_ID, FWLAB_M4_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, fwlab_bar_lab_ids);

static struct pci_driver fwlab_bar_lab_driver = {
	.name = FWLAB_BAR_LAB_NAME,
	.id_table = fwlab_bar_lab_ids,
	.probe = fwlab_bar_lab_probe,
	.remove = fwlab_bar_lab_remove,
};
module_pci_driver(fwlab_bar_lab_driver);

MODULE_DESCRIPTION("SSD FWLab private BAR/reset observer driver");
MODULE_AUTHOR("Evanshenf and contributors");
MODULE_LICENSE("GPL");
