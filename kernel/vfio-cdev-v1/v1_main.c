// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

/*
 * C2.2 bounded emulated VFIO cdev harness.
 *
 * Deliberate limits:
 *   - one platform-device instance, not a PCI function
 *   - non-mappable software data and control regions
 *   - synchronous vfio_dma_rw only
 *   - no pinning, dma_unmap callback, IRQ, worker, mmap, PCI or protocol
 */

#include <linux/err.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>

#include "c21_state.h"
#include "vfio_rw.h"

#define FWLAB_V1_NAME "ssd-fwlab-vfio-v1"
#define FWLAB_V1_DATA_REGION_INDEX 0U
#define FWLAB_V1_CONTROL_REGION_INDEX 1U
#define FWLAB_V1_NUM_REGIONS 2U
#define FWLAB_V1_REGION_SHIFT 40U
#define FWLAB_V1_REGION_MASK ((1ULL << FWLAB_V1_REGION_SHIFT) - 1ULL)

struct fwlab_v1_position {
	u32 index;
	u64 offset;
};

struct fwlab_v1 {
	struct vfio_device vdev;
	/* Serializes engine state, lifecycle transitions and synchronous copies. */
	struct mutex lock;
	struct fwlab_c21_device engine;
	struct fwlab_c21_vfio_rw vfio_rw;
	struct fwlab_c21_copy_provider provider;
};

struct fwlab_v1_transition_context {
	struct vfio_device *vdev;
	u32 *pt_id;
	bool called;
};

static struct platform_device *fwlab_v1_pdev;
static char fwlab_v1_ops_name[] = FWLAB_V1_NAME;

static struct fwlab_v1 *to_fwlab_v1(struct vfio_device *vdev)
{
	return container_of(vdev, struct fwlab_v1, vdev);
}

static void fwlab_v1_lock(void *context)
{
	mutex_lock(context);
}

static void fwlab_v1_unlock(void *context)
{
	mutex_unlock(context);
}

static const struct fwlab_c21_lock_ops fwlab_v1_lock_ops = {
	.lock = fwlab_v1_lock,
	.unlock = fwlab_v1_unlock,
};

static u64 fwlab_v1_region_offset(u32 index)
{
	return (u64)index << FWLAB_V1_REGION_SHIFT;
}

static int fwlab_v1_decode_position(loff_t position,
				    struct fwlab_v1_position *decoded)
{
	u64 value;

	if (position < 0)
		return -EINVAL;
	value = (u64)position;
	decoded->index = value >> FWLAB_V1_REGION_SHIFT;
	decoded->offset = value & FWLAB_V1_REGION_MASK;
	if (decoded->index >= FWLAB_V1_NUM_REGIONS)
		return -EINVAL;
	return 0;
}

static int fwlab_v1_transition_call(void *context,
				    enum fwlab_c21_transition transition)
{
	struct fwlab_v1_transition_context *transition_context = context;

	transition_context->called = true;
	switch (transition) {
	case FWLAB_C21_TRANSITION_ATTACH:
	case FWLAB_C21_TRANSITION_REPLACE:
		if (!transition_context->pt_id)
			return -EINVAL;
		return vfio_iommufd_emulated_attach_ioas(transition_context->vdev,
						       transition_context->pt_id);
	case FWLAB_C21_TRANSITION_DETACH:
		vfio_iommufd_emulated_detach_ioas(transition_context->vdev);
		return 0;
	default:
		return -EINVAL;
	}
}

static int fwlab_v1_open(struct vfio_device *vdev)
{
	return fwlab_c21_device_open(&to_fwlab_v1(vdev)->engine);
}

static void fwlab_v1_close(struct vfio_device *vdev)
{
	struct fwlab_v1 *fdev = to_fwlab_v1(vdev);
	int ret;

	ret = fwlab_c21_device_close(&fdev->engine);
	if (ret)
		dev_warn(vdev->dev, "engine close failed: %d\n", ret);
}

