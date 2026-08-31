#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

"""Kill C4.2 mutants by compiling fresh production-source copies."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
HIF = ROOT / "frontends/headless-c4"


def mutations() -> list[dict[str, object]]:
    return [
        {"name": "BM_HEAD_ADVANCES_DURING_ADMIT_QUERY",
         "target": "c42_remediation_unit",
         "edits": [
             ("frontends/headless-c4/hif/c42_queue.c",
              "    if (result == FWLAB_HIF_PORT_IN_PROGRESS) {\n"
              "        command->state = C42_COMMAND_ADMIT_QUERY;\n"
              "        return 1;\n"
              "    }",
              "    if (result == FWLAB_HIF_PORT_IN_PROGRESS) {\n"
              "        if (command->admit_started == 1) {\n"
              "            struct c42_sq_record *early_sq =\n"
              "                &controller->sq[command->sq_index];\n\n"
              "            early_sq->device_head = (uint16_t)(\n"
              "                (early_sq->device_head + 1u) % early_sq->depth\n"
              "            );\n"
              "            command->admit_started = 2;\n"
              "        }\n"
              "        command->state = C42_COMMAND_ADMIT_QUERY;\n"
              "        return 1;\n"
              "    }"),
             ("frontends/headless-c4/hif/c42_queue.c",
              "    command->state = C42_COMMAND_HIF_COMMITTED;\n"
              "    sq->device_head = (uint16_t)((sq->device_head + 1u) % sq->depth);\n"
              "    sq->pending--;",
              "    command->state = C42_COMMAND_HIF_COMMITTED;\n"
              "    if (command->admit_started != 2) {\n"
              "        sq->device_head = (uint16_t)(\n"
              "            (sq->device_head + 1u) % sq->depth\n"
              "        );\n"
              "    }\n"
              "    sq->pending--;"),
         ]},
        {"name": "BM_REREAD_SQE_ON_BACKPRESSURE",
         "target": "c42_identity_unit",
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "        command->state = C42_COMMAND_CAPTURED;\n"
                    "        command->prepare_started = 0;\n"
                    "        return 1;",
                    "        clear_command_all(controller, index);\n"
                    "        return 1;")]},
        {"name": "BM_DUPLICATE_CID_ALLOWED", "target": "c42_queue_unit",
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "    if (duplicate_cid(\n"
                    "            controller, sq_index, sq->ring_generation, local.raw.command_id)) {",
                    "    if (duplicate_cid(\n"
                    "            controller, sq_index, sq->ring_generation, local.raw.command_id) &&\n"
                    "        controller->fault_cause == UINT32_MAX) {"),
                   ("frontends/headless-c4/hif/c42_queue.c",
                    "    if (c42_find_active(\n"
                    "            controller, command->sq_index, command->sq_ring_generation,\n"
                    "            command->command_id) != NULL) {",
                    "    if (c42_find_active(\n"
                    "            controller, command->sq_index, command->sq_ring_generation,\n"
                    "            command->command_id) != NULL &&\n"
                    "        controller->fault_cause == UINT32_MAX) {")]},
        {"name": "BM_MATCH_CID_WITHOUT_RING_GENERATION",
         "target": "c42_identity_unit",
         "edits": [("frontends/headless-c4/hif/c42_identity.c",
                    "        struct c42_command_record *command = &controller->commands[index];\n\n"
                    "        if (c42_command_record_active(command) &&\n"
                    "            command->sq_index == sq_index &&\n"
                    "            command->sq_ring_generation == sq_generation &&\n"
                    "            command->command_id == command_id) {",
                    "        struct c42_command_record *command = &controller->commands[index];\n\n"
                    "        if (c42_command_record_active(command) &&\n"
                    "            command->sq_index == sq_index &&\n"
                    "            sq_generation != 0 &&\n"
                    "            command->command_id == command_id) {")]},
        {"name": "BM_INVALID_TAIL_REMAINS_LIVE", "target": "c42_queue_unit",
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "    if (event->new_tail >= sq->depth) {\n"
                    "        c42_fault_sq(controller, index, C42_FAULT_INVALID_DOORBELL);\n"
                    "        return C42_FAULTED;\n"
                    "    }",
                    "    if (event->new_tail >= sq->depth) {\n"
                    "        return C42_INVALID;\n"
                    "    }")]},
        {"name": "BM_SQHD_AT_CAPTURE", "target": "c42_remediation_unit",
         "edits": [
             ("frontends/headless-c4/hif/c42_publication.c",
              "    command->intent = intent;\n"
              "    command->lease = lease;\n"
              "    command->state = C42_COMMAND_LEASED;",
              "    command->intent = intent;\n"
              "    command->lease = lease;\n"
              "    command->sqhd_snapshot =\n"
              "        controller->sq[command->sq_index].device_head;\n"
              "    command->state = C42_COMMAND_LEASED;"),
             ("frontends/headless-c4/hif/c42_publication.c",
              "    command->sqhd_snapshot = sq->device_head;",
              "    (void)command->sqhd_snapshot;"),
         ]},
        {"name": "BM_CQ_OVERWRITE_FULL", "target": "c42_publication_unit",
         "edits": [("frontends/headless-c4/hif/c42_publication.c",
                    "    if (cq->life != C42_QUEUE_LIVE ||\n"
                    "        cq->unacked_count + cq->reserved_count >= cq->depth - 1u) {",
                    "    if (cq->life != C42_QUEUE_LIVE ||\n"
                    "        (0 != 0 &&\n"
                    "         cq->unacked_count + cq->reserved_count >= cq->depth - 1u)) {"),
                   ("frontends/headless-c4/hif/c42_publication.c",
                    "    if (cq->unacked_count + cq->reserved_count >= cq->depth - 1u ||\n"
                    "        cq->slots[cq->device_tail].state != C42_SLOT_FREE) {",
                    "    if (0 != 0 &&\n"
                    "        (cq->unacked_count + cq->reserved_count >= cq->depth - 1u ||\n"
                    "         cq->slots[cq->device_tail].state != C42_SLOT_FREE)) {")]},
        {"name": "BM_PHASE_TOGGLE_ON_ACK", "target": "c42_publication_unit",
         "edits": [("frontends/headless-c4/hif/c42_publication.c",
                    "    cq->unacked_count = (uint16_t)(cq->unacked_count - delta);\n"
                    "    c42_try_finish_tombstones(controller);",
                    "    cq->unacked_count = (uint16_t)(cq->unacked_count - delta);\n"
                    "    cq->device_phase ^= 1u;\n"
                    "    c42_try_finish_tombstones(controller);")]},
        {"name": "BM_MARKER_VISIBLE_BEFORE_BODY", "target": "c42_publication_unit",
         "edits": [("frontends/headless-c4/hif/c42_publication.c",
                    "                return progress_body(controller, index);",
                    "                if (controller->fault_cause == UINT32_MAX) {\n"
                    "                    return progress_body(controller, index);\n"
                    "                }\n"
                    "                return progress_marker(controller, index);")]},
        {"name": "BM_CONSUME_COMMIT_BEFORE_MARKER", "target": "c42_publication_unit",
         "edits": [("frontends/headless-c4/hif/c42_publication.c",
                    "    if (publication->marker_visible == 0) {\n"
                    "        return progress_marker(controller, command_index);\n"
                    "    }",
                    "    if (0 != 0 && publication->marker_visible == 0) {\n"
                    "        return progress_marker(controller, command_index);\n"
                    "    }")]},
        {"name": "BM_CID_RELEASE_BEFORE_CROSS_COMMIT", "target": "c42_publication_unit",
         "edits": [
             ("frontends/headless-c4/hif/c42_identity.c",
              "    return (command->state >= C42_COMMAND_HIF_COMMITTED &&\n"
              "            command->state <= C42_COMMAND_RELEASE_RECONCILE) ||\n"
              "           command->state == C42_COMMAND_CONSUME_POISON_HOLD;",
              "    return (command->state >= C42_COMMAND_HIF_COMMITTED &&\n"
              "            command->state <= C42_COMMAND_RELEASE_RECONCILE &&\n"
              "            command->state != C42_COMMAND_MARKER_RECONCILE) ||\n"
              "           command->state == C42_COMMAND_CONSUME_POISON_HOLD;"),
             ("frontends/headless-c4/hif/c42_queue.c",
              "        if (command->state != C42_COMMAND_FREE &&\n"
              "            command->sq_index == sq_index &&",
              "        if (command->state != C42_COMMAND_FREE &&\n"
              "            command->state != C42_COMMAND_MARKER_RECONCILE &&\n"
              "            command->sq_index == sq_index &&"),
         ]},
        {"name": "BM_CID_HELD_UNTIL_HOST_ACK", "target": "c42_identity_unit",
         "edits": [("frontends/headless-c4/hif/c42_publication.c",
                    "    c42_release_command_record(controller, command_index);",
                    "    (void)command_index;")]},
        {"name": "BM_ACK_NONCOMMITTED_SLOT", "target": "c42_remediation_unit",
         "edits": [("frontends/headless-c4/hif/c42_publication.c",
                    "    if (delta > cq->unacked_count) {",
                    "    if (delta > cq->unacked_count + cq->reserved_count) {"),
                   ("frontends/headless-c4/hif/c42_publication.c",
                    "        if (cq->slots[slot].state != C42_SLOT_CQE_COMMITTED) {",
                    "        if (cq->slots[slot].state == C42_SLOT_FREE) {")]},
        {"name": "BM_BLIND_REWRITE_UNKNOWN_MARKER", "target": "c42_publication_unit",
         "edits": [("frontends/headless-c4/hif/c42_publication.c",
                    "    if (publication->marker_started == 0) {",
                    "    if (publication->marker_started <= 1) {")]},
        {"name": "BM_NOTIFY_BEFORE_CROSS_COMMIT", "target": "c42_publication_unit",
         "edits": [("frontends/headless-c4/hif/c42_publication.c",
                    "    } else if (effect == C42_MEMORY_UNKNOWN) {\n"
                    "        publication->marker_visible = 0;",
                    "    } else if (effect == C42_MEMORY_UNKNOWN) {\n"
                    "        controller->notifications[command_index].state = C42_NOTIFY_READY;\n"
                    "        publication->marker_visible = 0;")]},
        {"name": "BM_DELETE_CQ_WITH_UNACKED", "target": "c42_remediation_unit",
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "    return controller->cq[queue_index].life == C42_QUEUE_LIVE &&\n"
                    "           cq_delete_dependencies_clear(controller, queue_index);",
                    "    return controller->cq[queue_index].life == C42_QUEUE_LIVE;")]},
        {"name": "BM_CREATE_LIVE_FROM_SCRUB_UNKNOWN",
         "target": "c42_queue_unit",
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "    if (candidate->state != C42_CANDIDATE_READY ||\n"
                    "        !c42_queue_index(candidate->descriptor.queue_id, &index)) {",
                    "    if ((candidate->state != C42_CANDIDATE_READY &&\n"
                    "         candidate->state != C42_CANDIDATE_SCRUB_UNKNOWN) ||\n"
                    "        !c42_queue_index(candidate->descriptor.queue_id, &index)) {")]},
        {"name": "BM_RECREATE_BEFORE_TOMBSTONE_CLEAR", "target": "c42_reset_delete_unit",
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "         controller->sq[queue_index].life != C42_QUEUE_ABSENT)",
                    "         controller->sq[queue_index].life != C42_QUEUE_ABSENT &&\n"
                    "         controller->sq[queue_index].life != C42_QUEUE_TOMBSTONED)")]},
        {"name": "BM_RESET_REOPEN_BEFORE_REVOKE", "target": "c42_remediation_unit",
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "    controller->phase = C42_CONTROLLER_RESETTING;",
                    "    controller->phase = C42_CONTROLLER_COLD_NO_QUEUES;")]},
        {"name": "BM_DELETE_DROPS_DOORBELLED_SQE", "target": "c42_reset_delete_unit",
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "        if (controller->sq[index].device_head !=\n"
                    "                controller->sq[index].frozen_tail ||\n"
                    "            controller->sq[index].pending != 0) {",
                    "        if (0 != 0 &&\n"
                    "            (controller->sq[index].device_head !=\n"
                    "                 controller->sq[index].frozen_tail ||\n"
                    "             controller->sq[index].pending != 0)) {")]},
        {"name": "BM_RETIRE_UNKNOWN_NOT_SUPERSEDED",
         "target": "c42_phase_cuts",
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "        if (controller->candidates[index].in_use != 0) {\n"
                    "            controller->candidates[index].state = C42_CANDIDATE_SUPERSEDED;\n"
                    "        }",
                    "        if (controller->candidates[index].in_use != 0 &&\n"
                    "            controller->candidates[index].state !=\n"
                    "                C42_CANDIDATE_RETIRE_UNKNOWN) {\n"
                    "            controller->candidates[index].state =\n"
                    "                C42_CANDIDATE_SUPERSEDED;\n"
                    "        }")]},
        {"name": "BM_POISONED_NOT_SUPERSEDED",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "        if (controller->candidates[index].in_use != 0) {\n"
                    "            controller->candidates[index].state = C42_CANDIDATE_SUPERSEDED;\n"
                    "        }",
                    "        if (controller->candidates[index].in_use != 0 &&\n"
                    "            controller->candidates[index].state !=\n"
                    "                C42_CANDIDATE_POISONED) {\n"
                    "            controller->candidates[index].state =\n"
                    "                C42_CANDIDATE_SUPERSEDED;\n"
                    "        }")]},
        {"name": "BM_READY_NOTIFICATION_NOT_SUPPRESSED",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "            (notification->state == C42_NOTIFY_RESERVED ||\n"
                    "             notification->state == C42_NOTIFY_READY ||\n"
                    "             notification->state == C42_NOTIFY_ACQUIRED)) {",
                    "            (notification->state == C42_NOTIFY_RESERVED ||\n"
                    "             notification->state == C42_NOTIFY_ACQUIRED)) {")]},
        {"name": "BM_TAKEOVER_ACCEPTS_RESET_BEGIN_IN_PROGRESS",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [
             ("frontends/headless-c4/hif/c42_queue.c",
              "            if (port_result == FWLAB_HIF_PORT_OK) {\n"
              "                reset->port_started = 1;\n"
              "            } else if (port_result != FWLAB_HIF_PORT_IN_PROGRESS) {\n"
              "                record->state = C42_CONTROL_POISONED;\n"
              "            }",
              "            if (port_result == FWLAB_HIF_PORT_OK ||\n"
              "                port_result == FWLAB_HIF_PORT_IN_PROGRESS) {\n"
              "                reset->port_started = 1;\n"
              "            } else {\n"
              "                record->state = C42_CONTROL_POISONED;\n"
              "            }"),
             ("frontends/headless-c4/hif/c42_queue.c",
              "            if (memory_result == C42_MEMORY_OK) {\n"
              "                reset->memory_started = 1;\n"
              "            } else if (memory_result != C42_MEMORY_IN_PROGRESS) {\n"
              "                record->state = C42_CONTROL_POISONED;\n"
              "            }",
              "            if (memory_result == C42_MEMORY_OK ||\n"
              "                memory_result == C42_MEMORY_IN_PROGRESS) {\n"
              "                reset->memory_started = 1;\n"
              "            } else {\n"
              "                record->state = C42_CONTROL_POISONED;\n"
              "            }")
         ]},
        {"name": "BM_SQ_READY_NOT_SUPERSEDED",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "        if (controller->candidates[index].in_use != 0) {\n"
                    "            controller->candidates[index].state = C42_CANDIDATE_SUPERSEDED;\n"
                    "        }",
                    "        if (controller->candidates[index].in_use != 0 &&\n"
                    "            !(controller->candidates[index].state ==\n"
                    "                  C42_CANDIDATE_READY &&\n"
                    "              controller->candidates[index].descriptor.kind ==\n"
                    "                  C42_QUEUE_SQ)) {\n"
                    "            controller->candidates[index].state =\n"
                    "                C42_CANDIDATE_SUPERSEDED;\n"
                    "        }")]},
        {"name": "BM_POST_LP_TARGET_PREPARE",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [("frontends/headless-c4/hif/c42_identity.c",
                    "        controller->phase != C42_CONTROLLER_READY) {",
                    "        (controller->phase != C42_CONTROLLER_READY &&\n"
                    "         controller->phase != C42_CONTROLLER_RESETTING &&\n"
                    "         controller->phase != C42_CONTROLLER_TEARING_DOWN)) {")]},
        {"name": "BM_POST_LP_CANDIDATE_PREPARE",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "        (controller->phase != C42_CONTROLLER_COLD_NO_QUEUES &&\n"
                    "         controller->phase != C42_CONTROLLER_READY) ||\n"
                    "        !descriptor_valid(controller, descriptor, &queue_index)) {",
                    "        (controller->phase != C42_CONTROLLER_COLD_NO_QUEUES &&\n"
                    "         controller->phase != C42_CONTROLLER_READY &&\n"
                    "         controller->phase != C42_CONTROLLER_RESETTING &&\n"
                    "         controller->phase != C42_CONTROLLER_TEARING_DOWN) ||\n"
                    "        !descriptor_valid(controller, descriptor, &queue_index)) {")]},
        {"name": "BM_POST_LP_DELETE_START",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "        controller->phase != C42_CONTROLLER_READY ||\n"
                    "        (kind != C42_QUEUE_SQ && kind != C42_QUEUE_CQ) ||",
                    "        (controller->phase != C42_CONTROLLER_READY &&\n"
                    "         controller->phase != C42_CONTROLLER_RESETTING &&\n"
                    "         controller->phase != C42_CONTROLLER_TEARING_DOWN) ||\n"
                    "        (kind != C42_QUEUE_SQ && kind != C42_QUEUE_CQ) ||")]},
        {"name": "BM_POST_LP_SQ_TAIL",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [("frontends/headless-c4/hif/c42_queue.c",
                    "    if (controller->phase != C42_CONTROLLER_READY) {\n"
                    "        return controller->phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED ?",
                    "    if (controller->phase != C42_CONTROLLER_READY &&\n"
                    "        controller->phase != C42_CONTROLLER_RESETTING &&\n"
                    "        controller->phase != C42_CONTROLLER_TEARING_DOWN) {\n"
                    "        return controller->phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED ?")]},
        {"name": "BM_POST_LP_CQ_HEAD",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [("frontends/headless-c4/hif/c42_publication.c",
                    "    if (controller->phase != C42_CONTROLLER_READY) {\n"
                    "        return controller->phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED ?",
                    "    if (controller->phase != C42_CONTROLLER_READY &&\n"
                    "        controller->phase != C42_CONTROLLER_RESETTING &&\n"
                    "        controller->phase != C42_CONTROLLER_TEARING_DOWN) {\n"
                    "        return controller->phase == C42_CONTROLLER_FAULTED_RESET_REQUIRED ?")]},
        {"name": "BM_OBSERVER_TOKEN_NONCE_MISMATCH",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [("frontends/headless-c4/hif/c42_runtime.c",
                    "static void observer_candidate_fill(\n"
                    "    const struct c42_candidate_record *source,\n"
                    "    struct c42_observer_candidate_v2 *target)\n"
                    "{\n"
                    "    target->token = source->token;\n"
                    "    target->controller_epoch = source->controller_epoch;",
                    "static void observer_candidate_fill(\n"
                    "    const struct c42_candidate_record *source,\n"
                    "    struct c42_observer_candidate_v2 *target)\n"
                    "{\n"
                    "    target->token = source->token;\n"
                    "    if (target->token.instance_nonce != 0) {\n"
                    "        target->token.instance_nonce ^= UINT64_C(1);\n"
                    "    }\n"
                    "    target->controller_epoch = source->controller_epoch;")]},
        {"name": "BM_OBSERVER_PUBLICATION_TOKEN_UID_MISMATCH",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [("frontends/headless-c4/hif/c42_runtime.c",
                    "    target->publication_uid = source->publication_uid;\n"
                    "    target->body_token_uid = source->body_token.uid;\n"
                    "    target->marker_token_uid = source->marker_token.uid;",
                    "    target->publication_uid = source->publication_uid;\n"
                    "    target->body_token_uid = source->body_token.uid;\n"
                    "    target->marker_token_uid = source->marker_token.uid;\n"
                    "    if (target->body_token_uid != 0 &&\n"
                    "        target->marker_token_uid != 0) {\n"
                    "        target->body_token_uid++;\n"
                    "        target->marker_token_uid++;\n"
                    "    }")]},
        {"name": "BM_OBSERVER_TARGET_COMMAND_UID_MISMATCH",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [("frontends/headless-c4/hif/c42_runtime.c",
                    "    target->token = source->value.token;\n"
                    "    target->handle = source->value.handle;\n"
                    "    target->sq_ring_generation = source->sq_ring_generation;",
                    "    target->token = source->value.token;\n"
                    "    target->handle = source->value.handle;\n"
                    "    if (target->handle.command_uid != 0) {\n"
                    "        target->handle.command_uid++;\n"
                    "    }\n"
                    "    target->sq_ring_generation = source->sq_ring_generation;")]},
        {"name": "BM_STEP_SKIPS_EPOCH_CONTROL",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [("frontends/headless-c4/hif/c42_runtime.c",
                    "        if (c42_progress_queue_controls(controller) != 0) {\n"
                    "            progressed = 1;\n"
                    "        } else if (controller->phase == C42_CONTROLLER_RESETTING ||\n"
                    "                   controller->phase == C42_CONTROLLER_TEARING_DOWN) {",
                    "        if (controller->phase != C42_CONTROLLER_RESETTING &&\n"
                    "            controller->phase != C42_CONTROLLER_TEARING_DOWN &&\n"
                    "            c42_progress_queue_controls(controller) != 0) {\n"
                    "            progressed = 1;\n"
                    "        } else if (controller->phase == C42_CONTROLLER_RESETTING ||\n"
                    "                   controller->phase == C42_CONTROLLER_TEARING_DOWN) {")]},
        {"name": "BM_NOTIFICATION_FAILURE_CLOBBERS_OUTPUT",
         "target": "c42_phase_cuts", "replay": False,
         "edits": [("frontends/headless-c4/hif/c42_publication.c",
                    "    if (controller->phase != C42_CONTROLLER_READY) {\n"
                    "        return C42_SUPERSEDED;\n"
                    "    }",
                    "    if (controller->phase != C42_CONTROLLER_READY) {\n"
                    "        memset(notification, 0, sizeof(*notification));\n"
                    "        return C42_SUPERSEDED;\n"
                    "    }")]},
    ]


MUTANT_FAMILY = {
    "BM_HEAD_ADVANCES_DURING_ADMIT_QUERY": "F03-capture-backpressure",
    "BM_REREAD_SQE_ON_BACKPRESSURE": "F03-capture-backpressure",
    "BM_DUPLICATE_CID_ALLOWED": "F04-sq-invalid-cid",
    "BM_MATCH_CID_WITHOUT_RING_GENERATION": "F08-cid-reuse-target",
    "BM_INVALID_TAIL_REMAINS_LIVE": "F04-sq-invalid-cid",
    "BM_SQHD_AT_CAPTURE": "F05-delayed-out-of-order",
    "BM_CQ_OVERWRITE_FULL": "F07-cq-full-lease",
    "BM_PHASE_TOGGLE_ON_ACK": "F06-cq-phase-ack",
    "BM_MARKER_VISIBLE_BEFORE_BODY": "F09-publication-faults",
    "BM_CONSUME_COMMIT_BEFORE_MARKER": "F09-publication-faults",
    "BM_CID_RELEASE_BEFORE_CROSS_COMMIT": "F08-cid-reuse-target",
    "BM_CID_HELD_UNTIL_HOST_ACK": "F08-cid-reuse-target",
    "BM_ACK_NONCOMMITTED_SLOT": "F04-sq-invalid-cid",
    "BM_BLIND_REWRITE_UNKNOWN_MARKER": "F09-publication-faults",
    "BM_NOTIFY_BEFORE_CROSS_COMMIT": "F09-publication-faults",
    "BM_DELETE_CQ_WITH_UNACKED": "F10-delete-tombstone",
    "BM_CREATE_LIVE_FROM_SCRUB_UNKNOWN": "F01-create-contract",
    "BM_RECREATE_BEFORE_TOMBSTONE_CLEAR": "F10-delete-tombstone",
    "BM_RESET_REOPEN_BEFORE_REVOKE": "F11-reset-teardown",
    "BM_DELETE_DROPS_DOORBELLED_SQE": "F10-delete-tombstone",
    "BM_RETIRE_UNKNOWN_NOT_SUPERSEDED": "F11-reset-teardown",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def tracked_files() -> list[str]:
    output = subprocess.check_output(
        ["git", "ls-files"], cwd=ROOT, text=True
    )
    return [line for line in output.splitlines() if line]


def replace_unique(path: Path, before: str, after: str) -> None:
    text = path.read_text(encoding="utf-8")
    if text.count(before) != 1:
        raise RuntimeError(f"non-unique mutation anchor: {path}: {before!r}")
    changed = text.replace(before, after, 1)
    if changed == text:
        raise RuntimeError(f"mutation did not change bytes: {path}")
    path.write_text(changed, encoding="utf-8")


def build_binary(
    root: Path,
    compiler: str,
    target: str,
    output: Path,
    arguments: tuple[str, ...] = (),
    expected_family: str | None = None,
) -> bytes:
    make_target = output / target
    build = subprocess.run(
        ["make", "-C", str(root / "frontends/headless-c4"),
         f"CC={compiler}", f"BUILD_DIR={output}", str(make_target)],
        cwd=root, check=False, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300,
    )
    if build.returncode != 0 or not make_target.is_file():
        raise RuntimeError(f"{compiler}/{target} compile failed:\n{build.stdout}")
    run = subprocess.run(
        [str(make_target), *arguments], cwd=root, check=False,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120,
    )
    if run.returncode == 0:
        raise RuntimeError(f"mutant escaped: {compiler}/{target}")
    expected = None if expected_family is None else (
        f"C4.2 DUT reference FAIL: family={expected_family} path=".encode()
    )
    if run.returncode != 1 or b"FAIL:" not in run.stdout or (
            expected is not None and expected not in run.stdout):
        raise RuntimeError(
            f"mutant died without an oracle mismatch: {compiler}/{target}: "
            f"return={run.returncode}\n{run.stdout.decode(errors='replace')}"
        )
    if expected_family is not None:
        pattern = re.compile(
            rb"C4\.2 DUT reference FAIL: family=" +
            re.escape(expected_family.encode()) + rb" path=([^ \r\n]+)"
        )
        paths = pattern.findall(run.stdout)
        if len(paths) != 1:
            raise RuntimeError(
                f"{compiler}/{target} did not emit one named reference path:\n"
                f"{run.stdout.decode(errors='replace')}"
            )
        path = paths[0].decode(errors="strict")
        if path in ("<bootstrap>", "<root>"):
            raise RuntimeError(
                f"{compiler}/{target} used a bootstrap/root false kill: {path}"
            )
        if len(path.split(">")) > 20:
            raise RuntimeError(
                f"{compiler}/{target} counterexample exceeds depth 20: {path}"
            )
    return run.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--only")
    args = parser.parse_args()
    root = args.root.resolve()
    if root != ROOT:
        print("C4.2 dynamic mutation root must be the live source root", file=sys.stderr)
        return 1
    compilers = [name for name in ("gcc", "clang") if shutil.which(name)]
    if len(compilers) != 2:
        print("C4.2 mutations require gcc and clang", file=sys.stderr)
        return 1
    tracked = tracked_files()
    baseline_hashes = {name: sha256(ROOT / name) for name in tracked}
    total = 0
    aggregate_binaries = 0
    replay_mutants = 0
    try:
        selected = [
            mutation for mutation in mutations()
            if args.only is None or mutation["name"] == args.only
        ]
        if not selected:
            raise RuntimeError(f"unknown mutation selection: {args.only}")
        for mutation in selected:
            name = str(mutation["name"])
            replay = bool(mutation.get("replay", True))
            family = MUTANT_FAMILY.get(name)
            if replay and family is None:
                raise RuntimeError(f"missing replay family: {name}")
            allowed = {str(edit[0]) for edit in mutation["edits"]}
            with tempfile.TemporaryDirectory(
                    prefix=f"c42-mutant-{name.lower()}-") as directory:
                mutant_root = Path(directory) / "repo"
                shutil.copytree(
                    ROOT, mutant_root,
                    ignore=shutil.ignore_patterns(
                        ".git", "build", "__pycache__", "*.pyc", "*.o"
                    ),
                )
                for relative, before, after in mutation["edits"]:
                    replace_unique(mutant_root / relative, before, after)
                changed = {
                    relative for relative in tracked
                    if sha256(mutant_root / relative) != baseline_hashes[relative]
                }
                if changed != allowed:
                    raise RuntimeError(
                        f"{name} changed unexpected files: {sorted(changed ^ allowed)}"
                    )
                outputs = []
                for compiler in compilers:
                    selected_output = build_binary(
                        mutant_root, compiler, str(mutation["target"]),
                        Path(directory) / f"build-{compiler}"
                    )
                    replay_output = b""
                    if replay:
                        replay_output = build_binary(
                            mutant_root, compiler, "c42_dut_replay",
                            Path(directory) / f"build-{compiler}",
                            ("--family", str(family)), str(family)
                        )
                    outputs.append(selected_output + replay_output)
                if outputs[0] != outputs[1]:
                    raise RuntimeError(
                        f"{name} GCC/Clang mismatch differs:\n"
                        f"--- gcc ---\n{outputs[0].decode(errors='replace')}\n"
                        f"--- clang ---\n{outputs[1].decode(errors='replace')}"
                    )
                total += 1
                aggregate_binaries += 4 if replay else 2
                replay_mutants += 1 if replay else 0
                print(f"C4.2 production mutant {name}: PASS")
    except (OSError, RuntimeError, subprocess.CalledProcessError,
            subprocess.TimeoutExpired) as error:
        print(f"C4.2 dynamic mutations: FAIL: {error}", file=sys.stderr)
        return 1
    print(f"C4.2 dynamic mutations: PASS mutants={total} compilers=2 "
          f"unit-plus-replay={replay_mutants} unit-specific="
          f"{total - replay_mutants} aggregate-binaries={aggregate_binaries}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
