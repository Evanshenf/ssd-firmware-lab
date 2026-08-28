// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

/*
 * V0: minimal emulated VFIO cdev + iommufd contract harness.
 *
 * Deliberate limits:
 *   - platform-device test shell, not a PCI function
 *   - one 4 KiB software region accessed only through pread/pwrite
 *   - no mmap, IRQ, DMA, pinning, migration, or protocol semantics
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>

#define FWLAB_VFIO_V0_NAME "ssd-fwlab-vfio-v0"
#define FWLAB_VFIO_V0_REGION_INDEX 0
#define FWLAB_VFIO_V0_NUM_REGIONS 1
#define FWLAB_VFIO_V0_REGION_SIZE PAGE_SIZE

struct fwlab_vfio_v0 {
	struct vfio_device vdev;
	/* Protects opened, reset_generation, and region. */
	struct mutex lock;
	bool opened;
	u64 reset_generation;
	u8 region[FWLAB_VFIO_V0_REGION_SIZE];
};

static struct platform_device *fwlab_vfio_v0_pdev;
static char fwlab_vfio_v0_ops_name[] = FWLAB_VFIO_V0_NAME;

static struct fwlab_vfio_v0 *to_fwlab_vfio_v0(struct vfio_device *vdev)
{
	return container_of(vdev, struct fwlab_vfio_v0, vdev);
}

static int fwlab_vfio_v0_open(struct vfio_device *vdev)
{
	struct fwlab_vfio_v0 *fdev = to_fwlab_vfio_v0(vdev);
	int ret = 0;

	mutex_lock(&fdev->lock);
	if (fdev->opened) {
		ret = -EBUSY;
	} else {
		memset(fdev->region, 0, sizeof(fdev->region));
		fdev->opened = true;
	}
	mutex_unlock(&fdev->lock);
	return ret;
}

static void fwlab_vfio_v0_close(struct vfio_device *vdev)
{
	struct fwlab_vfio_v0 *fdev = to_fwlab_vfio_v0(vdev);

	mutex_lock(&fdev->lock);
	memset(fdev->region, 0, sizeof(fdev->region));
	fdev->opened = false;
	mutex_unlock(&fdev->lock);
}

static ssize_t fwlab_vfio_v0_read(struct vfio_device *vdev,
				  char __user *buf, size_t count, loff_t *ppos)
{
	struct fwlab_vfio_v0 *fdev = to_fwlab_vfio_v0(vdev);
	ssize_t ret;

	if (mutex_lock_interruptible(&fdev->lock))
		return -ERESTARTSYS;
	if (!fdev->opened) {
		ret = -EIO;
		goto out_unlock;
	}
	ret = simple_read_from_buffer(buf, count, ppos, fdev->region,
				      sizeof(fdev->region));

out_unlock:
	mutex_unlock(&fdev->lock);
	return ret;
}

static ssize_t fwlab_vfio_v0_write(struct vfio_device *vdev,
				   const char __user *buf, size_t count,
				   loff_t *ppos)
{
	struct fwlab_vfio_v0 *fdev = to_fwlab_vfio_v0(vdev);
	ssize_t ret;

	if (mutex_lock_interruptible(&fdev->lock))
		return -ERESTARTSYS;
	if (!fdev->opened) {
		ret = -EIO;
		goto out_unlock;
	}
	ret = simple_write_to_buffer(fdev->region, sizeof(fdev->region), ppos,
				     buf, count);

out_unlock:
	mutex_unlock(&fdev->lock);
	return ret;
}

static int fwlab_vfio_v0_get_region_info(struct vfio_device *vdev,
					 struct vfio_region_info *info,
					 struct vfio_info_cap *caps)
{
	(void)vdev;
	(void)caps;

	if (info->index != FWLAB_VFIO_V0_REGION_INDEX)
		return -EINVAL;

	info->offset = 0;
	info->size = FWLAB_VFIO_V0_REGION_SIZE;
	info->cap_offset = 0;
	info->flags = VFIO_REGION_INFO_FLAG_READ |
		      VFIO_REGION_INFO_FLAG_WRITE;
	return 0;
}

