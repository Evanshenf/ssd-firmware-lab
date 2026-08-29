# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CLANG ?= clang
SAN_CC ?= $(CC)
TSAN_CC ?= $(CLANG)
BUILD_DIR ?= build
FWLAB_FAKE_OUTPUT ?= $(BUILD_DIR)/c33_fake_link

CPPFLAGS += -I../include -I.
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CFLAGS ?= $(BASE_CFLAGS)

MODEL_SOURCES := nfc_model.c nfc_scheduler.c nfc_fault.c nfc_media.c \
	nfc_codec.c
ADAPTER_SOURCE := nfc_adapter.c
FIXTURE_SOURCES := fakes/nfc_buffer.c fakes/nfc_memory_media.c \
	fakes/nfc_scripted.c
SUPPORT_SOURCE := tests/c33_test_support.c
CONTRACT_BIN := $(BUILD_DIR)/c33_contract_tests
LEGALITY_BIN := $(BUILD_DIR)/c33_legality_tests
CODEC_BIN := $(BUILD_DIR)/c33_codec_tests
REPLACE_BIN := $(BUILD_DIR)/c33_replaceability_tests
SCHEDULER_BIN := $(BUILD_DIR)/c33_scheduler_tests
ECC_BIN := $(BUILD_DIR)/c33_ecc_wear_tests
RESET_BIN := $(BUILD_DIR)/c33_reset_power_tests
MODEL_BIN := $(BUILD_DIR)/c33_model
BROKEN_BIN := $(BUILD_DIR)/c33_broken
ISOLATION_BIN := $(BUILD_DIR)/c33_isolation_tests

.PHONY: all check-unit check-exhaustive check-negative check-replaceability \
	check-determinism check-architecture check check-clang check-sanitize \
	check-thread check-cross check-all fake-link clean

all: $(CONTRACT_BIN) $(LEGALITY_BIN) $(CODEC_BIN) $(REPLACE_BIN) \
	$(SCHEDULER_BIN) $(ECC_BIN) $(RESET_BIN) $(MODEL_BIN) $(BROKEN_BIN) \
	$(ISOLATION_BIN)

$(BUILD_DIR):
	mkdir -p $@

