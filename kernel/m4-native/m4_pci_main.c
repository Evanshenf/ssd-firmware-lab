// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

/* Synthetic PCI endpoint for the native userspace firmware binding. */

#include <linux/device.h>
#include <linux/module.h>

#include "m4_internal.h"

static struct fwlab_m4_pci_ctx *fwlab_m4_pci_ctx;
static struct device *fwlab_m4_pci_root_dev;

static int __init fwlab_m4_pci_init(void)
{
	struct fwlab_m4_pci_ctx *ctx;
	int ret;

	fwlab_m4_pci_root_dev = root_device_register(FWLAB_M4_PCI_NAME);
	if (IS_ERR(fwlab_m4_pci_root_dev))
		return PTR_ERR(fwlab_m4_pci_root_dev);

	ret = fwlab_m4_pci_prepare(fwlab_m4_pci_root_dev, &ctx);
	if (ret)
		goto err_root;
	fwlab_m4_pci_ctx = ctx;

	ret = fwlab_m4_pci_scan(ctx);
	if (ret)
		goto err_pci;

	ret = fwlab_m4_pci_publish(ctx);
	if (ret)
		goto err_pci;

	pr_info(FWLAB_M4_PCI_NAME
		": ready bdf=%s group=%d rid=%#x firmware-attach-required\n",
		pci_name(ctx->pdev), ctx->iommu_group_id, ctx->requester_id);
	return 0;

err_pci:
	fwlab_m4_pci_remove(ctx);
	fwlab_m4_pci_ctx = NULL;
	fwlab_m4_pci_free(ctx);
err_root:
	root_device_unregister(fwlab_m4_pci_root_dev);
	fwlab_m4_pci_root_dev = NULL;
	return ret;
}

static void __exit fwlab_m4_pci_exit(void)
{
	struct fwlab_m4_pci_ctx *ctx = fwlab_m4_pci_ctx;

	if (ctx) {
		fwlab_m4_pci_remove(ctx);
		fwlab_m4_pci_ctx = NULL;
		fwlab_m4_pci_free(ctx);
	}
	if (fwlab_m4_pci_root_dev) {
		root_device_unregister(fwlab_m4_pci_root_dev);
		fwlab_m4_pci_root_dev = NULL;
	}
	pr_info(FWLAB_M4_PCI_NAME ": removed\n");
}

module_init(fwlab_m4_pci_init);
module_exit(fwlab_m4_pci_exit);

MODULE_DESCRIPTION("SSD FWLab native firmware PCI endpoint");
MODULE_AUTHOR("Evanshenf and contributors");
MODULE_LICENSE("GPL");
