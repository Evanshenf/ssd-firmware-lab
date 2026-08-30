/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "c42_internal.h"

#include <string.h>

#define C42_FAULT_INVALID_DOORBELL 1u
#define C42_FAULT_QUEUE_MEMORY 2u
#define C42_FAULT_DUPLICATE_CID 3u
#define C42_FAULT_PORT_CONTRACT 4u
#define C42_FAULT_CANONICAL 5u
#define C42_FAULT_PROVIDER_POISON 6u
#define C42_FAULT_COUNTER_EXHAUSTED 7u

static int counter_available(const struct c42_counter *counter)
{
    return counter->next != 0 && counter->next <= counter->maximum;
}

static int generation_available(uint32_t generation)
{
    return generation != 0 && generation != UINT32_MAX;
}

static int memory_result_known(enum c42_memory_result result)
{
    return (unsigned int)result <= (unsigned int)C42_MEMORY_RETIRED;
}

static int memory_token_equal(
    const struct c42_memory_token *left,
    const struct c42_memory_token *right)
{
    return left->instance_nonce == right->instance_nonce &&
           left->uid == right->uid && left->generation == right->generation &&
           left->kind == right->kind && left->reserved == right->reserved;
}

static enum c42_memory_result memory_effect(
    enum c42_memory_result call_result,
    const struct c42_memory_status *status,
    const struct c42_memory_token *token,
    int *valid)
{
    enum c42_memory_result effect = call_result;

    *valid = 1;
    if (call_result == C42_MEMORY_IN_PROGRESS) {
        return call_result;
    }
    if (call_result != C42_MEMORY_OK) {
        *valid = 0;
        return call_result;
    }
    if (!memory_token_equal(&status->token, token) ||
        status->result > C42_MEMORY_RETIRED || status->committed > 1 ||
        status->quiescent > 1 ||
        !c42_bytes_zero(status->reserved, sizeof(status->reserved))) {
        *valid = 0;
        return C42_MEMORY_POISONED;
    }
    effect = (enum c42_memory_result)status->result;
    return effect;
}

static int descriptor_valid(
    const struct c42_controller *controller,
    const struct c42_queue_descriptor *descriptor,
    uint16_t *index)
{
    uint8_t expected_role;
    uint32_t expected_bytes;

    if (descriptor == NULL || descriptor->version != C42_COMPONENT_VERSION ||
        descriptor->size != sizeof(*descriptor) || descriptor->reserved0 != 0 ||
        !c42_bytes_zero(descriptor->reserved, sizeof(descriptor->reserved)) ||
        !c42_queue_index(descriptor->queue_id, index) ||
        descriptor->associated_cq_id != descriptor->queue_id ||
        descriptor->depth < 2 ||
        descriptor->depth > controller->config.maximum_queue_depth ||
        (descriptor->kind != C42_QUEUE_SQ &&
         descriptor->kind != C42_QUEUE_CQ) ||
        (descriptor->queue_class != FWLAB_NVME_QUEUE_ADMIN &&
         descriptor->queue_class != FWLAB_NVME_QUEUE_IO) ||
        (descriptor->queue_id == 0 &&
         descriptor->queue_class != FWLAB_NVME_QUEUE_ADMIN) ||
        (descriptor->queue_id != 0 &&
         descriptor->queue_class != FWLAB_NVME_QUEUE_IO) ||
        !c42_queue_memory_cap_valid(&descriptor->memory)) {
        return 0;
    }
    expected_role = descriptor->kind == C42_QUEUE_SQ ?
                    C42_MEMORY_SQ_READ : C42_MEMORY_CQ_PUBLISH;
    expected_bytes = (uint32_t)descriptor->depth *
                     (descriptor->kind == C42_QUEUE_SQ ?
                      C42_SQE_BYTES : C42_CQE_BYTES);
    return descriptor->memory.instance_nonce ==
               controller->config.instance_nonce &&
           descriptor->memory.owner_epoch == controller->config.owner_epoch &&
           descriptor->memory.controller_epoch == controller->controller_epoch &&
           descriptor->memory.queue_id == descriptor->queue_id &&
           descriptor->memory.role == expected_role &&
           descriptor->memory.exact_bytes == expected_bytes;
}

static struct c42_candidate_record *find_candidate(
    struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    uint16_t index;

    for (index = 0; index < C42_CANDIDATE_SLOTS; ++index) {
        struct c42_candidate_record *candidate = &controller->candidates[index];

        if (candidate->in_use != 0 &&
            c42_operation_token_equal(&candidate->token, token)) {
            return candidate;
        }
    }
    return NULL;
}

static const struct c42_candidate_record *find_candidate_const(
    const struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    uint16_t index;

    for (index = 0; index < C42_CANDIDATE_SLOTS; ++index) {
        const struct c42_candidate_record *candidate =
            &controller->candidates[index];

        if (candidate->in_use != 0 &&
            c42_operation_token_equal(&candidate->token, token)) {
            return candidate;
        }
    }
    return NULL;
}

static uint16_t candidate_tombstone_index(uint8_t kind, uint16_t queue_id)
{
    return (uint16_t)(((uint16_t)kind - 1u) * C42_QUEUE_SLOTS + queue_id);
}

static struct c42_candidate_tombstone *find_candidate_tombstone(
    struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    uint16_t index;

    for (index = 0; index < C42_CANDIDATE_SLOTS; ++index) {
        struct c42_candidate_tombstone *tombstone =
            &controller->candidate_tombstones[index];

        if (tombstone->valid != 0 &&
            c42_operation_token_equal(&tombstone->token, token)) {
            return tombstone;
        }
    }
    return NULL;
}

static const struct c42_candidate_tombstone *find_candidate_tombstone_const(
    const struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    uint16_t index;

    for (index = 0; index < C42_CANDIDATE_SLOTS; ++index) {
        const struct c42_candidate_tombstone *tombstone =
            &controller->candidate_tombstones[index];

        if (tombstone->valid != 0 &&
            c42_operation_token_equal(&tombstone->token, token)) {
            return tombstone;
        }
    }
    return NULL;
}

static void retire_candidate_record(
    struct c42_controller *controller,
    struct c42_candidate_record *candidate)
{
    uint16_t index = candidate_tombstone_index(
        candidate->descriptor.kind, candidate->descriptor.queue_id
    );
    struct c42_candidate_tombstone *tombstone =
        &controller->candidate_tombstones[index];

    memset(tombstone, 0, sizeof(*tombstone));
    tombstone->token = candidate->token;
    tombstone->queue_id = candidate->descriptor.queue_id;
    tombstone->kind = candidate->descriptor.kind;
    tombstone->valid = 1;
    memset(candidate, 0, sizeof(*candidate));
}

static enum c42_result candidate_access_result(
    const struct c42_controller *controller,
    const struct c42_candidate_record *candidate)
{
    if (candidate->controller_epoch != controller->controller_epoch ||
        candidate->state == C42_CANDIDATE_SUPERSEDED) {
        return C42_SUPERSEDED;
    }
    if (controller->phase == C42_CONTROLLER_COLD_NO_QUEUES ||
        controller->phase == C42_CONTROLLER_READY) {
        return C42_OK;
    }
    return controller->phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED ?
           C42_FAULTED : C42_WRONG_STATE;
}

static void queue_prepare_life(
    struct c42_controller *controller,
    const struct c42_queue_descriptor *descriptor,
    uint16_t index)
{
    if (descriptor->kind == C42_QUEUE_SQ) {
        controller->sq[index].associated_cq_id =
            descriptor->associated_cq_id;
        controller->sq[index].queue_class = descriptor->queue_class;
        controller->sq[index].depth = descriptor->depth;
        controller->sq[index].life = C42_QUEUE_PREPARED;
    } else {
        controller->cq[index].life = C42_QUEUE_PREPARED;
    }
}

enum c42_result c42_candidate_prepare(
    struct c42_controller *controller,
    const struct c42_queue_descriptor *descriptor,
    struct c42_operation_token *token)
{
    struct c42_candidate_record *candidate = NULL;
    uint16_t queue_index;
    uint16_t index;
    uint64_t uid;
    uint32_t generation;
    enum c42_memory_result memory_result;

