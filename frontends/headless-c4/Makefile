# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

# C42_GUARD_BEGIN
override SHELL := /bin/sh
override .SHELLFLAGS := -eu -c
override PATH := /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH

ifneq ($(words $(MAKEFILE_LIST)),1)
$(error C4.2 refuses preloaded or multiple primary Makefiles)
endif
ifneq ($(strip $(MAKEFILES)),)
$(error C4.2 refuses inherited MAKEFILES)
endif
ifneq ($(strip $(-*-eval-flags-*-)),)
$(error C4.2 refuses Make --eval/-E input)
endif
_FWLAB_FLAG_WORD := $(if $(findstring =,$(firstword $(MAKEFLAGS))),,$(firstword $(MAKEFLAGS)))
ifneq ($(findstring i,$(_FWLAB_FLAG_WORD)),)
$(error C4.2 refuses inherited Make ignore-error mode)
endif
ifneq ($(strip $(findstring n,$(_FWLAB_FLAG_WORD))$(findstring t,$(_FWLAB_FLAG_WORD))$(findstring q,$(_FWLAB_FLAG_WORD))),)
$(error C4.2 refuses non-executing Make modes)
endif
ifneq ($(filter --ignore-errors -i,$(MAKEFLAGS) $(MFLAGS)),)
$(error C4.2 refuses inherited Make ignore-error mode)
endif
ifneq ($(filter --dry-run --just-print --recon --touch --question -n -t -q,$(MAKEFLAGS) $(MFLAGS) $(GNUMAKEFLAGS)),)
$(error C4.2 refuses non-executing Make modes)
endif
_FWLAB_UNSAFE_DIRECTIVE := $(shell /usr/bin/awk '/^[[:space:]]*\.IGNORE([[:space:]]|:)/ || /^[[:space:]]*(override[[:space:]]+)?(MAKEFLAGS|MFLAGS|GNUMAKEFLAGS|MAKEFILES)[[:space:]]*[:+?]?=/ { print "unsafe"; exit }' $(MAKEFILE_LIST))
ifneq ($(_FWLAB_UNSAFE_DIRECTIVE),)
$(error C4.2 refuses .IGNORE or in-file MAKEFLAGS/MFLAGS assignments)
endif

ifneq ($(filter environment%,$(origin CC)),)
override CC := cc
endif
ifneq ($(filter environment%,$(origin CLANG)),)
override CLANG := clang
endif
ifneq ($(filter environment%,$(origin SAN_CC)),)
override SAN_CC := $(CC)
endif
ifneq ($(filter environment%,$(origin TSAN_CC)),)
override TSAN_CC := $(CLANG)
endif
ifneq ($(filter environment%,$(origin BUILD_DIR)),)
override BUILD_DIR := build
endif
ifneq ($(filter environment%,$(origin CFLAGS)),)
override CFLAGS :=
endif
ifneq ($(filter environment%,$(origin LDFLAGS)),)
override LDFLAGS :=
endif

CC ?= cc
CLANG ?= clang
SAN_CC ?= $(CC)
TSAN_CC ?= $(CLANG)
BUILD_DIR ?= build
C41_FAKE_OUTPUT := $(BUILD_DIR)/c41_headless_fake_link

override CPPFLAGS := -I../../include -I.
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -fno-common
CFLAGS ?= $(BASE_CFLAGS)

override CORE_SOURCES := ../../core/c4-nvme/c41_codec.c \
	../../core/c4-nvme/c41_profile.c ../../core/c4-nvme/c41_action.c
override HIF_SOURCES := c41_wire.c
UNIT_BIN := $(BUILD_DIR)/c41_wire_tests
FUZZ_BIN := $(BUILD_DIR)/c41_wire_fuzz
include c42_sources.mk