static int fwlab_v1_attach_ioas(struct vfio_device *vdev, u32 *pt_id)
{
	struct fwlab_v1_transition_context context = {
		.vdev = vdev,
		.pt_id = pt_id,
	};
	int ret;

	ret = fwlab_c21_device_transition(&to_fwlab_v1(vdev)->engine,
					  FWLAB_C21_TRANSITION_ATTACH,
					  fwlab_v1_transition_call, &context);
	if (ret != -EINVAL || context.called)
		return ret;

	/* ATTACH was rejected by local state, so this may be an IOAS replace. */
	return fwlab_c21_device_transition(&to_fwlab_v1(vdev)->engine,
					   FWLAB_C21_TRANSITION_REPLACE,
					   fwlab_v1_transition_call, &context);
}

static void fwlab_v1_detach_ioas(struct vfio_device *vdev)
{
	struct fwlab_v1_transition_context context = {
		.vdev = vdev,
	};
	int ret;

	ret = fwlab_c21_device_transition(&to_fwlab_v1(vdev)->engine,
					  FWLAB_C21_TRANSITION_DETACH,
					  fwlab_v1_transition_call, &context);
	if (!ret)
		return;

	/* No callback means this invocation has not detached the VFIO access. */
	if (!context.called) {
		dev_warn(vdev->dev, "engine detach rejected: %d; forcing VFIO detach\n",
			 ret);
		vfio_iommufd_emulated_detach_ioas(vdev);
		return;
	}
	/* The callback already detached exactly once; never retry it here. */
	dev_warn(vdev->dev, "engine detach commit failed after VFIO detach: %d\n",
		 ret);
}

static ssize_t fwlab_v1_data_write(struct fwlab_v1 *fdev,
				   const char __user *buffer, size_t count,
				   u64 offset)
{
	unsigned char *snapshot;
	size_t available;
	int ret;

	if (!count || offset >= FWLAB_C21_DATA_REGION_SIZE)
		return 0;
	available = FWLAB_C21_DATA_REGION_SIZE - (size_t)offset;
	if (count > available)
		count = available;
	snapshot = memdup_user(buffer, count);
	if (IS_ERR(snapshot))
		return PTR_ERR(snapshot);
	ret = fwlab_c21_data_write(&fdev->engine, (u32)offset, snapshot, count);
	kfree(snapshot);
	return ret;
}

static ssize_t fwlab_v1_data_read(struct fwlab_v1 *fdev, char __user *buffer,
				  size_t count, u64 offset)
{
	unsigned char *snapshot;
	size_t available;
	int ret;

	if (!count || offset >= FWLAB_C21_DATA_REGION_SIZE)
		return 0;
	available = FWLAB_C21_DATA_REGION_SIZE - (size_t)offset;
	if (count > available)
		count = available;
	snapshot = kmalloc(count, GFP_KERNEL);
	if (!snapshot)
		return -ENOMEM;
	ret = fwlab_c21_data_read(&fdev->engine, (u32)offset, snapshot, count);
	if (ret > 0 && copy_to_user(buffer, snapshot, ret))
		ret = -EFAULT;
	kfree(snapshot);
	return ret;
}

static ssize_t fwlab_v1_control_write(struct fwlab_v1 *fdev,
				      const char __user *buffer, size_t count,
				      u64 offset)
{
	unsigned char snapshot[FWLAB_C21_RECORD_SIZE];

	if (offset > U32_MAX)
		return -EINVAL;
	if (count != sizeof(snapshot))
		return -EMSGSIZE;
	if (copy_from_user(snapshot, buffer, sizeof(snapshot)))
		return -EFAULT;
	return fwlab_c21_control_write(&fdev->engine, (u32)offset, snapshot,
				       sizeof(snapshot));
}