    if (!c42_controller_valid(controller) || token == NULL ||
        (controller->phase != C42_CONTROLLER_COLD_NO_QUEUES &&
         controller->phase != C42_CONTROLLER_READY) ||
        !descriptor_valid(controller, descriptor, &queue_index)) {
        return C42_INVALID;
    }
    if ((descriptor->kind == C42_QUEUE_SQ &&
         controller->sq[queue_index].life != C42_QUEUE_ABSENT) ||
        (descriptor->kind == C42_QUEUE_CQ &&
         controller->cq[queue_index].life != C42_QUEUE_ABSENT)) {
        return C42_WRONG_STATE;
    }
    if (descriptor->kind == C42_QUEUE_SQ) {
        const struct c42_cq_record *cq = &controller->cq[queue_index];

        if (cq->life != C42_QUEUE_LIVE ||
            cq->queue_class != descriptor->queue_class) {
            return C42_WRONG_STATE;
        }
    }
    for (index = 0; index < C42_CANDIDATE_SLOTS; ++index) {
        if (controller->candidates[index].in_use == 0 && candidate == NULL) {
            candidate = &controller->candidates[index];
        } else if (controller->candidates[index].in_use != 0 &&
                   controller->candidates[index].descriptor.kind ==
                       descriptor->kind &&
                   controller->candidates[index].descriptor.queue_id ==
                       descriptor->queue_id) {
            return C42_WRONG_STATE;
        }
    }
    if (candidate == NULL) {
        return C42_NO_CAPACITY;
    }
    if ((descriptor->kind == C42_QUEUE_SQ &&
         (descriptor->memory.ring_generation <=
              controller->sq[queue_index].last_ring_generation ||
          descriptor->memory.mapping_generation <=
              controller->sq[queue_index].last_mapping_generation)) ||
        (descriptor->kind == C42_QUEUE_CQ &&
         (descriptor->memory.ring_generation <=
              controller->cq[queue_index].last_ring_generation ||
          descriptor->memory.mapping_generation <=
              controller->cq[queue_index].last_mapping_generation))) {
        return C42_STALE;
    }
    memory_result = controller->providers.memory.ops->validate(
        controller->providers.memory.context,
        &descriptor->memory,
        descriptor->memory.role,
        descriptor->memory.exact_bytes
    );
    if (!memory_result_known(memory_result)) {
        c42_fault_controller(controller, C42_FAULT_PROVIDER_POISON);
        return C42_POISONED;
    }
    if (memory_result != C42_MEMORY_OK) {
        return memory_result == C42_MEMORY_STALE ? C42_STALE : C42_INVALID;
    }
    if (!counter_available(&controller->candidate_uid) ||
        !generation_available(controller->next_candidate_generation)) {
        return C42_COUNTER_EXHAUSTED;
    }
    (void)c42_counter_take(&controller->candidate_uid, &uid);
    (void)c42_generation_take(
        &controller->next_candidate_generation, &generation
    );
    memset(candidate, 0, sizeof(*candidate));
    candidate->in_use = 1;
    candidate->descriptor = *descriptor;
    candidate->controller_epoch = controller->controller_epoch;
    if (descriptor->kind == C42_QUEUE_SQ) {
        candidate->associated_cq_ring_generation =
            controller->cq[queue_index].ring_generation;
        candidate->associated_cq_mapping_generation =
            controller->cq[queue_index].mapping_generation;
    }
    candidate->token.instance_nonce = controller->config.instance_nonce;
    candidate->token.uid = uid;
    candidate->token.generation = generation;
    candidate->token.kind = descriptor->kind;
    candidate->state = descriptor->kind == C42_QUEUE_SQ ?
                       C42_CANDIDATE_READY : C42_CANDIDATE_PREPARED;
    candidate->scrub_token.instance_nonce = controller->config.instance_nonce;
    candidate->scrub_token.uid = uid;
    candidate->scrub_token.generation = generation;
    candidate->scrub_token.kind = C42_QUEUE_CQ;
    memset(
        &controller->candidate_tombstones[candidate_tombstone_index(
            descriptor->kind, descriptor->queue_id)],
        0,
        sizeof(controller->candidate_tombstones[0])
    );
    queue_prepare_life(controller, descriptor, queue_index);
    *token = candidate->token;
    return C42_OK;
}

static int progress_candidate_once(
    struct c42_controller *controller,
    struct c42_candidate_record *candidate)
{
    struct c42_memory_status status = {0};
    enum c42_memory_result call_result;
    enum c42_memory_result effect;
    int valid;

    if (candidate->state == C42_CANDIDATE_READY ||
        candidate->state == C42_CANDIDATE_COMMITTED ||
        candidate->state == C42_CANDIDATE_COMMITTED_AWAIT_RETIRE ||
        candidate->state == C42_CANDIDATE_RETIRE_UNKNOWN ||
        candidate->state == C42_CANDIDATE_RETIRE_READY ||
        candidate->state == C42_CANDIDATE_ABORTED ||
        candidate->state == C42_CANDIDATE_POISONED ||
        candidate->state == C42_CANDIDATE_SUPERSEDED) {
        return 0;
    }
    if (candidate->state == C42_CANDIDATE_ABORTING) {
        call_result = controller->providers.memory.ops->scrub_abort(
            controller->providers.memory.context,
            &candidate->descriptor.memory,
            candidate->descriptor.depth,
            0,
            &candidate->scrub_token,
            &status
        );
        effect = memory_effect(
            call_result, &status, &candidate->scrub_token, &valid
        );
        if (valid != 0 && effect == C42_MEMORY_RETIRED &&
            status.quiescent == 1) {
            candidate->provider_retired = 1;
            candidate->state = C42_CANDIDATE_ABORTED;
        } else if (valid == 0 || effect == C42_MEMORY_POISONED ||
                   effect == C42_MEMORY_INVALID) {
            candidate->state = C42_CANDIDATE_POISONED;
            c42_fault_controller(controller, C42_FAULT_PROVIDER_POISON);
        }
        return 1;
    }
    if (candidate->scrub_started == 0) {
        call_result = controller->providers.memory.ops->scrub_start(
            controller->providers.memory.context,
            &candidate->descriptor.memory,
            candidate->descriptor.depth,
            0,
            &candidate->scrub_token,
            &status
        );
        candidate->scrub_started = 1;
    } else {
        call_result = controller->providers.memory.ops->scrub_query(
            controller->providers.memory.context,
            &candidate->descriptor.memory,
            candidate->descriptor.depth,
            0,
            &candidate->scrub_token,
            &status
        );
    }
    effect = memory_effect(
        call_result, &status, &candidate->scrub_token, &valid
    );
    if (valid == 0 || effect == C42_MEMORY_POISONED ||
        effect == C42_MEMORY_INVALID || effect == C42_MEMORY_STALE) {
        candidate->state = C42_CANDIDATE_POISONED;
        c42_fault_controller(controller, C42_FAULT_PROVIDER_POISON);
    } else if (effect == C42_MEMORY_FULL && status.committed == 1 &&
               status.quiescent == 1) {
        candidate->state = C42_CANDIDATE_READY;
    } else if (effect == C42_MEMORY_FULL ||
               effect == C42_MEMORY_UNKNOWN ||
               effect == C42_MEMORY_IN_PROGRESS ||
               effect == C42_MEMORY_NO_EFFECT ||
               effect == C42_MEMORY_EXACT_PREFIX) {
        candidate->state = C42_CANDIDATE_SCRUB_UNKNOWN;
    } else {
        candidate->state = C42_CANDIDATE_POISONED;
        c42_fault_controller(controller, C42_FAULT_PROVIDER_POISON);
    }
    return 1;
}

enum c42_result c42_candidate_progress(
    struct c42_controller *controller,
    const struct c42_operation_token *token,
    uint32_t budget)
{
    struct c42_candidate_record *candidate;
    enum c42_result access;
    uint32_t unit;

    if (!c42_controller_valid(controller) || token == NULL) {
        return C42_INVALID;
    }
    candidate = find_candidate(controller, token);
    if (candidate == NULL) {
        return find_candidate_tombstone(controller, token) != NULL ?
               C42_NO_EFFECT : C42_STALE;
    }
    access = candidate_access_result(controller, candidate);
    if (access != C42_OK) {
        return access;
    }
    for (unit = 0; unit < budget; ++unit) {
        if (progress_candidate_once(controller, candidate) == 0) {
            break;
        }
    }
    return candidate->state == C42_CANDIDATE_POISONED ?
           C42_POISONED : C42_OK;
}

enum c42_result c42_candidate_query(
    const struct c42_controller *controller,
    const struct c42_operation_token *token,
    struct c42_candidate_status *status)
{
    const struct c42_candidate_record *candidate;
    struct c42_candidate_status local = {0};

    if (!c42_controller_valid(controller) || token == NULL || status == NULL) {
        return C42_INVALID;
    }
    candidate = find_candidate_const(controller, token);
    if (candidate == NULL) {
        const struct c42_candidate_tombstone *tombstone =
            find_candidate_tombstone_const(controller, token);

        if (tombstone == NULL) {
            return C42_STALE;
        }
        local.token = tombstone->token;
        local.state = C42_CANDIDATE_RETIRED;
        *status = local;
        return C42_OK;
    }
    local.token = candidate->token;
    local.state = candidate->state;
    local.cause = candidate->cause;
    local.retry = candidate->retry;
    *status = local;
    return C42_OK;
}

