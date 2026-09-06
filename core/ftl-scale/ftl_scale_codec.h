/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_FTL_SCALE_CODEC_H
#define FWLAB_FTL_SCALE_CODEC_H
#include "ftl_scale_internal.h"
#define SF_DIGEST_SEED UINT64_C(1469598103934665603)
bool sf_root_encode(const struct sf_root *, uint8_t *, uint8_t *);
bool sf_root_decode(const struct fwlab_ftl_scale *, uint32_t,
                    const uint8_t *, const uint8_t *, struct sf_root *);
bool sf_cp_encode(const struct fwlab_ftl_scale *, const struct sf_root *,
                  uint32_t, uint8_t *, uint8_t *);
bool sf_cp_decode(struct fwlab_ftl_scale *, uint32_t,
                  const uint8_t *, const uint8_t *);
void sf_rail_header_encode(const struct sf_root *, uint32_t, uint8_t *, uint8_t *);
bool sf_rail_header_valid(const struct sf_root *, uint32_t,
                          const uint8_t *, const uint8_t *);
bool sf_record_encode(const struct sf_root *, const struct sf_record *,
                      uint32_t, uint32_t, uint8_t *, uint8_t *);
bool sf_record_decode(const struct sf_root *, uint32_t, uint32_t,
                      const uint8_t *, const uint8_t *, struct sf_record *);
uint64_t sf_page_digest(uint64_t, const uint8_t *, const uint8_t *);
uint32_t sf_cp_ppa(const struct sf_root *, uint32_t);
uint32_t sf_journal_ppa(const struct sf_root *, uint32_t, uint32_t);
#endif
