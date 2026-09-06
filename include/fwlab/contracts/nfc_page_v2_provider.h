/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_CONTRACTS_NFC_PAGE_V2_PROVIDER_H
#define FWLAB_CONTRACTS_NFC_PAGE_V2_PROVIDER_H

#include <stdbool.h>
#include "fwlab/contracts/nfc_page_v2_types.h"

/* One serialized caller/worker; callbacks must not reenter this provider.
 * Same active key and canonical shape returns ACCEPTED without reading spans
 * again. Retired UIDs cannot be admitted again. Reset permanently closes this
 * construction; create a fresh instance/epoch only after full drain. */
struct fwlab_nfc_page_v2_provider_ops {
    uint16_t version;
    uint16_t size;
    uint32_t reserved;
    struct fwlab_nfc_submit_result (*try_submit)(void *,
        const struct fwlab_nfc_page_v2_request *);
    enum fwlab_nfc_api_result (*cancel)(void *,
        const struct fwlab_nfc_operation_token *);
    enum fwlab_nfc_api_result (*step)(void *, uint32_t,
        struct fwlab_nfc_page_v2_step_result *);
    /* WRONG_STATE means still pending; only OK consumes. NULL output or both
     * absent/zero spans explicitly discards payload. Otherwise READ requires
     * exact group lengths and nonoverlapping caller-owned output spans. No
     * output is copied unless every page is valid. Invalid arguments retain
     * the result. The result/output spans must not overlap each other or the
     * model arena; a successful copy reports delivered_pages=page_count. */
    enum fwlab_nfc_api_result (*take_result)(void *,
        const struct fwlab_nfc_operation_token *,
        struct fwlab_nfc_page_v2_result *,
        const struct fwlab_nfc_page_v2_output *);
    enum fwlab_nfc_api_result (*reset_begin)(void *, uint64_t, uint32_t);
    enum fwlab_nfc_api_result (*quiescent)(void *, uint64_t, uint32_t, bool *);
};
struct fwlab_nfc_page_v2_provider {
    const struct fwlab_nfc_page_v2_provider_ops *ops;
    void *context;
};

#endif
