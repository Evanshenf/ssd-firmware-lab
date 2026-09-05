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
ifneq ($(findstring i,$(J0A_ACTIVE_SHORT_FLAGS)),)
$(error check-j0a cannot run with make ignore-errors mode)
endif

override J0B_ACTIVE_SHORT_FLAGS := $(filter-out -%,$(firstword $(MAKEFLAGS)))
ifneq ($(findstring n,$(J0B_ACTIVE_SHORT_FLAGS)),)
$(error check-j0b cannot run with make dry-run mode)
endif
ifneq ($(findstring t,$(J0B_ACTIVE_SHORT_FLAGS)),)
$(error check-j0b cannot run with make touch mode)
endif
ifneq ($(findstring q,$(J0B_ACTIVE_SHORT_FLAGS)),)
$(error check-j0b cannot run with make question mode)
endif
ifneq ($(findstring i,$(J0B_ACTIVE_SHORT_FLAGS)),)
$(error check-j0b cannot run with make ignore-errors mode)
endif

CPPFLAGS += -I../../include -I../../core/m3p -I../../media/file-nand-v0 \
	-I../../core/command-spine
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -fno-common
CFLAGS ?= $(BASE_CFLAGS)

include j0.mk

.PHONY: all check-j0a j0a-authority-guard check-j0a-owned check-j0b \
	j0b-authority-guard check-j0b-owned clean clean-j0a-owned \
	clean-j0b-owned

.INTERMEDIATE: $(J0A_INTERMEDIATES) $(J0B_INTERMEDIATES)

all: check-j0a

# Expand these prerequisites after every makefile has been read, before any
# shell is launched. A target-specific shell cannot suppress a Make error.
.SECONDEXPANSION:
j0a-authority-guard: $$(if $$(filter 2,$$(words $$(MAKEFILE_LIST))),,$$(error J0-A makefile count differs)) \
	$$(if $$(filter Makefile,$$(word 1,$$(MAKEFILE_LIST))),,$$(error J0-A primary makefile differs)) \
	$$(if $$(filter j0.mk,$$(word 2,$$(MAKEFILE_LIST))),,$$(error J0-A manifest differs)) \
	$$(if $$(filter file,$$(origin MAKEFILE_LIST)),,$$(error J0-A makefile list origin differs)) \
	$$(if $$(findstring --eval,$$(MAKEFLAGS)),$$(error J0-A make eval is not supported))

j0b-authority-guard: $$(if $$(filter 2,$$(words $$(MAKEFILE_LIST))),,$$(error J0-B makefile count differs)) \
	$$(if $$(filter Makefile,$$(word 1,$$(MAKEFILE_LIST))),,$$(error J0-B primary makefile differs)) \
	$$(if $$(filter j0.mk,$$(word 2,$$(MAKEFILE_LIST))),,$$(error J0-B manifest differs)) \
	$$(if $$(filter file,$$(origin MAKEFILE_LIST)),,$$(error J0-B makefile list origin differs)) \
	$$(if $$(findstring --eval,$$(MAKEFLAGS)),$$(error J0-B make eval is not supported))

check-j0a: j0a-authority-guard
	$(MAKE) --no-print-directory clean-j0a-owned
	$(MAKE) --no-print-directory check-j0a-owned

check-j0b: j0b-authority-guard
	$(MAKE) --no-print-directory clean-j0a-owned clean-j0b-owned
	$(MAKE) --no-print-directory check-j0b-owned

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

$(J0B_BUILD_DIR):
	mkdir -p $@

