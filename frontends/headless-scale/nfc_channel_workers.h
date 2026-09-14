/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef FWLAB_NFC_CHANNEL_WORKERS_H
#define FWLAB_NFC_CHANNEL_WORKERS_H

#include "fwlab/private/nfc_channel_v2_job.h"

#define FWLAB_NFC_CHANNEL_WORKERS_MAX 4u
#define FWLAB_NFC_CHANNEL_WORKER_AFFINITY_WORDS 16u

struct fwlab_nfc_channel_v2;
struct fwlab_nfc_channel_workers;
struct fwlab_nfc_channel_workers_config {
    uint32_t channels; /* 1..4; four workers requires four channels. */
    uint32_t workers;  /* 1 owns every channel; 4 owns channel i on worker i. */
    uint8_t pin_workers;
    uint8_t reserved[3];
    int32_t cpus[FWLAB_NFC_CHANNEL_WORKERS_MAX]; /* Used only when pin_workers=1. */
};
struct fwlab_nfc_channel_worker_stats {
    uint64_t thread_id, cpu_ns, completed_jobs, actor_quanta;
    uint64_t affinity[FWLAB_NFC_CHANNEL_WORKER_AFFINITY_WORDS];
    uint32_t channel_mask;
    int32_t requested_cpu;
    uint8_t created, joined, exited, failed;
};
struct fwlab_nfc_channel_workers_stats {
    uint64_t submitted_jobs, returned_jobs, wait_calls, wake_events;
    uint32_t channels, workers, created_workers, joined_workers, occupied_mailboxes;
    uint8_t stopping, failed, reserved[2];
    struct fwlab_nfc_channel_worker_stats worker[FWLAB_NFC_CHANNEL_WORKERS_MAX];
};

/* Creates only idle joinable workers; no actor or media call precedes submit.
 * Runtime/control calls have one serialized coordinator. Jobs and granted
 * frames outlive their executor ownership. The caller owns the hub/media.
 * Ordinary failure returns NULL after stopping/joining partial workers. An
 * exceptional failed join returns INVARIANT_FAILURE with a retained non-NULL
 * handle: no job admission is possible, and shutdown/ownership remains due. */
enum fwlab_nfc_api_result fwlab_nfc_channel_workers_create(
    const struct fwlab_nfc_channel_workers_config *, struct fwlab_nfc_channel_workers **);
struct fwlab_nfc_channel_executor fwlab_nfc_channel_workers_executor(struct fwlab_nfc_channel_workers *);

/* Linux composition helper, never an FTL callback. Only the hub's explicit
 * all-posted external-wait state may block. Drains the doorbell then rechecks
 * hub eligibility and acquire-visible replies before bounded polling (<=1s).
 * notified is a wake hint, not an idle, completion or quiescence certificate. */
enum fwlab_nfc_api_result fwlab_nfc_channel_workers_wait(
    struct fwlab_nfc_channel_workers *, const struct fwlab_nfc_channel_v2 *,
    uint32_t timeout_ms, bool *notified);

/* Cumulative worker CPU from startup handshake; capture phase deltas while
 * live. Final CPU and affinity remain available after join. Coordinator CPU
 * is intentionally not included. Counts include only synchronized job replies;
 * affinity is the actual readback mask for Linux CPU indices0..1023. */
enum fwlab_nfc_api_result fwlab_nfc_channel_workers_snapshot(
    struct fwlab_nfc_channel_workers *, struct fwlab_nfc_channel_workers_stats *);

/* Requires all mailboxes returned and every created worker actually joined.
 * Never implicitly cancels, detaches, closes media or frees a failed join. */
enum fwlab_nfc_api_result fwlab_nfc_channel_workers_destroy(struct fwlab_nfc_channel_workers *);

#endif