static void commit_sq(
    struct c42_controller *controller,
    const struct c42_queue_descriptor *descriptor,
    uint16_t index)
{
    struct c42_sq_record *sq = &controller->sq[index];
    uint32_t last_ring = sq->last_ring_generation;
    uint32_t last_mapping = sq->last_mapping_generation;

    memset(sq, 0, sizeof(*sq));
    sq->memory = descriptor->memory;
    sq->ring_generation = descriptor->memory.ring_generation;
    sq->mapping_generation = descriptor->memory.mapping_generation;
    sq->last_ring_generation = descriptor->memory.ring_generation > last_ring ?
                               descriptor->memory.ring_generation : last_ring;
    sq->last_mapping_generation =
        descriptor->memory.mapping_generation > last_mapping ?
        descriptor->memory.mapping_generation : last_mapping;
    sq->queue_id = descriptor->queue_id;
    sq->associated_cq_id = descriptor->associated_cq_id;
    sq->depth = descriptor->depth;
    sq->queue_class = descriptor->queue_class;
    sq->life = C42_QUEUE_LIVE;
}

static void commit_cq(
    struct c42_controller *controller,
    const struct c42_queue_descriptor *descriptor,
    uint16_t index)
{
    struct c42_cq_record *cq = &controller->cq[index];
    uint32_t last_ring = cq->last_ring_generation;
    uint32_t last_mapping = cq->last_mapping_generation;
    uint32_t slot_generation = cq->next_slot_generation;

    memset(cq, 0, sizeof(*cq));
    cq->memory = descriptor->memory;
    cq->ring_generation = descriptor->memory.ring_generation;
    cq->mapping_generation = descriptor->memory.mapping_generation;
    cq->last_ring_generation = descriptor->memory.ring_generation > last_ring ?
                               descriptor->memory.ring_generation : last_ring;
    cq->last_mapping_generation =
        descriptor->memory.mapping_generation > last_mapping ?
        descriptor->memory.mapping_generation : last_mapping;
    cq->next_slot_generation = slot_generation == 0 ? 1 : slot_generation;
    cq->queue_id = descriptor->queue_id;
    cq->depth = descriptor->depth;
    cq->queue_class = descriptor->queue_class;
    cq->life = C42_QUEUE_LIVE;
    cq->device_phase = 1;
}

enum c42_result c42_candidate_commit(
    struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    struct c42_candidate_record *candidate;
    enum c42_result access;
    uint16_t index;

    if (!c42_controller_valid(controller) || token == NULL) {
        return C42_INVALID;
    }
    candidate = find_candidate(controller, token);
    if (candidate == NULL) {
        return find_candidate_tombstone(controller, token) != NULL ?
               C42_NO_EFFECT : C42_STALE;
    }
    access = candidate_access_result(controller, candidate);
    if (access != C42_OK) {
        return access;
    }
    if (candidate->state != C42_CANDIDATE_READY ||
        !c42_queue_index(candidate->descriptor.queue_id, &index)) {
        return C42_WRONG_STATE;
    }
    if (candidate->descriptor.kind == C42_QUEUE_SQ) {
        const struct c42_cq_record *cq = &controller->cq[index];

        if (controller->sq[index].life != C42_QUEUE_PREPARED ||
            controller->sq[index].associated_cq_id != cq->queue_id ||
            cq->life != C42_QUEUE_LIVE ||
            cq->queue_class != candidate->descriptor.queue_class ||
            cq->ring_generation !=
                candidate->associated_cq_ring_generation ||
            cq->mapping_generation !=
                candidate->associated_cq_mapping_generation) {
            return C42_WRONG_STATE;
        }
        commit_sq(controller, &candidate->descriptor, index);
    } else {
        if (controller->cq[index].life != C42_QUEUE_PREPARED) {
            return C42_WRONG_STATE;
        }
        commit_cq(controller, &candidate->descriptor, index);
    }
    candidate->state = candidate->descriptor.kind == C42_QUEUE_CQ ?
                       C42_CANDIDATE_COMMITTED_AWAIT_RETIRE :
                       C42_CANDIDATE_RETIRE_READY;
    return C42_OK;
}

static void abort_prepared_queue(
    struct c42_controller *controller,
    const struct c42_queue_descriptor *descriptor)
{
    uint16_t index;

    if (!c42_queue_index(descriptor->queue_id, &index)) {
        return;
    }
    if (descriptor->kind == C42_QUEUE_SQ &&
        controller->sq[index].life == C42_QUEUE_PREPARED) {
        controller->sq[index].life = C42_QUEUE_ABSENT;
    } else if (descriptor->kind == C42_QUEUE_CQ &&
               controller->cq[index].life == C42_QUEUE_PREPARED) {
        controller->cq[index].life = C42_QUEUE_ABSENT;
    }
}

enum c42_result c42_candidate_abort(
    struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    struct c42_candidate_record *candidate;
    enum c42_result access;

    if (!c42_controller_valid(controller) || token == NULL) {
        return C42_INVALID;
    }
    candidate = find_candidate(controller, token);
    if (candidate == NULL) {
        return find_candidate_tombstone(controller, token) != NULL ?
               C42_NO_EFFECT : C42_STALE;
    }
    access = candidate_access_result(controller, candidate);
    if (access != C42_OK) {
        return access;
    }
    if (candidate->state == C42_CANDIDATE_COMMITTED ||
        candidate->state == C42_CANDIDATE_COMMITTED_AWAIT_RETIRE ||
        candidate->state == C42_CANDIDATE_RETIRE_UNKNOWN ||
        candidate->state == C42_CANDIDATE_RETIRE_READY) {
        return C42_TOO_LATE;
    }
    if (candidate->state == C42_CANDIDATE_ABORTED) {
        return C42_NO_EFFECT;
    }
    abort_prepared_queue(controller, &candidate->descriptor);
    if (candidate->descriptor.kind == C42_QUEUE_SQ ||
        candidate->scrub_started == 0) {
        candidate->provider_retired = 1;
        candidate->state = C42_CANDIDATE_ABORTED;
    } else {
        candidate->state = C42_CANDIDATE_ABORTING;
    }
    return C42_OK;
}

enum c42_result c42_candidate_retire(
    struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    struct c42_candidate_record *candidate;
    struct c42_memory_status status = {0};
    enum c42_memory_result call_result;
    enum c42_memory_result effect;
    enum c42_result access;
    uint16_t queue_index;
    int valid;

    if (!c42_controller_valid(controller) || token == NULL) {
        return C42_INVALID;
    }
    candidate = find_candidate(controller, token);
    if (candidate == NULL) {
        return find_candidate_tombstone(controller, token) != NULL ?
               C42_NO_EFFECT : C42_STALE;
    }
    access = candidate_access_result(controller, candidate);
    if (access != C42_OK) {
        return access;
    }
    if (candidate->state == C42_CANDIDATE_ABORTED) {
        if (candidate->provider_retired == 0) {
            return C42_WRONG_STATE;
        }
        retire_candidate_record(controller, candidate);
        return C42_OK;
    }
    if (candidate->state == C42_CANDIDATE_RETIRE_READY) {
        if (candidate->descriptor.kind == C42_QUEUE_CQ) {
            if (!c42_queue_index(
                    candidate->descriptor.queue_id, &queue_index) ||
                controller->cq[queue_index].life != C42_QUEUE_LIVE ||
                controller->cq[queue_index].ring_generation !=
                    candidate->descriptor.memory.ring_generation ||
                controller->cq[queue_index].mapping_generation !=
                    candidate->descriptor.memory.mapping_generation ||
                candidate->provider_retired == 0) {
                return C42_WRONG_STATE;
            }
            controller->cq[queue_index].create_scrub_retired = 1;
        }
        retire_candidate_record(controller, candidate);
        return C42_OK;
    }
    if (candidate->state != C42_CANDIDATE_COMMITTED_AWAIT_RETIRE &&
        candidate->state != C42_CANDIDATE_RETIRE_UNKNOWN) {
        return C42_WRONG_STATE;
    }
    if (candidate->descriptor.kind != C42_QUEUE_CQ) {
        return C42_WRONG_STATE;
    }
    if (candidate->retire_started == 0) {
        call_result = controller->providers.memory.ops->scrub_retire_start(
            controller->providers.memory.context,
            &candidate->descriptor.memory,
            candidate->descriptor.depth,
            0,
            &candidate->scrub_token,
            &status
        );
        candidate->retire_started = 1;
    } else {
        call_result = controller->providers.memory.ops->scrub_retire_query(
            controller->providers.memory.context,
            &candidate->descriptor.memory,
            candidate->descriptor.depth,
            0,
            &candidate->scrub_token,
            &status
        );
    }
    effect = memory_effect(
        call_result, &status, &candidate->scrub_token, &valid
    );
    if (effect == C42_MEMORY_IN_PROGRESS || effect == C42_MEMORY_UNKNOWN) {
        candidate->state = C42_CANDIDATE_RETIRE_UNKNOWN;
        return C42_IN_PROGRESS;
    }
    if (valid != 0 && effect == C42_MEMORY_RETIRED &&
        status.quiescent == 1) {
        candidate->provider_retired = 1;
        candidate->state = C42_CANDIDATE_RETIRE_READY;
        return C42_IN_PROGRESS;
    }
    candidate->state = C42_CANDIDATE_POISONED;
    c42_fault_controller(controller, C42_FAULT_PROVIDER_POISON);
    return C42_POISONED;
}

