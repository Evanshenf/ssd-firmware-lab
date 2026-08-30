# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

# Frozen C4.2 build-input closure. Later C4 gates use separate source lists.
C42_SOURCES := hif/c42_identity.c hif/c42_queue.c \
	hif/c42_publication.c hif/c42_runtime.c
C42_FAKE_SOURCES := fakes/c42_event.c fakes/c42_memory.c fakes/c42_command.c
C42_SUPPORT := tests/c42_support.c
C42_REFERENCE := tests/c42_reference.c
C42_TEST_SOURCES := tests/test_c42_queue.c \
	tests/test_c42_publication.c tests/test_c42_identity.c \
	tests/test_c42_reset_delete.c tests/test_c42_remediation.c \
	tests/test_c42_dut_replay.c tests/test_c42_thread.c tests/fuzz_c42.c \
	tests/c42_model.c tests/model_c42.c tests/broken_c42.c \
	fakes/c42_fake_main.c
