// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

/* Disposable memory/regular-file provider for the native-NVMe PoC fixture. */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "m4_media.h"

struct fwlab_m4_media_fixture {
	u8 *memory;
	struct file *file;
	u64 capacity;
};

static int fwlab_m4_media_range_valid(const struct fwlab_m4_media *media,
				      size_t length, u64 offset)
{
	u64 end;

	return media && media->ops && media->context && length &&
	       !check_add_overflow(offset, (u64)length, &end) &&
	       end <= media->capacity;
}

int fwlab_m4_media_bind(struct fwlab_m4_media *media,
			const struct fwlab_m4_media_ops *ops,
			void *context, u64 capacity)
{
	if (!media || !ops || !context || !capacity ||
	    ops->version != FWLAB_M4_MEDIA_OPS_VERSION ||
	    ops->size != sizeof(*ops) || !ops->read || !ops->write ||
	    !ops->flush || !ops->destroy)
		return -EINVAL;
	media->ops = ops;
	media->context = context;
	media->capacity = capacity;
	return 0;
}

int fwlab_m4_media_read(struct fwlab_m4_media *media, void *buffer,
			 size_t length, u64 offset)
{
	if (!buffer || !fwlab_m4_media_range_valid(media, length, offset))
		return -EINVAL;
	return media->ops->read(media->context, buffer, length, offset);
}

int fwlab_m4_media_write(struct fwlab_m4_media *media, const void *buffer,
			  size_t length, u64 offset)
{
	if (!buffer || !fwlab_m4_media_range_valid(media, length, offset))
		return -EINVAL;
	return media->ops->write(media->context, buffer, length, offset);
}

int fwlab_m4_media_flush(struct fwlab_m4_media *media)
{
	if (!media || !media->ops || !media->context)
		return -EINVAL;
	return media->ops->flush(media->context);
}

void fwlab_m4_media_unbind(struct fwlab_m4_media *media)
{
	if (!media || !media->ops || !media->context)
		return;
	media->ops->destroy(media->context);
	memset(media, 0, sizeof(*media));
}

static int fwlab_m4_file_io(struct file *file, void *buffer, size_t length,
			    loff_t offset, bool write)
{
	loff_t position = offset;
	size_t done = 0;

	while (done < length) {
		ssize_t transferred;

		if (write)
			transferred = kernel_write(file, (u8 *)buffer + done,
						   length - done, &position);
		else
			transferred = kernel_read(file, (u8 *)buffer + done,
						  length - done, &position);
		if (transferred <= 0)
			return transferred ? (int)transferred : -EIO;
		done += transferred;
	}
	return 0;
}

static int fwlab_m4_fixture_read(void *context, void *buffer, size_t length,
				 u64 offset)
{
	struct fwlab_m4_media_fixture *fixture = context;

	if (fixture->file)
		return fwlab_m4_file_io(fixture->file, buffer, length, offset,
					  false);
	memcpy(buffer, fixture->memory + offset, length);
	return 0;
}

static int fwlab_m4_fixture_write(void *context, const void *buffer,
				  size_t length, u64 offset)
{
	struct fwlab_m4_media_fixture *fixture = context;

	if (fixture->file)
		return fwlab_m4_file_io(fixture->file, (void *)buffer, length,
					  offset, true);
	memcpy(fixture->memory + offset, buffer, length);
	return 0;
}

static int fwlab_m4_fixture_flush(void *context)
{
	struct fwlab_m4_media_fixture *fixture = context;

	return fixture->file ? vfs_fsync(fixture->file, 0) : 0;
}

static void fwlab_m4_fixture_destroy(void *context)
{
	struct fwlab_m4_media_fixture *fixture = context;

	if (fixture->file) {
		vfs_fsync(fixture->file, 0);
		filp_close(fixture->file, NULL);
	}
	kvfree(fixture->memory);
	kfree(fixture);
}

static const struct fwlab_m4_media_ops fwlab_m4_fixture_ops = {
	.version = FWLAB_M4_MEDIA_OPS_VERSION,
	.size = sizeof(struct fwlab_m4_media_ops),
	.read = fwlab_m4_fixture_read,
	.write = fwlab_m4_fixture_write,
	.flush = fwlab_m4_fixture_flush,
	.destroy = fwlab_m4_fixture_destroy,
};

int fwlab_m4_media_fixture_create(const char *path, u64 capacity,
				   struct fwlab_m4_media *media)
{
	struct fwlab_m4_media_fixture *fixture;
	int ret;

	if (!media || !capacity || capacity > SIZE_MAX)
		return -EINVAL;
	fixture = kzalloc(sizeof(*fixture), GFP_KERNEL);
	if (!fixture)
		return -ENOMEM;
	fixture->capacity = capacity;
	if (path && path[0]) {
		fixture->file = filp_open(path, O_RDWR, 0);
		if (IS_ERR(fixture->file)) {
			ret = PTR_ERR(fixture->file);
			fixture->file = NULL;
			goto err_fixture;
		}
		if (!S_ISREG(file_inode(fixture->file)->i_mode) ||
		    i_size_read(file_inode(fixture->file)) != capacity) {
			ret = -EINVAL;
			goto err_fixture;
		}
	} else {
		fixture->memory = kvzalloc((size_t)capacity, GFP_KERNEL);
		if (!fixture->memory) {
			ret = -ENOMEM;
			goto err_fixture;
		}
	}

	ret = fwlab_m4_media_bind(media, &fwlab_m4_fixture_ops, fixture,
				  capacity);
	if (!ret)
		return 0;

err_fixture:
	if (fixture->file)
		filp_close(fixture->file, NULL);
	kvfree(fixture->memory);
	kfree(fixture);
	return ret;
}
