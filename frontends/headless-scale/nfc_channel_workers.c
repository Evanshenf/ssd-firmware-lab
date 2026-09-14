/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
#define _GNU_SOURCE
#include "nfc_channel_workers.h"
#include "fwlab/private/nfc_channel_v2.h"

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define WORKERS_MAGIC UINT64_C(0x4e4643574f524b31)
#define WORKERS FWLAB_NFC_CHANNEL_WORKERS_MAX
enum mailbox_state { MAILBOX_EMPTY, MAILBOX_QUEUED, MAILBOX_RUNNING, MAILBOX_REPLIED, MAILBOX_BROKEN };

struct worker_mailbox {
    atomic_uint state;
    struct fwlab_nfc_channel_job *job;
    enum fwlab_nfc_api_result result;
    uint64_t quanta;
};
struct channel_worker {
    struct fwlab_nfc_channel_workers *runtime;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    clockid_t cpu_clock;
    cpu_set_t affinity;
    uint64_t thread_id, cpu_start, cpu_final;
    uint64_t completed_jobs, actor_quanta; /* Coordinator-only cached counters. */
    uint32_t channel_mask, cursor;
    int requested_cpu, startup_error;
    atomic_int error;
    uint8_t mutex_initialized, condition_initialized, created, joined;
    uint8_t startup_collected; /* Coordinator-only ACK consumption. */
    uint8_t startup_done, stop, exited; /* Worker sleep-mutex protected. */
};
struct fwlab_nfc_channel_workers {
    uint64_t magic, submitted_jobs, returned_jobs, wait_calls, wake_events;
    struct fwlab_nfc_channel_workers_config config;
    struct channel_worker worker[WORKERS];
    struct worker_mailbox mailbox[WORKERS];
    uint32_t created, joined, startup_acks;
    enum fwlab_nfc_api_result start_result;
    int completion_fd;
    uint8_t published, stopping, failed, join_wait, join_failed;
};
_Static_assert(CPU_SETSIZE <= FWLAB_NFC_CHANNEL_WORKER_AFFINITY_WORDS * 64u,
               "private affinity bitmap covers the supported Linux CPU set");

static bool live(const struct fwlab_nfc_channel_workers *r)
{ return r && r->magic == WORKERS_MAGIC; }

/* A corrupted synchronization primitive cannot support an ordinary cleanup
 * return. This fail-stop path neither frees live ownership nor cancels threads. */
