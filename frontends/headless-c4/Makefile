# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CLANG ?= clang
SAN_CC ?= $(CC)
TSAN_CC ?= $(CLANG)
BUILD_DIR ?= build
FWLAB_FAKE_OUTPUT ?= $(BUILD_DIR)/c41_headless_fake_link

CPPFLAGS += -I../../include -I.
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -fno-common
CFLAGS ?= $(BASE_CFLAGS)

CORE_SOURCES := ../../core/c4-nvme/c41_codec.c \
	../../core/c4-nvme/c41_profile.c ../../core/c4-nvme/c41_action.c
HIF_SOURCES := c41_wire.c
UNIT_BIN := $(BUILD_DIR)/c41_wire_tests
FUZZ_BIN := $(BUILD_DIR)/c41_wire_fuzz

.PHONY: all check-unit check-fuzz check-local-determinism check-architecture \
	check-c41 check check-clang check-sanitize check-thread check-analysis \
	check-strict check-determinism check-cross check-all fake-link-c41 \
	fake-link clean

all: $(UNIT_BIN) $(FUZZ_BIN)

$(BUILD_DIR):
	mkdir -p $@

$(UNIT_BIN): $(CORE_SOURCES) $(HIF_SOURCES) tests/test_c41_wire.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) tests/test_c41_wire.c

$(FUZZ_BIN): $(CORE_SOURCES) $(HIF_SOURCES) tests/fuzz_c41.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) tests/fuzz_c41.c

check-unit: $(UNIT_BIN)
	$(UNIT_BIN)

check-fuzz: $(FUZZ_BIN)
	$(FUZZ_BIN)

check-local-determinism: $(UNIT_BIN) $(FUZZ_BIN)
	@set -eu; left=$$(mktemp); right=$$(mktemp); \
	trap 'rm -f "$$left" "$$right"' EXIT; \
	$(UNIT_BIN) >"$$left"; $(UNIT_BIN) >"$$right"; \
	diff -u "$$left" "$$right"; \
	$(FUZZ_BIN) >"$$left"; $(FUZZ_BIN) >"$$right"; \
	diff -u "$$left" "$$right"; \
	echo "C4.1 headless deterministic stdout: PASS"

check-architecture:
	python3 ../../scripts/check_c4_architecture.py --cc $(CC)

check-c41: check-unit check-fuzz check-local-determinism check-architecture
	$(MAKE) -C ../../core/c4-nvme \
		CC=$(CC) BUILD_DIR=$(abspath $(BUILD_DIR)/core) check-c41

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

check-analysis:
	python3 ../../scripts/check_c4_analysis.py

check-strict:
	$(MAKE) BUILD_DIR=build/strict \
		CFLAGS='-std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -fno-common' \
		check-c41

check-determinism:
	python3 ../../scripts/check_c4_determinism.py

check-cross:
	python3 ../../scripts/check_c4_cross.py

check-all: check-c41 check-clang check-sanitize check-thread check-analysis \
	check-strict check-determinism check-cross

$(FWLAB_FAKE_OUTPUT): $(CORE_SOURCES) $(HIF_SOURCES) fakes/c41_fake_main.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) fakes/c41_fake_main.c

fake-link-c41: $(FWLAB_FAKE_OUTPUT)

fake-link: check-c41 $(FWLAB_FAKE_OUTPUT)

clean:
	$(MAKE) -C ../../core/c4-nvme clean
	rm -f build/c41_wire_tests build/c41_wire_fuzz \
		build/c41_headless_fake_link \
		build/core/c41_portable_tests build/core/c41_portable_model \
		build/clang/c41_wire_tests build/clang/c41_wire_fuzz \
		build/clang/core/c41_portable_tests \
		build/clang/core/c41_portable_model \
		build/sanitize/c41_wire_tests build/sanitize/c41_wire_fuzz \
		build/sanitize/core/c41_portable_tests \
		build/sanitize/core/c41_portable_model \
		build/thread/c41_wire_tests build/thread/c41_wire_fuzz \
		build/thread/core/c41_portable_tests \
		build/thread/core/c41_portable_model \
		build/strict/c41_wire_tests build/strict/c41_wire_fuzz \
		build/strict/core/c41_portable_tests \
		build/strict/core/c41_portable_model
	@rmdir build/clang/core build/clang build/sanitize/core build/sanitize \
		build/thread/core build/thread build/strict/core build/strict \
		build/core build 2>/dev/null || true
