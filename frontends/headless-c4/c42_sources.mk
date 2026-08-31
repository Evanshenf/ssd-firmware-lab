# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

# Frozen C4.2 build-input closure. Later C4 gates use separate source lists.
override C42_SOURCES := hif/c42_identity.c hif/c42_queue.c \
	hif/c42_publication.c hif/c42_runtime.c
override C42_FAKE_SOURCES := fakes/c42_event.c fakes/c42_memory.c fakes/c42_command.c
override C42_SUPPORT := tests/c42_support.c
override C42_REFERENCE := tests/c42_reference.c tests/c42_dut_bfs.c
override C42_HEADERS := hif/c42.h hif/c42_internal.h hif/c42_memory_port.h \
	fakes/c42_event.h fakes/c42_memory.h fakes/c42_command.h \
	tests/c42_support.h tests/c42_reference.h tests/c42_dut_bfs.h \
	tests/c42_model.h \
	../../include/fwlab/contracts/hif_command_port.h
override C42_TEST_SOURCES := tests/test_c42_queue.c \
	tests/test_c42_publication.c tests/test_c42_identity.c \
	tests/test_c42_reset_delete.c tests/test_c42_remediation.c \
	tests/test_c42_provider_matrix.c tests/test_c42_phase_cuts.c \
	tests/test_c42_dut_replay.c \
	tests/test_c42_public_abi.c \
	tests/test_c42_thread.c tests/fuzz_c42.c \
	tests/c42_model.c tests/model_c42.c tests/broken_c42.c \
	fakes/c42_fake_main.c ../../scripts/check_c4_architecture.py \
	../../scripts/check_c35_architecture.py \
	../../scripts/check_c42_analysis.py \
	../../scripts/check_c42_determinism.py \
	../../scripts/check_c42_cross.py \
	../../scripts/check_c42_make_integrity.py \
	../../scripts/check_c42_provider_mutations.py \
	../../scripts/check_c42_runner_integrity.py \
	../../scripts/run_c42_gate.py
override C42_EVIDENCE_INPUTS := evidence/c42a-p1/profile.toml \
	evidence/c42a-p1/interface-inventory.toml \
	evidence/c42a-p1/claims.toml \
	evidence/c42a-p1/provider-model.toml \
	evidence/c42a-p1/identity-model.toml \
	evidence/c42a-p1/phase-model.toml \
	evidence/c42a-p1/build-trust.toml \
	evidence/c42a-p1/fault-operators.toml \
	evidence/c42a-p1/mutation-ownership.toml \
	evidence/c42a-p1/lanes.toml \
	evidence/c42a-p1/obligations.lock.toml \
	evidence/c42a-p1/authority.lock.toml \
	evidence/c42a-p1/c35-reference.toml \
	../../scripts/c42_authority.py \
	../../scripts/check_c42_authority.py \
	../../scripts/extract_c42_interface_inventory.py \
	../../scripts/check_c42_claim_models.py \
	../../scripts/gen_c42_obligations.py
override C42_ALL_INPUTS := $(C42_SOURCES) $(C42_FAKE_SOURCES) $(C42_SUPPORT) \
	$(C42_REFERENCE) $(C42_TEST_SOURCES) $(C42_HEADERS) \
	$(C42_EVIDENCE_INPUTS)
override C42_CHECK_TARGETS := check-c42-build-closure \
	check-c42-claim-models check-c42-unit \
	check-c42-model check-c42-negative check-c42-fuzz \
	check-c42-local-determinism check-c42-architecture \
	check-c42-dynamic-mutations check-c42-replay \
	check-c42-provider-mutations check-c42-make-integrity \
	check-c42-authority \
	check-c42-runner-integrity \
	check-c42-artifact-receipt
override C42_BUILD_TEST_TARGETS := check-c42-build-closure \
	check-c42-unit check-c42-model check-c42-negative check-c42-fuzz \
	check-c42-local-determinism check-c42-replay \
	check-c42-artifact-receipt
