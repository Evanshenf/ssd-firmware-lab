/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "fwlab/private/nfc_trace_window.h"
#include "../../nfc/nfc_internal.h"

#include <string.h>

/* The frozen C3 reference keeps its complete-trace/fault-on-full behavior.
 * This separate NFC-owned consumer is used only by the live lab binding. */
enum fwlab_nfc_api_result fwlab_nfc_trace_window_retire(
    struct fwlab_nfc_model *model, uint32_t *retired)
{
    uint32_t index;
    if (!c33_model_valid(model) || retired == NULL)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    if (model->phase != FWLAB_NFC_MODEL_READY || model->event_count != 0)
        return FWLAB_NFC_API_WRONG_STATE;
    for (index = 0; index < model->config.capacity.operations; ++index)
        if (model->operation[index].state != C33_OP_FREE)
            return FWLAB_NFC_API_WRONG_STATE;
    if (model->trace_count > model->config.capacity.trace_entries)
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    *retired = model->trace_count;
    memset(model->trace, 0, (size_t)model->trace_count * sizeof(*model->trace));
    model->trace_count = 0;
    return FWLAB_NFC_API_OK;
}