static ssize_t fwlab_v1_control_read(struct fwlab_v1 *fdev,
				     char __user *buffer, size_t count,
				     u64 offset)
{
	unsigned char snapshot[FWLAB_C21_RECORD_SIZE];
	int ret;

	if (offset > U32_MAX)
		return -EINVAL;
	if (count != sizeof(snapshot))
		return -EMSGSIZE;
	ret = fwlab_c21_control_read(&fdev->engine, (u32)offset, snapshot,
				     sizeof(snapshot));
	if (ret > 0 && copy_to_user(buffer, snapshot, ret))
		return -EFAULT;
	return ret;
}

static ssize_t fwlab_v1_read(struct vfio_device *vdev, char __user *buffer,
			     size_t count, loff_t *position)
{
	struct fwlab_v1_position decoded;
	struct fwlab_v1 *fdev = to_fwlab_v1(vdev);
	ssize_t ret;

	ret = fwlab_v1_decode_position(*position, &decoded);
	if (ret)
		return ret;
	if (decoded.index == FWLAB_V1_DATA_REGION_INDEX)
		ret = fwlab_v1_data_read(fdev, buffer, count, decoded.offset);
	else
		ret = fwlab_v1_control_read(fdev, buffer, count, decoded.offset);
	if (ret > 0)
		*position += ret;
	return ret;
}

static ssize_t fwlab_v1_write(struct vfio_device *vdev,
			      const char __user *buffer, size_t count,
			      loff_t *position)
{
	struct fwlab_v1_position decoded;
	struct fwlab_v1 *fdev = to_fwlab_v1(vdev);
	ssize_t ret;

	ret = fwlab_v1_decode_position(*position, &decoded);
	if (ret)
		return ret;
	if (decoded.index == FWLAB_V1_DATA_REGION_INDEX)
		ret = fwlab_v1_data_write(fdev, buffer, count, decoded.offset);
	else
		ret = fwlab_v1_control_write(fdev, buffer, count, decoded.offset);
	if (ret > 0)
		*position += ret;
	return ret;
}

static int fwlab_v1_get_region_info(struct vfio_device *vdev,
				    struct vfio_region_info *info,
				    struct vfio_info_cap *caps)
{
	(void)vdev;
	(void)caps;

	if (info->index >= FWLAB_V1_NUM_REGIONS)
		return -EINVAL;
	info->offset = fwlab_v1_region_offset(info->index);
	info->size = info->index == FWLAB_V1_DATA_REGION_INDEX ?
			     FWLAB_C21_DATA_REGION_SIZE :
			     FWLAB_C21_CONTROL_REGION_SIZE;
	info->cap_offset = 0;
	info->flags = VFIO_REGION_INFO_FLAG_READ |
		      VFIO_REGION_INFO_FLAG_WRITE;
	return 0;
}

static long fwlab_v1_ioctl(struct vfio_device *vdev, unsigned int command,
			   unsigned long argument)
{
	void __user *argument_pointer = (void __user *)argument;

	switch (command) {
	case VFIO_DEVICE_GET_INFO: {
		unsigned long minimum_size =
			offsetofend(struct vfio_device_info, num_irqs);
		struct vfio_device_info info;

		if (copy_from_user(&info, argument_pointer, minimum_size))
			return -EFAULT;
		if (info.argsz < minimum_size)
			return -EINVAL;
		info.flags = VFIO_DEVICE_FLAGS_RESET;
		info.num_regions = FWLAB_V1_NUM_REGIONS;
		info.num_irqs = 0;
		return copy_to_user(argument_pointer, &info, minimum_size) ?
			       -EFAULT : 0;
	}
	case VFIO_DEVICE_RESET:
		return fwlab_c21_device_reset(&to_fwlab_v1(vdev)->engine);
	default:
		return -ENOTTY;
	}
}

static void fwlab_v1_release(struct vfio_device *vdev)
{
	mutex_destroy(&to_fwlab_v1(vdev)->lock);
}

