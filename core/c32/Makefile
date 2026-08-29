# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CLANG ?= clang
SAN_CC ?= $(CC)
TSAN_CC ?= $(CLANG)
BUILD_DIR ?= build
FWLAB_FAKE_OUTPUT ?= $(BUILD_DIR)/c32_fake_link

CPPFLAGS += -I../../include
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CFLAGS ?= $(BASE_CFLAGS)

POLICY_SOURCE := c32_policy.c
RECOVERY_SOURCES := c32_canonical.c c32_recovery.c c32_invariants.c
EXHAUSTIVE_SOURCES := $(POLICY_SOURCE) $(RECOVERY_SOURCES) c32_model.c
UNIT_BIN := $(BUILD_DIR)/c32_policy_tests
RECOVERY_BIN := $(BUILD_DIR)/c32_recovery_tests
INVARIANT_BIN := $(BUILD_DIR)/c32_invariant_tests
MODEL_BIN := $(BUILD_DIR)/c32_model
BROKEN_BIN := $(BUILD_DIR)/c32_broken

.PHONY: all check-unit check-exhaustive check-negative check-determinism \
	check-architecture check check-clang check-sanitize check-thread \
	check-cross check-all fake-link clean

all: $(UNIT_BIN) $(RECOVERY_BIN) $(INVARIANT_BIN) $(MODEL_BIN) $(BROKEN_BIN)

$(BUILD_DIR):
	mkdir -p $@

$(UNIT_BIN): $(POLICY_SOURCE) $(RECOVERY_SOURCES) tests/test_policy.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(POLICY_SOURCE) $(RECOVERY_SOURCES) tests/test_policy.c

$(RECOVERY_BIN): $(RECOVERY_SOURCES) tests/test_recovery.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(RECOVERY_SOURCES) tests/test_recovery.c

$(INVARIANT_BIN): $(RECOVERY_SOURCES) tests/test_invariants.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(RECOVERY_SOURCES) tests/test_invariants.c

$(MODEL_BIN): $(EXHAUSTIVE_SOURCES) tests/model_c32.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(EXHAUSTIVE_SOURCES) tests/model_c32.c

$(BROKEN_BIN): $(EXHAUSTIVE_SOURCES) tests/broken_c32.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(EXHAUSTIVE_SOURCES) tests/broken_c32.c

check-unit: $(UNIT_BIN) $(RECOVERY_BIN) $(INVARIANT_BIN)
	$(UNIT_BIN)
	$(RECOVERY_BIN)
	$(INVARIANT_BIN)

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
	echo "C3.2 deterministic stdout: PASS"

check-architecture: | $(BUILD_DIR)
	@for source in $(POLICY_SOURCE) $(RECOVERY_SOURCES) c32_model.c; do \
		object="$(BUILD_DIR)/$${source%.c}.audit.o"; \
		$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$source" -o "$$object" || exit 1; \
	done
	@if nm -u $(BUILD_DIR)/*.audit.o | \
		grep -E 'U[[:space:]]+(malloc|calloc|realloc|free|pthread_create|pthread_join|clock_gettime|nanosleep|sleep)(@.*)?$$'; then \
		echo "forbidden dependency in C3.2 portable objects" >&2; \
		exit 1; \
	fi
	@echo "C3.2 architecture dependency gate: PASS"
	python3 ../../scripts/check_c32_architecture.py --cc $(CC)

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
	python3 ../../scripts/check_c32_cross.py

check-all: check check-clang check-sanitize check-thread check-cross

$(FWLAB_FAKE_OUTPUT): $(POLICY_SOURCE) fakes/c32_fake_main.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(POLICY_SOURCE) fakes/c32_fake_main.c

fake-link: check $(FWLAB_FAKE_OUTPUT)

clean:
	rm -f build/c32_policy_tests build/c32_recovery_tests \
		build/c32_invariant_tests build/c32_model build/c32_broken \
		build/*.audit.o \
		build/c32_fake_link build/clang/c32_policy_tests \
		build/clang/c32_recovery_tests build/clang/c32_invariant_tests \
		build/clang/c32_model build/clang/c32_broken build/clang/*.audit.o \
		build/sanitize/c32_policy_tests build/sanitize/c32_recovery_tests \
		build/sanitize/c32_invariant_tests build/sanitize/c32_model \
		build/sanitize/c32_broken build/sanitize/*.audit.o \
		build/thread/c32_policy_tests build/thread/c32_recovery_tests \
		build/thread/c32_invariant_tests build/thread/c32_model \
		build/thread/c32_broken build/thread/*.audit.o
	@rmdir build/clang build/sanitize build/thread build 2>/dev/null || true
