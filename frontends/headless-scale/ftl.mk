# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause
CC ?= cc
CPPFLAGS += -I../../include -I../../core/ftl-scale -I../../core/m3p \
	-I../../core/command-spine -I../../media/file-nand-v0 \
	-I../../media/file-nand-v1 -I../../media/file-nand-v2 -I../../nfc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -fno-common
FWLAB_CRC_NATIVE ?= 0
ifeq ($(FWLAB_CRC_NATIVE),1)
override CFLAGS += -march=native
BUILD ?= build/scale-ftl-native
else
ifneq ($(FWLAB_CRC_NATIVE),0)
$(error FWLAB_CRC_NATIVE must be 0 or 1)
endif
BUILD ?= build/scale-ftl
endif
FWLAB_TEST_MEDIA_DIR ?= /run/fwlab-test-media
FWLAB_MEDIA_EXCLUSIVE ?= 0
ifneq ($(FWLAB_MEDIA_EXCLUSIVE),0)
ifneq ($(FWLAB_MEDIA_EXCLUSIVE),1)
$(error FWLAB_MEDIA_EXCLUSIVE must be 0 or 1)
endif
endif
override CPPFLAGS += -DFWLAB_MEDIA_EXCLUSIVE=$(FWLAB_MEDIA_EXCLUSIVE)
MEDIA_CONFIG := $(BUILD)/.fwlab-media-config
# Preserve shell quoting in flag values as literal stamp data (including -D strings).
config_quote = '$(subst ','"'"',$(1))'
BUILD_CONFIG_VALUES = $(call config_quote,CC=$(CC)) \
	$(call config_quote,CPPFLAGS=$(CPPFLAGS)) \
	$(call config_quote,CFLAGS=$(CFLAGS)) \
	$(call config_quote,LDFLAGS=$(LDFLAGS)) \
	$(call config_quote,LDLIBS=$(LDLIBS)) \
	$(call config_quote,FWLAB_MEDIA_EXCLUSIVE=$(FWLAB_MEDIA_EXCLUSIVE)) \
	$(call config_quote,FWLAB_CRC_NATIVE=$(FWLAB_CRC_NATIVE))
export FWLAB_TEST_MEDIA_DIR
SOURCES := \
	core/command-spine/spine_contracts.c \
	core/command-spine/spine_lifecycle.c \
	core/command-spine/profiles/c43_p1_adapter.c \
	core/command-spine/profiles/linux_profile_v1_adapter.c \
	core/c4-nvme/c41_codec.c \
	core/m3p/m3p_codec.c core/m3p/m3p_mapping.c core/m3p/m3p_nfc.c \
	core/m3p/m3p_gc.c core/m3p/m3p_recovery.c core/m3p/m3p_runtime.c \
	core/ftl-scale/ftl_scale_codec.c core/ftl-scale/ftl_scale_recovery.c \
	core/ftl-scale/ftl_scale_mapping.c core/ftl-scale/ftl_scale_gc.c \
	core/ftl-scale/ftl_scale_heads.c core/ftl-scale/ftl_scale_write.c \
	core/ftl-scale/ftl_scale_runtime.c core/ftl-scale/ftl_scale_parent.c \
	core/ftl-scale/ftl_scale_nfc.c core/ftl-scale/ftl_scale_nfc_v2.c \
	core/ftl-scale/ftl_scale_window.c core/ftl-scale/ftl_scale_read.c \
	core/nfc-page-v2/nfc_page_v2.c core/nfc-page-v2/nfc_page_v2_lab.c \
	core/nfc-page-v2/nfc_channel_v2.c core/nfc-page-v2/nfc_channel_v2_actor.c \
	core/nfc-runtime/nfc_trace_window.c core/nfc-runtime/nfc_scaled_model.c \
	nfc/nfc_model.c nfc/nfc_scheduler.c nfc/nfc_fault.c nfc/nfc_media.c \
	media/file-nand-v0/file_nand_codec.c media/file-nand-v0/file_nand_engine.c \
	media/file-nand-v0/file_nand_media.c media/file-nand-v0/file_nand_posix.c \
	media/file-nand-v1/compact_nand.c media/file-nand-v1/compact_nand_posix.c \
	media/file-nand-v2/physical_nand.c media/file-nand-v2/physical_nand_codec.c \
	media/file-nand-v2/physical_nand_posix.c media/file-nand-v2/physical_nand_batch.c \
	media/file-nand-v2/channel_volume.c \
	frontends/headless-j0/j0_controller_buffer.c \
	frontends/headless-j0/j0_host_data.c frontends/headless-j0/j0_action_drivers.c \
	frontends/headless-j0/j0_construction.c \
	frontends/headless-scale/scale_storage.c frontends/headless-scale/test_ftl.c