static const struct vfio_device_ops fwlab_v1_ops = {
	.name = fwlab_v1_ops_name,
	.release = fwlab_v1_release,
	.bind_iommufd = vfio_iommufd_emulated_bind,
	.unbind_iommufd = vfio_iommufd_emulated_unbind,
	.attach_ioas = fwlab_v1_attach_ioas,
	.detach_ioas = fwlab_v1_detach_ioas,
	.open_device = fwlab_v1_open,
	.close_device = fwlab_v1_close,
	.read = fwlab_v1_read,
	.write = fwlab_v1_write,
	.ioctl = fwlab_v1_ioctl,
	.get_region_info_caps = fwlab_v1_get_region_info,
};

static int fwlab_v1_probe(struct platform_device *pdev)
{
	struct fwlab_v1 *fdev;
	int ret;

	if (PAGE_SIZE != FWLAB_C21_IOAS_PAGE_SIZE)
		return -EOPNOTSUPP;
	fdev = vfio_alloc_device(fwlab_v1, vdev, &pdev->dev, &fwlab_v1_ops);
	if (IS_ERR(fdev))
		return PTR_ERR(fdev);
	mutex_init(&fdev->lock);
	fwlab_c21_vfio_rw_init(&fdev->vfio_rw, &fdev->vdev, &fdev->provider);
	ret = fwlab_c21_device_init(&fdev->engine, &fwlab_v1_lock_ops,
				    &fdev->lock, &fdev->provider);
	if (ret)
		goto err_put_device;
	platform_set_drvdata(pdev, fdev);
	ret = vfio_register_emulated_iommu_dev(&fdev->vdev);
	if (ret)
		goto err_clear_drvdata;
	dev_info(&pdev->dev,
		 "registered bounded V1 cdev: data/control regions, synchronous IOAS copy\n");
	return 0;

err_clear_drvdata:
	platform_set_drvdata(pdev, NULL);
err_put_device:
	vfio_put_device(&fdev->vdev);
	return ret;
}

static void fwlab_v1_remove(struct platform_device *pdev)
{
	struct fwlab_v1 *fdev = platform_get_drvdata(pdev);

	if (!fdev)
		return;
	platform_set_drvdata(pdev, NULL);
	vfio_unregister_group_dev(&fdev->vdev);
	vfio_put_device(&fdev->vdev);
}

static struct platform_driver fwlab_v1_driver = {
	.probe = fwlab_v1_probe,
	.remove = fwlab_v1_remove,
	.driver = {
		.name = FWLAB_V1_NAME,
	},
};

static int __init fwlab_v1_init(void)
{
	int ret;

	ret = platform_driver_register(&fwlab_v1_driver);
	if (ret)
		return ret;
	fwlab_v1_pdev =
		platform_device_register_simple(FWLAB_V1_NAME,
						PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(fwlab_v1_pdev)) {
		ret = PTR_ERR(fwlab_v1_pdev);
		fwlab_v1_pdev = NULL;
		platform_driver_unregister(&fwlab_v1_driver);
		return ret;
	}
	if (!platform_get_drvdata(fwlab_v1_pdev)) {
		platform_device_unregister(fwlab_v1_pdev);
		fwlab_v1_pdev = NULL;
		platform_driver_unregister(&fwlab_v1_driver);
		return -ENODEV;
	}
	return 0;
}

static void __exit fwlab_v1_exit(void)
{
	if (fwlab_v1_pdev) {
		platform_device_unregister(fwlab_v1_pdev);
		fwlab_v1_pdev = NULL;
	}
	platform_driver_unregister(&fwlab_v1_driver);
}

module_init(fwlab_v1_init);
module_exit(fwlab_v1_exit);

MODULE_DESCRIPTION("SSD FWLab C2.2 bounded VFIO IOAS copy harness");
MODULE_AUTHOR("Evanshenf and contributors");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: vfio iommufd");
