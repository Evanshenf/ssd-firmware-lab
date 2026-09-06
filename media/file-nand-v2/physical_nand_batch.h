/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_PHYSICAL_NAND_V2_BATCH_H
#define FWLAB_PHYSICAL_NAND_V2_BATCH_H

#include "physical_nand.h"
#include "fwlab/private/nand_batch_v2.h"

/* All callbacks, geometry and UUID are bound to this same open instance.
 * An invalid/closed/quarantined instance yields a zero descriptor. */
struct fwlab_nand_batch_v2 fwlab_file_nand_v2_batch(
    struct fwlab_file_nand_v2 *media);

#endif
