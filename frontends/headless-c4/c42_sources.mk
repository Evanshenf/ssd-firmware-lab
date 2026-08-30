# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

# Frozen C4.2 build-input closure. Later C4 gates use separate source lists.
C42_SOURCES := hif/c42_identity.c hif/c42_queue.c \
	hif/c42_publication.c hif/c42_runtime.c
C42_FAKE_SOURCES := fakes/c42_event.c fakes/c42_memory.c fakes/c42_command.c
C42_SUPPORT := tests/c42_support.c
C42_REFERENCE := tests/c42_reference.c tests/c42_dut_bfs.c
C42_HEADERS := hif/c42.h hif/c42_internal.h hif/c42_memory_port.h \
	fakes/c42_event.h fakes/c42_memory.h fakes/c42_command.h \
	tests/c42_support.h tests/c42_reference.h tests/c42_dut_bfs.h \
	tests/c42_model.h ../../include/fwlab/contracts/hif_command_port.h
C42_TEST_SOURCES := tests/test_c42_queue.c \
	tests/test_c42_publication.c tests/test_c42_identity.c \
	tests/test_c42_reset_delete.c tests/test_c42_remediation.c \
	tests/test_c42_dut_replay.c tests/test_c42_public_abi.c \
	tests/test_c42_thread.c tests/fuzz_c42.c \
	tests/c42_model.c tests/model_c42.c tests/broken_c42.c \
	fakes/c42_fake_main.c
C42_ALL_INPUTS := $(C42_SOURCES) $(C42_FAKE_SOURCES) $(C42_SUPPORT) \
	$(C42_REFERENCE) $(C42_TEST_SOURCES) $(C42_HEADERS)
C42_CHECK_TARGETS := check-c42-build-closure check-c42-unit \
	check-c42-model check-c42-negative check-c42-fuzz \
	check-c42-local-determinism check-c42-architecture \
	check-c42-dynamic-mutations check-c42-replay
