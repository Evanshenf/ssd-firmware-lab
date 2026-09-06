# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
BUILD_DIR ?= build/nfc-geometry
CPPFLAGS ?=
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -fno-common
LDFLAGS ?=
LDLIBS ?=

SOURCES := ../../nfc/nfc_model.c ../../nfc/nfc_scheduler.c \
           ../../nfc/nfc_fault.c ../../nfc/nfc_media.c \
           ../../core/nfc-runtime/nfc_scaled_model.c \
           ../../core/nfc-runtime/nfc_trace_window.c \
           ../../media/file-nand-v1/compact_nand.c \
           ../../media/file-nand-v1/compact_nand_posix.c test_nfc_geometry.c
HEADERS := ../../nfc/nfc_internal.h \
           ../../include/fwlab/private/nfc_scaled_model.h \
           ../../include/fwlab/private/nfc_trace_window.h \
           ../../include/fwlab/portable/nfc_model.h \
           ../../include/fwlab/portable/nfc_types.h \
           ../../include/fwlab/contracts/nfc_provider.h \
           ../../include/fwlab/contracts/nand_media.h \
           ../../media/file-nand-v1/compact_nand.h \
           ../../media/file-nand-v1/compact_nand_internal.h
PROGRAM := $(BUILD_DIR)/test_nfc_geometry

.PHONY: all check
all: $(PROGRAM)

$(PROGRAM): $(SOURCES) $(HEADERS) nfc_geometry.mk
	mkdir -p "$(BUILD_DIR)"
	$(CC) -I../../include -I../../media/file-nand-v1 $(CPPFLAGS) $(CFLAGS) \
		$(SOURCES) $(LDFLAGS) $(LDLIBS) -o "$@"

check: $(PROGRAM)
	"$(PROGRAM)"
