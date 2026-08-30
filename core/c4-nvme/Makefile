# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CLANG ?= clang
SAN_CC ?= $(CC)
TSAN_CC ?= $(CLANG)
BUILD_DIR ?= build
FWLAB_FAKE_OUTPUT ?= $(BUILD_DIR)/c41_core_fake_link

CPPFLAGS += -I../../include
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -fno-common
CFLAGS ?= $(BASE_CFLAGS)

SOURCES := c41_codec.c c41_profile.c c41_action.c
UNIT_BIN := $(BUILD_DIR)/c41_portable_tests
MODEL_BIN := $(BUILD_DIR)/c41_portable_model

.PHONY: all check-unit check-model check-determinism check-c41 check \
	check-clang check-sanitize check-thread check-all fake-link-c41 \
	fake-link clean

all: $(UNIT_BIN) $(MODEL_BIN)

$(BUILD_DIR):
	mkdir -p $@

$(UNIT_BIN): $(SOURCES) tests/test_c41_portable.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(SOURCES) tests/test_c41_portable.c

$(MODEL_BIN): $(SOURCES) tests/model_c41.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(SOURCES) tests/model_c41.c

check-unit: $(UNIT_BIN)
	$(UNIT_BIN)

check-model: $(MODEL_BIN)
	$(MODEL_BIN)

check-determinism: $(UNIT_BIN) $(MODEL_BIN)
	@set -eu; left=$$(mktemp); right=$$(mktemp); \
	trap 'rm -f "$$left" "$$right"' EXIT; \
	$(UNIT_BIN) >"$$left"; $(UNIT_BIN) >"$$right"; \
	diff -u "$$left" "$$right"; \
	$(MODEL_BIN) >"$$left"; $(MODEL_BIN) >"$$right"; \
	diff -u "$$left" "$$right"; \
	echo "C4.1 portable deterministic stdout: PASS"

check-c41: check-unit check-model check-determinism

check: check-c41

check-clang:
	$(MAKE) CC=$(CLANG) BUILD_DIR=build/clang check-c41

check-sanitize:
	$(MAKE) CC=$(SAN_CC) BUILD_DIR=build/sanitize \
		CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -fno-common -fno-omit-frame-pointer -fsanitize=address,undefined' \
		LDFLAGS='-fsanitize=address,undefined' check-c41

check-thread:
	$(MAKE) CC=$(TSAN_CC) BUILD_DIR=build/thread \
		CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -fno-common -fno-omit-frame-pointer -fsanitize=thread' \
		LDFLAGS='-fsanitize=thread' check-c41

check-all: check-c41 check-clang check-sanitize check-thread

$(FWLAB_FAKE_OUTPUT): $(SOURCES) fakes/c41_fake_main.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(SOURCES) fakes/c41_fake_main.c

fake-link-c41: $(FWLAB_FAKE_OUTPUT)

fake-link: check-c41 $(FWLAB_FAKE_OUTPUT)

clean:
	rm -f build/c41_portable_tests build/c41_portable_model \
		build/c41_core_fake_link \
		build/clang/c41_portable_tests build/clang/c41_portable_model \
		build/sanitize/c41_portable_tests build/sanitize/c41_portable_model \
		build/thread/c41_portable_tests build/thread/c41_portable_model
	@rmdir build/clang build/sanitize build/thread build 2>/dev/null || true