ifneq ($(words $(MAKEFILE_LIST)),2)
$(error C4.2 refuses nested or unexpected included Makefiles)
endif
_FWLAB_INCLUDED_UNSAFE_DIRECTIVE := $(shell /usr/bin/awk '/^[[:space:]]*\.IGNORE([[:space:]]|:)/ || /^[[:space:]]*(override[[:space:]]+)?(MAKEFLAGS|MFLAGS|GNUMAKEFLAGS|MAKEFILES)[[:space:]]*[:+?]?=/ { print "unsafe"; exit }' $(MAKEFILE_LIST))
ifneq ($(_FWLAB_INCLUDED_UNSAFE_DIRECTIVE),)
$(error C4.2 refuses unsafe included Make semantics)
endif
# C42_GUARD_END

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

check: check-c41 check-c42

# C42_FROZEN_RULES_BEGIN
# These targets are defense in depth. scripts/run_c42_gate.py is authoritative.
FWLAB_FAKE_OUTPUT ?= $(BUILD_DIR)/c42_headless_fake_link
C42_FAKE_OUTPUT := $(FWLAB_FAKE_OUTPUT)
C42_QUEUE_BIN := $(BUILD_DIR)/c42_queue_unit
C42_PUBLICATION_BIN := $(BUILD_DIR)/c42_publication_unit
C42_IDENTITY_BIN := $(BUILD_DIR)/c42_identity_unit
C42_RESET_DELETE_BIN := $(BUILD_DIR)/c42_reset_delete_unit
C42_REMEDIATION_BIN := $(BUILD_DIR)/c42_remediation_unit
C42_PROVIDER_BIN := $(BUILD_DIR)/c42_provider_matrix
C42_CUTS_BIN := $(BUILD_DIR)/c42_phase_cuts
C42_REPLAY_BIN := $(BUILD_DIR)/c42_dut_replay
C42_ABI_BIN := $(BUILD_DIR)/c42_public_abi
C42_MODEL_BIN := $(BUILD_DIR)/c42_model
C42_BROKEN_BIN := $(BUILD_DIR)/c42_broken
C42_FUZZ_BIN := $(BUILD_DIR)/c42_fuzz
C42_THREAD_BIN := $(BUILD_DIR)/c42_thread
C42_RECEIPT := $(BUILD_DIR)/c42_execution.receipt

.PHONY: check-c42-unit check-c42-local-determinism check-c42 fake-link-c42 \
	check-c42-model check-c42-negative check-c42-fuzz check-c42-thread \
	check-c42-architecture check-c42-remediation check-c42-build-closure \
	check-c42-dynamic-mutations check-c42-replay \
	check-c42-provider-mutations check-c42-make-integrity \
	check-c42-authority \
	check-c42-runner-integrity \
	check-c42-claim-models \
	check-c42-build-tests \
	check-c42-artifact-receipt

.SECONDEXPANSION:
_FWLAB_FINAL_PARSE_GUARD = $(if $(filter 2,$(words $(MAKEFILE_LIST))),,$(error C4.2 refuses late or multiple Makefile inputs))
$(C42_CHECK_TARGETS) $(C42_BUILD_TEST_TARGETS) \
	check-c42-build-tests check-c42: $$(_FWLAB_FINAL_PARSE_GUARD)

$(C42_QUEUE_BIN): $(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) $(C42_HEADERS) \
		tests/test_c42_queue.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) tests/test_c42_queue.c

$(C42_PUBLICATION_BIN): $(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) $(C42_HEADERS) \
		tests/test_c42_publication.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) tests/test_c42_publication.c

$(C42_IDENTITY_BIN): $(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) $(C42_HEADERS) \
		tests/test_c42_identity.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) tests/test_c42_identity.c

$(C42_RESET_DELETE_BIN): $(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) $(C42_HEADERS) \
		tests/test_c42_reset_delete.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) tests/test_c42_reset_delete.c

$(C42_REMEDIATION_BIN): $(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) $(C42_HEADERS) \
		tests/test_c42_remediation.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) tests/test_c42_remediation.c

$(C42_PROVIDER_BIN): $(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) $(C42_HEADERS) \
		tests/test_c42_provider_matrix.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) tests/test_c42_provider_matrix.c

