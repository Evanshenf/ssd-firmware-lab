/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef FWLAB_PRIVATE_NFC_TRACE_WINDOW_H
#define FWLAB_PRIVATE_NFC_TRACE_WINDOW_H

#include "fwlab/portable/nfc_model.h"

/* Model-specific, opt-in diagnostic consumer. Not the generic NFC provider ABI.
 * Call only with serialized model access. No operation/cache/epoch/media state
 * is reset. Trace ordinals become window-local; trace sequence stays monotonic.
 * Reference/oracle bindings that need complete history must not call this. */
enum fwlab_nfc_api_result fwlab_nfc_trace_window_retire(
    struct fwlab_nfc_model *model, uint32_t *retired);

#endif /* FWLAB_PRIVATE_NFC_TRACE_WINDOW_H */
