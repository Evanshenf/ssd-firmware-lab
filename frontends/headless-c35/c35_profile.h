/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_C35_PROFILE_H
#define FWLAB_C35_PROFILE_H

#include <stdint.h>

#include "fwlab/portable/c31_types.h"
#include "fwlab/portable/nfc_types.h"

#define C35_PROFILE_VERSION 2u
#define C35_GEOMETRY_WIRE_BYTES 32u
#define C35_MEDIA_WIRE_BYTES 64u

struct c35_profile_descriptor {
    uint8_t geometry_wire[C35_GEOMETRY_WIRE_BYTES];
    uint8_t media_wire[C35_MEDIA_WIRE_BYTES];
    uint32_t geometry_id;
    uint32_t media_profile_id;
};

void c35_profile_fixed(struct c35_profile_descriptor *profile);
int c35_profile_valid(const struct c35_profile_descriptor *profile);
void c35_profile_uuid(
    const struct c35_profile_descriptor *profile,
    uint32_t image_serial,
    uint8_t uuid[16]
);
int c35_fixed_capacity_dominance_valid(
    const struct fwlab_c31_capacity *c31,
    const struct fwlab_nfc_model_config *nfc,
    uint32_t c34_inner_uid_limit,
    uint32_t c34_physical_op_limit,
    uint32_t c34_physical_sequence_limit,
    uint8_t exclusive_nfc_producer
);

#endif