$(C42_CUTS_BIN): $(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) $(C42_HEADERS) \
		tests/test_c42_phase_cuts.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) tests/test_c42_phase_cuts.c

check-c42-remediation: $(C42_REMEDIATION_BIN)
	$(C42_REMEDIATION_BIN)

$(C42_REPLAY_BIN): $(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) $(C42_REFERENCE) \
		$(C42_HEADERS) tests/test_c42_dut_replay.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) $(C42_REFERENCE) \
		tests/test_c42_dut_replay.c

check-c42-replay: $(C42_REPLAY_BIN)
	$(C42_REPLAY_BIN)

$(C42_ABI_BIN): tests/test_c42_public_abi.c $(C42_HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/test_c42_public_abi.c

check-c42-unit: $(C42_QUEUE_BIN) $(C42_PUBLICATION_BIN) \
		$(C42_IDENTITY_BIN) $(C42_RESET_DELETE_BIN) $(C42_REMEDIATION_BIN) \
		$(C42_PROVIDER_BIN) $(C42_CUTS_BIN) $(C42_ABI_BIN)
	$(C42_QUEUE_BIN)
	$(C42_PUBLICATION_BIN)
	$(C42_IDENTITY_BIN)
	$(C42_RESET_DELETE_BIN)
	$(C42_REMEDIATION_BIN)
	$(C42_PROVIDER_BIN)
	$(C42_CUTS_BIN)
	$(C42_ABI_BIN)

$(C42_MODEL_BIN): tests/c42_model.c tests/model_c42.c $(C42_HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/c42_model.c tests/model_c42.c

$(C42_BROKEN_BIN): tests/c42_model.c tests/broken_c42.c $(C42_HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/c42_model.c tests/broken_c42.c

$(C42_FUZZ_BIN): $(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) $(C42_HEADERS) \
		tests/fuzz_c42.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) tests/fuzz_c42.c

$(C42_THREAD_BIN): $(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) $(C42_HEADERS) \
		tests/test_c42_thread.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -pthread -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) tests/test_c42_thread.c

check-c42-model: $(C42_MODEL_BIN)
	$(C42_MODEL_BIN)

check-c42-negative: $(C42_BROKEN_BIN)
	$(C42_BROKEN_BIN)

check-c42-fuzz: $(C42_FUZZ_BIN)
	$(C42_FUZZ_BIN)

check-c42-thread: $(C42_THREAD_BIN)
	$(C42_THREAD_BIN)

check-c42-local-determinism: check-c42-unit check-c42-model \
		check-c42-negative check-c42-fuzz $(C42_REPLAY_BIN) $(C42_FAKE_OUTPUT)
	@set -eu; left=$$(mktemp); right=$$(mktemp); \
	trap 'rm -f "$$left" "$$right"' EXIT; \
	for binary in $(C42_QUEUE_BIN) $(C42_PUBLICATION_BIN) \
		$(C42_IDENTITY_BIN) $(C42_RESET_DELETE_BIN) \
		$(C42_REMEDIATION_BIN) $(C42_PROVIDER_BIN) $(C42_CUTS_BIN) \
		$(C42_REPLAY_BIN) \
		$(C42_MODEL_BIN) \
		$(C42_ABI_BIN) $(C42_BROKEN_BIN) $(C42_FUZZ_BIN) \
		$(C42_FAKE_OUTPUT); do \
		$$binary >"$$left"; $$binary >"$$right"; \
		diff -u "$$left" "$$right"; \
	done; \
	echo "C4.2 headless deterministic stdout: PASS"

check-c42-architecture:
	python3 ../../scripts/check_c42_architecture.py --cc $(CC)

check-c42-dynamic-mutations:
	python3 ../../scripts/check_c42_dynamic_mutations.py

check-c42-provider-mutations:
	python3 ../../scripts/check_c42_provider_mutations.py

check-c42-make-integrity:
	python3 ../../scripts/check_c42_make_integrity.py

check-c42-runner-integrity:
	python3 ../../scripts/check_c42_runner_integrity.py

check-c42-authority:
	PYTHONDONTWRITEBYTECODE=1 python3 ../../scripts/check_c42_authority.py

check-c42-claim-models:
	PYTHONDONTWRITEBYTECODE=1 python3 \
		../../scripts/extract_c42_interface_inventory.py --check --cc clang
	PYTHONDONTWRITEBYTECODE=1 python3 \
		../../scripts/extract_c42_interface_inventory.py --check --cc gcc
	PYTHONDONTWRITEBYTECODE=1 python3 \
		../../scripts/gen_c42_obligations.py --check
	PYTHONDONTWRITEBYTECODE=1 python3 \
		../../scripts/check_c42_claim_models.py --self-test

check-c42-artifact-receipt: $(C42_QUEUE_BIN) $(C42_PUBLICATION_BIN) \
		$(C42_IDENTITY_BIN) $(C42_RESET_DELETE_BIN) $(C42_REMEDIATION_BIN) \
		$(C42_PROVIDER_BIN) $(C42_CUTS_BIN) $(C42_REPLAY_BIN) \
		$(C42_ABI_BIN) $(C42_MODEL_BIN) $(C42_BROKEN_BIN) $(C42_FUZZ_BIN) \
		$(C42_FAKE_OUTPUT)
	@set -eu; temporary='$(C42_RECEIPT).tmp'; \
	trap 'rm -f "$$temporary"' EXIT; : >"$$temporary"; \
	for binary in $(C42_QUEUE_BIN) $(C42_PUBLICATION_BIN) \
		$(C42_IDENTITY_BIN) $(C42_RESET_DELETE_BIN) \
		$(C42_REMEDIATION_BIN) $(C42_PROVIDER_BIN) $(C42_CUTS_BIN) \
		$(C42_REPLAY_BIN) $(C42_ABI_BIN) $(C42_MODEL_BIN) \
		$(C42_BROKEN_BIN) $(C42_FUZZ_BIN) $(C42_FAKE_OUTPUT); do \
		test -f "$$binary" && test -x "$$binary"; \
		output="$$($$binary)"; \
		case "$$output" in *PASS*|c42-fake-v1*) ;; *) exit 1 ;; esac; \
		/usr/bin/sha256sum "$$binary" >>"$$temporary"; \
	done; \
	test "$$(/usr/bin/wc -l <"$$temporary")" -eq 13; \
	mv "$$temporary" '$(C42_RECEIPT)'; trap - EXIT; \
	echo "C4.2 executed-artifact receipt: PASS binaries=13"

