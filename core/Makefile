# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CLANG ?= clang
SAN_CC ?= $(CC)
TSAN_CC ?= $(CLANG)
BUILD_DIR ?= build
FWLAB_FAKE_OUTPUT ?= $(BUILD_DIR)/c31_fake_link

CPPFLAGS += -I../include -Ifakes
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CFLAGS ?= $(BASE_CFLAGS)

CORE_SOURCES := c31.c c31_codec.c
FAKE_SOURCES := fakes/c31_fake_provider.c fakes/c31_fake_dma.c \
	fakes/c31_fake_nfc.c
TEST_SOURCES := tests/test_c31.c
TEST_BIN := $(BUILD_DIR)/c31_tests
MODEL_BIN := $(BUILD_DIR)/c31_model
FUZZ_BIN := $(BUILD_DIR)/c31_fuzz

.PHONY: all check check-architecture check-clang check-sanitize check-thread \
	check-cross fake-link clean

all: $(TEST_BIN) $(MODEL_BIN) $(FUZZ_BIN)

$(BUILD_DIR):
	mkdir -p $@

$(TEST_BIN): $(CORE_SOURCES) $(FAKE_SOURCES) $(TEST_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(FAKE_SOURCES) $(TEST_SOURCES)

$(MODEL_BIN): $(CORE_SOURCES) $(FAKE_SOURCES) tests/model_c31.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(FAKE_SOURCES) tests/model_c31.c

$(FUZZ_BIN): $(CORE_SOURCES) tests/fuzz_c31.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) tests/fuzz_c31.c

check: $(TEST_BIN) $(MODEL_BIN) $(FUZZ_BIN) check-architecture
	$(TEST_BIN)
	$(MODEL_BIN)
	$(FUZZ_BIN)

check-architecture: | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c c31.c -o $(BUILD_DIR)/c31.audit.o
	$(CC) $(CPPFLAGS) $(CFLAGS) -c c31_codec.c \
		-o $(BUILD_DIR)/c31_codec.audit.o
	@if nm -u $(BUILD_DIR)/c31.audit.o $(BUILD_DIR)/c31_codec.audit.o | \
		grep -E 'U[[:space:]]+(malloc|calloc|realloc|free|pthread_create|pthread_join|clock_gettime|nanosleep|sleep)(@.*)?$$'; then \
		echo "forbidden dependency in C3.1 portable objects" >&2; \
		exit 1; \
	fi
	@echo "C3.1 architecture dependency gate: PASS"

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
	python3 ../scripts/check_c31_cross.py

$(FWLAB_FAKE_OUTPUT): $(CORE_SOURCES) $(FAKE_SOURCES) \
		fakes/c31_fake_main.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(FAKE_SOURCES) fakes/c31_fake_main.c

fake-link: check $(FWLAB_FAKE_OUTPUT)

clean:
	rm -f build/c31_tests build/c31_model build/c31_fuzz \
		build/c31.audit.o build/c31_codec.audit.o \
		build/c31_fake_link build/clang/c31_tests \
		build/clang/c31_model build/clang/c31_fuzz \
		build/clang/c31.audit.o build/clang/c31_codec.audit.o \
		build/sanitize/c31_tests build/sanitize/c31.audit.o \
		build/sanitize/c31_model build/sanitize/c31_fuzz \
		build/sanitize/c31_codec.audit.o \
		build/thread/c31_tests build/thread/c31_model build/thread/c31_fuzz \
		build/thread/c31.audit.o build/thread/c31_codec.audit.o
	@rmdir build/clang build/sanitize build/thread build 2>/dev/null || true