static long fwlab_vfio_v0_ioctl(struct vfio_device *vdev, unsigned int cmd,
				unsigned long arg)
{
	struct fwlab_vfio_v0 *fdev = to_fwlab_vfio_v0(vdev);
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case VFIO_DEVICE_GET_INFO: {
		unsigned long minsz = offsetofend(struct vfio_device_info,
						 num_irqs);
		struct vfio_device_info info;

		if (copy_from_user(&info, argp, minsz))
			return -EFAULT;
		if (info.argsz < minsz)
			return -EINVAL;

		info.flags = VFIO_DEVICE_FLAGS_RESET;
		info.num_regions = FWLAB_VFIO_V0_NUM_REGIONS;
		info.num_irqs = 0;
		return copy_to_user(argp, &info, minsz) ? -EFAULT : 0;
	}
	case VFIO_DEVICE_RESET:
		mutex_lock(&fdev->lock);
		memset(fdev->region, 0, sizeof(fdev->region));
		fdev->reset_generation++;
		mutex_unlock(&fdev->lock);
		return 0;
	default:
		return -ENOTTY;
	}
}

static void fwlab_vfio_v0_release(struct vfio_device *vdev)
{
	struct fwlab_vfio_v0 *fdev = to_fwlab_vfio_v0(vdev);

	mutex_destroy(&fdev->lock);
}

static const struct vfio_device_ops fwlab_vfio_v0_ops = {
	.name = fwlab_vfio_v0_ops_name,
	.release = fwlab_vfio_v0_release,
	.bind_iommufd = vfio_iommufd_emulated_bind,
	.unbind_iommufd = vfio_iommufd_emulated_unbind,
	.attach_ioas = vfio_iommufd_emulated_attach_ioas,
	.detach_ioas = vfio_iommufd_emulated_detach_ioas,
	.open_device = fwlab_vfio_v0_open,
	.close_device = fwlab_vfio_v0_close,
	.read = fwlab_vfio_v0_read,
	.write = fwlab_vfio_v0_write,
	.ioctl = fwlab_vfio_v0_ioctl,
	.get_region_info_caps = fwlab_vfio_v0_get_region_info,
};

static int fwlab_vfio_v0_probe(struct platform_device *pdev)
{
	struct fwlab_vfio_v0 *fdev;
	int ret;

	fdev = vfio_alloc_device(fwlab_vfio_v0, vdev, &pdev->dev,
				 &fwlab_vfio_v0_ops);
	if (IS_ERR(fdev))
		return PTR_ERR(fdev);

	mutex_init(&fdev->lock);
	platform_set_drvdata(pdev, fdev);

	ret = vfio_register_emulated_iommu_dev(&fdev->vdev);
	if (ret) {
		platform_set_drvdata(pdev, NULL);
		vfio_put_device(&fdev->vdev);
		return ret;
	}

	dev_info(&pdev->dev,
		 "registered emulated VFIO cdev: 1 software region, no DMA/IRQ/mmap\n");
	return 0;
}

static void fwlab_vfio_v0_remove(struct platform_device *pdev)
{
	struct fwlab_vfio_v0 *fdev = platform_get_drvdata(pdev);

	if (!fdev)
		return;

	platform_set_drvdata(pdev, NULL);
	vfio_unregister_group_dev(&fdev->vdev);
	vfio_put_device(&fdev->vdev);
}

static struct platform_driver fwlab_vfio_v0_driver = {
	.probe = fwlab_vfio_v0_probe,
	.remove = fwlab_vfio_v0_remove,
	.driver = {
		.name = FWLAB_VFIO_V0_NAME,
	},
};

static int __init fwlab_vfio_v0_init(void)
{
	int ret;

	ret = platform_driver_register(&fwlab_vfio_v0_driver);
	if (ret)
		return ret;

	fwlab_vfio_v0_pdev =
		platform_device_register_simple(FWLAB_VFIO_V0_NAME,
						PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(fwlab_vfio_v0_pdev)) {
		ret = PTR_ERR(fwlab_vfio_v0_pdev);
		fwlab_vfio_v0_pdev = NULL;
		platform_driver_unregister(&fwlab_vfio_v0_driver);
		return ret;
	}

	if (!platform_get_drvdata(fwlab_vfio_v0_pdev)) {
		platform_device_unregister(fwlab_vfio_v0_pdev);
		fwlab_vfio_v0_pdev = NULL;
		platform_driver_unregister(&fwlab_vfio_v0_driver);
		return -ENODEV;
	}

	return 0;
}

static void __exit fwlab_vfio_v0_exit(void)
{
	if (fwlab_vfio_v0_pdev) {
		platform_device_unregister(fwlab_vfio_v0_pdev);
		fwlab_vfio_v0_pdev = NULL;
	}
	platform_driver_unregister(&fwlab_vfio_v0_driver);
}

module_init(fwlab_vfio_v0_init);
module_exit(fwlab_vfio_v0_exit);

MODULE_DESCRIPTION("SSD FWLab V0 emulated VFIO cdev contract harness");
MODULE_AUTHOR("Evanshenf and contributors");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: vfio iommufd");
