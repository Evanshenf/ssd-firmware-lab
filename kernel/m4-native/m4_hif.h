/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef FWLAB_M4_NATIVE_HIF_H
#define FWLAB_M4_NATIVE_HIF_H

#include <linux/types.h>

struct fwlab_m4_pci_ctx;
struct fwlab_m4_hif;

int fwlab_m4_hif_create(struct fwlab_m4_pci_ctx *pci,
			struct fwlab_m4_hif **out);
int fwlab_m4_hif_publish(struct fwlab_m4_hif *hif);
int fwlab_m4_hif_request_reset(struct fwlab_m4_hif *hif, u32 epoch);
int fwlab_m4_hif_step(struct fwlab_m4_hif *hif);
int fwlab_m4_hif_stop(struct fwlab_m4_hif *hif);
void fwlab_m4_hif_destroy(struct fwlab_m4_hif *hif);

#endif /* FWLAB_M4_NATIVE_HIF_H */
