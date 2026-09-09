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

TEST := $(BUILD_DIR)/physical_nand_test
OPERATION_TEST := $(BUILD_DIR)/posix_operation_test
FWLAB_TEST_MEDIA_DIR ?= /run/fwlab-test-media
export FWLAB_TEST_MEDIA_DIR
SOURCES := physical_nand.c physical_nand_codec.c physical_nand_batch.c tests/test_physical_nand.c
OPERATION_SOURCES := physical_nand.c physical_nand_codec.c physical_nand_batch.c \
physical_nand_posix.c ../../core/nfc-page-v2/nfc_page_v2.c ../../frontends/headless-scale/test_posix_operation.c
HEADERS := physical_nand.h physical_nand_internal.h physical_nand_codec.h physical_nand_batch.h \
	../../include/fwlab/private/nand_batch_v2.h \
	../../include/fwlab/contracts/nand_media.h \
	../../include/fwlab/portable/nfc_types.h ../../include/fwlab/portable/crc32c.h \
	../../include/fwlab/portable/crc32c_fast.h ../../include/fwlab/portable/crc32c_shift1024.h

.PHONY: all check check-operation check-mapped FORCE_MEDIA_CONFIG
all: $(TEST)

FORCE_MEDIA_CONFIG:

$(MEDIA_CONFIG): FORCE_MEDIA_CONFIG
	@mkdir -p "$(BUILD_DIR)"
	@printf 'FWLAB_MEDIA_EXCLUSIVE=%s\n' '$(FWLAB_MEDIA_EXCLUSIVE)' | cmp -s - "$@" || \
		printf 'FWLAB_MEDIA_EXCLUSIVE=%s\n' '$(FWLAB_MEDIA_EXCLUSIVE)' > "$@"

$(TEST): $(SOURCES) $(HEADERS) Makefile $(MEDIA_CONFIG)
	mkdir -p "$(BUILD_DIR)"
	$(CC) -I../../include -I. $(CPPFLAGS) $(CFLAGS) $(SOURCES) \
		$(LDFLAGS) $(LDLIBS) -o "$@"

check: $(TEST)
	"$(TEST)"

$(OPERATION_TEST): $(OPERATION_SOURCES) $(HEADERS) Makefile $(MEDIA_CONFIG) \
	../../include/fwlab/contracts/nfc_page_v2_types.h \
	../../include/fwlab/contracts/nfc_page_v2_provider.h \
	../../include/fwlab/private/nfc_page_v2_model.h
	mkdir -p "$(BUILD_DIR)"
	$(CC) -I../../include -I. $(CPPFLAGS) $(CFLAGS) $(OPERATION_SOURCES) \
		-Wl,--wrap=fallocate64,--wrap=mmap64,--wrap=madvise,--wrap=munmap,--wrap=close \
		$(LDFLAGS) $(LDLIBS) -o "$@"

check-operation: $(OPERATION_TEST)
	"$(OPERATION_TEST)"

check-mapped: $(OPERATION_TEST)
	"$(OPERATION_TEST)" --mapped
