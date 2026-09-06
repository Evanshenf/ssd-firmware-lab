# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
BUILD_DIR ?= build
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=

TEST := $(BUILD_DIR)/physical_nand_test
SOURCES := physical_nand.c physical_nand_codec.c tests/test_physical_nand.c
HEADERS := physical_nand.h physical_nand_internal.h physical_nand_codec.h \
	../../include/fwlab/contracts/nand_media.h \
	../../include/fwlab/portable/nfc_types.h ../../include/fwlab/portable/crc32c.h

.PHONY: all check
all: $(TEST)

$(TEST): $(SOURCES) $(HEADERS) Makefile
	mkdir -p "$(BUILD_DIR)"
	$(CC) -I../../include -I. $(CPPFLAGS) $(CFLAGS) $(SOURCES) \
		$(LDFLAGS) $(LDLIBS) -o "$@"

check: $(TEST)
	"$(TEST)"
