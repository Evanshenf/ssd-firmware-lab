# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause
CC ?= cc
BUILD_DIR ?= build
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=
BUILD_CONFIG := $(BUILD_DIR)/.fwlab-build-config
# Preserve shell quoting in flag values as literal stamp data (including -D strings).
config_quote = '$(subst ','"'"',$(1))'
BUILD_CONFIG_VALUES = $(call config_quote,CC=$(CC)) \
	$(call config_quote,CPPFLAGS=$(CPPFLAGS)) \
	$(call config_quote,CFLAGS=$(CFLAGS)) \
	$(call config_quote,LDFLAGS=$(LDFLAGS)) \
	$(call config_quote,LDLIBS=$(LDLIBS))
PROGRAM := $(BUILD_DIR)/test_nfc_page_v2
LAB_PROGRAM := $(BUILD_DIR)/test_nfc_page_v2_lab
MUTATION_PROGRAM := $(BUILD_DIR)/test_nfc_page_v2_mutation
SOURCES := nfc_page_v2.c tests/test_nfc_page_v2.c
LAB_SOURCES := nfc_page_v2.c nfc_page_v2_lab.c tests/test_nfc_page_v2_lab.c
MUTATION_SOURCES := nfc_page_v2.c nfc_page_v2_lab.c tests/test_nfc_page_v2_mutation.c
HEADERS := nfc_page_v2_internal.h ../../include/fwlab/contracts/nfc_page_v2_types.h \
    ../../include/fwlab/contracts/nfc_page_v2_provider.h \
    ../../include/fwlab/private/nfc_page_v2_model.h \
    ../../include/fwlab/private/nand_batch_v2.h \
    ../../include/fwlab/contracts/nand_media.h \
    ../../include/fwlab/portable/nfc_types.h
.PHONY: all check FORCE_BUILD_CONFIG
all: $(PROGRAM) $(LAB_PROGRAM) $(MUTATION_PROGRAM)
FORCE_BUILD_CONFIG:
$(BUILD_CONFIG): FORCE_BUILD_CONFIG
	@mkdir -p "$(BUILD_DIR)"
	@printf '%s\n' $(BUILD_CONFIG_VALUES) | cmp -s - "$@" || \
		printf '%s\n' $(BUILD_CONFIG_VALUES) > "$@"
$(PROGRAM): $(SOURCES) $(HEADERS) Makefile $(BUILD_CONFIG)
	mkdir -p "$(BUILD_DIR)"
	$(CC) -I../../include $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) $(LDLIBS) -o "$@"
$(LAB_PROGRAM): $(LAB_SOURCES) $(HEADERS) ../../include/fwlab/private/nfc_page_v2_lab.h Makefile $(BUILD_CONFIG)
	mkdir -p "$(BUILD_DIR)"
	$(CC) -I../../include $(CPPFLAGS) $(CFLAGS) $(LAB_SOURCES) $(LDFLAGS) $(LDLIBS) -o "$@"
$(MUTATION_PROGRAM): $(MUTATION_SOURCES) tests/test_nfc_page_v2_lab.c $(HEADERS) ../../include/fwlab/private/nfc_page_v2_lab.h Makefile $(BUILD_CONFIG)
	mkdir -p "$(BUILD_DIR)"
	$(CC) -I../../include $(CPPFLAGS) $(CFLAGS) $(MUTATION_SOURCES) $(LDFLAGS) $(LDLIBS) -o "$@"
.PHONY: check-mutation
check-mutation: $(MUTATION_PROGRAM)
	"$(MUTATION_PROGRAM)"
check: $(PROGRAM) $(LAB_PROGRAM) $(MUTATION_PROGRAM)
	"$(PROGRAM)"
	"$(LAB_PROGRAM)"
	"$(MUTATION_PROGRAM)"
