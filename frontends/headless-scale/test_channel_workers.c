/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */
/* Same real format3 fixture/adjacent buffer, not the old full capacity suite.
 * Link-only constructor/step wrappers select execution and wait composition;
 * the production FTL, local NAND engine and physical codec stay unchanged. */
#define MULTIHEAD_PARENT_ENTRY retained_multihead_fixture_entry
#define MH_MEDIA_PREFIX "fwlab-d214-workers"
#define __wrap_fwlab_file_nand_v2_program_pages retained_cooperative_program_witness
#include "test_multihead_parent.c"
#undef __wrap_fwlab_file_nand_v2_program_pages
#include "nfc_channel_workers.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>

static struct fwlab_nfc_channel_workers *workers;
static struct fwlab_nfc_channel_executor executor;
static struct fwlab_nfc_channel_v2 *active_hub;
static struct mh_fixture *active_fixture;
static pthread_mutex_t overlap_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t overlap_cond = PTHREAD_COND_INITIALIZER;
static unsigned overlap_seen, overlap_peak, overlap_active;
static pthread_t overlap_threads[4];
static bool overlap_enabled;
static atomic_uint shard_active;
static unsigned create_calls, create_fail_at, joined_calls;

int __real_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int __wrap_pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *p)
{
    if (++create_calls == create_fail_at) return EAGAIN;
    return __real_pthread_create(t, a, f, p);
}
int __real_pthread_join(pthread_t, void **);
int __wrap_pthread_join(pthread_t t, void **p)
{
    int r = __real_pthread_join(t, p);
    if (!r) ++joined_calls;
    return r;
}
enum fwlab_nfc_api_result __real_fwlab_nfc_channel_v2_init(void *, size_t,
    const struct fwlab_nfc_page_v2_lab_mutation_config *, const struct fwlab_nand_channel_v2 *,
    struct fwlab_nfc_channel_v2 **);
enum fwlab_nfc_api_result __wrap_fwlab_nfc_channel_v2_init(void *a, size_t n,
    const struct fwlab_nfc_page_v2_lab_mutation_config *t, const struct fwlab_nand_channel_v2 *m,
    struct fwlab_nfc_channel_v2 **h)
{
    enum fwlab_nfc_api_result r = workers ?
        fwlab_nfc_channel_v2_init_executor(a, n, t, m, &executor, h) :
        __real_fwlab_nfc_channel_v2_init(a, n, t, m, h);
    if (r == FWLAB_NFC_API_OK) active_hub = *h;
    return r;
}
enum fwlab_spine_result_v0 __real_fwlab_ftl_scale_step(struct fwlab_ftl_scale *, uint32_t, uint32_t *);
enum fwlab_spine_result_v0 __wrap_fwlab_ftl_scale_step(struct fwlab_ftl_scale *f, uint32_t n, uint32_t *used)
{
    enum fwlab_spine_result_v0 r = __real_fwlab_ftl_scale_step(f, n, used);
    if (workers && active_hub && r == FWLAB_SPINE_V0_OK) {
        bool notified;
        CHECK(fwlab_nfc_channel_workers_wait(workers, active_hub, 50, &notified) == FWLAB_NFC_API_OK);
    }
    return r;
}
enum fwlab_nfc_api_result __wrap_fwlab_file_nand_v2_program_pages(
    struct fwlab_file_nand_v2 *media, const struct fwlab_nfc_ppa *first, uint32_t count,
    const uint8_t *main, size_t main_bytes, const uint8_t *oob, size_t oob_bytes,
    struct fwlab_nand_media_result *results, size_t capacity)
{
    unsigned channel = 0;
    CHECK(active_fixture);
    while (channel < 4 && active_fixture->assembly.channel[channel].scalar.context != media) ++channel;
    CHECK(channel < 4);
    unsigned bit = 1u << channel;
    CHECK(!(atomic_fetch_or(&shard_active, bit) & bit));
    /* Fixed real-callback rendezvous demonstrates distinct workers owning
     * independent channel calls. It is not parallel CRC bandwidth evidence. */
    if (overlap_enabled && oob_bytes >= 128 && mh_get16(oob + 6) == 5) {
        CHECK(pthread_mutex_lock(&overlap_lock) == 0);
        if (!(overlap_seen & bit)) {
            struct timespec deadline;
            CHECK(clock_gettime(CLOCK_REALTIME, &deadline) == 0); deadline.tv_sec += 5;
            overlap_threads[channel] = pthread_self(); overlap_seen |= bit;
            ++overlap_active;
            if (overlap_peak < overlap_active) overlap_peak = overlap_active;
            CHECK(pthread_cond_broadcast(&overlap_cond) == 0);
            while (overlap_seen != 15) CHECK(pthread_cond_timedwait(&overlap_cond, &overlap_lock, &deadline) == 0);
            --overlap_active;
        }
        CHECK(pthread_mutex_unlock(&overlap_lock) == 0);
    }
    enum fwlab_nfc_api_result r = __real_fwlab_file_nand_v2_program_pages(media, first, count,
        main, main_bytes, oob, oob_bytes, results, capacity);
    CHECK(atomic_fetch_and(&shard_active, ~bit) & bit);
    return r;
}