enum c42_result c42_enable(struct c42_controller *controller)
{
    if (!c42_controller_valid(controller)) {
        return C42_INVALID;
    }
    if (controller->phase != C42_CONTROLLER_COLD_NO_QUEUES ||
        controller->cq[0].life != C42_QUEUE_LIVE ||
        controller->sq[0].life != C42_QUEUE_LIVE ||
        controller->sq[0].associated_cq_id != controller->cq[0].queue_id) {
        return C42_WRONG_STATE;
    }
    controller->phase = C42_CONTROLLER_READY;
    return C42_OK;
}

enum c42_result c42_sq_tail_event_apply(
    struct c42_controller *controller,
    const struct c42_sq_tail_event *event)
{
    struct c42_sq_record *sq;
    uint16_t index;
    uint16_t delta;
    uint16_t available;

    if (!c42_controller_valid(controller) || event == NULL ||
        event->reserved != 0 ||
        event->instance_nonce != controller->config.instance_nonce ||
        !c42_queue_index(event->queue_id, &index)) {
        return C42_INVALID;
    }
    if (event->controller_epoch != controller->controller_epoch) {
        return C42_STALE;
    }
    if (controller->phase != C42_CONTROLLER_READY) {
        return controller->phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED ?
               C42_FAULTED : C42_WRONG_STATE;
    }
    sq = &controller->sq[index];
    if (event->ring_generation != sq->ring_generation) {
        return C42_STALE;
    }
    if (sq->life != C42_QUEUE_LIVE) {
        return C42_WRONG_STATE;
    }
    if (event->new_tail >= sq->depth) {
        c42_fault_sq(controller, index, C42_FAULT_INVALID_DOORBELL);
        return C42_FAULTED;
    }
    if (event->new_tail == sq->host_tail) {
        return C42_NO_EFFECT;
    }
    delta = (uint16_t)(((uint32_t)event->new_tail + sq->depth -
                        sq->host_tail) % sq->depth);
    available = (uint16_t)(sq->depth - 1u - sq->pending);
    if (delta > available) {
        c42_fault_sq(controller, index, C42_FAULT_INVALID_DOORBELL);
        return C42_FAULTED;
    }
    sq->host_tail = event->new_tail;
    sq->pending = (uint16_t)(sq->pending + delta);
    return C42_OK;
}

static int command_identity_matches(
    const struct c42_controller *controller,
    const struct c42_command_record *command,
    const struct fwlab_nvme_command_handle *handle,
    const struct fwlab_nvme_origin_token *origin)
{
    return c42_handle_equal(&command->prepared.handle, handle) &&
           c42_origin_equal(&command->origin, origin) &&
           handle->instance_nonce == controller->config.instance_nonce &&
           handle->controller_epoch == controller->controller_epoch;
}

static int prepare_result_valid(
    const struct c42_command_record *command,
    const struct fwlab_hif_prepare_result *result)
{
    if (result->version != FWLAB_HIF_COMMAND_PORT_VERSION ||
        result->size != sizeof(*result) ||
        !c42_bytes_zero(result->reserved, sizeof(result->reserved)) ||
        result->disposition > FWLAB_HIF_PREPARE_REJECTED) {
        return 0;
    }
    if (result->disposition == FWLAB_HIF_PREPARE_RESERVED) {
        return fwlab_hif_prepared_token_valid(&result->prepared) &&
               c42_origin_equal(&result->prepared.origin, &command->origin);
    }
    return 1;
}

static void prepare_key_fill(
    const struct c42_controller *controller,
    const struct c42_command_record *command,
    struct fwlab_hif_prepare_key *key)
{
    memset(key, 0, sizeof(*key));
    key->version = FWLAB_HIF_COMMAND_PORT_VERSION;
    key->size = sizeof(*key);
    key->origin = command->origin;
    key->client_uid = command->client_uid;
    key->instance_nonce = controller->config.instance_nonce;
    key->controller_epoch = controller->controller_epoch;
    key->client_generation = command->active_generation;
    key->queue_class = command->queue_class;
    key->worst_case_actions = controller->config.worst_case_actions;
}

static void clear_command_all(
    struct c42_controller *controller,
    uint16_t index)
{
    memset(&controller->commands[index], 0,
           sizeof(controller->commands[index]));
    memset(&controller->publications[index], 0,
           sizeof(controller->publications[index]));
    memset(&controller->reconciles[index], 0,
           sizeof(controller->reconciles[index]));
    memset(&controller->notifications[index], 0,
           sizeof(controller->notifications[index]));
}

static int duplicate_cid(
    const struct c42_controller *controller,
    uint16_t sq_index,
    uint32_t sq_generation,
    uint16_t command_id)
{
    uint32_t index;

    for (index = 0; index < controller->config.command_capacity; ++index) {
        const struct c42_command_record *command = &controller->commands[index];

        if (command->state != C42_COMMAND_FREE &&
            command->sq_index == sq_index &&
            command->sq_ring_generation == sq_generation &&
            command->command_id == command_id) {
            return 1;
        }
    }
    return 0;
}

static int admission_resources_available(
    const struct c42_controller *controller,
    uint16_t *record_index)
{
    uint16_t index;

    if (!counter_available(&controller->origin_uid) ||
        !counter_available(&controller->client_uid) ||
        !counter_available(&controller->release_uid) ||
        !counter_available(&controller->trace_uid) ||
        !counter_available(&controller->publication_uid) ||
        !counter_available(&controller->notification_uid) ||
        !generation_available(controller->next_active_generation) ||
        !generation_available(controller->next_notification_generation)) {
        return -1;
    }
    for (index = 0; index < controller->config.command_capacity; ++index) {
        if (controller->commands[index].state == C42_COMMAND_FREE &&
            controller->publications[index].in_use == 0 &&
            controller->reconciles[index].in_use == 0 &&
            controller->notifications[index].in_use == 0) {
            *record_index = index;
            return 1;
        }
    }
    return 0;
}

static int capture_one(
    struct c42_controller *controller,
    uint16_t sq_index)
{
    struct c42_sq_record *sq = &controller->sq[sq_index];
    struct c42_command_record local = {0};
    struct c42_command_record *command;
    struct c42_publication_record *publication;
    struct c42_reconcile_record *reconcile;
    struct c42_notification_record *notification;
    enum c42_memory_result memory_result;
    uint16_t record_index;
    uint16_t cq_index;
    uint64_t origin_uid;
    uint64_t notification_uid;
    uint32_t notification_generation;
    int resources;

    resources = admission_resources_available(controller, &record_index);
    if (resources < 0) {
        c42_fault_sq(controller, sq_index, C42_FAULT_COUNTER_EXHAUSTED);
        return 1;
    }
    if (resources == 0) {
        return 0;
    }
    memory_result = controller->providers.memory.ops->capture(
        controller->providers.memory.context,
        &sq->memory,
        sq->device_head,
        local.raw_bytes,
        sizeof(local.raw_bytes)
    );
    if (memory_result != C42_MEMORY_OK ||
        c41_sqe_decode(local.raw_bytes, sizeof(local.raw_bytes), &local.raw) !=
            C41_WIRE_OK) {
        c42_fault_sq(controller, sq_index, C42_FAULT_QUEUE_MEMORY);
        return 1;
    }
    if (duplicate_cid(
            controller, sq_index, sq->ring_generation, local.raw.command_id)) {
        c42_fault_sq(controller, sq_index, C42_FAULT_DUPLICATE_CID);
        return 1;
    }
    if (!c42_queue_index(sq->associated_cq_id, &cq_index)) {
        c42_fault_sq(controller, sq_index, C42_FAULT_QUEUE_MEMORY);
        return 1;
    }
    command = &controller->commands[record_index];
    *command = local;
    command->sq_index = sq_index;
    command->cq_index = cq_index;
    command->sq_ring_generation = sq->ring_generation;
    command->command_id = local.raw.command_id;
    command->queue_class = sq->queue_class;
    (void)c42_counter_take(&controller->origin_uid, &origin_uid);
    command->origin.word[0] = controller->config.origin_domain_nonce;
    command->origin.word[1] = origin_uid;
    (void)c42_counter_take(&controller->client_uid, &command->client_uid);
    (void)c42_counter_take(&controller->release_uid, &command->release_uid);
    (void)c42_counter_take(&controller->trace_uid, &command->trace_cookie);
    (void)c42_counter_take(
        &controller->publication_uid, &command->publication_uid
    );
    (void)c42_counter_take(
        &controller->notification_uid, &notification_uid
    );
    command->notification_uid = notification_uid;
    (void)c42_generation_take(
        &controller->next_active_generation, &command->active_generation
    );
    (void)c42_generation_take(
        &controller->next_notification_generation, &notification_generation
    );
    command->state = C42_COMMAND_CAPTURED;
    publication = &controller->publications[record_index];
    publication->in_use = 1;
    publication->publication_uid = command->publication_uid;
    publication->command_index = record_index;
    reconcile = &controller->reconciles[record_index];
    reconcile->in_use = 1;
    reconcile->publication_uid = command->publication_uid;
    reconcile->command_index = record_index;
    reconcile->state = C42_RECONCILE_RESERVED;
    notification = &controller->notifications[record_index];
    notification->in_use = 1;
    notification->state = C42_NOTIFY_RESERVED;
    notification->controller_epoch = controller->controller_epoch;
    notification->publication_uid = command->publication_uid;
    notification->completion_queue_id =
        controller->cq[cq_index].queue_id;
    notification->token.instance_nonce = controller->config.instance_nonce;
    notification->token.uid = notification_uid;
    notification->token.generation = notification_generation;
    notification->token.kind = 1;
    return 1;
}

