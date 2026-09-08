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

/* Explicit POSIX-only operating profile. Checks the complete file predicate
 * at entry/exit of each synchronous runtime operation, reusing it between
 * byte callbacks. Requires serialized exclusive backend control throughout
 * that interval; OFD locks do not prevent uncooperative external truncation.
 * This does NOT promise the default per-callback detection interval. Existing
 * constructors/default descriptors, cold recovery and hash remain strict. */
struct fwlab_nand_batch_v2 fwlab_file_nand_v2_posix_operation_batch(
    struct fwlab_file_nand_v2 *media);

#endif