static uint64_t clock_ns(clockid_t id)
{
    struct timespec t; CHECK(clock_gettime(id, &t) == 0);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static struct fwlab_nfc_channel_workers_config config(unsigned n)
{
    struct fwlab_nfc_channel_workers_config c = { .channels = 4, .workers = n, .pin_workers = 1 };
    cpu_set_t available;
    CHECK(sched_getaffinity(0, sizeof(available), &available) == 0);
    unsigned found = 0;
    for (int i = 0; i < CPU_SETSIZE && found < n; ++i)
        if (CPU_ISSET(i, &available)) c.cpus[found++] = i;
    CHECK(found == n); return c;
}
static void workers_start(unsigned n)
{
    CHECK(!workers);
    if (!n) return;
    struct fwlab_nfc_channel_workers_config c = config(n);
    CHECK(fwlab_nfc_channel_workers_create(&c, &workers) == FWLAB_NFC_API_OK && workers);
    executor = fwlab_nfc_channel_workers_executor(workers); CHECK(executor.ops);
}
static void workers_end(unsigned n)
{
    if (!n) return;
    struct fwlab_nfc_channel_workers_stats s;
    CHECK(fwlab_nfc_channel_workers_snapshot(workers, &s) == FWLAB_NFC_API_OK &&
        s.created_workers == n && s.joined_workers == n && !s.occupied_mailboxes && !s.failed &&
        s.submitted_jobs == s.returned_jobs);
    printf("WORKERS_JOIN|workers=%u|created=%u|joined=%u|mailboxes=0|jobs=%llu|actual_joins=%u\n",
        n, s.created_workers, s.joined_workers, (unsigned long long)s.returned_jobs, joined_calls);
    CHECK(fwlab_nfc_channel_workers_destroy(workers) == FWLAB_NFC_API_OK);
    workers = NULL; memset(&executor, 0, sizeof(executor));
}
static void live_idle(struct mh_fixture *m)
{
    bool idle = false;
    /* Normal FTL completion may leave its final MAP retirement ACK for the
     * next admission/reset. For an explicit quiescent-media observation, this
     * same serialized composition owner drains ONLY those lower ACKs. Do not
     * invent an FTL idle callback or a second result consumer. */
    CHECK(!m->base.ftl->parent.owned && sf_io_idle(m->base.ftl) &&
        !sf_meta_busy(m->base.ftl) && m->base.ftl->work.kind == SF_WORK_NONE);
    struct fwlab_nfc_page_v2_provider p = fwlab_nfc_channel_v2_provider(m->hub);
    for (unsigned i = 0; i < STEPS; ++i) {
        CHECK(fwlab_nfc_channel_v2_live_idle(m->hub, &idle) == FWLAB_NFC_API_OK);
        if (idle) break;
        struct fwlab_nfc_page_v2_step_result result;
        CHECK(p.ops->step(p.context, 1, &result) == FWLAB_NFC_API_OK && result.units_used <= 1);
        if (workers) {
            bool notified;
            CHECK(fwlab_nfc_channel_workers_wait(workers, m->hub, 50, &notified) == FWLAB_NFC_API_OK);
        }
    }
    CHECK(idle && !atomic_load(&shard_active));
}
struct outcome {
    uint64_t media_hash, channel_hash[4], sequence[4], frontier, model_ns;
    struct fwlab_nfc_page_v2_lab_stats channel[4];
};
static struct outcome outcome(struct mh_fixture *m)
{
    struct outcome o = {0}; live_idle(m);
    struct fwlab_nfc_channel_v2_stats s = mh_stats(m);
    o.media_hash = m->assembly.aggregate.ops->hash(m->assembly.aggregate.context); CHECK(o.media_hash);
    o.frontier = m->base.ftl->durable_frontier; o.model_ns = s.now_ns;
    for (unsigned i = 0; i < 4; ++i) {
        const struct fwlab_nand_media *p = &m->assembly.channel[i].scalar;
        o.channel_hash[i] = p->ops->hash(p->context); CHECK(o.channel_hash[i]);
        o.sequence[i] = fwlab_file_nand_v2_sequence(p->context); o.channel[i] = s.channel[i];
    }
    return o;
}
static void equal_files(struct mh_fixture *a, struct mh_fixture *b)
{
    uint8_t left[16384], right[16384];
    CHECK(!a->volume && !b->volume && !workers);
    for (unsigned i = 0; i < MH_FILES; ++i) {
        int x = openat(a->base.directory_fd, mh_name(i), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        int y = openat(b->base.directory_fd, mh_name(i), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        struct stat xs, ys; CHECK(x >= 0 && y >= 0 && fstat(x, &xs) == 0 && fstat(y, &ys) == 0 && xs.st_size == ys.st_size);
        for (off_t at = 0; at < xs.st_size;) {
            size_t n = (uint64_t)(xs.st_size - at) > sizeof(left) ? sizeof(left) : (size_t)(xs.st_size - at);
            CHECK(pread(x, left, n, at) == (ssize_t)n && pread(y, right, n, at) == (ssize_t)n && !memcmp(left, right, n));
            at += (off_t)n;
        }
        CHECK(close(x) == 0 && close(y) == 0);
    }
}
static void constructor_failure(void)
{
    struct fwlab_nfc_channel_workers_config c = config(4);
    create_calls = joined_calls = 0; create_fail_at = 2;
    CHECK(fwlab_nfc_channel_workers_create(&c, &workers) == FWLAB_NFC_API_NO_CAPACITY && !workers);
    CHECK(create_calls == 2 && joined_calls == 1); create_fail_at = 0;
    puts("WORKERS_CONSTRUCTOR|real_first_thread=1|second_create_EAGAIN=1|first_actually_joined=1|no_media_jobs=1");
}
static struct mh_fixture *journey(unsigned n, struct outcome *out)
{
    struct mh_fixture *m = mh_create(4); struct fixture *f = &m->base;
    active_fixture = m; workers_start(n); mh_open(m, true);
    overlap_seen = overlap_active = overlap_peak = 0; overlap_enabled = n == 4;
    io(f, FWLAB_BLOCK_V0_WRITE, 0, MH_LBAS, 0x51); live_idle(m);
    overlap_enabled = false;
    if (n == 4) {
        CHECK(overlap_seen == 15 && overlap_peak == 4);
        for (unsigned i = 0; i < 4; ++i) for (unsigned j = 0; j < i; ++j)
            CHECK(!pthread_equal(overlap_threads[i], overlap_threads[j]));
        puts("WORKERS_OVERLAP|distinct_threads=4|independent_real_program_calls=4|controlled_rendezvous=1|not_parallel_CRC_bandwidth=1");
    }
    io(f, FWLAB_BLOCK_V0_WRITE, 7, 104, 0x52);
    io(f, FWLAB_BLOCK_V0_READ, 0, MH_LBAS, 0);
    check_pattern(f->buffer.bytes, 0, 7, 0x51);
    check_pattern(f->buffer.bytes + 7 * 512, 7, 104, 0x52);
    check_pattern(f->buffer.bytes + 111 * 512, 111, MH_LBAS - 111, 0x51);
    live_idle(m);
    uint64_t gc = f->ftl->garbage_collections;
    CHECK(fwlab_ftl_scale_gc_start(f->ftl, 1) == FWLAB_SPINE_V0_OK);
    for (unsigned i = 0; i < STEPS &&
        (f->ftl->work.kind != SF_WORK_NONE || sf_meta_busy(f->ftl) || !sf_io_idle(f->ftl)); ++i) step(f, 0);
    CHECK(f->ftl->garbage_collections == gc + 1); live_idle(m);
    struct fwlab_nfc_channel_workers_stats before = {0}, after = {0};
    if (n) CHECK(fwlab_nfc_channel_workers_snapshot(workers, &before) == FWLAB_NFC_API_OK);
    uint64_t wall = clock_ns(CLOCK_MONOTONIC), cpu = clock_ns(CLOCK_THREAD_CPUTIME_ID);
    uint64_t process = clock_ns(CLOCK_PROCESS_CPUTIME_ID), model = mh_stats(m).now_ns;
    for (unsigned i = 0; i < 4; ++i) io(f, FWLAB_BLOCK_V0_WRITE, 0, MH_LBAS, (uint8_t)(0x60 + i));
    for (unsigned i = 0; i < 8; ++i) {
        io(f, FWLAB_BLOCK_V0_READ, 0, MH_LBAS, 0); check_pattern(f->buffer.bytes, 0, MH_LBAS, 0x63);
    }
    live_idle(m);
    wall = clock_ns(CLOCK_MONOTONIC) - wall; cpu = clock_ns(CLOCK_THREAD_CPUTIME_ID) - cpu;
    process = clock_ns(CLOCK_PROCESS_CPUTIME_ID) - process; model = mh_stats(m).now_ns - model;
    if (n) CHECK(fwlab_nfc_channel_workers_snapshot(workers, &after) == FWLAB_NFC_API_OK);
    printf("WORKERS_COST|workers=%u|medium=local_tmpfs_ordinary_POSIX|write_bytes=4194304|read_bytes=8388608|wall_ns=%llu|coordinator_cpu_ns=%llu|process_cpu_ns=%llu|model_ns=%llu|includes_harness_fill_verify=1|not_native_SSD_bandwidth=1\n",
        n, (unsigned long long)wall, (unsigned long long)cpu, (unsigned long long)process, (unsigned long long)model);
    for (unsigned i = 0; i < n; ++i) {
        const struct fwlab_nfc_channel_worker_stats *s = &after.worker[i];
        CHECK(s->created && !s->failed && s->cpu_ns >= before.worker[i].cpu_ns && s->requested_cpu >= 0 &&
            (s->affinity[(unsigned)s->requested_cpu / 64] & (UINT64_C(1) << ((unsigned)s->requested_cpu % 64))));
        printf("WORKER_CPU|workers=%u|index=%u|tid=%llu|channel_mask=%u|cpu=%d|cpu_ns=%llu|jobs=%llu\n",
            n, i, (unsigned long long)s->thread_id, s->channel_mask, s->requested_cpu,
            (unsigned long long)(s->cpu_ns - before.worker[i].cpu_ns),
            (unsigned long long)(s->completed_jobs - before.worker[i].completed_jobs));
    }
    *out = outcome(m);
    printf("WORKERS_OUTCOME|workers=%u|frontier=%llu|model_ns=%llu|media_hash=%016llx\n", n,
        (unsigned long long)out->frontier, (unsigned long long)out->model_ns, (unsigned long long)out->media_hash);
    mh_close(m); active_hub = NULL; workers_end(n);
    workers_start(n); mh_open(m, false);
    CHECK(f->ftl->durable_frontier == out->frontier);
    io(f, FWLAB_BLOCK_V0_READ, 0, MH_LBAS, 0); check_pattern(f->buffer.bytes, 0, MH_LBAS, 0x63);
    mh_close(m); active_hub = NULL; workers_end(n); active_fixture = NULL;
    return m;
}
int main(void)
{
    struct outcome expected, got;
    struct rusage usage;
    constructor_failure();
    struct mh_fixture *reference = journey(0, &expected);
    for (unsigned n = 1; n <= 4; n += 3) {
        struct mh_fixture *m = journey(n, &got);
        CHECK(!memcmp(&expected, &got, sizeof(got)));
        equal_files(reference, m); mh_destroy(m);
        printf("WORKERS_EQUIVALENT|workers=%u|model_stats_media_hash_sequence=exact|closed_all_six_files=byte_exact|reopen=exact\n", n);
    }
    mh_destroy(reference);
    CHECK(getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss < 256 * 1024);
    printf("WORKERS_PASS|same_FTL3_NFC_media=1|cooperative_1_4=1|physical4_totalcredits4=1|peak_RSS_KiB=%ld|tmpfs_cache_excluded=1|no_NUMA_or_native_claim=1\n", usage.ru_maxrss);
    return 0;
}