$(CONTRACT_BIN): $(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		tests/test_contract.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		tests/test_contract.c

$(LEGALITY_BIN): $(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		tests/test_legality.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		tests/test_legality.c

$(CODEC_BIN): nfc_codec.c tests/test_codec.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		nfc_codec.c tests/test_codec.c

$(REPLACE_BIN): $(MODEL_SOURCES) $(ADAPTER_SOURCE) $(FIXTURE_SOURCES) \
		$(SUPPORT_SOURCE) tests/test_replaceability.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(MODEL_SOURCES) $(ADAPTER_SOURCE) $(FIXTURE_SOURCES) \
		$(SUPPORT_SOURCE) tests/test_replaceability.c

$(SCHEDULER_BIN): $(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		tests/test_scheduler.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		tests/test_scheduler.c

$(ECC_BIN): $(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		tests/test_ecc_wear.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		tests/test_ecc_wear.c

$(RESET_BIN): $(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		tests/test_reset_power.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		tests/test_reset_power.c

$(MODEL_BIN): tests/c33_oracle.c tests/model_c33.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/c33_oracle.c tests/model_c33.c

$(BROKEN_BIN): tests/c33_oracle.c tests/broken_c33.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/c33_oracle.c tests/broken_c33.c

$(ISOLATION_BIN): $(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		tests/test_isolation.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		tests/test_isolation.c

check-unit: all
	$(CONTRACT_BIN)
	$(LEGALITY_BIN)
	$(CODEC_BIN)
	$(REPLACE_BIN)
	$(SCHEDULER_BIN)
	$(ECC_BIN)
	$(RESET_BIN)
	$(MODEL_BIN)
	$(BROKEN_BIN)
	$(ISOLATION_BIN)

check-exhaustive: $(MODEL_BIN)
	$(MODEL_BIN)

check-negative: $(BROKEN_BIN)
	$(BROKEN_BIN)

check-replaceability: $(REPLACE_BIN) $(ISOLATION_BIN)
	$(REPLACE_BIN)
	$(ISOLATION_BIN)

check-determinism: $(MODEL_BIN) $(BROKEN_BIN)
	@set -eu; left=$$(mktemp); right=$$(mktemp); \
	trap 'rm -f "$$left" "$$right"' EXIT; \
	$(MODEL_BIN) >"$$left"; $(MODEL_BIN) >"$$right"; \
	diff -u "$$left" "$$right"; \
	$(BROKEN_BIN) >"$$left"; $(BROKEN_BIN) >"$$right"; \
	diff -u "$$left" "$$right"; \
	echo "C3.3 deterministic stdout: PASS"

check-architecture: | $(BUILD_DIR)
	@for source in $(MODEL_SOURCES) $(ADAPTER_SOURCE); do \
		object="$(BUILD_DIR)/$${source%.c}.audit.o"; \
		$(CC) $(CPPFLAGS) $(CFLAGS) -c "$$source" -o "$$object" || exit 1; \
	done
	@if nm -u $(BUILD_DIR)/*.audit.o | \
		grep -E 'U[[:space:]]+(malloc|calloc|realloc|free|pthread_create|pthread_join|clock_gettime|nanosleep|sleep|rand|random|srand|open|fsync|mmap)(@.*)?$$'; then \
		echo "forbidden dependency in C3.3 portable objects" >&2; \
		exit 1; \
	fi
	python3 ../scripts/check_c33_architecture.py --cc $(CC)

check: check-unit check-determinism check-architecture

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
	python3 ../scripts/check_c33_cross.py

check-all: check check-clang check-sanitize check-thread check-cross

$(FWLAB_FAKE_OUTPUT): $(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		fakes/nfc_fake_main.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(MODEL_SOURCES) $(FIXTURE_SOURCES) $(SUPPORT_SOURCE) \
		fakes/nfc_fake_main.c

fake-link: check $(FWLAB_FAKE_OUTPUT)

clean:
	rm -f build/c33_contract_tests build/c33_legality_tests \
		build/c33_codec_tests build/c33_replaceability_tests \
		build/c33_scheduler_tests build/c33_ecc_wear_tests \
		build/c33_reset_power_tests \
		build/c33_model build/c33_broken \
		build/c33_isolation_tests \
		build/c33_fake_link \
		build/*.audit.o \
		build/clang/c33_contract_tests build/clang/c33_legality_tests \
		build/clang/c33_codec_tests build/clang/c33_replaceability_tests \
		build/clang/c33_scheduler_tests build/clang/c33_ecc_wear_tests \
		build/clang/c33_reset_power_tests \
		build/clang/c33_model build/clang/c33_broken \
		build/clang/c33_isolation_tests \
		build/clang/*.audit.o \
		build/sanitize/c33_contract_tests \
		build/sanitize/c33_legality_tests build/sanitize/c33_codec_tests \
		build/sanitize/c33_replaceability_tests \
		build/sanitize/c33_scheduler_tests build/sanitize/c33_ecc_wear_tests \
		build/sanitize/c33_reset_power_tests \
		build/sanitize/c33_model build/sanitize/c33_broken \
		build/sanitize/c33_isolation_tests \
		build/sanitize/*.audit.o \
		build/thread/c33_contract_tests build/thread/c33_legality_tests \
		build/thread/c33_codec_tests build/thread/c33_replaceability_tests \
		build/thread/c33_scheduler_tests build/thread/c33_ecc_wear_tests \
		build/thread/c33_reset_power_tests build/thread/c33_model \
		build/thread/c33_broken build/thread/c33_isolation_tests \
		build/thread/*.audit.o
	@rmdir build/clang build/sanitize build/thread build 2>/dev/null || true
