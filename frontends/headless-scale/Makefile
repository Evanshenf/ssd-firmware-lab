# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CPPFLAGS += -I../../include -I../../core/m3p -I../../core/command-spine \
	-I../../media/file-nand-v0 -I../../media/file-nand-v1 -I../../nfc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -fno-common
BUILD := build/scale-media

# SCALE-A integration harness; all firmware execution uses production sources.
SOURCES := \
	core/command-spine/spine_contracts.c \
	core/command-spine/spine_lifecycle.c \
	core/command-spine/profiles/c43_p1_adapter.c \
	core/command-spine/profiles/linux_profile_v1_adapter.c \
	core/c4-nvme/c41_codec.c \
	core/m3p/m3p_codec.c \
	core/m3p/m3p_mapping.c \
	core/m3p/m3p_nfc.c \
	core/m3p/m3p_gc.c \
	core/m3p/m3p_recovery.c \
	core/m3p/m3p_runtime.c \
	core/nfc-runtime/nfc_trace_window.c \
	nfc/nfc_model.c \
	nfc/nfc_scheduler.c \
	nfc/nfc_fault.c \
	nfc/nfc_media.c \
	media/file-nand-v0/file_nand_codec.c \
	media/file-nand-v0/file_nand_engine.c \
	media/file-nand-v0/file_nand_media.c \
	media/file-nand-v0/file_nand_posix.c \
	media/file-nand-v1/compact_nand.c \
	media/file-nand-v1/compact_nand_posix.c \
	frontends/headless-j0/j0_controller_buffer.c \
	frontends/headless-j0/j0_host_data.c \
	frontends/headless-j0/j0_action_drivers.c \
	frontends/headless-j0/j0_construction.c \
	frontends/headless-scale/test_media.c
OBJECTS := $(addprefix $(BUILD)/,$(SOURCES:.c=.o))
PROGRAM := $(BUILD)/test_scale_media

.PHONY: all check
all: $(PROGRAM)

check: $(PROGRAM)
	$(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(BUILD)/%.o: ../../%.c Makefile
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

-include $(OBJECTS:.o=.d)
