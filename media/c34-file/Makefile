# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CLANG ?= clang
SAN_CC ?= $(CC)
TSAN_CC ?= $(CLANG)
BUILD_DIR ?= build
FWLAB_FAKE_OUTPUT ?= $(BUILD_DIR)/c34_file_fake_link

CPPFLAGS += -I../../include -I.
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CFLAGS ?= $(BASE_CFLAGS)

ENGINE_SOURCES := c34_file_codec.c c34_file_recovery.c c34_file_engine.c \
	c34_file_media.c
POSIX_SOURCE := c34_file_posix.c
TEST_SUPPORT := tests/c34_file_test_support.c
ORACLE := tests/c34_file_oracle.c
CONTRACT_BIN := $(BUILD_DIR)/c34_file_contract_tests
CRASH_BIN := $(BUILD_DIR)/c34_file_crash_tests
POSIX_BIN := $(BUILD_DIR)/c34_file_posix_tests
MODEL_BIN := $(BUILD_DIR)/c34_file_model
BROKEN_BIN := $(BUILD_DIR)/c34_file_broken

.PHONY: all check-unit check-exhaustive check-negative check-determinism \
	check-architecture check check-clang check-sanitize check-thread \
	check-cross check-all fake-link clean

all: $(CONTRACT_BIN) $(CRASH_BIN) $(POSIX_BIN) $(MODEL_BIN) $(BROKEN_BIN)

$(BUILD_DIR):
	mkdir -p $@

$(CONTRACT_BIN): $(ENGINE_SOURCES) $(POSIX_SOURCE) $(TEST_SUPPORT) \
		tests/test_file_contract.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(ENGINE_SOURCES) $(POSIX_SOURCE) $(TEST_SUPPORT) \
		tests/test_file_contract.c

$(CRASH_BIN): $(ENGINE_SOURCES) $(TEST_SUPPORT) $(ORACLE) \
		tests/test_file_crash.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(ENGINE_SOURCES) $(TEST_SUPPORT) $(ORACLE) tests/test_file_crash.c

$(POSIX_BIN): $(ENGINE_SOURCES) $(POSIX_SOURCE) $(TEST_SUPPORT) \
		tests/test_file_posix.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(ENGINE_SOURCES) $(POSIX_SOURCE) $(TEST_SUPPORT) \
		tests/test_file_posix.c

$(MODEL_BIN): $(ENGINE_SOURCES) $(TEST_SUPPORT) \
		tests/model_c34_file.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(ENGINE_SOURCES) $(TEST_SUPPORT) tests/model_c34_file.c

$(BROKEN_BIN): $(ENGINE_SOURCES) $(TEST_SUPPORT) \
		tests/broken_c34_file.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(ENGINE_SOURCES) $(TEST_SUPPORT) tests/broken_c34_file.c

check-unit: $(CONTRACT_BIN) $(CRASH_BIN) $(POSIX_BIN)
	$(CONTRACT_BIN)
	$(CRASH_BIN)
	$(POSIX_BIN)

check-exhaustive: $(MODEL_BIN)
	$(MODEL_BIN)

check-negative: $(BROKEN_BIN)
	$(BROKEN_BIN)

check-determinism: $(MODEL_BIN) $(BROKEN_BIN)
	@set -eu; left=$$(mktemp); right=$$(mktemp); \
	trap 'rm -f "$$left" "$$right"' EXIT; \
	$(MODEL_BIN) >"$$left"; $(MODEL_BIN) >"$$right"; \
	diff -u "$$left" "$$right"; \
	$(BROKEN_BIN) >"$$left"; $(BROKEN_BIN) >"$$right"; \
	diff -u "$$left" "$$right"; \
	echo "C3.4 file deterministic stdout: PASS"

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

$(FWLAB_FAKE_OUTPUT): $(ENGINE_SOURCES) $(TEST_SUPPORT) c34_file_fake_main.c \
		| $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(ENGINE_SOURCES) $(TEST_SUPPORT) c34_file_fake_main.c

fake-link: check $(FWLAB_FAKE_OUTPUT)

clean:
	rm -f build/c34_file_contract_tests build/c34_file_crash_tests \
		build/c34_file_posix_tests build/c34_file_model \
		build/c34_file_broken build/c34_file_fake_link \
		build/clang/c34_file_contract_tests build/clang/c34_file_crash_tests \
		build/clang/c34_file_posix_tests build/clang/c34_file_model \
		build/clang/c34_file_broken \
		build/sanitize/c34_file_contract_tests \
		build/sanitize/c34_file_crash_tests \
		build/sanitize/c34_file_posix_tests build/sanitize/c34_file_model \
		build/sanitize/c34_file_broken \
		build/thread/c34_file_contract_tests build/thread/c34_file_crash_tests \
		build/thread/c34_file_posix_tests build/thread/c34_file_model \
		build/thread/c34_file_broken
	@rmdir build/clang build/sanitize build/thread build 2>/dev/null || true