$(J0B_BUILD_DIR)/m3p_%.o: ../../core/m3p/m3p_%.c \
		../../core/m3p/m3p.h ../../core/m3p/m3p_internal.h j0.mk \
		| $(J0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(J0B_BUILD_DIR)/nfc_%.o: ../../nfc/nfc_%.c ../../nfc/nfc_internal.h \
		j0.mk | $(J0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I../../nfc -c -o $@ $<

$(J0B_BUILD_DIR)/file_nand_%.o: \
		../../media/file-nand-v0/file_nand_%.c \
		../../media/file-nand-v0/file_nand.h \
		../../media/file-nand-v0/file_nand_internal.h j0.mk \
		| $(J0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(J0B_BUILD_DIR)/spine_%.o: ../../core/command-spine/spine_%.c \
		../../core/command-spine/spine_internal.h j0.mk \
		| $(J0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(J0B_BUILD_DIR)/c43_p1_adapter.o: \
		../../core/command-spine/profiles/c43_p1_adapter.c \
		../../core/command-spine/spine_internal.h j0.mk \
		| $(J0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(J0B_BUILD_DIR)/linux_profile_v1_adapter.o: \
		../../core/command-spine/profiles/linux_profile_v1_adapter.c \
		../../core/command-spine/spine_internal.h j0.mk \
		| $(J0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(J0B_C41_OBJECT): $(J0B_C41_SOURCE) j0.mk | $(J0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(J0B_BUILD_DIR)/j0_%.o: j0_%.c j0_internal.h j0.mk \
		| $(J0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(J0B_TEST_OBJECT): $(J0B_TEST_SOURCE) j0_internal.h j0.mk \
		../../core/m3p/m3p_internal.h \
		| $(J0B_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(J0B_M3P_ARCHIVE): $(J0B_M3P_OBJECTS)
	rm -f -- $@
	$(AR) rcs $@ $(J0B_M3P_OBJECTS)

$(J0B_NFC_ARCHIVE): $(J0B_NFC_OBJECTS)
	rm -f -- $@
	$(AR) rcs $@ $(J0B_NFC_OBJECTS)

$(J0B_FILE_ARCHIVE): $(J0B_FILE_OBJECTS)
	rm -f -- $@
	$(AR) rcs $@ $(J0B_FILE_OBJECTS)

$(J0B_SPINE_ARCHIVE): $(J0B_SPINE_OBJECTS)
	rm -f -- $@
	$(AR) rcs $@ $(J0B_SPINE_OBJECTS)

$(J0B_PROFILE_ARCHIVE): $(J0B_PROFILE_OBJECTS)
	rm -f -- $@
	$(AR) rcs $@ $(J0B_PROFILE_OBJECTS)

$(J0B_MATRIX): $(J0B_TEST_OBJECT) $(J0B_HEADLESS_OBJECTS) \
		$(J0B_C41_OBJECT) $(J0B_M3P_ARCHIVE) $(J0B_NFC_ARCHIVE) \
		$(J0B_FILE_ARCHIVE) $(J0B_SPINE_ARCHIVE) $(J0B_PROFILE_ARCHIVE)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(J0B_TEST_OBJECT) $(J0B_HEADLESS_OBJECTS) $(J0B_C41_OBJECT) \
		-Wl,--start-group $(J0B_M3P_ARCHIVE) $(J0B_NFC_ARCHIVE) \
		$(J0B_FILE_ARCHIVE) $(J0B_SPINE_ARCHIVE) \
		$(J0B_PROFILE_ARCHIVE) -Wl,--end-group

check-j0b-owned: $(J0B_MATRIX)
	@set -eu; \
	lifecycle_sha=$$(sha256sum $(J0B_SPINE_ARCHIVE) | cut -d' ' -f1); \
	host_sha=$$(sha256sum build/j0b/j0_host_data.o | cut -d' ' -f1); \
	m3p_sha=$$(sha256sum $(J0B_M3P_ARCHIVE) | cut -d' ' -f1); \
	nfc_sha=$$(sha256sum $(J0B_NFC_ARCHIVE) | cut -d' ' -f1); \
	file_sha=$$(sha256sum $(J0B_FILE_ARCHIVE) | cut -d' ' -f1); \
	elf_sha=$$(sha256sum $(J0B_MATRIX) | cut -d' ' -f1); \
	if ! receipts=$$($(J0B_MATRIX) --lifecycle-sha "$$lifecycle_sha" \
		--host-sha "$$host_sha" --m3p-sha "$$m3p_sha" \
		--nfc-sha "$$nfc_sha" --file-sha "$$file_sha" \
		--elf-sha "$$elf_sha"); then \
		echo "J0-B matrix execution failed" >&2; \
		exit 1; \
	fi; \
	printf '%s\n' "$$receipts"; \
	printf '%s\n' "$$receipts" | python3 \
		../../scripts/check_spine_architecture.py --phase j0b \
		--lifecycle $(J0B_SPINE_ARCHIVE) \
		--profiles $(J0B_PROFILE_ARCHIVE) \
		--m3p $(J0B_M3P_ARCHIVE) --nfc $(J0B_NFC_ARCHIVE) \
		--file $(J0B_FILE_ARCHIVE) --matrix $(J0B_MATRIX) --receipts -

clean-j0a-owned:
	rm -f -- $(J0A_OWNED)
	@rmdir $(J0A_BUILD_DIR) build 2>/dev/null || true

clean-j0b-owned:
	rm -f -- $(J0B_OWNED)
	@rmdir $(J0B_BUILD_DIR) build 2>/dev/null || true

clean: clean-j0a-owned clean-j0b-owned
