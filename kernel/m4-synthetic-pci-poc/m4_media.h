/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SSD_FWLAB_M4_MEDIA_H
#define SSD_FWLAB_M4_MEDIA_H

#include <linux/types.h>

#define FWLAB_M4_MEDIA_OPS_VERSION 1U

struct fwlab_m4_media_ops {
	u32 version;
	u32 size;
	int (*read)(void *context, void *buffer, size_t length, u64 offset);
	int (*write)(void *context, const void *buffer, size_t length, u64 offset);
	int (*flush)(void *context);
	void (*destroy)(void *context);
};

struct fwlab_m4_media {
	const struct fwlab_m4_media_ops *ops;
	void *context;
	u64 capacity;
};

int fwlab_m4_media_bind(struct fwlab_m4_media *media,
			const struct fwlab_m4_media_ops *ops,
			void *context, u64 capacity);
int fwlab_m4_media_read(struct fwlab_m4_media *media, void *buffer,
			 size_t length, u64 offset);
int fwlab_m4_media_write(struct fwlab_m4_media *media, const void *buffer,
			  size_t length, u64 offset);
int fwlab_m4_media_flush(struct fwlab_m4_media *media);
void fwlab_m4_media_unbind(struct fwlab_m4_media *media);

int fwlab_m4_media_fixture_create(const char *path, u64 capacity,
				   struct fwlab_m4_media *media);

#endif /* SSD_FWLAB_M4_MEDIA_H */