OBJECTS := $(addprefix $(BUILD)/,$(SOURCES:.c=.o))
PROGRAM := $(BUILD)/test_scale_ftl
PARENT_OBJECTS := $(filter-out $(BUILD)/frontends/headless-scale/test_ftl.o,$(OBJECTS)) \
	$(BUILD)/frontends/headless-scale/test_parent.o
PARENT_PROGRAM := $(BUILD)/test_scale_parent
PARALLEL_READ_OBJECTS := $(filter-out $(BUILD)/frontends/headless-scale/test_ftl.o,$(OBJECTS)) \
	$(BUILD)/frontends/headless-scale/test_parallel_read_j0.o
PARALLEL_READ_PROGRAM := $(BUILD)/test_parallel_read_j0
MUTATION_J0_OBJECTS := $(filter-out $(BUILD)/frontends/headless-scale/test_ftl.o,$(OBJECTS)) \
	$(BUILD)/frontends/headless-scale/test_mutation_j0.o
MUTATION_J0_PROGRAM := $(BUILD)/test_mutation_j0
CHANNEL_J0_OBJECTS := $(filter-out $(BUILD)/frontends/headless-scale/test_ftl.o,$(OBJECTS)) \
	$(BUILD)/frontends/headless-scale/test_channel_j0.o
CHANNEL_J0_PROGRAM := $(BUILD)/test_channel_j0
MULTIHEAD_J0_OBJECTS := $(filter-out $(BUILD)/frontends/headless-scale/test_ftl.o,$(OBJECTS)) \
	$(BUILD)/frontends/headless-scale/test_multihead_j0.o
MULTIHEAD_J0_PROGRAM := $(BUILD)/test_multihead_j0
MULTIHEAD_PARENT_OBJECTS := $(filter-out $(BUILD)/frontends/headless-scale/test_parent.o,$(PARENT_OBJECTS)) \
	$(BUILD)/frontends/headless-scale/test_multihead_parent.o
MULTIHEAD_PARENT_PROGRAM := $(BUILD)/test_multihead_parent
CHANNEL_WORKER_OBJECTS := $(filter-out $(BUILD)/frontends/headless-scale/test_ftl.o,$(OBJECTS)) \
	$(BUILD)/frontends/headless-scale/nfc_channel_workers.o \
	$(BUILD)/frontends/headless-scale/test_channel_workers.o
CHANNEL_WORKER_PROGRAM := $(BUILD)/test_channel_workers
CHANNEL_WORKER_J0_OBJECTS := $(filter-out $(BUILD)/frontends/headless-scale/test_channel_workers.o,$(CHANNEL_WORKER_OBJECTS)) \
	$(BUILD)/frontends/headless-scale/test_channel_workers_j0.o
CHANNEL_WORKER_J0_PROGRAM := $(BUILD)/test_channel_workers_j0
PLANE_J0_OBJECTS := $(filter-out $(BUILD)/frontends/headless-scale/test_ftl.o,$(OBJECTS)) \
	$(BUILD)/frontends/headless-scale/test_plane_read_j0.o
PLANE_J0_PROGRAM := $(BUILD)/test_plane_read_j0
CRC_OBJECTS := $(BUILD)/core/ftl-scale/ftl_scale_codec.o \
	$(BUILD)/frontends/headless-scale/test_crc.o
