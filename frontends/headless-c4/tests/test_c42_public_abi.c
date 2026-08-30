/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: BSD-3-Clause */

#include "hif/c42.h"

#include <stddef.h>
#include <stdio.h>

#define ABI_SIZE(type, value) \
    _Static_assert(sizeof(type) == (value), #type " size")
#define ABI_OFFSET(type, member, value) \
    _Static_assert(offsetof(type, member) == (value), #type "." #member)

_Static_assert(C42_COMPONENT_VERSION == 2 && C42_MEMORY_PORT_VERSION == 2 &&
               C42_OBSERVER_VERSION == 2, "C4.2 public versions");
_Static_assert(C42_OK == 0 && C42_INVALID == 1 && C42_WRONG_STATE == 2 &&
               C42_STALE == 3 && C42_NO_EFFECT == 4 &&
               C42_BACKPRESSURE == 5 && C42_NO_CAPACITY == 6 &&
               C42_IN_PROGRESS == 7 && C42_TOO_LATE == 8 &&
               C42_FAULTED == 9 && C42_COUNTER_EXHAUSTED == 10 &&
               C42_NOT_FOUND == 11 && C42_POISONED == 12 &&
               C42_SUPERSEDED == 13, "c42_result values");
_Static_assert(C42_CONTROLLER_COLD_NO_QUEUES == 0 &&
               C42_CONTROLLER_READY == 1 && C42_CONTROLLER_RESETTING == 2 &&
               C42_CONTROLLER_FAULTED_RESET_REQUIRED == 3 &&
               C42_CONTROLLER_TEARING_DOWN == 4 && C42_CONTROLLER_DEAD == 5,
               "controller phase values");
_Static_assert(C42_QUEUE_SQ == 1 && C42_QUEUE_CQ == 2,
               "queue kind values");
_Static_assert(C42_QUEUE_ABSENT == 0 && C42_QUEUE_PREPARED == 1 &&
               C42_QUEUE_LIVE == 2 && C42_QUEUE_PREQUIESCE == 3 &&
               C42_QUEUE_QUIESCING == 4 && C42_QUEUE_TOMBSTONED == 5 &&
               C42_QUEUE_FAULTED_RESET_REQUIRED == 6,
               "queue life values");
_Static_assert(C42_CANDIDATE_PREPARED == 1 &&
               C42_CANDIDATE_SCRUB_UNKNOWN == 2 &&
               C42_CANDIDATE_READY == 3 && C42_CANDIDATE_ABORTING == 4 &&
               C42_CANDIDATE_COMMITTED == 5 && C42_CANDIDATE_ABORTED == 6 &&
               C42_CANDIDATE_POISONED == 7 &&
               C42_CANDIDATE_SUPERSEDED == 8 &&
               C42_CANDIDATE_COMMITTED_AWAIT_RETIRE == 9 &&
               C42_CANDIDATE_RETIRE_UNKNOWN == 10 &&
               C42_CANDIDATE_RETIRE_READY == 11 &&
               C42_CANDIDATE_RETIRED == 12, "candidate state values");
_Static_assert(C42_CONTROL_DELETE_SQ == 1 && C42_CONTROL_DELETE_CQ == 2 &&
               C42_CONTROL_RESET == 3 && C42_CONTROL_TEARDOWN == 4,
               "control kind values");
_Static_assert(C42_CONTROL_STARTED == 1 && C42_CONTROL_WAITING == 2 &&
               C42_CONTROL_COMMITTED == 3 &&
               C42_CONTROL_CLEANUP_PENDING == 4 &&
               C42_CONTROL_RETIRED == 5 && C42_CONTROL_POISONED == 6 &&
               C42_CONTROL_SUPERSEDED == 7, "control state values");
_Static_assert(C42_PUBLIC_SLOT_FREE == 0 && C42_PUBLIC_SLOT_RESERVED == 1 &&
               C42_PUBLIC_SLOT_CQE_COMMITTED == 2,
               "public slot values");
_Static_assert(C42_NOTIFICATION_READY == 1 &&
               C42_NOTIFICATION_ACQUIRED == 2 &&
               C42_NOTIFICATION_CONSUMED == 3 &&
               C42_NOTIFICATION_SUPPRESSED == 4,
               "notification values");
_Static_assert(C42_MEMORY_SQ_READ == 1 && C42_MEMORY_CQ_PUBLISH == 2,
               "memory role values");
_Static_assert(C42_MEMORY_OK == 0 && C42_MEMORY_INVALID == 1 &&
               C42_MEMORY_STALE == 2 && C42_MEMORY_NO_EFFECT == 3 &&
               C42_MEMORY_EXACT_PREFIX == 4 && C42_MEMORY_FULL == 5 &&
               C42_MEMORY_UNKNOWN == 6 && C42_MEMORY_POISONED == 7 &&
               C42_MEMORY_IN_PROGRESS == 8 && C42_MEMORY_RETIRED == 9,
               "memory result values");
_Static_assert(C42_OBSERVER_SLOT_FREE == 0 &&
               C42_OBSERVER_SLOT_RESERVED == 1 &&
               C42_OBSERVER_SLOT_CQE_COMMITTED == 2 &&
               C42_OBSERVER_SLOT_BODY_STAGED == 10 &&
               C42_OBSERVER_SLOT_MARKER_RECONCILE == 11 &&
               C42_OBSERVER_SLOT_INVALID == 255, "observer slot values");
_Static_assert(C42_OBSERVER_COMMAND_FREE == 0 &&
               C42_OBSERVER_COMMAND_CAPTURED == 1 &&
               C42_OBSERVER_COMMAND_PREPARE_QUERY == 2 &&
               C42_OBSERVER_COMMAND_PORT_RESERVED == 3 &&
               C42_OBSERVER_COMMAND_ADMIT_QUERY == 4 &&
               C42_OBSERVER_COMMAND_PORT_COMMITTED == 5 &&
               C42_OBSERVER_COMMAND_HIF_COMMITTED == 6 &&
               C42_OBSERVER_COMMAND_READY == 7 &&
               C42_OBSERVER_COMMAND_LEASED == 8 &&
               C42_OBSERVER_COMMAND_CONSUME_PREPARE == 9 &&
               C42_OBSERVER_COMMAND_PUB_RESERVED == 10 &&
               C42_OBSERVER_COMMAND_MARKER_RECONCILE == 11 &&
               C42_OBSERVER_COMMAND_RELEASE_RECONCILE == 12 &&
               C42_OBSERVER_COMMAND_ABORT_RECONCILE == 13 &&
               C42_OBSERVER_COMMAND_ADMIT_POISON_HOLD == 14 &&
               C42_OBSERVER_COMMAND_CONSUME_POISON_HOLD == 15 &&
               C42_OBSERVER_COMMAND_INVALID == 255,
               "observer command values");
_Static_assert(C42_OBSERVER_RECONCILE_RESERVED == 0 &&
               C42_OBSERVER_RECONCILE_PREPARED == 1 &&
               C42_OBSERVER_RECONCILE_COMMIT_UNKNOWN == 2 &&
               C42_OBSERVER_RECONCILE_CLEANUP_PENDING == 3 &&
               C42_OBSERVER_RECONCILE_RETIRE_READY == 4 &&
               C42_OBSERVER_RECONCILE_INVALID == 255,
               "observer reconcile values");
_Static_assert(C42_OBSERVER_NOTIFY_RESERVED == 0 &&
               C42_OBSERVER_NOTIFY_READY == 1 &&
               C42_OBSERVER_NOTIFY_ACQUIRED == 2 &&
               C42_OBSERVER_NOTIFY_CONSUMED == 3 &&
               C42_OBSERVER_NOTIFY_SUPPRESSED == 4 &&
               C42_OBSERVER_NOTIFY_INVALID == 255,
               "observer notification values");

ABI_SIZE(struct c42_counter_seed, 16);
ABI_SIZE(struct c42_config, 232);
ABI_OFFSET(struct c42_config, instance_nonce, 16);
ABI_OFFSET(struct c42_config, origin_uid, 40);
ABI_OFFSET(struct c42_config, teardown_uid, 200);
ABI_OFFSET(struct c42_config, initial_controller_epoch, 216);
ABI_SIZE(struct c42_providers, 32);
ABI_SIZE(struct c42_operation_token, 24);
ABI_SIZE(struct c42_queue_descriptor, 80);
ABI_OFFSET(struct c42_queue_descriptor, memory, 16);
ABI_OFFSET(struct c42_queue_descriptor, reserved, 64);
ABI_SIZE(struct c42_candidate_status, 48);
ABI_SIZE(struct c42_control_status, 48);
ABI_SIZE(struct c42_sq_tail_event, 24);
ABI_SIZE(struct c42_cq_head_event, 24);
ABI_SIZE(struct c42_target_ref, 120);
ABI_SIZE(struct c42_notification, 48);
ABI_SIZE(struct c42_step_result, 16);
ABI_SIZE(struct c42_queue_snapshot, 24);
ABI_SIZE(struct c42_snapshot, 176);
ABI_OFFSET(struct c42_snapshot, sq, 48);
ABI_OFFSET(struct c42_snapshot, cq, 96);
ABI_OFFSET(struct c42_snapshot, reserved, 144);
ABI_SIZE(struct c42_queue_memory_cap, 48);
ABI_OFFSET(struct c42_queue_memory_cap, controller_epoch, 24);
ABI_OFFSET(struct c42_queue_memory_cap, queue_id, 40);
ABI_SIZE(struct c42_memory_token, 24);
ABI_SIZE(struct c42_memory_status, 40);
ABI_OFFSET(struct c42_memory_status, result, 24);
ABI_OFFSET(struct c42_memory_status, committed, 32);
ABI_SIZE(struct c42_memory_ops, 128);
ABI_OFFSET(struct c42_memory_ops, validate, 8);
ABI_OFFSET(struct c42_memory_ops, scrub_retire_start, 48);
ABI_OFFSET(struct c42_memory_ops, teardown_quiescent, 120);
ABI_SIZE(struct c42_memory_port, 16);

ABI_SIZE(struct c42_observer_slot_v2, 64);
ABI_OFFSET(struct c42_observer_slot_v2, publication_uid, 0);
ABI_OFFSET(struct c42_observer_slot_v2, notification_uid, 8);
ABI_OFFSET(struct c42_observer_slot_v2, cq_ring_generation, 16);
ABI_OFFSET(struct c42_observer_slot_v2, source_sq_generation, 20);
ABI_OFFSET(struct c42_observer_slot_v2, slot_generation, 24);
ABI_OFFSET(struct c42_observer_slot_v2, source_sq_id, 28);
ABI_OFFSET(struct c42_observer_slot_v2, command_id, 30);
ABI_OFFSET(struct c42_observer_slot_v2, submission_queue_head, 32);
ABI_OFFSET(struct c42_observer_slot_v2, ordinal, 34);
ABI_OFFSET(struct c42_observer_slot_v2, phase, 36);
ABI_OFFSET(struct c42_observer_slot_v2, state, 37);
ABI_OFFSET(struct c42_observer_slot_v2, owner_present, 38);
ABI_OFFSET(struct c42_observer_slot_v2, origin_matches_owner, 39);
ABI_OFFSET(struct c42_observer_slot_v2, wire, 40);
ABI_OFFSET(struct c42_observer_slot_v2, reserved, 56);

ABI_SIZE(struct c42_observer_queue_v2, 2104);
ABI_OFFSET(struct c42_observer_queue_v2, ring_generation, 0);
ABI_OFFSET(struct c42_observer_queue_v2, mapping_generation, 4);
ABI_OFFSET(struct c42_observer_queue_v2, last_ring_generation, 8);
ABI_OFFSET(struct c42_observer_queue_v2, last_mapping_generation, 12);
ABI_OFFSET(struct c42_observer_queue_v2, next_slot_generation, 16);
ABI_OFFSET(struct c42_observer_queue_v2, queue_id, 20);
ABI_OFFSET(struct c42_observer_queue_v2, associated_cq_id, 22);
ABI_OFFSET(struct c42_observer_queue_v2, depth, 24);
ABI_OFFSET(struct c42_observer_queue_v2, host_index, 26);
ABI_OFFSET(struct c42_observer_queue_v2, device_index, 28);
ABI_OFFSET(struct c42_observer_queue_v2, pending, 30);
ABI_OFFSET(struct c42_observer_queue_v2, frozen_tail, 32);
ABI_OFFSET(struct c42_observer_queue_v2, unacked_count, 34);
ABI_OFFSET(struct c42_observer_queue_v2, reserved_count, 36);
ABI_OFFSET(struct c42_observer_queue_v2, pending_ack_head, 38);
ABI_OFFSET(struct c42_observer_queue_v2, pending_ack_delta, 40);
ABI_OFFSET(struct c42_observer_queue_v2, kind, 42);
ABI_OFFSET(struct c42_observer_queue_v2, queue_class, 43);
ABI_OFFSET(struct c42_observer_queue_v2, life, 44);
ABI_OFFSET(struct c42_observer_queue_v2, phase, 45);
ABI_OFFSET(struct c42_observer_queue_v2, create_scrub_retired, 46);
ABI_OFFSET(struct c42_observer_queue_v2, pending_ack_valid, 47);
ABI_OFFSET(struct c42_observer_queue_v2, reserved0, 48);
ABI_OFFSET(struct c42_observer_queue_v2, slots, 56);

ABI_SIZE(struct c42_observer_command_v2, 88);
ABI_OFFSET(struct c42_observer_command_v2, handle, 0);
ABI_OFFSET(struct c42_observer_command_v2, client_uid, 24);
ABI_OFFSET(struct c42_observer_command_v2, publication_uid, 32);
ABI_OFFSET(struct c42_observer_command_v2, notification_uid, 40);
ABI_OFFSET(struct c42_observer_command_v2, sq_ring_generation, 48);
ABI_OFFSET(struct c42_observer_command_v2, active_generation, 52);
ABI_OFFSET(struct c42_observer_command_v2, command_id, 56);
ABI_OFFSET(struct c42_observer_command_v2, sq_index, 58);
ABI_OFFSET(struct c42_observer_command_v2, cq_index, 60);
ABI_OFFSET(struct c42_observer_command_v2, cq_slot, 62);
ABI_OFFSET(struct c42_observer_command_v2, sqhd_snapshot, 64);
ABI_OFFSET(struct c42_observer_command_v2, state, 66);
ABI_OFFSET(struct c42_observer_command_v2, queue_class, 67);
ABI_OFFSET(struct c42_observer_command_v2, prepared_origin_matches, 68);
ABI_OFFSET(struct c42_observer_command_v2, ticket_identity_matches, 69);
ABI_OFFSET(struct c42_observer_command_v2, ready_ticket_matches, 70);
ABI_OFFSET(struct c42_observer_command_v2, lease_ticket_matches, 71);
ABI_OFFSET(struct c42_observer_command_v2, consume_known, 72);
ABI_OFFSET(struct c42_observer_command_v2, reserved0, 73);
ABI_OFFSET(struct c42_observer_command_v2, reserved1, 74);
ABI_OFFSET(struct c42_observer_command_v2, reserved, 76);

ABI_SIZE(struct c42_observer_publication_v2, 40);
ABI_OFFSET(struct c42_observer_publication_v2, publication_uid, 0);
ABI_OFFSET(struct c42_observer_publication_v2, body_token_uid, 8);
ABI_OFFSET(struct c42_observer_publication_v2, marker_token_uid, 16);
ABI_OFFSET(struct c42_observer_publication_v2, command_index, 24);
ABI_OFFSET(struct c42_observer_publication_v2, body_prefix, 26);
ABI_OFFSET(struct c42_observer_publication_v2, in_use, 28);
ABI_OFFSET(struct c42_observer_publication_v2, body_started, 29);
ABI_OFFSET(struct c42_observer_publication_v2, marker_started, 30);
ABI_OFFSET(struct c42_observer_publication_v2, marker_visible, 31);
ABI_OFFSET(struct c42_observer_publication_v2, reserved, 32);

ABI_SIZE(struct c42_observer_reconcile_v2, 32);
ABI_OFFSET(struct c42_observer_reconcile_v2, publication_uid, 0);
ABI_OFFSET(struct c42_observer_reconcile_v2, consume_uid, 8);
ABI_OFFSET(struct c42_observer_reconcile_v2, command_index, 16);
ABI_OFFSET(struct c42_observer_reconcile_v2, in_use, 18);
ABI_OFFSET(struct c42_observer_reconcile_v2, state, 19);
ABI_OFFSET(struct c42_observer_reconcile_v2, consume_known, 20);
ABI_OFFSET(struct c42_observer_reconcile_v2, lease_matches_command, 21);
ABI_OFFSET(struct c42_observer_reconcile_v2, reserved0, 22);
ABI_OFFSET(struct c42_observer_reconcile_v2, reserved, 24);

ABI_SIZE(struct c42_observer_notification_v2, 56);
ABI_OFFSET(struct c42_observer_notification_v2, token, 0);
ABI_OFFSET(struct c42_observer_notification_v2, publication_uid, 24);
ABI_OFFSET(struct c42_observer_notification_v2, cq_ring_generation, 32);
ABI_OFFSET(struct c42_observer_notification_v2, controller_epoch, 36);
ABI_OFFSET(struct c42_observer_notification_v2, completion_queue_id, 40);
ABI_OFFSET(struct c42_observer_notification_v2, slot_ordinal, 42);
ABI_OFFSET(struct c42_observer_notification_v2, in_use, 44);
ABI_OFFSET(struct c42_observer_notification_v2, state, 45);
ABI_OFFSET(struct c42_observer_notification_v2, current_epoch, 46);
ABI_OFFSET(struct c42_observer_notification_v2, reserved0, 47);
ABI_OFFSET(struct c42_observer_notification_v2, reserved, 48);

ABI_SIZE(struct c42_observer_candidate_v2, 64);
ABI_OFFSET(struct c42_observer_candidate_v2, token, 0);
ABI_OFFSET(struct c42_observer_candidate_v2, controller_epoch, 24);
ABI_OFFSET(struct c42_observer_candidate_v2, state, 28);
ABI_OFFSET(struct c42_observer_candidate_v2,
           associated_cq_ring_generation, 32);
ABI_OFFSET(struct c42_observer_candidate_v2,
           associated_cq_mapping_generation, 36);
ABI_OFFSET(struct c42_observer_candidate_v2, queue_id, 40);
ABI_OFFSET(struct c42_observer_candidate_v2, associated_cq_id, 42);
ABI_OFFSET(struct c42_observer_candidate_v2, in_use, 44);
ABI_OFFSET(struct c42_observer_candidate_v2, kind, 45);
ABI_OFFSET(struct c42_observer_candidate_v2, scrub_started, 46);
ABI_OFFSET(struct c42_observer_candidate_v2, retire_started, 47);
ABI_OFFSET(struct c42_observer_candidate_v2, provider_retired, 48);
ABI_OFFSET(struct c42_observer_candidate_v2, reserved0, 49);
ABI_OFFSET(struct c42_observer_candidate_v2, reserved, 52);

ABI_SIZE(struct c42_observer_control_v2, 56);
ABI_OFFSET(struct c42_observer_control_v2, token, 0);
ABI_OFFSET(struct c42_observer_control_v2, controller_epoch, 24);
ABI_OFFSET(struct c42_observer_control_v2, old_epoch, 28);
ABI_OFFSET(struct c42_observer_control_v2, state, 32);
ABI_OFFSET(struct c42_observer_control_v2, queue_id, 36);
ABI_OFFSET(struct c42_observer_control_v2, in_use, 38);
ABI_OFFSET(struct c42_observer_control_v2, kind, 39);
ABI_OFFSET(struct c42_observer_control_v2, port_started, 40);
ABI_OFFSET(struct c42_observer_control_v2, memory_started, 41);
ABI_OFFSET(struct c42_observer_control_v2, reserved0, 42);
ABI_OFFSET(struct c42_observer_control_v2, reserved, 44);

ABI_SIZE(struct c42_observer_target_v2, 64);
ABI_OFFSET(struct c42_observer_target_v2, token, 0);
ABI_OFFSET(struct c42_observer_target_v2, handle, 24);
ABI_OFFSET(struct c42_observer_target_v2, sq_ring_generation, 48);
ABI_OFFSET(struct c42_observer_target_v2, sq_index, 52);
ABI_OFFSET(struct c42_observer_target_v2, in_use, 54);
ABI_OFFSET(struct c42_observer_target_v2, identity_matches_active, 55);
ABI_OFFSET(struct c42_observer_target_v2, reserved, 56);

ABI_SIZE(struct c42_observer_v2, 26888);
ABI_OFFSET(struct c42_observer_v2, version, 0);
ABI_OFFSET(struct c42_observer_v2, controller_epoch, 4);
ABI_OFFSET(struct c42_observer_v2, instance_nonce, 8);
ABI_OFFSET(struct c42_observer_v2, phase, 16);
ABI_OFFSET(struct c42_observer_v2, command_capacity, 24);
ABI_OFFSET(struct c42_observer_v2, admission_paused, 28);
ABI_OFFSET(struct c42_observer_v2, ready_poll_pending, 35);
ABI_OFFSET(struct c42_observer_v2, reserved0, 36);
ABI_OFFSET(struct c42_observer_v2, sq, 40);
ABI_OFFSET(struct c42_observer_v2, cq, 4248);
ABI_OFFSET(struct c42_observer_v2, candidates, 8456);
ABI_OFFSET(struct c42_observer_v2, commands, 8712);
ABI_OFFSET(struct c42_observer_v2, publications, 14344);
ABI_OFFSET(struct c42_observer_v2, reconciles, 16904);
ABI_OFFSET(struct c42_observer_v2, notifications, 18952);
ABI_OFFSET(struct c42_observer_v2, targets, 22536);
ABI_OFFSET(struct c42_observer_v2, controls, 26632);
ABI_OFFSET(struct c42_observer_v2, reserved, 26856);

int main(void)
{
    puts("C4.2 public ABI: PASS values=saturated layouts=all-observer-offsets");
    return 0;
}