static int progress_prepare(
    struct c42_controller *controller,
    uint16_t index)
{
    struct c42_command_record *command = &controller->commands[index];
    struct fwlab_hif_prepare_key key;
    struct fwlab_hif_prepare_result result = {0};
    enum fwlab_hif_command_port_result port_result;
    struct c41_capture_context context = {0};

    prepare_key_fill(controller, command, &key);
    if (command->state == C42_COMMAND_CAPTURED) {
        port_result = controller->providers.command.ops->prepare_start(
            controller->providers.command.context, &key, &result
        );
        command->prepare_started = 1;
    } else {
        port_result = controller->providers.command.ops->prepare_query(
            controller->providers.command.context, &key, &result
        );
    }
    if (port_result == FWLAB_HIF_PORT_IN_PROGRESS) {
        command->state = C42_COMMAND_PREPARE_QUERY;
        return 1;
    }
    if (port_result != FWLAB_HIF_PORT_OK ||
        !prepare_result_valid(command, &result)) {
        c42_fault_sq(controller, command->sq_index, C42_FAULT_PORT_CONTRACT);
        return 1;
    }
    if (result.disposition == FWLAB_HIF_PREPARE_BACKPRESSURE) {
        command->state = C42_COMMAND_CAPTURED;
        command->prepare_started = 0;
        return 1;
    }
    if (result.disposition == FWLAB_HIF_PREPARE_REJECTED) {
        uint16_t sq_index = command->sq_index;

        clear_command_all(controller, index);
        c42_fault_sq(controller, sq_index, C42_FAULT_PORT_CONTRACT);
        return 1;
    }
    command->prepared = result.prepared;
    if (!command_identity_matches(
            controller, command, &command->prepared.handle,
            &command->prepared.origin)) {
        command->state = C42_COMMAND_ABORT_RECONCILE;
        c42_fault_sq(controller, command->sq_index, C42_FAULT_PORT_CONTRACT);
        return 1;
    }
    context.handle = command->prepared.handle;
    context.origin = command->origin;
    context.trace_cookie = command->trace_cookie;
    context.safety_generation = controller->config.safety_generation;
    context.transport_fault = FWLAB_NVME_TRANSPORT_NONE;
    context.queue_class = command->queue_class;
    if (c41_capture_command(&command->raw, &context, &command->command) !=
        C41_WIRE_OK) {
        command->state = C42_COMMAND_ABORT_RECONCILE;
        c42_fault_sq(controller, command->sq_index, C42_FAULT_CANONICAL);
        return 1;
    }
    command->state = C42_COMMAND_PORT_RESERVED;
    return 1;
}

static int ticket_matches(
    const struct c42_command_record *command,
    const struct fwlab_hif_command_ticket *ticket)
{
    return fwlab_hif_command_ticket_valid(ticket) &&
           c42_handle_equal(&ticket->handle, &command->prepared.handle) &&
           c42_origin_equal(&ticket->origin, &command->origin);
}

static void admission_key_fill(
    const struct c42_command_record *command,
    struct fwlab_hif_admission_key *key)
{
    memset(key, 0, sizeof(*key));
    key->prepared = command->prepared;
    key->client_uid = command->client_uid;
    key->generation = command->active_generation;
}

static int hold_poisoned_admission(
    struct c42_controller *controller,
    struct c42_command_record *command)
{
    command->state = C42_COMMAND_ADMIT_POISON_HOLD;
    c42_fault_sq(controller, command->sq_index, C42_FAULT_PORT_CONTRACT);
    return 1;
}

static int progress_admit(
    struct c42_controller *controller,
    struct c42_command_record *command)
{
    struct fwlab_hif_admission_key key = {0};
    struct fwlab_hif_command_ticket ticket = {0};
    enum fwlab_hif_admission_state state =
        FWLAB_HIF_ADMISSION_NOT_STARTED;
    enum fwlab_hif_command_port_result result;

    admission_key_fill(command, &key);
    if (command->state == C42_COMMAND_PORT_RESERVED) {
        result = controller->providers.command.ops->admit_start(
            controller->providers.command.context, &key, &command->command,
            &state, &ticket
        );
        command->admit_started = 1;
    } else {
        result = controller->providers.command.ops->admit_query(
            controller->providers.command.context, &key, &command->command,
            &state, &ticket
        );
    }
    if (result == FWLAB_HIF_PORT_IN_PROGRESS) {
        command->state = C42_COMMAND_ADMIT_QUERY;
        return 1;
    }
    if (result != FWLAB_HIF_PORT_OK) {
        return hold_poisoned_admission(controller, command);
    }

    switch (state) {
    case FWLAB_HIF_ADMISSION_NOT_STARTED:
        command->state = C42_COMMAND_ADMIT_QUERY;
        return 1;
    case FWLAB_HIF_ADMISSION_COMMITTED:
        if (!ticket_matches(command, &ticket)) {
            return hold_poisoned_admission(controller, command);
        }
        command->ticket = ticket;
        command->state = C42_COMMAND_PORT_COMMITTED;
        return 1;
    case FWLAB_HIF_ADMISSION_ABORTED:
    case FWLAB_HIF_ADMISSION_POISONED:
        command->state = C42_COMMAND_ABORT_RECONCILE;
        c42_fault_sq(controller, command->sq_index, C42_FAULT_PORT_CONTRACT);
        return 1;
    default:
        return hold_poisoned_admission(controller, command);
    }
}

static int progress_admit_poison(
    struct c42_controller *controller,
    struct c42_command_record *command)
{
    struct fwlab_hif_admission_key key;
    struct fwlab_hif_command_ticket ticket = {0};
    enum fwlab_hif_admission_state state =
        FWLAB_HIF_ADMISSION_NOT_STARTED;
    enum fwlab_hif_command_port_result result;

    admission_key_fill(command, &key);
    result = controller->providers.command.ops->admit_query(
        controller->providers.command.context, &key, &command->command,
        &state, &ticket
    );
    if (result == FWLAB_HIF_PORT_OK &&
        (state == FWLAB_HIF_ADMISSION_NOT_STARTED ||
         state == FWLAB_HIF_ADMISSION_ABORTED)) {
        command->state = C42_COMMAND_ABORT_RECONCILE;
    }
    return 1;
}

static int progress_hif_commit(
    struct c42_controller *controller,
    struct c42_command_record *command)
{
    struct c42_sq_record *sq = &controller->sq[command->sq_index];

    if ((sq->life != C42_QUEUE_LIVE &&
         sq->life != C42_QUEUE_PREQUIESCE) ||
        sq->ring_generation != command->sq_ring_generation ||
        sq->pending == 0) {
        c42_fault_sq(controller, command->sq_index, C42_FAULT_PORT_CONTRACT);
        return 1;
    }
    if (c42_find_active(
            controller, command->sq_index, command->sq_ring_generation,
            command->command_id) != NULL) {
        c42_fault_sq(controller, command->sq_index, C42_FAULT_DUPLICATE_CID);
        return 1;
    }
    command->state = C42_COMMAND_HIF_COMMITTED;
    sq->device_head = (uint16_t)((sq->device_head + 1u) % sq->depth);
    sq->pending--;
    return 1;
}

static int progress_abort_reconcile(
    struct c42_controller *controller,
    uint16_t index)
{
    struct c42_command_record *command = &controller->commands[index];
    enum fwlab_hif_command_port_result result;
    bool aborted = false;

    if (!fwlab_hif_prepared_token_valid(&command->prepared)) {
        clear_command_all(controller, index);
        return 1;
    }
    if (command->release_started == 0) {
        result = controller->providers.command.ops->prepare_abort(
            controller->providers.command.context, &command->prepared, &aborted
        );
        command->release_started = 1;
    } else {
        result = controller->providers.command.ops->prepare_abort_query(
            controller->providers.command.context, &command->prepared, &aborted
        );
    }
    if (result == FWLAB_HIF_PORT_OK && aborted) {
        clear_command_all(controller, index);
    } else if (result != FWLAB_HIF_PORT_IN_PROGRESS &&
               result != FWLAB_HIF_PORT_OK) {
        c42_fault_controller(controller, C42_FAULT_PROVIDER_POISON);
    }
    return 1;
}

