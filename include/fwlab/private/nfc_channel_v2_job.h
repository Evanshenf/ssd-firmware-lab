/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_PRIVATE_NFC_CHANNEL_V2_JOB_H
#define FWLAB_PRIVATE_NFC_CHANNEL_V2_JOB_H

#include "fwlab/private/nfc_page_v2_lab.h"

#define FWLAB_NFC_CHANNEL_JOB_SLOTS 4u

struct fwlab_nfc_channel_actor;
enum fwlab_nfc_channel_command {
    FWLAB_CHANNEL_PREP = 1,
    FWLAB_CHANNEL_RUN,
    FWLAB_CHANNEL_RETIRE,
    FWLAB_CHANNEL_CLOSE
};

/* Separate frame grant: returning a PREP/RUN job does not recycle a frame.
 * RUN publishes immutable reports; RETIRE acknowledgement returns ownership. */
struct fwlab_nfc_channel_frame {
    struct fwlab_nfc_page_v2_result result;
    _Alignas(64) uint8_t main[FWLAB_NFC_PAGE_V2_MAX_PAGES * FWLAB_NFC_PAGE_V2_MAIN_BYTES];
    uint8_t oob[FWLAB_NFC_PAGE_V2_MAX_PAGES * FWLAB_NFC_PAGE_V2_OOB_BYTES];
};
struct fwlab_nfc_channel_job_entry {
    struct fwlab_nfc_page_v2_request request;
    struct fwlab_nfc_channel_frame *frame;
};
struct fwlab_nfc_channel_reply {
    uint64_t job_sequence, batch_uid, terminal_ns;
    uint32_t channel, command, status;
    uint8_t accepted_mask, completed_mask, lower_owned_mask, reports_held_mask;
    uint8_t closed, quiet, reserved[2];
    struct fwlab_nfc_page_v2_lab_stats stats;
    uint32_t trace_start, trace_count;
    struct fwlab_nfc_page_v2_lab_trace trace[FWLAB_NFC_PAGE_V2_LAB_TRACE_CAPACITY];
};
/* Input is immutable after submit until poll returns this exact stable job.
 * Entry index is the global credit index; slot_mask selects valid entries.
 * Reply is actor-owned until executor synchronization returns job ownership. */
struct fwlab_nfc_channel_job {
    struct fwlab_nfc_channel_actor *actor;
    uint64_t job_sequence, batch_uid, admission_floor_ns;
    uint32_t channel, command;
    uint8_t slot_mask;
    struct fwlab_nfc_channel_job_entry entry[FWLAB_NFC_CHANNEL_JOB_SLOTS];
    struct fwlab_nfc_channel_reply reply;
};

/* One semantic interpreter for cooperative and threaded execution. A valid
 * job completes with reply.status and truthful retained resource masks even
 * on actor failure. A quantum never invokes more than one child step (budget
 * one) or consumes more than one child result. No wall-time completion rule. */
enum fwlab_nfc_api_result fwlab_nfc_channel_v2_actor_job_step(
    struct fwlab_nfc_channel_job *, bool *advanced, bool *complete);

struct fwlab_nfc_channel_executor_ops {
    /* OK transfers job. NO_CAPACITY transfers nothing. */
    enum fwlab_nfc_api_result (*submit)(void *, struct fwlab_nfc_channel_job *);
    /* OK: returned job is owned by caller; NULL means none yet. advanced is
     * actual cooperative progress, not unchanged asynchronous polling. */
    enum fwlab_nfc_api_result (*poll)(void *, uint32_t channel,
                                    struct fwlab_nfc_channel_job **, bool *advanced);
    /* Only after actor CLOSE/no jobs; complete includes actual worker joins.
     * Also valid for construction cleanup before any job has been submitted. */
    enum fwlab_nfc_api_result (*shutdown)(void *, bool *advanced, bool *complete);
};
struct fwlab_nfc_channel_executor {
    const struct fwlab_nfc_channel_executor_ops *ops;
    void *context;
};

#endif
