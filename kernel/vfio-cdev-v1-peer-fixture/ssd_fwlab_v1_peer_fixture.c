// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

/*
 * C2.5 test-only instantiator for a second frozen V1 driver probe.
 *
 * This module deliberately contains no V1 state or implementation.  It owns
 * only the additional platform_device registration used by the isolation
 * oracle.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#define FWLAB_V1_PEER_DRIVER_NAME "ssd-fwlab-vfio-v1"
#define FWLAB_V1_PEER_DEVICE_ID 0

static struct platform_device *fwlab_v1_peer_pdev;

static bool fwlab_v1_peer_is_bound(struct platform_device *pdev)
{
	bool bound;

	device_lock(&pdev->dev);
	bound = pdev->dev.driver &&
		 !strcmp(pdev->dev.driver->name, FWLAB_V1_PEER_DRIVER_NAME) &&
		 platform_get_drvdata(pdev);
	device_unlock(&pdev->dev);
	return bound;
}

static int __init fwlab_v1_peer_init(void)
{
	int ret;

	fwlab_v1_peer_pdev =
		platform_device_register_simple(FWLAB_V1_PEER_DRIVER_NAME,
						FWLAB_V1_PEER_DEVICE_ID,
						NULL, 0);
	if (IS_ERR(fwlab_v1_peer_pdev)) {
		ret = PTR_ERR(fwlab_v1_peer_pdev);
		fwlab_v1_peer_pdev = NULL;
		return ret;
	}
	if (!fwlab_v1_peer_is_bound(fwlab_v1_peer_pdev)) {
		platform_device_unregister(fwlab_v1_peer_pdev);
		fwlab_v1_peer_pdev = NULL;
		return -ENODEV;
	}

	pr_info("ssd_fwlab_v1_peer_fixture: registered bound peer %s.0\n",
		FWLAB_V1_PEER_DRIVER_NAME);
	return 0;
}

static void __exit fwlab_v1_peer_exit(void)
{
	if (fwlab_v1_peer_pdev) {
		platform_device_unregister(fwlab_v1_peer_pdev);
		fwlab_v1_peer_pdev = NULL;
	}
	pr_info("ssd_fwlab_v1_peer_fixture: removed\n");
}

module_init(fwlab_v1_peer_init);
module_exit(fwlab_v1_peer_exit);

MODULE_DESCRIPTION("SSD FWLab C2.5 V1 peer platform-device fixture");
MODULE_AUTHOR("Evanshenf and contributors");
MODULE_LICENSE("GPL");
