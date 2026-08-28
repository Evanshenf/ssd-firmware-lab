# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CLANG ?= clang
SAN_CC ?= $(CC)
TSAN_CC ?= $(CLANG)
BUILD_DIR ?= build

ROOT := ../../..
CONTRACT := $(ROOT)/kernel/vfio-cdev-v1/contract
UAPI := $(ROOT)/kernel/vfio-cdev-v1/uapi/unstable

CPPFLAGS += -I$(CONTRACT) -I$(UAPI)
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CFLAGS ?= $(BASE_CFLAGS)
LDLIBS += -pthread

ENGINE_SOURCES := $(CONTRACT)/c21_wire.c $(CONTRACT)/c21_state.c
TEST_SOURCES := test_c21.c fake_copy.c fake_transition.c pthread_lock.c
TEST_HEADERS := $(CONTRACT)/c21_compat.h $(CONTRACT)/c21_copy.h \
	$(CONTRACT)/c21_wire.h $(CONTRACT)/c21_state.h \
	$(UAPI)/fwlab_c21_a1.h fake_copy.h fake_transition.h \
	golden_vectors.h pthread_lock.h
TEST_BIN := $(BUILD_DIR)/c21_tests
FUZZ_BIN := $(BUILD_DIR)/c21_wire_fuzz

.PHONY: all check check-architecture check-clang check-sanitize check-thread fuzz-smoke clean

all: $(TEST_BIN)

$(BUILD_DIR):
	mkdir -p $@

$(TEST_BIN): $(ENGINE_SOURCES) $(TEST_SOURCES) $(TEST_HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(ENGINE_SOURCES) $(TEST_SOURCES) $(LDLIBS)

check: $(TEST_BIN) check-architecture
	$(TEST_BIN)

check-architecture: | $(BUILD_DIR)
	@if grep -REn '(<linux/(vfio|iommufd)|vfio_dma_rw|vfio_pin_pages|fw_core|nfc_|media_)' \
		$(CONTRACT) --include='*.c' --include='*.h'; then \
		echo "forbidden full-stack or VFIO dependency in C2.1 contract" >&2; \
		exit 1; \
	fi
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $(CONTRACT)/c21_wire.c \
		-o $(BUILD_DIR)/c21_wire.audit.o
	@$(CC) $(CPPFLAGS) $(CFLAGS) -c $(CONTRACT)/c21_state.c \
		-o $(BUILD_DIR)/c21_state.audit.o
	@if nm -u $(BUILD_DIR)/c21_wire.audit.o $(BUILD_DIR)/c21_state.audit.o | \
		grep -E '(vfio|iommufd|fw_core|nfc_|media_)'; then \
		echo "forbidden unresolved dependency in C2.1 objects" >&2; \
		exit 1; \
	fi
	@echo "C2.1 architecture dependency gate: PASS"

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

fuzz-smoke: | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic \
		-DFWLAB_C21_STANDALONE_FUZZ -fsanitize=undefined \
		-fno-omit-frame-pointer \
		-o $(FUZZ_BIN) fuzz_wire.c $(CONTRACT)/c21_wire.c
	$(FUZZ_BIN)

clean:
	rm -f $(BUILD_DIR)/c21_tests $(BUILD_DIR)/c21_wire_fuzz \
		$(BUILD_DIR)/c21_wire.audit.o $(BUILD_DIR)/c21_state.audit.o \
		build/c21_tests build/c21_wire_fuzz \
		build/c21_wire.audit.o build/c21_state.audit.o \
		build/clang/c21_tests build/clang/c21_wire.audit.o \
		build/clang/c21_state.audit.o build/sanitize/c21_tests \
		build/sanitize/c21_wire.audit.o build/sanitize/c21_state.audit.o \
		build/thread/c21_tests build/thread/c21_wire.audit.o \
		build/thread/c21_state.audit.o
	@rmdir build/clang build/sanitize build/thread build 2>/dev/null || true