CRC_PROGRAM := $(BUILD)/test_crc
FAST_CRC_PROGRAM := $(BUILD)/test_crc_fast
FAST_CRC_OBJECT := $(BUILD)/frontends/headless-scale/test_crc_fast.o
.PHONY: all check check-crc check-crc-fast check-full check-cuts check-cost check-parent check-parent-window-v2 check-parent-window-v2-operation check-parent-window-v2-mapped check-media-v2 check-media-v2-cuts check-media-v2-cost check-window-v2 check-window-v2-operation check-window-v2-mapped check-window-v2-cost plan-64g check-64g
.PHONY: check-full-window-v2-mapped plan-64g-reference-v1 check-64g-reference-v1 check-parallel-read-j0
.PHONY: check-mutation-j0
.PHONY: check-channel-j0
.PHONY: check-multihead-j0
.PHONY: check-multihead-parent
.PHONY: check-channel-workers
.PHONY: check-channel-workers-j0
.PHONY: check-plane-read-j0
all: $(PROGRAM)
.PHONY: FORCE_MEDIA_CONFIG
FORCE_MEDIA_CONFIG:
$(MEDIA_CONFIG): FORCE_MEDIA_CONFIG
	@mkdir -p "$(BUILD)"
	@printf '%s\n' $(BUILD_CONFIG_VALUES) | cmp -s - "$@" || \
		printf '%s\n' $(BUILD_CONFIG_VALUES) > "$@"
check: check-crc check-crc-fast $(PROGRAM)
	$(PROGRAM)
check-crc: $(CRC_PROGRAM)
	$(CRC_PROGRAM)
check-crc-fast: $(FAST_CRC_PROGRAM)
	$(FAST_CRC_PROGRAM)
check-full: $(PROGRAM)
	$(PROGRAM) --full
check-cuts: $(PROGRAM)
	$(PROGRAM) --cuts
check-cost: $(PROGRAM)
	$(PROGRAM) --cost
check-parent: $(PARENT_PROGRAM)
	$(PARENT_PROGRAM)
check-parallel-read-j0: $(PARALLEL_READ_PROGRAM)
	$(PARALLEL_READ_PROGRAM)
check-mutation-j0: $(MUTATION_J0_PROGRAM)
	$(MUTATION_J0_PROGRAM)
check-channel-j0: $(CHANNEL_J0_PROGRAM)
	$(CHANNEL_J0_PROGRAM)
check-multihead-j0: $(MULTIHEAD_J0_PROGRAM)
	$(MULTIHEAD_J0_PROGRAM)
check-multihead-parent: $(MULTIHEAD_PARENT_PROGRAM)
	$(MULTIHEAD_PARENT_PROGRAM)
check-channel-workers: $(CHANNEL_WORKER_PROGRAM)
	$(CHANNEL_WORKER_PROGRAM)
check-channel-workers-j0: $(CHANNEL_WORKER_J0_PROGRAM)
	$(CHANNEL_WORKER_J0_PROGRAM)
check-plane-read-j0: $(PLANE_J0_PROGRAM)
	$(PLANE_J0_PROGRAM)

