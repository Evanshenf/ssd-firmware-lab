# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CLANG ?= clang
AR ?= ar
override SHELL := /bin/sh

override J0A_ACTIVE_SHORT_FLAGS := $(filter-out -%,$(firstword $(MAKEFLAGS)))
ifneq ($(findstring n,$(J0A_ACTIVE_SHORT_FLAGS)),)
$(error check-j0a cannot run with make dry-run mode)
endif
ifneq ($(findstring t,$(J0A_ACTIVE_SHORT_FLAGS)),)
$(error check-j0a cannot run with make touch mode)
endif
ifneq ($(findstring q,$(J0A_ACTIVE_SHORT_FLAGS)),)
$(error check-j0a cannot run with make question mode)
endif

CPPFLAGS += -I../../include -I../../core/m3p -I../../media/file-nand-v0
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -fno-common
CFLAGS ?= $(BASE_CFLAGS)

include j0.mk

.PHONY: all check-j0a j0a-authority-guard check-j0a-owned clean \
	clean-j0a-owned

.INTERMEDIATE: $(J0A_INTERMEDIATES)

all: check-j0a

check-j0a: j0a-authority-guard
	$(MAKE) --no-print-directory clean-j0a-owned
	$(MAKE) --no-print-directory check-j0a-owned

j0a-authority-guard:
	@set -eu; \
	if test "$(words $(MAKEFILE_LIST))" -ne 2 || \
		 test "$(word 1,$(MAKEFILE_LIST))" != Makefile || \
		 test "$(word 2,$(MAKEFILE_LIST))" != j0.mk; then \
		echo "J0-A makefile authority differs: $(MAKEFILE_LIST)" >&2; \
		exit 1; \
	fi

$(J0A_BUILD_DIR):
	mkdir -p $@

$(J0A_BUILD_DIR)/m3p_%.o: ../../core/m3p/m3p_%.c \
		../../core/m3p/m3p.h ../../core/m3p/m3p_internal.h j0.mk \
		| $(J0A_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(J0A_BUILD_DIR)/nfc_%.o: ../../nfc/nfc_%.c ../../nfc/nfc_internal.h \
		j0.mk | $(J0A_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I../../nfc -c -o $@ $<

$(J0A_BUILD_DIR)/file_nand_%.o: \
		../../media/file-nand-v0/file_nand_%.c \
		../../media/file-nand-v0/file_nand.h \
		../../media/file-nand-v0/file_nand_internal.h j0.mk \
		| $(J0A_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(J0A_FAKE_OBJECT): $(J0A_FAKE_SOURCE) \
		../../core/m3p/fakes/m3p_fake_adjacent.h j0.mk \
		| $(J0A_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(J0A_TEST_OBJECT): $(J0A_TEST_SOURCE) \
		../../core/m3p/fakes/m3p_fake_adjacent.h j0.mk \
		| $(J0A_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(J0A_M3P_ARCHIVE): $(J0A_M3P_OBJECTS)
	rm -f -- $@
	$(AR) rcs $@ $(J0A_M3P_OBJECTS)

$(J0A_NFC_ARCHIVE): $(J0A_NFC_OBJECTS)
	rm -f -- $@
	$(AR) rcs $@ $(J0A_NFC_OBJECTS)

$(J0A_FILE_ARCHIVE): $(J0A_FILE_OBJECTS)
	rm -f -- $@
	$(AR) rcs $@ $(J0A_FILE_OBJECTS)

$(J0A_MATRIX): $(J0A_TEST_OBJECT) $(J0A_FAKE_OBJECT) \
		$(J0A_M3P_ARCHIVE) $(J0A_NFC_ARCHIVE) $(J0A_FILE_ARCHIVE)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(J0A_TEST_OBJECT) $(J0A_FAKE_OBJECT) \
		-Wl,--start-group $(J0A_M3P_ARCHIVE) $(J0A_NFC_ARCHIVE) \
		$(J0A_FILE_ARCHIVE) -Wl,--end-group

check-j0a-owned: $(J0A_MATRIX)
	@set -eu; \
	m3p_sha=$$(sha256sum $(J0A_M3P_ARCHIVE) | cut -d' ' -f1); \
	nfc_sha=$$(sha256sum $(J0A_NFC_ARCHIVE) | cut -d' ' -f1); \
	file_sha=$$(sha256sum $(J0A_FILE_ARCHIVE) | cut -d' ' -f1); \
	elf_sha=$$(sha256sum $(J0A_MATRIX) | cut -d' ' -f1); \
	if ! receipts=$$($(J0A_MATRIX) --m3p-sha "$$m3p_sha" \
		--nfc-sha "$$nfc_sha" --file-sha "$$file_sha" \
		--elf-sha "$$elf_sha"); then \
		echo "J0-A matrix execution failed" >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$receipts"; \
	printf '%s\n' "$$receipts" | python3 \
		../../scripts/check_spine_architecture.py --phase j0a \
		--m3p $(J0A_M3P_ARCHIVE) --nfc $(J0A_NFC_ARCHIVE) \
		--file $(J0A_FILE_ARCHIVE) --matrix $(J0A_MATRIX) --receipts -

clean-j0a-owned:
	rm -f -- $(J0A_OWNED)
	@rmdir $(J0A_BUILD_DIR) build 2>/dev/null || true

clean: clean-j0a-owned