static void synchronized(int error)
{
    if (error) {
        fprintf(stderr, "NFC_CHANNEL_WORKERS_FATAL|synchronization_error=%d|ownership_not_released=1\n", error);
        abort();
    }
}
static bool clock_ns(clockid_t clock, uint64_t *ns)
{
    struct timespec time;
    if (clock_gettime(clock, &time) != 0 || time.tv_sec < 0 || time.tv_nsec < 0 ||
        time.tv_nsec >= 1000000000 || (uint64_t)time.tv_sec > UINT64_MAX / UINT64_C(1000000000)) return false;
    uint64_t seconds = (uint64_t)time.tv_sec * UINT64_C(1000000000);
    if ((uint64_t)time.tv_nsec > UINT64_MAX - seconds) return false;
    *ns = seconds + (uint64_t)time.tv_nsec;
    return true;
}
static void counter(struct fwlab_nfc_channel_workers *r, uint64_t *value, uint64_t add)
{
    if (add > UINT64_MAX - *value) { *value = UINT64_MAX; r->failed = 1; }
    else *value += add;
}
static uint32_t owner(const struct fwlab_nfc_channel_workers *r, uint32_t channel)
{ return r->config.workers == 1 ? 0 : channel; }
static bool workers_failed(const struct fwlab_nfc_channel_workers *r)
{
    if (r->failed || r->start_result != FWLAB_NFC_API_OK) return true;
    for (uint32_t i = 0; i < r->created; ++i)
        if (atomic_load_explicit(&r->worker[i].error, memory_order_acquire)) return true;
    return false;
}
static uint32_t occupied(const struct fwlab_nfc_channel_workers *r)
{
    uint32_t count = 0;
    for (uint32_t c = 0; c < r->config.channels; ++c)
        count += atomic_load_explicit(&r->mailbox[c].state, memory_order_acquire) != MAILBOX_EMPTY;
    return count;
}
static bool visible_reply(const struct fwlab_nfc_channel_workers *r)
{
    for (uint32_t c = 0; c < r->config.channels; ++c) {
        unsigned state = atomic_load_explicit(&r->mailbox[c].state, memory_order_acquire);
        if (state == MAILBOX_REPLIED || state == MAILBOX_BROKEN) return true;
    }
    return false;
}
static void notify_worker(struct channel_worker *w)
{
    uint64_t one = 1;
    ssize_t result;
    do { result = write(w->runtime->completion_fd, &one, sizeof(one)); } while (result < 0 && errno == EINTR);
    if (result != (ssize_t)sizeof(one) && !(result < 0 && errno == EAGAIN))
        atomic_store_explicit(&w->error, FWLAB_NFC_API_INVARIANT_FAILURE, memory_order_release);
}
static struct worker_mailbox *queued(struct channel_worker *w)
{
    for (uint32_t i = 0; i < w->runtime->config.channels; ++i) {
        uint32_t c = w->cursor++ % w->runtime->config.channels;
        if ((w->channel_mask & (1u << c)) &&
            atomic_load_explicit(&w->runtime->mailbox[c].state, memory_order_acquire) == MAILBOX_QUEUED)
            return &w->runtime->mailbox[c];
    }
    return NULL;
}
static void execute(struct channel_worker *w, struct worker_mailbox *mailbox)
{
    bool complete = false;
    atomic_store_explicit(&mailbox->state, MAILBOX_RUNNING, memory_order_release);
    mailbox->quanta = 0;
    while (!complete) {
        bool advanced = false;
        enum fwlab_nfc_api_result result = fwlab_nfc_channel_v2_actor_job_step(mailbox->job, &advanced, &complete);
        if (result != FWLAB_NFC_API_OK || (!advanced && !complete)) {
            /* The actor has no external dependency inside one owned job.
             * Missing progress is broken ownership, not synthetic completion. */
            mailbox->result = result == FWLAB_NFC_API_OK ? FWLAB_NFC_API_INVARIANT_FAILURE : result;
            atomic_store_explicit(&w->error, mailbox->result, memory_order_release);
            atomic_store_explicit(&mailbox->state, MAILBOX_BROKEN, memory_order_release);
            notify_worker(w);
            return;
        }
        if (advanced) ++mailbox->quanta; /* One job contains a finite admitted set. */
    }
    mailbox->result = FWLAB_NFC_API_OK;
    atomic_store_explicit(&mailbox->state, MAILBOX_REPLIED, memory_order_release);
    /* No job/frame/mailbox payload access follows publication. */
    notify_worker(w);
}
static void *worker_main(void *opaque)
{
    struct channel_worker *w = opaque;
    long tid = syscall(SYS_gettid);
    int error = pthread_getcpuclockid(pthread_self(), &w->cpu_clock);
    if (!error) error = pthread_getaffinity_np(pthread_self(), sizeof(w->affinity), &w->affinity);
    if (!error && w->requested_cpu >= 0 &&
        (CPU_COUNT(&w->affinity) != 1 || !CPU_ISSET(w->requested_cpu, &w->affinity))) error = EINVAL;
    if (!error && (tid <= 0 || !clock_ns(CLOCK_THREAD_CPUTIME_ID, &w->cpu_start))) error = EINVAL;
    synchronized(pthread_mutex_lock(&w->mutex));
    w->thread_id = tid > 0 ? (uint64_t)tid : 0;
    w->startup_error = error; w->startup_done = 1;
    synchronized(pthread_cond_signal(&w->condition));
    notify_worker(w); /* Startup has no hub, but uses the same wake doorbell. */
    for (;;) {
        struct worker_mailbox *mailbox;
        if (w->stop) break;
        mailbox = error ? NULL : queued(w);
        if (!mailbox) {
            synchronized(pthread_cond_wait(&w->condition, &w->mutex));
            continue;
        }
        synchronized(pthread_mutex_unlock(&w->mutex));
        execute(w, mailbox); /* Never hold a wake mutex over actor/media work. */
        synchronized(pthread_mutex_lock(&w->mutex));
    }
    uint64_t now = 0;
    if (!error && clock_ns(CLOCK_THREAD_CPUTIME_ID, &now) && now >= w->cpu_start)
        w->cpu_final = now - w->cpu_start;
    else if (!error) atomic_store_explicit(&w->error, FWLAB_NFC_API_INVARIANT_FAILURE, memory_order_release);
    w->exited = 1;
    synchronized(pthread_mutex_unlock(&w->mutex));
    notify_worker(w);
    return NULL;
}