int c42_progress_admission(struct c42_controller *controller)
{
    uint16_t offset;

    if (!c42_controller_valid(controller) ||
        (controller->phase != C42_CONTROLLER_READY &&
         controller->phase != C42_CONTROLLER_FAULTED_RESET_REQUIRED)) {
        return 0;
    }
    for (offset = 0; offset < controller->config.command_capacity; ++offset) {
        uint16_t index = (uint16_t)(
            (controller->admission_cursor + offset) %
            controller->config.command_capacity
        );
        struct c42_command_record *command = &controller->commands[index];

        if (command->state == C42_COMMAND_ADMIT_POISON_HOLD) {
            controller->admission_cursor = (uint8_t)(
                (index + 1u) % controller->config.command_capacity
            );
            return progress_admit_poison(controller, command);
        }
        if (command->state == C42_COMMAND_ABORT_RECONCILE) {
            controller->admission_cursor = (uint8_t)(
                (index + 1u) % controller->config.command_capacity
            );
            return progress_abort_reconcile(controller, index);
        }
    }
    if (controller->phase != C42_CONTROLLER_READY ||
        controller->admission_paused != 0) {
        return 0;
    }
    for (offset = 0; offset < controller->config.command_capacity; ++offset) {
        uint16_t index = (uint16_t)(
            (controller->admission_cursor + offset) %
            controller->config.command_capacity
        );
        struct c42_command_record *command = &controller->commands[index];

        if (command->state == C42_COMMAND_CAPTURED ||
            command->state == C42_COMMAND_PREPARE_QUERY) {
            controller->admission_cursor = (uint8_t)(
                (index + 1u) % controller->config.command_capacity
            );
            return progress_prepare(controller, index);
        }
        if (command->state == C42_COMMAND_PORT_RESERVED ||
            command->state == C42_COMMAND_ADMIT_QUERY) {
            controller->admission_cursor = (uint8_t)(
                (index + 1u) % controller->config.command_capacity
            );
            return progress_admit(controller, command);
        }
        if (command->state == C42_COMMAND_PORT_COMMITTED) {
            controller->admission_cursor = (uint8_t)(
                (index + 1u) % controller->config.command_capacity
            );
            return progress_hif_commit(controller, command);
        }
    }
    for (offset = 0; offset < C42_QUEUE_SLOTS; ++offset) {
        uint16_t index = (uint16_t)(
            (controller->sq_cursor + offset) % C42_QUEUE_SLOTS
        );
        const struct c42_sq_record *sq = &controller->sq[index];

        if ((sq->life == C42_QUEUE_LIVE ||
             sq->life == C42_QUEUE_PREQUIESCE) && sq->pending != 0) {
            controller->sq_cursor = (uint8_t)((index + 1u) % C42_QUEUE_SLOTS);
            return capture_one(controller, index);
        }
    }
    return 0;
}

static struct c42_control_record *find_control(
    struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    uint16_t index;

    for (index = 0; index < C42_BUSINESS_CONTROL_SLOTS; ++index) {
        if (controller->business_controls[index].in_use != 0 &&
            c42_operation_token_equal(
                &controller->business_controls[index].token, token)) {
            return &controller->business_controls[index];
        }
    }
    if (controller->reset_control.in_use != 0 &&
        c42_operation_token_equal(&controller->reset_control.token, token)) {
        return &controller->reset_control;
    }
    if (controller->teardown_control.in_use != 0 &&
        c42_operation_token_equal(&controller->teardown_control.token, token)) {
        return &controller->teardown_control;
    }
    return NULL;
}

static const struct c42_control_record *find_control_const(
    const struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    uint16_t index;

    for (index = 0; index < C42_BUSINESS_CONTROL_SLOTS; ++index) {
        if (controller->business_controls[index].in_use != 0 &&
            c42_operation_token_equal(
                &controller->business_controls[index].token, token)) {
            return &controller->business_controls[index];
        }
    }
    if (controller->reset_control.in_use != 0 &&
        c42_operation_token_equal(&controller->reset_control.token, token)) {
        return &controller->reset_control;
    }
    if (controller->teardown_control.in_use != 0 &&
        c42_operation_token_equal(&controller->teardown_control.token, token)) {
        return &controller->teardown_control;
    }
    return NULL;
}

static int cq_delete_dependencies_clear(
    const struct c42_controller *controller,
    uint16_t queue_index)
{
    uint16_t index;
    const struct c42_cq_record *cq = &controller->cq[queue_index];

    if (cq->unacked_count != 0 || cq->reserved_count != 0 ||
        cq->pending_ack.valid != 0 ||
        cq->create_scrub_retired == 0) {
        return 0;
    }
    for (index = 0; index < C42_QUEUE_SLOTS; ++index) {
        if (controller->sq[index].life != C42_QUEUE_ABSENT &&
            controller->sq[index].associated_cq_id == cq->queue_id) {
            return 0;
        }
    }
    for (index = 0; index < C42_CANDIDATE_SLOTS; ++index) {
        const struct c42_candidate_record *candidate =
            &controller->candidates[index];

        if (candidate->in_use != 0 &&
            candidate->descriptor.kind == C42_QUEUE_SQ &&
            candidate->descriptor.associated_cq_id == cq->queue_id &&
            candidate->state != C42_CANDIDATE_ABORTED &&
            candidate->state != C42_CANDIDATE_POISONED &&
            candidate->state != C42_CANDIDATE_SUPERSEDED) {
            return 0;
        }
    }
    return 1;
}

static int cq_delete_allowed(
    const struct c42_controller *controller,
    uint16_t queue_index)
{
    return controller->cq[queue_index].life == C42_QUEUE_LIVE &&
           cq_delete_dependencies_clear(controller, queue_index);
}

enum c42_result c42_delete_start(
    struct c42_controller *controller,
    uint8_t kind,
    uint16_t queue_id,
    struct c42_operation_token *token)
{
    struct c42_control_record *record = NULL;
    uint16_t queue_index;
    uint16_t index;
    uint64_t uid;
    uint32_t generation;

    if (!c42_controller_valid(controller) || token == NULL ||
        controller->phase != C42_CONTROLLER_READY ||
        (kind != C42_QUEUE_SQ && kind != C42_QUEUE_CQ) ||
        !c42_queue_index(queue_id, &queue_index)) {
        return C42_INVALID;
    }
    if ((kind == C42_QUEUE_SQ &&
         controller->sq[queue_index].life != C42_QUEUE_LIVE) ||
        (kind == C42_QUEUE_CQ &&
         !cq_delete_allowed(controller, queue_index))) {
        return C42_WRONG_STATE;
    }
    for (index = 0; index < C42_BUSINESS_CONTROL_SLOTS; ++index) {
        if (controller->business_controls[index].in_use == 0) {
            record = &controller->business_controls[index];
            break;
        }
    }
    if (record == NULL) {
        return C42_NO_CAPACITY;
    }
    if (!counter_available(&controller->control_uid) ||
        !generation_available(controller->next_control_generation)) {
        return C42_COUNTER_EXHAUSTED;
    }
    (void)c42_counter_take(&controller->control_uid, &uid);
    (void)c42_generation_take(
        &controller->next_control_generation, &generation
    );
    memset(record, 0, sizeof(*record));
    record->in_use = 1;
    record->kind = kind == C42_QUEUE_SQ ?
                   C42_CONTROL_DELETE_SQ : C42_CONTROL_DELETE_CQ;
    record->controller_epoch = controller->controller_epoch;
    record->queue_id = queue_id;
    record->state = C42_CONTROL_STARTED;
    record->token.instance_nonce = controller->config.instance_nonce;
    record->token.uid = uid;
    record->token.generation = generation;
    record->token.kind = record->kind;
    if (kind == C42_QUEUE_SQ) {
        controller->sq[queue_index].life = C42_QUEUE_PREQUIESCE;
        controller->sq[queue_index].frozen_tail =
            controller->sq[queue_index].host_tail;
    } else {
        controller->cq[queue_index].life = C42_QUEUE_QUIESCING;
    }
    *token = record->token;
    return C42_OK;
}

static int protected_control_start(
    struct c42_controller *controller,
    struct c42_control_record *record,
    struct c42_counter *uid_counter,
    uint32_t *generation_counter,
    uint8_t kind,
    struct c42_operation_token *token)
{
    uint64_t uid;
    uint32_t generation;

    if (record->in_use != 0 || !counter_available(uid_counter) ||
        !generation_available(*generation_counter)) {
        return 0;
    }
    (void)c42_counter_take(uid_counter, &uid);
    (void)c42_generation_take(generation_counter, &generation);
    memset(record, 0, sizeof(*record));
    record->in_use = 1;
    record->kind = kind;
    record->controller_epoch = controller->controller_epoch;
    record->state = C42_CONTROL_STARTED;
    record->token.instance_nonce = controller->config.instance_nonce;
    record->token.uid = uid;
    record->token.generation = generation;
    record->token.kind = kind;
    *token = record->token;
    return 1;
}

