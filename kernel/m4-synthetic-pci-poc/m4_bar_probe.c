// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

/*
 * P1 preflight: prove that a fixed boot reservation can be claimed and mapped
 * as an MMIO-style aperture.  This module does not create a PCI BAR.
 */

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/sizes.h>

#define FWLAB_BAR_PROBE_NAME "ssd_fwlab_bar_aperture_probe"
#define FWLAB_BAR_DEFAULT_START 0x70000000ULL
#define FWLAB_BAR_REQUIRED_SIZE SZ_16M
#define FWLAB_BAR_REQUIRED_ALIGNMENT SZ_16M
#define FWLAB_BAR_PROBE_MAGIC 0x46574231U

static unsigned long long bar_start = FWLAB_BAR_DEFAULT_START;
module_param(bar_start, ullong, 0444);
MODULE_PARM_DESC(bar_start, "Boot-reserved physical BAR aperture start");

static unsigned long long bar_size = FWLAB_BAR_REQUIRED_SIZE;
module_param(bar_size, ullong, 0444);
MODULE_PARM_DESC(bar_size, "Boot-reserved physical BAR aperture size");

static phys_addr_t fwlab_bar_start;
static phys_addr_t fwlab_bar_size;
static struct resource *fwlab_bar_resource;
static void __iomem *fwlab_bar_mapping;
static u32 fwlab_bar_saved_word;

static int __init fwlab_bar_probe_init(void)
{
	u32 observed;

	fwlab_bar_start = (phys_addr_t)bar_start;
	fwlab_bar_size = (phys_addr_t)bar_size;
	if (fwlab_bar_size != FWLAB_BAR_REQUIRED_SIZE ||
	    !IS_ALIGNED(fwlab_bar_start, FWLAB_BAR_REQUIRED_ALIGNMENT)) {
		pr_err(FWLAB_BAR_PROBE_NAME
		       ": invalid aperture start=%pa size=%pa\n",
		       &fwlab_bar_start, &fwlab_bar_size);
		return -EINVAL;
	}

	fwlab_bar_resource = request_mem_region(fwlab_bar_start,
						fwlab_bar_size,
						FWLAB_BAR_PROBE_NAME);
	if (!fwlab_bar_resource) {
		pr_err(FWLAB_BAR_PROBE_NAME
		       ": aperture is not claimable start=%pa size=%pa\n",
		       &fwlab_bar_start, &fwlab_bar_size);
		return -EBUSY;
	}

	fwlab_bar_mapping = ioremap(fwlab_bar_start, PAGE_SIZE);
	if (!fwlab_bar_mapping) {
		release_mem_region(fwlab_bar_start, fwlab_bar_size);
		fwlab_bar_resource = NULL;
		return -ENOMEM;
	}

	fwlab_bar_saved_word = readl(fwlab_bar_mapping);
	writel(FWLAB_BAR_PROBE_MAGIC, fwlab_bar_mapping);
	observed = readl(fwlab_bar_mapping);
	writel(fwlab_bar_saved_word, fwlab_bar_mapping);
	if (observed != FWLAB_BAR_PROBE_MAGIC) {
		iounmap(fwlab_bar_mapping);
		fwlab_bar_mapping = NULL;
		release_mem_region(fwlab_bar_start, fwlab_bar_size);
		fwlab_bar_resource = NULL;
		pr_err(FWLAB_BAR_PROBE_NAME
		       ": MMIO round trip mismatch observed=%#x\n", observed);
		return -EIO;
	}

	pr_info(FWLAB_BAR_PROBE_NAME
		": PASS start=%pa size=%pa claim+ioremap+roundtrip\n",
		&fwlab_bar_start, &fwlab_bar_size);
	return 0;
}

static void __exit fwlab_bar_probe_exit(void)
{
	if (fwlab_bar_mapping) {
		iounmap(fwlab_bar_mapping);
		fwlab_bar_mapping = NULL;
	}
	if (fwlab_bar_resource) {
		release_mem_region(fwlab_bar_start, fwlab_bar_size);
		fwlab_bar_resource = NULL;
	}
	pr_info(FWLAB_BAR_PROBE_NAME ": removed\n");
}

module_init(fwlab_bar_probe_init);
module_exit(fwlab_bar_probe_exit);

MODULE_DESCRIPTION("SSD FWLab fixed BAR-aperture preflight probe");
MODULE_AUTHOR("Evanshenf and contributors");
MODULE_LICENSE("GPL");
