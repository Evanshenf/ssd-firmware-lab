# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
BUILD_DIR ?= build

CPPFLAGS += -I../../include -I../../core/c34 \
	-I../../core/c34/fakes -I../../core/c34/tests \
	-I../../media/c34-file -I../../media/c34-file/tests
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic

C34_SOURCES := ../../core/c34/c34_codec.c \
	../../core/c34/c34_recovery.c ../../core/c34/c34_mapping.c \
	../../core/c34/c34_journal.c ../../core/c34/c34_checkpoint.c \
	../../core/c34/c34_nfc_graph.c ../../core/c34/c34_coordinator.c \
	../../core/c34/c34_drive.c ../../core/c34/c34_provider.c
FIXTURE_SOURCES := ../../core/c34/fakes/c34_memory_media.c \
	../../core/c34/fakes/c34_buffer.c ../../core/c34/tests/c34_test_support.c
PORTABLE_SOURCES := ../../core/c31.c ../../core/c31_codec.c \
	../../core/c32/c32_policy.c ../../nfc/nfc_model.c \
	../../nfc/nfc_scheduler.c ../../nfc/nfc_fault.c \
	../../nfc/nfc_media.c ../../nfc/nfc_codec.c
FILE_SOURCES := ../../media/c34-file/c34_file_codec.c \
	../../media/c34-file/c34_file_recovery.c \
	../../media/c34-file/c34_file_engine.c \
	../../media/c34-file/c34_file_media.c \
	../../media/c34-file/c34_file_posix.c

BIN := $(BUILD_DIR)/ftl_restart_poc

.PHONY: all check clean

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $@

$(BIN): ftl_restart_poc.c $(C34_SOURCES) $(FIXTURE_SOURCES) \
		$(PORTABLE_SOURCES) $(FILE_SOURCES) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(C34_SOURCES) $(FIXTURE_SOURCES) $(PORTABLE_SOURCES) \
		$(FILE_SOURCES) ftl_restart_poc.c

check: $(BIN)
	$(BIN)

clean:
	rm -f $(BIN)
	@rmdir $(BUILD_DIR) 2>/dev/null || true
