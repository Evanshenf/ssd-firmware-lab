# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
BUILD_DIR ?= build
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=
FWLAB_MEDIA_EXCLUSIVE ?= 0
ifneq ($(FWLAB_MEDIA_EXCLUSIVE),0)
ifneq ($(FWLAB_MEDIA_EXCLUSIVE),1)
$(error FWLAB_MEDIA_EXCLUSIVE must be 0 or 1)
endif
endif
override CPPFLAGS += -DFWLAB_MEDIA_EXCLUSIVE=$(FWLAB_MEDIA_EXCLUSIVE)
MEDIA_CONFIG := $(BUILD_DIR)/.fwlab-media-config
# Preserve shell quoting in flag values as literal stamp data (including -D strings).
config_quote = '$(subst ','"'"',$(1))'
BUILD_CONFIG_VALUES = $(call config_quote,CC=$(CC)) \
	$(call config_quote,CPPFLAGS=$(CPPFLAGS)) \
	$(call config_quote,CFLAGS=$(CFLAGS)) \
	$(call config_quote,LDFLAGS=$(LDFLAGS)) \
	$(call config_quote,LDLIBS=$(LDLIBS)) \
	$(call config_quote,FWLAB_MEDIA_EXCLUSIVE=$(FWLAB_MEDIA_EXCLUSIVE))

TEST := $(BUILD_DIR)/physical_nand_test
OPERATION_TEST := $(BUILD_DIR)/posix_operation_test
NFC_LAB_TEST := $(BUILD_DIR)/nfc_lab_media_test
NFC_MUTATION_TEST := $(BUILD_DIR)/nfc_lab_mutation_media_test
FWLAB_TEST_MEDIA_DIR ?= /run/fwlab-test-media
export FWLAB_TEST_MEDIA_DIR
SOURCES := physical_nand.c physical_nand_codec.c physical_nand_batch.c tests/test_physical_nand.c
OPERATION_SOURCES := physical_nand.c physical_nand_codec.c physical_nand_batch.c \
physical_nand_posix.c ../../core/nfc-page-v2/nfc_page_v2.c ../../frontends/headless-scale/test_posix_operation.c
NFC_LAB_SOURCES := physical_nand.c physical_nand_codec.c physical_nand_batch.c \
	physical_nand_posix.c ../../core/nfc-page-v2/nfc_page_v2.c \
	../../core/nfc-page-v2/nfc_page_v2_lab.c ../../frontends/headless-scale/test_nfc_lab_media.c
NFC_MUTATION_SOURCES := $(filter-out ../../frontends/headless-scale/test_nfc_lab_media.c,$(NFC_LAB_SOURCES)) \
	../../frontends/headless-scale/test_nfc_lab_mutation_media.c
HEADERS := physical_nand.h physical_nand_internal.h physical_nand_codec.h physical_nand_batch.h \
	../../include/fwlab/private/nand_batch_v2.h \
	../../include/fwlab/contracts/nand_media.h \
	../../include/fwlab/portable/nfc_types.h ../../include/fwlab/portable/crc32c.h \
	../../include/fwlab/portable/crc32c_fast.h ../../include/fwlab/portable/crc32c_shift1024.h

.PHONY: all check check-operation check-mapped check-nfc-lab FORCE_MEDIA_CONFIG
.PHONY: check-nfc-mutation
all: $(TEST)

FORCE_MEDIA_CONFIG:

$(MEDIA_CONFIG): FORCE_MEDIA_CONFIG
	@mkdir -p "$(BUILD_DIR)"
	@printf '%s\n' $(BUILD_CONFIG_VALUES) | cmp -s - "$@" || \
		printf '%s\n' $(BUILD_CONFIG_VALUES) > "$@"

$(TEST): $(SOURCES) $(HEADERS) Makefile $(MEDIA_CONFIG)
	mkdir -p "$(BUILD_DIR)"
	$(CC) -I../../include -I. $(CPPFLAGS) $(CFLAGS) $(SOURCES) \
		$(LDFLAGS) $(LDLIBS) -o "$@"

check: $(TEST)
	"$(TEST)"

$(OPERATION_TEST): $(OPERATION_SOURCES) $(HEADERS) Makefile $(MEDIA_CONFIG) \
	../../include/fwlab/contracts/nfc_page_v2_types.h \
	../../include/fwlab/contracts/nfc_page_v2_provider.h \
	../../include/fwlab/private/nfc_page_v2_model.h \
	../../core/nfc-page-v2/nfc_page_v2_internal.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) -I../../include -I. $(CPPFLAGS) $(CFLAGS) $(OPERATION_SOURCES) \
		-Wl,--wrap=fallocate64,--wrap=mmap64,--wrap=madvise,--wrap=munmap,--wrap=close \
		$(LDFLAGS) $(LDLIBS) -o "$@"

check-operation: $(OPERATION_TEST)
	"$(OPERATION_TEST)"

check-mapped: $(OPERATION_TEST)
	"$(OPERATION_TEST)" --mapped

$(NFC_LAB_TEST): $(NFC_LAB_SOURCES) $(HEADERS) Makefile $(MEDIA_CONFIG) \
	../../include/fwlab/contracts/nfc_page_v2_types.h \
	../../include/fwlab/contracts/nfc_page_v2_provider.h \
	../../include/fwlab/private/nfc_page_v2_model.h \
	../../include/fwlab/private/nfc_page_v2_lab.h \
	../../core/nfc-page-v2/nfc_page_v2_internal.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) -I../../include -I. $(CPPFLAGS) $(CFLAGS) $(NFC_LAB_SOURCES) \
		$(LDFLAGS) $(LDLIBS) -o "$@"

check-nfc-lab: $(NFC_LAB_TEST)
	"$(NFC_LAB_TEST)"

$(NFC_MUTATION_TEST): $(NFC_MUTATION_SOURCES) $(HEADERS) Makefile $(MEDIA_CONFIG) \
	../../include/fwlab/contracts/nfc_page_v2_types.h \
	../../include/fwlab/contracts/nfc_page_v2_provider.h \
	../../include/fwlab/private/nfc_page_v2_model.h \
	../../include/fwlab/private/nfc_page_v2_lab.h \
	../../core/nfc-page-v2/nfc_page_v2_internal.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) -I../../include -I. $(CPPFLAGS) $(CFLAGS) $(NFC_MUTATION_SOURCES) \
		$(LDFLAGS) $(LDLIBS) -o "$@"

check-nfc-mutation: $(NFC_MUTATION_TEST)
	"$(NFC_MUTATION_TEST)"
