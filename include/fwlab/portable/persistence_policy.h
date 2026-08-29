/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PORTABLE_PERSISTENCE_POLICY_H
#define FWLAB_PORTABLE_PERSISTENCE_POLICY_H

#include <stddef.h>

#include "fwlab/contracts/persistence_facts.h"

enum fwlab_persist_result fwlab_persist_profile_validate(
    const struct fwlab_persist_profile *profile
);

enum fwlab_persist_result fwlab_persist_plp_validate(
    const struct fwlab_persist_profile *profile,
    const struct fwlab_persist_plp_envelope *envelope
);

enum fwlab_persist_result fwlab_persist_command_witness(
    const struct fwlab_persist_profile *profile,
    const struct fwlab_persist_request *request,
    const struct fwlab_persist_atom_fact *facts,
    size_t fact_count,
    const struct fwlab_persist_plp_envelope *envelopes,
    size_t envelope_count,
    struct fwlab_persist_witness *witness
);

enum fwlab_persist_result fwlab_persist_fence_witness(
    const struct fwlab_persist_profile *profile,
    const struct fwlab_persist_request *fence,
    const struct fwlab_persist_obligation *obligations,
    size_t obligation_count,
    const struct fwlab_persist_plp_envelope *envelopes,
    size_t envelope_count,
    struct fwlab_persist_witness *witness
);

#endif
