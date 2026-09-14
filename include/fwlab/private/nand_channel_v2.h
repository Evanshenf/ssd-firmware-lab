/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_PRIVATE_NAND_CHANNEL_V2_H
#define FWLAB_PRIVATE_NAND_CHANNEL_V2_H

#include "fwlab/private/nand_batch_v2.h"

#define FWLAB_NAND_CHANNEL_V2_VERSION 1u
#define FWLAB_NAND_CHANNEL_V2_MAX 4u

/* Physical assembly, not a logical Block service. Global PPA channel selects
 * one child with local channel zero; main/OOB bytes are never translated.
 * Each child has its own UUID, local geometry and exclusively owned state.
 * The immutable descriptors and their owners outlive every admitted request,
 * result, frame and retirement acknowledgment. Unused children are zero.
 *
 * aggregate is real global-PPA scalar routing for construction/quiescent
 * diagnostics. It is NOT an independently usable concurrent path while actors
 * own the children. The composition serializes that access; OS placement and
 * file names do not cross this portable boundary. No global media sequence is
 * derived from the independent child transaction counters. */
struct fwlab_nand_channel_v2 {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    struct fwlab_nfc_geometry geometry;
    uint8_t media_uuid[16];
    struct fwlab_nand_media aggregate;
    struct fwlab_nand_batch_v2 channel[FWLAB_NAND_CHANNEL_V2_MAX];
};

#endif