check-c42-build-closure: $(C42_ALL_INPUTS)
	@:

check-c42-build-tests: $(C42_BUILD_TEST_TARGETS)

$(C42_FAKE_OUTPUT): $(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) $(C42_HEADERS) fakes/c42_fake_main.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) $(C42_SOURCES) \
		$(C42_FAKE_SOURCES) $(C42_SUPPORT) fakes/c42_fake_main.c

fake-link-c42: $(C42_FAKE_OUTPUT)

check-c42: $(C42_CHECK_TARGETS)
# C42_FROZEN_RULES_END

check-clang:
	$(MAKE) CC=$(CLANG) BUILD_DIR=build/clang \
		check-c41 check-c42-local-determinism

check-sanitize:
	$(MAKE) CC=$(SAN_CC) BUILD_DIR=build/sanitize \
		CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -fno-common -fno-omit-frame-pointer -fsanitize=address,undefined' \
		LDFLAGS='-fsanitize=address,undefined' \
		check-c41 check-c42-local-determinism

check-thread:
	$(MAKE) CC=$(TSAN_CC) BUILD_DIR=build/thread \
		CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -fno-common -fno-omit-frame-pointer -fsanitize=thread' \
		LDFLAGS='-fsanitize=thread' check-c41 check-c42-thread

check-analysis:
	python3 ../../scripts/check_c4_analysis.py
	python3 ../../scripts/check_c42_analysis.py

