# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CLANG ?= clang
SAN_CC ?= $(CC)
TSAN_CC ?= $(CLANG)
BUILD_DIR ?= build
FWLAB_FAKE_OUTPUT ?= $(BUILD_DIR)/c34_fake_link

CPPFLAGS += -I../../include -I. -I../../media/c34-file \
	-I../../media/c34-file/tests
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CFLAGS ?= $(BASE_CFLAGS)

CODEC_SOURCES := c34_codec.c c34_recovery.c
COORDINATOR_SOURCES := c34_mapping.c c34_journal.c c34_checkpoint.c \
	c34_nfc_graph.c c34_coordinator.c c34_drive.c c34_provider.c
FIXTURE_SOURCES := fakes/c34_memory_media.c fakes/c34_buffer.c
FROZEN_C31_SOURCES := ../c31.c ../c31_codec.c
FROZEN_C32_SOURCE := ../c32/c32_policy.c
FROZEN_NFC_SOURCES := ../../nfc/nfc_model.c ../../nfc/nfc_scheduler.c \
	../../nfc/nfc_fault.c ../../nfc/nfc_media.c ../../nfc/nfc_codec.c
TEST_SUPPORT := tests/c34_test_support.c
CODEC_BIN := $(BUILD_DIR)/c34_codec_recovery_tests
FLOW_BIN := $(BUILD_DIR)/c34_flow_tests
REPLACE_BIN := $(BUILD_DIR)/c34_replaceability_tests
CRASH_BIN := $(BUILD_DIR)/c34_crash_conformance_tests
POSIX_INTEGRATION_BIN := $(BUILD_DIR)/c34_posix_integration_tests
MODEL_BIN := $(BUILD_DIR)/c34_model
BROKEN_BIN := $(BUILD_DIR)/c34_broken
FILE_SOURCES := ../../media/c34-file/c34_file_codec.c \
	../../media/c34-file/c34_file_recovery.c \
	../../media/c34-file/c34_file_engine.c \
	../../media/c34-file/c34_file_media.c
FILE_TEST_SUPPORT := ../../media/c34-file/tests/c34_file_test_support.c

.PHONY: all check-unit check-exhaustive check-negative check-determinism \
	check-replaceability check-architecture check check-clang check-sanitize \
	check-thread check-cross check-all fake-link clean

all: $(CODEC_BIN) $(FLOW_BIN) $(REPLACE_BIN) $(CRASH_BIN) $(MODEL_BIN) \
	$(BROKEN_BIN) $(POSIX_INTEGRATION_BIN)

$(BUILD_DIR):
	mkdir -p $@

$(CODEC_BIN): $(CODEC_SOURCES) $(FIXTURE_SOURCES) \
		tests/test_codec_recovery.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CODEC_SOURCES) $(FIXTURE_SOURCES) tests/test_codec_recovery.c

$(FLOW_BIN): $(CODEC_SOURCES) $(COORDINATOR_SOURCES) $(FIXTURE_SOURCES) \
		$(FROZEN_C31_SOURCES) $(FROZEN_C32_SOURCE) $(FROZEN_NFC_SOURCES) \
		$(TEST_SUPPORT) tests/test_flows.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CODEC_SOURCES) $(COORDINATOR_SOURCES) $(FIXTURE_SOURCES) \
		$(FROZEN_C31_SOURCES) $(FROZEN_C32_SOURCE) $(FROZEN_NFC_SOURCES) \
		$(TEST_SUPPORT) tests/test_flows.c

$(REPLACE_BIN): $(CODEC_SOURCES) $(COORDINATOR_SOURCES) $(FIXTURE_SOURCES) \
		$(FROZEN_C31_SOURCES) $(FROZEN_C32_SOURCE) $(FROZEN_NFC_SOURCES) \
		$(TEST_SUPPORT) $(FILE_SOURCES) $(FILE_TEST_SUPPORT) \
		tests/test_replaceability.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CODEC_SOURCES) $(COORDINATOR_SOURCES) $(FIXTURE_SOURCES) \
		$(FROZEN_C31_SOURCES) $(FROZEN_C32_SOURCE) $(FROZEN_NFC_SOURCES) \
		$(TEST_SUPPORT) $(FILE_SOURCES) $(FILE_TEST_SUPPORT) \
		tests/test_replaceability.c

$(CRASH_BIN): $(CODEC_SOURCES) $(COORDINATOR_SOURCES) $(FIXTURE_SOURCES) \
		$(FROZEN_C31_SOURCES) $(FROZEN_C32_SOURCE) $(FROZEN_NFC_SOURCES) \
		$(TEST_SUPPORT) $(FILE_SOURCES) $(FILE_TEST_SUPPORT) \
		tests/test_crash_conformance.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CODEC_SOURCES) $(COORDINATOR_SOURCES) $(FIXTURE_SOURCES) \
		$(FROZEN_C31_SOURCES) $(FROZEN_C32_SOURCE) $(FROZEN_NFC_SOURCES) \
		$(TEST_SUPPORT) $(FILE_SOURCES) $(FILE_TEST_SUPPORT) \
		tests/test_crash_conformance.c

