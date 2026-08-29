/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include <stdint.h>
#include <string.h>

#include "fwlab/portable/persistence_policy.h"

int main(void)
{
    struct fwlab_persist_profile profile;

    memset(&profile, 0, sizeof(profile));
    profile.version = FWLAB_PERSIST_VERSION;
    profile.size = (uint16_t)sizeof(profile);
    profile.cache_enabled = 1;
    profile.plp_kind = FWLAB_PERSIST_PLP_NONE;
    return fwlab_persist_profile_validate(&profile) == FWLAB_PERSIST_OK ? 0 : 1;
}
