# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
BUILD_DIR ?= build
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=

TEST := $(BUILD_DIR)/compact_nand_test
SOURCES := compact_nand.c tests/test_compact_nand.c
HEADERS := compact_nand.h compact_nand_internal.h \
           ../../include/fwlab/contracts/nand_media.h \
           ../../include/fwlab/portable/nfc_types.h

.PHONY: all check
all: $(TEST)

$(TEST): $(SOURCES) $(HEADERS) Makefile
	mkdir -p "$(BUILD_DIR)"
	$(CC) -I../../include -I. $(CPPFLAGS) $(CFLAGS) $(SOURCES) \
		$(LDFLAGS) $(LDLIBS) -o "$@"

check: $(TEST)
	"$(TEST)"