static enum fwlab_nfc_api_result submit(void *opaque, struct fwlab_nfc_channel_job *job)
{
    struct fwlab_nfc_channel_workers *r = opaque;
    struct worker_mailbox *mailbox;
    struct channel_worker *w;
    if (!live(r) || !job || !job->actor || !job->job_sequence || job->channel >= r->config.channels ||
        job->command < FWLAB_CHANNEL_PREP || job->command > FWLAB_CHANNEL_CLOSE) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (!r->published || r->stopping) return FWLAB_NFC_API_WRONG_STATE;
    if (workers_failed(r)) return FWLAB_NFC_API_INVARIANT_FAILURE;
    mailbox = &r->mailbox[job->channel];
    if (atomic_load_explicit(&mailbox->state, memory_order_acquire) != MAILBOX_EMPTY)
        return FWLAB_NFC_API_NO_CAPACITY;
    if (r->submitted_jobs == UINT64_MAX) return FWLAB_NFC_API_COUNTER_EXHAUSTED;
    w = &r->worker[owner(r, job->channel)];
    synchronized(pthread_mutex_lock(&w->mutex));
    mailbox->job = job;
    mailbox->result = FWLAB_NFC_API_OK;
    atomic_store_explicit(&mailbox->state, MAILBOX_QUEUED, memory_order_release);
    synchronized(pthread_cond_signal(&w->condition));
    synchronized(pthread_mutex_unlock(&w->mutex));
    ++r->submitted_jobs;
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result poll_job(void *opaque, uint32_t channel,
    struct fwlab_nfc_channel_job **job, bool *advanced)
{
    struct fwlab_nfc_channel_workers *r = opaque;
    struct worker_mailbox *mailbox;
    unsigned state;
    if (!live(r) || !job || !advanced || channel >= r->config.channels) return FWLAB_NFC_API_INVALID_CONTRACT;
    *job = NULL; *advanced = false;
    mailbox = &r->mailbox[channel];
    state = atomic_load_explicit(&mailbox->state, memory_order_acquire);
    if (state == MAILBOX_BROKEN) return mailbox->result; /* Retain the job. */
    if (state != MAILBOX_REPLIED) return FWLAB_NFC_API_OK;
    struct channel_worker *w = &r->worker[owner(r, channel)];
    counter(r, &w->completed_jobs, 1); counter(r, &w->actor_quanta, mailbox->quanta);
    counter(r, &r->returned_jobs, 1);
    *job = mailbox->job; mailbox->job = NULL;
    atomic_store_explicit(&mailbox->state, MAILBOX_EMPTY, memory_order_release);
    *advanced = true;
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result shutdown_common(void *opaque, bool *advanced,
    bool *complete, bool blocking)
{
    struct fwlab_nfc_channel_workers *r = opaque;
    if (!live(r) || !advanced || !complete) return FWLAB_NFC_API_INVALID_CONTRACT;
    *advanced = *complete = false;
    if (occupied(r)) return FWLAB_NFC_API_WRONG_STATE;
    if (r->join_failed) return FWLAB_NFC_API_INVARIANT_FAILURE;
    if (!r->stopping) {
        r->stopping = 1;
        for (uint32_t i = 0; i < r->created; ++i) {
            struct channel_worker *w = &r->worker[i];
            synchronized(pthread_mutex_lock(&w->mutex));
            w->stop = 1;
            synchronized(pthread_cond_signal(&w->condition));
            synchronized(pthread_mutex_unlock(&w->mutex));
        }
        *advanced = true;
    }
    for (uint32_t i = 0; i < r->created; ++i) {
        struct channel_worker *w = &r->worker[i];
        if (w->joined) continue;
        /* All jobs are returned. Join one worker, never detach, cancel or
         * substitute an exit flag. Even a tryjoin is not a realtime promise. */
        int error = blocking ? pthread_join(w->thread, NULL) : pthread_tryjoin_np(w->thread, NULL);
        if (!blocking && error == EBUSY) {
            r->join_wait = 1;
            return FWLAB_NFC_API_OK;
        }
        r->join_wait = 0;
        if (error) { r->failed = r->join_failed = 1; return FWLAB_NFC_API_INVARIANT_FAILURE; }
        w->joined = 1; ++r->joined; *advanced = true;
        break;
    }
    *complete = r->created == r->joined;
    if (*complete) r->join_wait = 0;
    return FWLAB_NFC_API_OK;
}
static enum fwlab_nfc_api_result shutdown_workers(void *opaque, bool *advanced, bool *complete)
{ return shutdown_common(opaque, advanced, complete, true); }
static enum fwlab_nfc_api_result shutdown_polling(void *opaque, bool *advanced, bool *complete)
{ return shutdown_common(opaque, advanced, complete, false); }
static const struct fwlab_nfc_channel_executor_ops executor_ops = {
    submit, poll_job, shutdown_workers
};
static const struct fwlab_nfc_channel_executor_ops polling_executor_ops = {
    submit, poll_job, shutdown_polling
};
struct fwlab_nfc_channel_executor fwlab_nfc_channel_workers_executor(struct fwlab_nfc_channel_workers *r)
{
    struct fwlab_nfc_channel_executor executor = {0};
    if (live(r)) { executor.ops = &executor_ops; executor.context = r; }
    return executor;
}
struct fwlab_nfc_channel_executor fwlab_nfc_channel_workers_executor_polling(struct fwlab_nfc_channel_workers *r)
{
    struct fwlab_nfc_channel_executor executor = {0};
    if (live(r)) { executor.ops = &polling_executor_ops; executor.context = r; }
    return executor;
}

enum fwlab_nfc_api_result fwlab_nfc_channel_workers_prepare(
    const struct fwlab_nfc_channel_workers_config *config, struct fwlab_nfc_channel_workers **out)
{
    struct fwlab_nfc_channel_workers *r;
    enum fwlab_nfc_api_result result = FWLAB_NFC_API_NO_CAPACITY;
    if (out) *out = NULL;
    if (!out || !config || !config->channels || config->channels > WORKERS ||
        (config->workers != 1 && config->workers != 4) ||
        (config->workers == 4 && config->channels != 4) || config->pin_workers > 1 ||
        config->reserved[0] || config->reserved[1] || config->reserved[2]) return FWLAB_NFC_API_INVALID_CONTRACT;
    for (uint32_t i = 0; i < config->workers; ++i)
        if (config->pin_workers && (config->cpus[i] < 0 || config->cpus[i] >= CPU_SETSIZE))
            return FWLAB_NFC_API_INVALID_CONTRACT;
    r = calloc(1, sizeof(*r));
    if (!r) return result;
    r->magic = WORKERS_MAGIC; r->config = *config; r->completion_fd = -1;
    r->start_result = FWLAB_NFC_API_OK;
    for (uint32_t i = 0; i < WORKERS; ++i) {
        atomic_init(&r->mailbox[i].state, MAILBOX_EMPTY);
        atomic_init(&r->worker[i].error, 0);
    }
    r->completion_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (r->completion_fd < 0) goto failed;
    for (uint32_t i = 0; i < config->workers; ++i) {
        struct channel_worker *w = &r->worker[i];
        w->runtime = r; w->requested_cpu = config->pin_workers ? config->cpus[i] : -1;
        w->channel_mask = config->workers == 1 ? (1u << config->channels) - 1u : 1u << i;
        if (pthread_mutex_init(&w->mutex, NULL) != 0) goto failed;
        w->mutex_initialized = 1;
        if (pthread_cond_init(&w->condition, NULL) != 0) goto failed;
        w->condition_initialized = 1;
    }
    *out = r;
    return FWLAB_NFC_API_OK;
failed:
    /* No pthread_create has occurred: only local allocation/primitives exist. */
    if (fwlab_nfc_channel_workers_destroy(r) != FWLAB_NFC_API_OK) {
        *out = r; return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    return result;
}

enum fwlab_nfc_api_result fwlab_nfc_channel_workers_start_step(
    struct fwlab_nfc_channel_workers *r, bool *advanced, bool *complete)
{
    if (!live(r) || !advanced || !complete) return FWLAB_NFC_API_INVALID_CONTRACT;
    *advanced = *complete = false;
    if (r->stopping) return FWLAB_NFC_API_WRONG_STATE;
    if (r->start_result != FWLAB_NFC_API_OK) return r->start_result;
    if (workers_failed(r)) return FWLAB_NFC_API_INVARIANT_FAILURE;
    if (r->published) { *complete = true; return FWLAB_NFC_API_OK; }
    if (r->created < r->config.workers) {
        uint32_t i = r->created;
        struct channel_worker *w = &r->worker[i];
        pthread_attr_t attributes;
        int error;
        if (pthread_attr_init(&attributes) != 0) {
            r->start_result = FWLAB_NFC_API_NO_CAPACITY;
            return r->start_result;
        }
        if (r->config.pin_workers) {
            cpu_set_t selected;
            CPU_ZERO(&selected); CPU_SET(r->config.cpus[i], &selected);
            error = pthread_attr_setaffinity_np(&attributes, sizeof(selected), &selected);
            if (error) {
                synchronized(pthread_attr_destroy(&attributes));
                r->start_result = FWLAB_NFC_API_INVALID_CONTRACT;
                return r->start_result;
            }
        }
        error = pthread_create(&w->thread, &attributes, worker_main, w);
        synchronized(pthread_attr_destroy(&attributes));
        if (error) { r->start_result = FWLAB_NFC_API_NO_CAPACITY; return r->start_result; }
        w->created = 1; ++r->created;
        *advanced = true;
        return FWLAB_NFC_API_OK;
    }
    for (uint32_t i = 0; i < r->created; ++i) {
        struct channel_worker *w = &r->worker[i];
        if (w->startup_collected) continue;
        int error = pthread_mutex_trylock(&w->mutex);
        if (error == EBUSY) continue;
        synchronized(error);
        if (!w->startup_done) {
            synchronized(pthread_mutex_unlock(&w->mutex));
            continue;
        }
        error = w->startup_error;
        synchronized(pthread_mutex_unlock(&w->mutex));
        w->startup_collected = 1; ++r->startup_acks; *advanced = true;
        if (error) { r->start_result = FWLAB_NFC_API_INVALID_CONTRACT; return r->start_result; }
        if (r->startup_acks == r->config.workers) { r->published = 1; *complete = true; }
        return FWLAB_NFC_API_OK;
    }
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_nfc_channel_workers_create(
    const struct fwlab_nfc_channel_workers_config *config, struct fwlab_nfc_channel_workers **out)
{
    struct fwlab_nfc_channel_workers *r = NULL;
    enum fwlab_nfc_api_result result = fwlab_nfc_channel_workers_prepare(config, out);
    bool complete = false, advanced;
    if (result != FWLAB_NFC_API_OK) return result;
    r = *out; *out = NULL;
    while (!complete) {
        result = fwlab_nfc_channel_workers_start_step(r, &advanced, &complete);
        if (result != FWLAB_NFC_API_OK) goto failed;
        if (!advanced && !complete) {
            /* Preserve the blocking convenience API, using the same startup
             * state rather than a second thread-creation implementation. */
            for (uint32_t i = 0; i < r->created; ++i) {
                struct channel_worker *w = &r->worker[i];
                if (w->startup_collected) continue;
                synchronized(pthread_mutex_lock(&w->mutex));
                while (!w->startup_done) synchronized(pthread_cond_wait(&w->condition, &w->mutex));
                synchronized(pthread_mutex_unlock(&w->mutex));
                break;
            }
        }
    }
    *out = r;
    return FWLAB_NFC_API_OK;
failed:
    complete = false;
    while (!complete) {
        if (shutdown_workers(r, &advanced, &complete) != FWLAB_NFC_API_OK) {
            *out = r; return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
    }
    if (fwlab_nfc_channel_workers_destroy(r) != FWLAB_NFC_API_OK) {
        *out = r; return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    return result;
}

enum fwlab_nfc_api_result fwlab_nfc_channel_workers_wait(
    struct fwlab_nfc_channel_workers *r, const struct fwlab_nfc_channel_v2 *hub,
    uint32_t timeout_ms, bool *notified)
{
    bool eligible = false;
    uint64_t notifications;
    ssize_t bytes;
    struct pollfd fd;
    int result;
    if (!live(r) || !hub || !notified || timeout_ms > 1000) return FWLAB_NFC_API_INVALID_CONTRACT;
    *notified = false;
    if (fwlab_nfc_channel_v2_external_wait(hub, &eligible) != FWLAB_NFC_API_OK) return FWLAB_NFC_API_INVARIANT_FAILURE;
    if (!eligible) return FWLAB_NFC_API_OK;
    counter(r, &r->wait_calls, 1);
    do {
        bytes = read(r->completion_fd, &notifications, sizeof(notifications));
        if (bytes == (ssize_t)sizeof(notifications)) counter(r, &r->wake_events, notifications);
    } while (bytes == (ssize_t)sizeof(notifications) || (bytes < 0 && errno == EINTR));
    if (bytes >= 0 || errno != EAGAIN || workers_failed(r)) return FWLAB_NFC_API_INVARIANT_FAILURE;
    if (fwlab_nfc_channel_v2_external_wait(hub, &eligible) != FWLAB_NFC_API_OK) return FWLAB_NFC_API_INVARIANT_FAILURE;
    if (!eligible || visible_reply(r) || r->stopping) {
        *notified = true; return FWLAB_NFC_API_OK;
    }
    if (!occupied(r)) return FWLAB_NFC_API_OK;
    fd = (struct pollfd){ .fd = r->completion_fd, .events = POLLIN };
    result = poll(&fd, 1, (int)timeout_ms);
    /* EINTR returns to the coordinator; retrying a full timeout could extend
     * a bounded wait indefinitely. Readable bytes are drained on the next call. */
    if (result < 0 && errno == EINTR) return FWLAB_NFC_API_OK;
    if (result < 0 || (fd.revents & (POLLERR | POLLHUP | POLLNVAL))) return FWLAB_NFC_API_INVARIANT_FAILURE;
    *notified = result > 0;
    return FWLAB_NFC_API_OK;
}

static enum fwlab_nfc_api_result lifecycle_pending(
    struct fwlab_nfc_channel_workers *r, bool *pending)
{
    *pending = false;
    if (r->join_failed) return FWLAB_NFC_API_INVARIANT_FAILURE;
    if (r->stopping) {
        *pending = r->join_wait && r->joined < r->created && !occupied(r);
        return FWLAB_NFC_API_OK;
    }
    if (r->published || workers_failed(r) || r->created < r->config.workers)
        return FWLAB_NFC_API_OK; /* Ready, failed, or local thread-create work. */
    bool waiting = false;
    for (uint32_t i = 0; i < r->created; ++i) {
        struct channel_worker *w = &r->worker[i];
        if (w->startup_collected) continue;
        int error = pthread_mutex_trylock(&w->mutex);
        if (error == EBUSY) return FWLAB_NFC_API_OK; /* No proven wait snapshot. */
        synchronized(error);
        bool done = w->startup_done != 0;
        synchronized(pthread_mutex_unlock(&w->mutex));
        if (done) return FWLAB_NFC_API_OK; /* A startup ACK can be collected. */
        waiting = true;
    }
    *pending = waiting;
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_nfc_channel_workers_lifecycle_wait(
    struct fwlab_nfc_channel_workers *r, uint32_t timeout_ms,
    bool *eligible, bool *notified)
{
    uint64_t notifications;
    ssize_t bytes;
    struct pollfd fd;
    int result;
    enum fwlab_nfc_api_result status;
    if (!live(r) || !eligible || !notified || timeout_ms > 1)
        return FWLAB_NFC_API_INVALID_CONTRACT;
    *eligible = *notified = false;
    status = lifecycle_pending(r, eligible);
    if (status != FWLAB_NFC_API_OK || !*eligible) return status;
    counter(r, &r->wait_calls, 1);
    /* One nonblocking read drains the accumulated eventfd count. New events
     * racing this read remain readable for poll; EINTR returns to control. */
    bytes = read(r->completion_fd, &notifications, sizeof(notifications));
    if (bytes < 0 && errno == EINTR) return FWLAB_NFC_API_OK;
    if (bytes == (ssize_t)sizeof(notifications)) {
        counter(r, &r->wake_events, notifications);
        *notified = true;
    } else if (bytes >= 0 || errno != EAGAIN) return FWLAB_NFC_API_INVARIANT_FAILURE;
    if (r->failed) return FWLAB_NFC_API_INVARIANT_FAILURE;
    status = lifecycle_pending(r, eligible);
    if (status != FWLAB_NFC_API_OK || !*eligible || *notified) return status;
    /* An exit flag is deliberately not a permanent immediate-return hint:
     * tryjoin can still report EBUSY until the thread's actual final return. */
    fd = (struct pollfd){ .fd = r->completion_fd, .events = POLLIN };
    result = poll(&fd, 1, (int)timeout_ms);
    if (result < 0 && errno == EINTR) return FWLAB_NFC_API_OK;
    if (result < 0 || (fd.revents & (POLLERR | POLLHUP | POLLNVAL)))
        return FWLAB_NFC_API_INVARIANT_FAILURE;
    *notified = result > 0;
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_nfc_channel_workers_snapshot(
    struct fwlab_nfc_channel_workers *r, struct fwlab_nfc_channel_workers_stats *out)
{
    if (!live(r) || !out) return FWLAB_NFC_API_INVALID_CONTRACT;
    memset(out, 0, sizeof(*out));
    out->channels = r->config.channels; out->workers = r->config.workers;
    out->created_workers = r->created; out->joined_workers = r->joined;
    out->occupied_mailboxes = occupied(r); out->stopping = r->stopping; out->failed = workers_failed(r);
    out->submitted_jobs = r->submitted_jobs; out->returned_jobs = r->returned_jobs;
    out->wait_calls = r->wait_calls; out->wake_events = r->wake_events;
    for (uint32_t i = 0; i < r->created; ++i) {
        struct channel_worker *w = &r->worker[i];
        struct fwlab_nfc_channel_worker_stats *s = &out->worker[i];
        uint64_t now;
        s->channel_mask = w->channel_mask; s->requested_cpu = w->requested_cpu;
        s->created = w->created; s->joined = w->joined;
        s->completed_jobs = w->completed_jobs; s->actor_quanta = w->actor_quanta;
        s->failed = atomic_load_explicit(&w->error, memory_order_acquire) != 0;
        synchronized(pthread_mutex_lock(&w->mutex));
        if (!w->startup_done) {
            /* cpu_clock/affinity/start time are written before the worker
             * takes this mutex to publish its startup ACK. Do not read them. */
            synchronized(pthread_mutex_unlock(&w->mutex));
            continue;
        }
        s->thread_id = w->thread_id; s->exited = w->exited;
        s->failed |= w->startup_error != 0;
        out->failed |= s->failed;
        for (unsigned cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            if (CPU_ISSET(cpu, &w->affinity)) s->affinity[cpu / 64u] |= UINT64_C(1) << (cpu % 64u);
        if (w->exited) s->cpu_ns = w->cpu_final;
        else if (w->startup_error) s->cpu_ns = 0;
        else if (clock_ns(w->cpu_clock, &now) && now >= w->cpu_start)
            s->cpu_ns = now - w->cpu_start;
        else {
            synchronized(pthread_mutex_unlock(&w->mutex));
            return FWLAB_NFC_API_INVARIANT_FAILURE;
        }
        /* The worker cannot publish final CPU/exit while this mutex is held;
         * live clock sampling therefore cannot race thread-clock destruction. */
        synchronized(pthread_mutex_unlock(&w->mutex));
    }
    return FWLAB_NFC_API_OK;
}

enum fwlab_nfc_api_result fwlab_nfc_channel_workers_destroy(struct fwlab_nfc_channel_workers *r)
{
    if (!live(r)) return FWLAB_NFC_API_INVALID_CONTRACT;
    if (r->created != r->joined || occupied(r)) return FWLAB_NFC_API_WRONG_STATE;
    for (uint32_t i = 0; i < r->config.workers; ++i) {
        struct channel_worker *w = &r->worker[i];
        if (w->condition_initialized) {
            if (pthread_cond_destroy(&w->condition) != 0) return FWLAB_NFC_API_INVARIANT_FAILURE;
            w->condition_initialized = 0;
        }
        if (w->mutex_initialized) {
            if (pthread_mutex_destroy(&w->mutex) != 0) return FWLAB_NFC_API_INVARIANT_FAILURE;
            w->mutex_initialized = 0;
        }
    }
    if (r->completion_fd >= 0) {
        int fd = r->completion_fd;
        r->completion_fd = -1; /* Linux close consumes this FD even on EINTR. */
        if (close(fd) != 0) return FWLAB_NFC_API_INVARIANT_FAILURE;
    }
    r->magic = 0; free(r);
    return FWLAB_NFC_API_OK;
}