$(POSIX_INTEGRATION_BIN): $(CODEC_SOURCES) $(COORDINATOR_SOURCES) \
		$(FIXTURE_SOURCES) $(FROZEN_C31_SOURCES) $(FROZEN_C32_SOURCE) \
		$(FROZEN_NFC_SOURCES) $(TEST_SUPPORT) $(FILE_SOURCES) \
		../../media/c34-file/c34_file_posix.c \
		tests/test_posix_integration.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CODEC_SOURCES) $(COORDINATOR_SOURCES) $(FIXTURE_SOURCES) \
		$(FROZEN_C31_SOURCES) $(FROZEN_C32_SOURCE) $(FROZEN_NFC_SOURCES) \
		$(TEST_SUPPORT) $(FILE_SOURCES) \
		../../media/c34-file/c34_file_posix.c \
		tests/test_posix_integration.c

$(MODEL_BIN): tests/c34_oracle.c tests/model_c34.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/c34_oracle.c tests/model_c34.c

$(BROKEN_BIN): tests/c34_oracle.c tests/broken_c34.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/c34_oracle.c tests/broken_c34.c

check-unit: $(CODEC_BIN) $(FLOW_BIN) $(REPLACE_BIN) $(CRASH_BIN) \
	$(POSIX_INTEGRATION_BIN)
	$(CODEC_BIN)
	$(FLOW_BIN)
	$(REPLACE_BIN)
	$(CRASH_BIN)
	$(POSIX_INTEGRATION_BIN)

check-exhaustive: $(MODEL_BIN)
	$(MODEL_BIN)

check-negative: $(BROKEN_BIN)
	$(BROKEN_BIN)

check-replaceability: $(REPLACE_BIN)
	$(REPLACE_BIN)

check-determinism: $(MODEL_BIN) $(BROKEN_BIN)
	@set -eu; left=$$(mktemp); right=$$(mktemp); \
	trap 'rm -f "$$left" "$$right"' EXIT; \
	$(MODEL_BIN) >"$$left"; $(MODEL_BIN) >"$$right"; \
	diff -u "$$left" "$$right"; \
	$(BROKEN_BIN) >"$$left"; $(BROKEN_BIN) >"$$right"; \
	diff -u "$$left" "$$right"; \
	echo "C3.4 deterministic stdout: PASS"

check-architecture:
	python3 ../../scripts/check_c34_architecture.py --cc $(CC)

check: check-unit check-exhaustive check-negative check-determinism \
	check-architecture

check-clang:
	$(MAKE) CC=$(CLANG) BUILD_DIR=build/clang check

check-sanitize:
	$(MAKE) CC=$(SAN_CC) BUILD_DIR=build/sanitize \
		CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -fno-omit-frame-pointer -fsanitize=address,undefined' \
		LDFLAGS='-fsanitize=address,undefined' check

check-thread:
	$(MAKE) CC=$(TSAN_CC) BUILD_DIR=build/thread \
		CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -fno-omit-frame-pointer -fsanitize=thread' \
		LDFLAGS='-fsanitize=thread' check

check-cross:
	python3 ../../scripts/check_c34_cross.py

check-all: check check-clang check-sanitize check-thread check-cross

$(FWLAB_FAKE_OUTPUT): $(CODEC_SOURCES) $(FIXTURE_SOURCES) \
		fakes/c34_fake_main.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CODEC_SOURCES) $(FIXTURE_SOURCES) fakes/c34_fake_main.c

fake-link: check $(FWLAB_FAKE_OUTPUT)

clean:
	rm -f build/c34_codec_recovery_tests build/c34_fake_link \
		build/c34_flow_tests build/c34_replaceability_tests \
		build/c34_crash_conformance_tests \
		build/c34_posix_integration_tests \
		build/c34_model build/c34_broken \
		build/clang/c34_codec_recovery_tests build/clang/c34_flow_tests \
		build/clang/c34_replaceability_tests build/clang/c34_model \
		build/clang/c34_broken build/clang/c34_crash_conformance_tests \
		build/clang/c34_posix_integration_tests \
		build/sanitize/c34_codec_recovery_tests \
		build/sanitize/c34_flow_tests build/sanitize/c34_replaceability_tests \
		build/sanitize/c34_model build/sanitize/c34_broken \
		build/sanitize/c34_crash_conformance_tests \
		build/sanitize/c34_posix_integration_tests \
		build/thread/c34_codec_recovery_tests build/thread/c34_flow_tests \
		build/thread/c34_replaceability_tests build/thread/c34_model \
		build/thread/c34_broken build/thread/c34_crash_conformance_tests \
		build/thread/c34_posix_integration_tests
	@rmdir build/clang build/sanitize build/thread build 2>/dev/null || true
