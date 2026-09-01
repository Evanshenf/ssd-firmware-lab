// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

/* Private one-vector isolated MSI-X observer for M4-C. */

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "m4_internal.h"

#define FWLAB_MSIX_LAB_NAME "ssd_fwlab_msix_lab"

static uint test_stage = 4;
module_param(test_stage, uint, 0444);
MODULE_PARM_DESC(test_stage, "1=alloc 2=request 3=deliver 4=mask/revoke");

struct fwlab_msix_lab {
	void __iomem *bar;
	struct completion completion;
	atomic_t count;
};

static irqreturn_t fwlab_msix_handler(int irq, void *data)
{
	struct fwlab_msix_lab *lab = data;

	(void)irq;
	atomic_inc(&lab->count);
	complete(&lab->completion);
	return IRQ_HANDLED;
}

static int fwlab_msix_trigger(struct fwlab_msix_lab *lab, u32 token,
			      int old_count)
{
	reinit_completion(&lab->completion);
	writel(token, lab->bar + FWLAB_M4_REG_IRQ_TRIGGER);
	wmb();
	if (!wait_for_completion_timeout(&lab->completion, HZ))
		return -ETIMEDOUT;
	if (atomic_read(&lab->count) != old_count + 1 ||
	    readl(lab->bar + FWLAB_M4_REG_IRQ_ACK) != token)
		return -EIO;
	return 0;
}

static int fwlab_msix_lab_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	struct fwlab_msix_lab *lab;
	int irq;
	int old_count;
	int ret;
	bool vectors = false;
	bool handler = false;

	(void)id;
	if (test_stage < 1 || test_stage > 4)
		return -EINVAL;
	if (!msi_device_has_isolated_msi(&pdev->dev))
		return -EPERM;

	lab = kzalloc(sizeof(*lab), GFP_KERNEL);
	if (!lab)
		return -ENOMEM;
	init_completion(&lab->completion);
	atomic_set(&lab->count, 0);

	ret = pci_enable_device_mem(pdev);
	if (ret)
		goto err_free;
	ret = pci_request_region(pdev, 0, FWLAB_MSIX_LAB_NAME);
	if (ret)
		goto err_disable;
	lab->bar = pci_iomap_range(pdev, 0, 0, FWLAB_M4_BAR_MAP_SIZE);
	if (!lab->bar) {
		ret = -ENOMEM;
		goto err_region;
	}
	pci_set_drvdata(pdev, lab);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSIX);
	if (ret != 1) {
		ret = ret < 0 ? ret : -ENOSPC;
		goto err_iounmap;
	}
	vectors = true;
	dev_info(&pdev->dev, "M4-C stage 1 vector allocation: PASS\n");
	if (test_stage == 1)
		goto success_vectors;
	irq = pci_irq_vector(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_vectors;
	}
	ret = request_irq(irq, fwlab_msix_handler, 0, FWLAB_MSIX_LAB_NAME,
			  lab);
	if (ret)
		goto err_vectors;
	handler = true;
	dev_info(&pdev->dev, "M4-C stage 2 handler request: PASS\n");
	if (test_stage == 2)
		goto success_irq;

	ret = fwlab_msix_trigger(lab, 0x11111111U, 0);
	if (ret)
		goto err_irq;
	dev_info(&pdev->dev, "M4-C stage 3 irq_work delivery: PASS\n");
	if (test_stage == 3)
		goto success_irq;

	disable_irq(irq);
	dev_info(&pdev->dev, "M4-C mask ctrl=%#x\n",
		 readl(lab->bar + FWLAB_M4_MSIX_TABLE_OFFSET +
		       PCI_MSIX_ENTRY_VECTOR_CTRL));
	old_count = atomic_read(&lab->count);
	writel(0x22222222U, lab->bar + FWLAB_M4_REG_IRQ_TRIGGER);
	wmb();
	msleep(50);
	if (atomic_read(&lab->count) != old_count ||
	    readl(lab->bar + FWLAB_M4_REG_IRQ_ACK) == 0x22222222U) {
		ret = -EIO;
		enable_irq(irq);
		goto err_irq;
	}
	reinit_completion(&lab->completion);
	enable_irq(irq);
	if (!wait_for_completion_timeout(&lab->completion, HZ) ||
	    atomic_read(&lab->count) != old_count + 1 ||
	    readl(lab->bar + FWLAB_M4_REG_IRQ_ACK) != 0x22222222U) {
		ret = -EIO;
		goto err_irq;
	}

	success_irq:
	free_irq(irq, lab);
	handler = false;
	success_vectors:
	pci_free_irq_vectors(pdev);
	vectors = false;
	if (test_stage < 4)
		goto success_reset;
	old_count = atomic_read(&lab->count);
	writel(0x33333333U, lab->bar + FWLAB_M4_REG_IRQ_TRIGGER);
	wmb();
	msleep(50);
	if (atomic_read(&lab->count) != old_count ||
	    readl(lab->bar + FWLAB_M4_REG_IRQ_ACK) == 0x33333333U) {
		ret = -EIO;
		goto err_iounmap;
	}

	success_reset:
	ret = pci_reset_function_locked(pdev);
	if (ret)
		goto err_iounmap;
	if (test_stage == 4)
		dev_info(&pdev->dev,
			 "M4-C isolated MSI-X deliver/mask/unmask/revoke: PASS\n");
	else
		dev_info(&pdev->dev, "M4-C stage %u completed: PASS\n",
			 test_stage);
	return 0;

err_irq:
	if (handler)
		free_irq(irq, lab);
err_vectors:
	if (vectors)
		pci_free_irq_vectors(pdev);
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

static void fwlab_msix_lab_remove(struct pci_dev *pdev)
{
	struct fwlab_msix_lab *lab = pci_get_drvdata(pdev);

	if (!lab)
		return;
	pci_set_drvdata(pdev, NULL);
	pci_iounmap(pdev, lab->bar);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	kfree(lab);
}

static const struct pci_device_id fwlab_msix_lab_ids[] = {
	{ PCI_DEVICE(FWLAB_M4_VENDOR_ID, FWLAB_M4_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, fwlab_msix_lab_ids);

static struct pci_driver fwlab_msix_lab_driver = {
	.name = FWLAB_MSIX_LAB_NAME,
	.id_table = fwlab_msix_lab_ids,
	.probe = fwlab_msix_lab_probe,
	.remove = fwlab_msix_lab_remove,
};
module_pci_driver(fwlab_msix_lab_driver);

MODULE_DESCRIPTION("SSD FWLab private isolated MSI-X observer");
MODULE_AUTHOR("Evanshenf and contributors");
MODULE_LICENSE("GPL");