static void supersede_exported_records(struct c42_controller *controller)
{
    uint16_t index;

    for (index = 0; index < C42_CANDIDATE_SLOTS; ++index) {
        if (controller->candidates[index].in_use != 0) {
            controller->candidates[index].state = C42_CANDIDATE_SUPERSEDED;
        }
    }
    for (index = 0; index < C42_BUSINESS_CONTROL_SLOTS; ++index) {
        struct c42_control_record *record =
            &controller->business_controls[index];

        if (record->in_use != 0 &&
            record->state != C42_CONTROL_COMMITTED) {
            record->state = C42_CONTROL_SUPERSEDED;
        }
    }
    for (index = 0; index < controller->config.command_capacity; ++index) {
        struct c42_notification_record *notification =
            &controller->notifications[index];

        if (notification->in_use != 0 &&
            (notification->state == C42_NOTIFY_RESERVED ||
             notification->state == C42_NOTIFY_READY ||
             notification->state == C42_NOTIFY_ACQUIRED)) {
            notification->state = C42_NOTIFY_SUPPRESSED;
        }
    }
}

enum c42_result c42_reset_start(
    struct c42_controller *controller,
    struct c42_operation_token *token)
{
    uint32_t old_epoch;

    if (!c42_controller_valid(controller) || token == NULL ||
        (controller->phase != C42_CONTROLLER_COLD_NO_QUEUES &&
         controller->phase != C42_CONTROLLER_READY &&
         controller->phase != C42_CONTROLLER_FAULTED_RESET_REQUIRED)) {
        return C42_INVALID;
    }
    if (controller->controller_epoch == UINT32_MAX) {
        return C42_COUNTER_EXHAUSTED;
    }
    if (!protected_control_start(
            controller, &controller->reset_control, &controller->reset_uid,
            &controller->next_reset_generation, C42_CONTROL_RESET, token)) {
        return controller->reset_control.in_use != 0 ?
               C42_WRONG_STATE : C42_COUNTER_EXHAUSTED;
    }
    old_epoch = controller->controller_epoch;
    controller->reset_control.old_epoch = old_epoch;
    controller->controller_epoch++;
    controller->phase = C42_CONTROLLER_RESETTING;
    controller->admission_paused = 1;
    supersede_exported_records(controller);
    memset(controller->targets, 0, sizeof(controller->targets));
    return C42_OK;
}

enum c42_result c42_teardown_start(
    struct c42_controller *controller,
    struct c42_operation_token *token)
{
    uint32_t old_epoch;

    if (!c42_controller_valid(controller) || token == NULL ||
        controller->phase == C42_CONTROLLER_TEARING_DOWN ||
        controller->phase == C42_CONTROLLER_DEAD) {
        return C42_INVALID;
    }
    if (!protected_control_start(
            controller, &controller->teardown_control,
            &controller->teardown_uid,
            &controller->next_teardown_generation,
            C42_CONTROL_TEARDOWN, token)) {
        return controller->teardown_control.in_use != 0 ?
               C42_WRONG_STATE : C42_COUNTER_EXHAUSTED;
    }
    old_epoch = controller->controller_epoch;
    controller->teardown_control.old_epoch = old_epoch;
    if (controller->controller_epoch != UINT32_MAX) {
        controller->controller_epoch++;
    }
    controller->phase = C42_CONTROLLER_TEARING_DOWN;
    controller->admission_paused = 1;
    supersede_exported_records(controller);
    if (controller->reset_control.in_use != 0 &&
        controller->reset_control.state != C42_CONTROL_COMMITTED) {
        controller->reset_control.state = C42_CONTROL_SUPERSEDED;
    }
    memset(controller->targets, 0, sizeof(controller->targets));
    return C42_OK;
}

static int command_for_sq_exists(
    const struct c42_controller *controller,
    uint16_t sq_index,
    uint32_t generation)
{
    uint32_t index;

    for (index = 0; index < controller->config.command_capacity; ++index) {
        if (controller->commands[index].state != C42_COMMAND_FREE &&
            controller->commands[index].sq_index == sq_index &&
            controller->commands[index].sq_ring_generation == generation) {
            return 1;
        }
    }
    return 0;
}

static int target_for_sq_exists(
    const struct c42_controller *controller,
    uint16_t sq_index,
    uint32_t generation)
{
    uint32_t index;

    for (index = 0; index < controller->config.target_capacity; ++index) {
        if (controller->targets[index].in_use != 0 &&
            controller->targets[index].sq_index == sq_index &&
            controller->targets[index].sq_ring_generation == generation) {
            return 1;
        }
    }
    return 0;
}

static int committed_for_sq_exists(
    const struct c42_controller *controller,
    uint16_t sq_id,
    uint32_t generation)
{
    uint16_t cq_index;

    for (cq_index = 0; cq_index < C42_QUEUE_SLOTS; ++cq_index) {
        const struct c42_cq_record *cq = &controller->cq[cq_index];
        uint16_t slot;

        for (slot = 0; slot < cq->depth; ++slot) {
            if (cq->slots[slot].state == C42_SLOT_CQE_COMMITTED &&
                cq->slots[slot].source_sq_id == sq_id &&
                cq->slots[slot].source_sq_generation == generation) {
                return 1;
            }
        }
    }
    return 0;
}

static void clear_sq_to_absent(struct c42_sq_record *sq)
{
    uint16_t qid = sq->queue_id;
    uint32_t last_ring = sq->last_ring_generation;
    uint32_t last_mapping = sq->last_mapping_generation;

    memset(sq, 0, sizeof(*sq));
    sq->queue_id = qid;
    sq->last_ring_generation = last_ring;
    sq->last_mapping_generation = last_mapping;
    sq->life = C42_QUEUE_ABSENT;
}

static void clear_cq_to_absent(struct c42_cq_record *cq)
{
    uint16_t qid = cq->queue_id;
    uint32_t last_ring = cq->last_ring_generation;
    uint32_t last_mapping = cq->last_mapping_generation;
    uint32_t next_slot = cq->next_slot_generation;

    memset(cq, 0, sizeof(*cq));
    cq->queue_id = qid;
    cq->last_ring_generation = last_ring;
    cq->last_mapping_generation = last_mapping;
    cq->next_slot_generation = next_slot == 0 ? 1 : next_slot;
    cq->life = C42_QUEUE_ABSENT;
}

static int progress_delete(
    struct c42_controller *controller,
    struct c42_control_record *record)
{
    uint16_t index;

    if (!c42_queue_index(record->queue_id, &index)) {
        record->state = C42_CONTROL_POISONED;
        return 1;
    }
    if (record->kind == C42_CONTROL_DELETE_CQ) {
        if (controller->cq[index].life != C42_QUEUE_QUIESCING ||
            !cq_delete_dependencies_clear(controller, index)) {
            return 0;
        }
        clear_cq_to_absent(&controller->cq[index]);
        record->state = C42_CONTROL_COMMITTED;
        return 1;
    }
    if (controller->sq[index].life == C42_QUEUE_PREQUIESCE) {
        if (controller->sq[index].device_head !=
                controller->sq[index].frozen_tail ||
            controller->sq[index].pending != 0) {
            return 0;
        }
        controller->sq[index].life = C42_QUEUE_QUIESCING;
        record->state = C42_CONTROL_WAITING;
        return 1;
    }
    if (controller->sq[index].life == C42_QUEUE_QUIESCING) {
        uint32_t generation = controller->sq[index].ring_generation;

        if (command_for_sq_exists(controller, index, generation) ||
            target_for_sq_exists(controller, index, generation)) {
            return 0;
        }
        if (committed_for_sq_exists(
                controller, controller->sq[index].queue_id, generation)) {
            controller->sq[index].life = C42_QUEUE_TOMBSTONED;
        } else {
            clear_sq_to_absent(&controller->sq[index]);
        }
        record->state = C42_CONTROL_COMMITTED;
        return 1;
    }
    return 0;
}

static void clear_runtime_tables(struct c42_controller *controller)
{
    uint16_t index;

    memset(controller->sq, 0, sizeof(controller->sq));
    memset(controller->cq, 0, sizeof(controller->cq));
    memset(controller->candidates, 0, sizeof(controller->candidates));
    memset(controller->candidate_tombstones, 0,
           sizeof(controller->candidate_tombstones));
    memset(controller->commands, 0, sizeof(controller->commands));
    memset(controller->publications, 0, sizeof(controller->publications));
    memset(controller->reconciles, 0, sizeof(controller->reconciles));
    memset(controller->notifications, 0, sizeof(controller->notifications));
    memset(controller->targets, 0, sizeof(controller->targets));
    memset(controller->business_controls, 0,
           sizeof(controller->business_controls));
    for (index = 0; index < C42_QUEUE_SLOTS; ++index) {
        controller->sq[index].queue_id = index;
        controller->sq[index].life = C42_QUEUE_ABSENT;
        controller->cq[index].queue_id = index;
        controller->cq[index].life = C42_QUEUE_ABSENT;
        controller->cq[index].next_slot_generation = 1;
    }
}

