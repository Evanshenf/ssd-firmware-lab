/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_TEST_VFIO_EPOCH_H
#define FWLAB_TEST_VFIO_EPOCH_H

#include <stdint.h>

#define J3_VFIO_MEMORY_BYTES 12288u
#define J3_VFIO_IOVA UINT64_C(0x40000000)

/* A literal Admin-queue producer, not an NVMe/FTL executor. */
struct j3_vfio_epoch {
    int iommu, device, irq;
    int extra_irq[2];
    uint32_t ioas;
    uint64_t bar, config;
    uint8_t *memory;
    uint8_t *mq_memory;
    int mq_mapped;
    int attached, mapped, routed;
};

void j3_vfio_init(struct j3_vfio_epoch *epoch);
int j3_vfio_open(struct j3_vfio_epoch *epoch, const char *bdf, int reused_irq_fd);
int j3_vfio_open_mq2(struct j3_vfio_epoch *epoch, const char *bdf);
int j3_vfio_mq2_run(struct j3_vfio_epoch *epoch, int worker_pid,
                    const char *worker_log, uint32_t *tested_epoch);
int j3_vfio_identify(struct j3_vfio_epoch *epoch, uint16_t cid);
int j3_vfio_data_valid(const struct j3_vfio_epoch *epoch);
int j3_vfio_complete(struct j3_vfio_epoch *epoch, uint16_t cid);
int j3_vfio_quiet(const struct j3_vfio_epoch *epoch, int old_irq);
int j3_vfio_close(struct j3_vfio_epoch *epoch);
void j3_vfio_memory_free(struct j3_vfio_epoch *epoch);

#endif /* FWLAB_TEST_VFIO_EPOCH_H */
