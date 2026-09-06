# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause
CC ?= cc
BUILD_DIR ?= build
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=
PROGRAM := $(BUILD_DIR)/test_nfc_page_v2
SOURCES := nfc_page_v2.c tests/test_nfc_page_v2.c
HEADERS := ../../include/fwlab/contracts/nfc_page_v2_types.h \
    ../../include/fwlab/contracts/nfc_page_v2_provider.h \
    ../../include/fwlab/private/nfc_page_v2_model.h \
    ../../include/fwlab/private/nand_batch_v2.h \
    ../../include/fwlab/contracts/nand_media.h \
    ../../include/fwlab/portable/nfc_types.h
.PHONY: all check
all: $(PROGRAM)
$(PROGRAM): $(SOURCES) $(HEADERS) Makefile
	mkdir -p "$(BUILD_DIR)"
	$(CC) -I../../include $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o "$@"
check: $(PROGRAM)
	"$(PROGRAM)"