static int progress_epoch_control(
    struct c42_controller *controller,
    struct c42_control_record *record,
    int teardown)
{
    enum fwlab_hif_command_port_result port_result;
    enum c42_memory_result memory_result;
    bool port_quiescent = false;
    bool memory_quiescent = false;

    if (teardown != 0 && controller->reset_control.in_use != 0 &&
        controller->reset_control.state != C42_CONTROL_COMMITTED) {
        struct c42_control_record *reset = &controller->reset_control;

        if (reset->port_started == 0) {
            port_result = controller->providers.command.ops->reset_begin(
                controller->providers.command.context,
                controller->config.instance_nonce, reset->old_epoch
            );
            if (port_result == FWLAB_HIF_PORT_OK) {
                reset->port_started = 1;
            } else if (port_result != FWLAB_HIF_PORT_IN_PROGRESS) {
                record->state = C42_CONTROL_POISONED;
            }
            return 1;
        }
        if (reset->memory_started == 0) {
            memory_result = controller->providers.memory.ops->reset_begin(
                controller->providers.memory.context,
                controller->config.instance_nonce, reset->old_epoch
            );
            if (memory_result == C42_MEMORY_OK) {
                reset->memory_started = 1;
            } else if (memory_result != C42_MEMORY_IN_PROGRESS) {
                record->state = C42_CONTROL_POISONED;
            }
            return 1;
        }
    }

    if (record->port_started == 0) {
        port_result = teardown != 0 ?
            controller->providers.command.ops->teardown_begin(
                controller->providers.command.context,
                controller->config.instance_nonce, record->old_epoch) :
            controller->providers.command.ops->reset_begin(
                controller->providers.command.context,
                controller->config.instance_nonce, record->old_epoch);
        if (port_result == FWLAB_HIF_PORT_OK) {
            record->port_started = 1;
        } else if (port_result == FWLAB_HIF_PORT_IN_PROGRESS) {
            record->state = C42_CONTROL_WAITING;
        } else {
            record->state = C42_CONTROL_POISONED;
        }
        return 1;
    }
    if (record->memory_started == 0) {
        memory_result = teardown != 0 ?
            controller->providers.memory.ops->teardown_begin(
                controller->providers.memory.context,
                controller->config.instance_nonce, record->old_epoch) :
            controller->providers.memory.ops->reset_begin(
                controller->providers.memory.context,
                controller->config.instance_nonce, record->old_epoch);
        if (memory_result == C42_MEMORY_OK) {
            record->memory_started = 1;
        } else if (memory_result == C42_MEMORY_IN_PROGRESS) {
            record->state = C42_CONTROL_WAITING;
        } else {
            record->state = C42_CONTROL_POISONED;
        }
        return 1;
    }
    port_result = teardown != 0 ?
        controller->providers.command.ops->teardown_quiescent(
            controller->providers.command.context,
            controller->config.instance_nonce, record->old_epoch,
            &port_quiescent) :
        controller->providers.command.ops->reset_quiescent(
            controller->providers.command.context,
            controller->config.instance_nonce, record->old_epoch,
            &port_quiescent);
    memory_result = teardown != 0 ?
        controller->providers.memory.ops->teardown_quiescent(
            controller->providers.memory.context,
            controller->config.instance_nonce, record->old_epoch,
            &memory_quiescent) :
        controller->providers.memory.ops->reset_quiescent(
            controller->providers.memory.context,
            controller->config.instance_nonce, record->old_epoch,
            &memory_quiescent);
    if (port_result == FWLAB_HIF_PORT_OK && memory_result == C42_MEMORY_OK &&
        port_quiescent && memory_quiescent) {
        clear_runtime_tables(controller);
        controller->fault_cause = 0;
        controller->admission_paused = 0;
        controller->scheduler_cursor = 0;
        controller->admission_cursor = 0;
        controller->publication_cursor = 0;
        controller->reconcile_cursor = 0;
        controller->sq_cursor = 0;
        controller->ready_cursor = 0;
        controller->phase = teardown != 0 ?
                            C42_CONTROLLER_DEAD :
                            C42_CONTROLLER_COLD_NO_QUEUES;
        record->state = C42_CONTROL_COMMITTED;
        if (teardown != 0) {
            memset(&controller->reset_control, 0,
                   sizeof(controller->reset_control));
        }
    } else if ((port_result == FWLAB_HIF_PORT_OK ||
                port_result == FWLAB_HIF_PORT_IN_PROGRESS) &&
               (memory_result == C42_MEMORY_OK ||
                memory_result == C42_MEMORY_IN_PROGRESS)) {
        record->state = C42_CONTROL_WAITING;
    } else {
        record->state = C42_CONTROL_POISONED;
    }
    return 1;
}

static int progress_control_once(
    struct c42_controller *controller,
    struct c42_control_record *record)
{
    if (record->in_use == 0 ||
        record->state == C42_CONTROL_COMMITTED ||
        record->state == C42_CONTROL_RETIRED ||
        record->state == C42_CONTROL_POISONED ||
        record->state == C42_CONTROL_SUPERSEDED) {
        return 0;
    }
    if (record->kind == C42_CONTROL_DELETE_SQ ||
        record->kind == C42_CONTROL_DELETE_CQ) {
        return progress_delete(controller, record);
    }
    return progress_epoch_control(
        controller, record, record->kind == C42_CONTROL_TEARDOWN
    );
}

int c42_progress_queue_controls(struct c42_controller *controller)
{
    uint16_t index;

    if (!c42_controller_valid(controller)) {
        return 0;
    }
    if (controller->teardown_control.in_use != 0 &&
        controller->teardown_control.state != C42_CONTROL_COMMITTED) {
        return progress_control_once(controller, &controller->teardown_control);
    }
    if (controller->reset_control.in_use != 0 &&
        controller->reset_control.state != C42_CONTROL_COMMITTED &&
        controller->phase != C42_CONTROLLER_TEARING_DOWN) {
        return progress_control_once(controller, &controller->reset_control);
    }
    for (index = 0; index < C42_BUSINESS_CONTROL_SLOTS; ++index) {
        if (progress_control_once(
                controller, &controller->business_controls[index]) != 0) {
            return 1;
        }
    }
    return 0;
}

enum c42_result c42_control_progress(
    struct c42_controller *controller,
    const struct c42_operation_token *token,
    uint32_t budget)
{
    struct c42_control_record *record;
    uint32_t unit;

    if (!c42_controller_valid(controller) || token == NULL) {
        return C42_INVALID;
    }
    record = find_control(controller, token);
    if (record == NULL) {
        return C42_STALE;
    }
    if (record->state == C42_CONTROL_SUPERSEDED ||
        (record->kind != C42_CONTROL_RESET &&
         record->kind != C42_CONTROL_TEARDOWN &&
         record->controller_epoch != controller->controller_epoch)) {
        return C42_SUPERSEDED;
    }
    for (unit = 0; unit < budget; ++unit) {
        if (progress_control_once(controller, record) == 0) {
            break;
        }
    }
    return record->state == C42_CONTROL_POISONED ? C42_POISONED : C42_OK;
}

enum c42_result c42_control_query(
    const struct c42_controller *controller,
    const struct c42_operation_token *token,
    struct c42_control_status *status)
{
    const struct c42_control_record *record;
    struct c42_control_status local = {0};

    if (!c42_controller_valid(controller) || token == NULL || status == NULL) {
        return C42_INVALID;
    }
    record = find_control_const(controller, token);
    if (record == NULL) {
        return C42_STALE;
    }
    local.token = record->token;
    local.state = record->state;
    local.cause = record->cause;
    local.retry = record->retry;
    *status = local;
    return C42_OK;
}

enum c42_result c42_control_retire(
    struct c42_controller *controller,
    const struct c42_operation_token *token)
{
    struct c42_control_record *record;

    if (!c42_controller_valid(controller) || token == NULL) {
        return C42_INVALID;
    }
    record = find_control(controller, token);
    if (record == NULL) {
        return C42_STALE;
    }
    if (record->state == C42_CONTROL_SUPERSEDED &&
        (record->kind == C42_CONTROL_DELETE_SQ ||
         record->kind == C42_CONTROL_DELETE_CQ)) {
        memset(record, 0, sizeof(*record));
        return C42_OK;
    }
    if (record->state != C42_CONTROL_COMMITTED) {
        return C42_WRONG_STATE;
    }
    memset(record, 0, sizeof(*record));
    return C42_OK;
}

void c42_try_finish_tombstones(struct c42_controller *controller)
{
    uint16_t index;

    for (index = 0; index < C42_QUEUE_SLOTS; ++index) {
        struct c42_sq_record *sq = &controller->sq[index];

        if (sq->life == C42_QUEUE_TOMBSTONED &&
            !committed_for_sq_exists(
                controller, sq->queue_id, sq->ring_generation)) {
            clear_sq_to_absent(sq);
        }
    }
}