check-strict:
	$(MAKE) BUILD_DIR=build/strict \
		CFLAGS='-std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -fno-common' \
		check-c41 check-c42-local-determinism

check-determinism:
	python3 ../../scripts/check_c4_determinism.py
	python3 ../../scripts/check_c42_determinism.py

check-cross:
	python3 ../../scripts/check_c4_cross.py
	python3 ../../scripts/check_c42_cross.py

check-all: check check-clang check-sanitize check-thread check-analysis \
	check-strict check-determinism check-cross

$(C41_FAKE_OUTPUT): $(CORE_SOURCES) $(HIF_SOURCES) fakes/c41_fake_main.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(CORE_SOURCES) $(HIF_SOURCES) fakes/c41_fake_main.c

fake-link-c41: $(C41_FAKE_OUTPUT)

fake-link: check-c41 check-c42-local-determinism $(C42_FAKE_OUTPUT)

clean:
	$(MAKE) -C ../../core/c4-nvme clean
	rm -f build/c41_wire_tests build/c41_wire_fuzz \
		build/c41_headless_fake_link \
		build/c42_queue_unit build/c42_publication_unit \
	build/c42_identity_unit build/c42_reset_delete_unit \
	build/c42_remediation_unit build/c42_provider_matrix build/c42_phase_cuts \
		build/c42_dut_replay build/c42_public_abi \
		build/c42_execution.receipt \
		build/c42_model build/c42_broken build/c42_fuzz build/c42_thread \
		build/c42_headless_fake_link \
		build/core/c41_portable_tests build/core/c41_portable_model \
		build/clang/c41_wire_tests build/clang/c41_wire_fuzz \
		build/clang/core/c41_portable_tests \
		build/clang/core/c41_portable_model \
		build/clang/c42_queue_unit build/clang/c42_publication_unit \
	build/clang/c42_identity_unit build/clang/c42_reset_delete_unit \
	build/clang/c42_remediation_unit build/clang/c42_provider_matrix \
	build/clang/c42_phase_cuts \
		build/clang/c42_dut_replay build/clang/c42_public_abi \
		build/clang/c42_model build/clang/c42_broken \
		build/clang/c42_fuzz build/clang/c42_thread \
		build/clang/c42_headless_fake_link \
		build/sanitize/c41_wire_tests build/sanitize/c41_wire_fuzz \
		build/sanitize/core/c41_portable_tests \
		build/sanitize/core/c41_portable_model \
		build/sanitize/c42_queue_unit build/sanitize/c42_publication_unit \
		build/sanitize/c42_identity_unit \
	build/sanitize/c42_reset_delete_unit \
	build/sanitize/c42_remediation_unit build/sanitize/c42_provider_matrix \
	build/sanitize/c42_phase_cuts \
		build/sanitize/c42_dut_replay build/sanitize/c42_public_abi \
		build/sanitize/c42_model build/sanitize/c42_broken \
		build/sanitize/c42_fuzz build/sanitize/c42_thread \
		build/sanitize/c42_headless_fake_link \
		build/thread/c41_wire_tests build/thread/c41_wire_fuzz \
		build/thread/core/c41_portable_tests \
		build/thread/core/c41_portable_model \
		build/thread/c42_thread \
		build/strict/c41_wire_tests build/strict/c41_wire_fuzz \
		build/strict/core/c41_portable_tests \
		build/strict/core/c41_portable_model \
		build/strict/c42_queue_unit build/strict/c42_publication_unit \
	build/strict/c42_identity_unit build/strict/c42_reset_delete_unit \
	build/strict/c42_remediation_unit build/strict/c42_provider_matrix \
	build/strict/c42_phase_cuts \
		build/strict/c42_dut_replay build/strict/c42_public_abi \
		build/strict/c42_model build/strict/c42_broken \
		build/strict/c42_fuzz build/strict/c42_thread \
		build/strict/c42_headless_fake_link
	@rmdir build/clang/core build/clang build/sanitize/core build/sanitize \
		build/thread/core build/thread build/strict/core build/strict \
		build/core build 2>/dev/null || true