$(PLANE_J0_PROGRAM): $(PLANE_J0_OBJECTS)
	$(CC) $(CFLAGS) $(PLANE_J0_OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

$(CHANNEL_WORKER_J0_PROGRAM): $(CHANNEL_WORKER_J0_OBJECTS)
	$(CC) $(CFLAGS) $(CHANNEL_WORKER_J0_OBJECTS) $(LDFLAGS) $(LDLIBS) -pthread \
		-Wl,--wrap=fwlab_ftl_scale_step -o $@

$(CHANNEL_WORKER_PROGRAM): $(CHANNEL_WORKER_OBJECTS)
	$(CC) $(CFLAGS) $(CHANNEL_WORKER_OBJECTS) $(LDFLAGS) $(LDLIBS) -pthread \
		-Wl,--wrap=fwlab_nfc_channel_v2_init -Wl,--wrap=fwlab_ftl_scale_step \
		-Wl,--wrap=fwlab_file_nand_v2_program_pages -Wl,--wrap=pthread_create -Wl,--wrap=pthread_join -o $@

$(BUILD)/frontends/headless-scale/nfc_channel_workers.o \
$(BUILD)/frontends/headless-scale/test_channel_workers.o \
$(BUILD)/frontends/headless-scale/test_channel_workers_j0.o: override CFLAGS += -pthread
$(BUILD)/frontends/headless-scale/test_channel_workers_j0.o: \
	../../frontends/headless-scale/test_channel_j0.c ../../frontends/headless-scale/test_ftl.c
$(BUILD)/frontends/headless-scale/test_channel_workers.o: \
	../../frontends/headless-scale/test_multihead_parent.c \
	../../frontends/headless-scale/test_parent.c ../../frontends/headless-scale/parent_window.inc

$(MULTIHEAD_PARENT_PROGRAM): $(MULTIHEAD_PARENT_OBJECTS)
	$(CC) $(CFLAGS) $(MULTIHEAD_PARENT_OBJECTS) $(LDFLAGS) $(LDLIBS) \
		-Wl,--wrap=fwlab_file_nand_v2_program_pages -o $@

$(BUILD)/frontends/headless-scale/test_multihead_parent.o: \
	../../frontends/headless-scale/test_parent.c ../../frontends/headless-scale/parent_window.inc

$(MULTIHEAD_J0_PROGRAM): $(MULTIHEAD_J0_OBJECTS)
	$(CC) $(CFLAGS) $(MULTIHEAD_J0_OBJECTS) $(LDFLAGS) $(LDLIBS) \
		-Wl,--wrap=fwlab_nfc_channel_v2_provider -Wl,--wrap=fwlab_file_nand_v2_program_pages -o $@

$(CHANNEL_J0_PROGRAM): $(CHANNEL_J0_OBJECTS)
	$(CC) $(CFLAGS) $(CHANNEL_J0_OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

$(PARALLEL_READ_PROGRAM): $(PARALLEL_READ_OBJECTS)
	$(CC) $(CFLAGS) $(PARALLEL_READ_OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@
$(MUTATION_J0_PROGRAM): $(MUTATION_J0_OBJECTS)
	$(CC) $(CFLAGS) $(MUTATION_J0_OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@
check-parent-window-v2: $(PARENT_PROGRAM)
	$(PARENT_PROGRAM) --window-v2
check-parent-window-v2-operation: $(PARENT_PROGRAM)
	$(PARENT_PROGRAM) --window-v2-operation
check-parent-window-v2-mapped: $(PARENT_PROGRAM)
	$(PARENT_PROGRAM) --window-v2-mapped
check-media-v2: $(PROGRAM)
	$(PROGRAM) --media-v2
check-media-v2-cuts: $(PROGRAM)
	$(PROGRAM) --media-v2-cuts
check-media-v2-cost: $(PROGRAM)
	$(PROGRAM) --media-v2-cost
check-window-v2: $(PROGRAM)
	$(PROGRAM) --window-v2
check-window-v2-operation: $(PROGRAM)
	$(PROGRAM) --window-v2-operation
check-window-v2-mapped: $(PROGRAM)
	$(PROGRAM) --window-v2-mapped
check-full-window-v2-mapped: $(PROGRAM)
	$(PROGRAM) --full-window-v2-mapped
check-window-v2-cost: $(PROGRAM)
	$(PROGRAM) --window-v2-cost
plan-64g: $(PROGRAM)
	$(PROGRAM) --plan-64g
check-64g: $(PROGRAM)
	$(PROGRAM) --full-64g
# Historical C3/compact-v1 campaign; never selected by the current large entry.
plan-64g-reference-v1: $(PROGRAM)
	$(PROGRAM) --plan-64g-reference-v1
check-64g-reference-v1: $(PROGRAM)
	$(PROGRAM) --full-64g-reference-v1
$(PROGRAM): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)
$(CRC_PROGRAM): $(CRC_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(CRC_OBJECTS) $(LDLIBS)
$(FAST_CRC_PROGRAM): $(FAST_CRC_OBJECT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(FAST_CRC_OBJECT) $(LDLIBS)
$(PARENT_PROGRAM): $(PARENT_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(PARENT_OBJECTS) $(LDLIBS)
$(BUILD)/%.o: ../../%.c ftl.mk $(MEDIA_CONFIG)
	mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<
-include $(OBJECTS:.o=.d) $(CRC_OBJECTS:.o=.d) $(PARENT_OBJECTS:.o=.d) $(FAST_CRC_OBJECT:.o=.d) \
	$(PARALLEL_READ_OBJECTS:.o=.d) $(MUTATION_J0_OBJECTS:.o=.d) $(CHANNEL_WORKER_OBJECTS:.o=.d) \
	$(CHANNEL_WORKER_J0_OBJECTS:.o=.d) $(PLANE_J0_OBJECTS:.o=.d)
