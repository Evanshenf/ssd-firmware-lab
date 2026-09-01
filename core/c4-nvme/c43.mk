# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

AR ?= ar

override C43_ALLOWED_BUILD_DIRS := build build/clang build/sanitize build/thread
ifneq ($(words $(BUILD_DIR)),1)
$(error C43 BUILD_DIR must be exactly one path token)
endif
ifeq ($(filter $(BUILD_DIR),$(C43_ALLOWED_BUILD_DIRS)),)
$(error C43 BUILD_DIR is outside the fixed phase-1 lane set)
endif
ifneq ($(filter /%,$(BUILD_DIR)),)
$(error C43 BUILD_DIR must be relative to core/c4-nvme)
endif
ifneq ($(findstring ..,$(BUILD_DIR)),)
$(error C43 BUILD_DIR must not contain '..')
endif
override C43_BUILD_ROOT := $(abspath build)
override C43_RESOLVED_BUILD_DIR := $(abspath $(BUILD_DIR))
ifeq ($(filter $(C43_BUILD_ROOT) $(C43_BUILD_ROOT)/%,$(C43_RESOLVED_BUILD_DIR)),)
$(error C43 BUILD_DIR must resolve inside core/c4-nvme/build)
endif

override C43_BUILD_DIR := $(BUILD_DIR)/c43
override C43_ARCHIVE := $(C43_BUILD_DIR)/libfwlab_c43.a
override C43_PUBLIC_ABI_BIN := $(C43_BUILD_DIR)/c43_public_abi
override C43_FAKE_OUTPUT := $(C43_BUILD_DIR)/c43_core_fake_link
override C43_RESERVATION_BIN := $(C43_BUILD_DIR)/c43_reservation_unit

override C43_ARCHIVE_SOURCES := c43_instance.c c43_policy.c c43_identify.c \
	c43_graph.c c43_control.c c43_completion.c c43_actions.c
override C43_ARCHIVE_OBJECTS := \
	$(patsubst %.c,$(C43_BUILD_DIR)/%.o,$(C43_ARCHIVE_SOURCES))
override C43_C41_SUPPORT_SOURCES := c41_action.c c41_codec.c c41_profile.c
override C43_C41_SUPPORT_OBJECTS := \
	$(patsubst %.c,$(C43_BUILD_DIR)/%.o,$(C43_C41_SUPPORT_SOURCES))
override C43_PUBLIC_HEADERS := ../../include/fwlab/portable/c4_command_graph.h \
	../../include/fwlab/portable/nvme_policy.h \
	../../include/fwlab/contracts/hif_queue_effect_port.h \
	../../include/fwlab/contracts/hif_target_resolver_port.h \
	../../include/fwlab/contracts/block_action_port.h
override C43_FROZEN_HEADERS := ../../include/fwlab/portable/nvme_types.h \
	../../include/fwlab/portable/nvme_codec.h \
	../../include/fwlab/contracts/hif_action.h \
	../../include/fwlab/contracts/hif_command_port.h
override C43_INTERNAL_HEADERS := c43_internal.h
override C43_FAKE_SOURCES := fakes/c43_fake_services.c
override C43_FAKE_HEADERS := fakes/c43_fake_services.h

.PHONY: c43-archive check-c43-public-abi check-c43-architecture \
	check-c43-reservation check-c43-phase1 check-c43-phase2 check-c43 \
	fake-link-c43 clean-c43

clean: clean-c43

$(C43_BUILD_DIR):
	mkdir -p $@

$(C43_BUILD_DIR)/c43_%.o: c43_%.c c43.mk $(C43_PUBLIC_HEADERS) \
		$(C43_FROZEN_HEADERS) $(C43_INTERNAL_HEADERS) | $(C43_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(C43_BUILD_DIR)/c41_%.o: c41_%.c c43.mk $(C43_FROZEN_HEADERS) | $(C43_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(C43_ARCHIVE): $(C43_ARCHIVE_OBJECTS)
	rm -f $@
	$(AR) rcs $@ $(C43_ARCHIVE_OBJECTS)

c43-archive: $(C43_ARCHIVE)

$(C43_PUBLIC_ABI_BIN): $(C43_ARCHIVE) $(C43_C41_SUPPORT_OBJECTS) \
		tests/test_c43_public_abi.c $(C43_PUBLIC_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/test_c43_public_abi.c $(C43_ARCHIVE) \
		$(C43_C41_SUPPORT_OBJECTS)

check-c43-public-abi: $(C43_PUBLIC_ABI_BIN)
	$(C43_PUBLIC_ABI_BIN)

$(C43_FAKE_OUTPUT): $(C43_ARCHIVE) $(C43_C41_SUPPORT_OBJECTS) \
		$(C43_FAKE_SOURCES) $(C43_FAKE_HEADERS) \
		fakes/c43_phase1_fake_main.c
	$(CC) $(CPPFLAGS) -Ifakes $(CFLAGS) $(LDFLAGS) -o $@ \
		$(C43_FAKE_SOURCES) fakes/c43_phase1_fake_main.c \
		-Wl,--whole-archive $(C43_ARCHIVE) -Wl,--no-whole-archive \
		$(C43_C41_SUPPORT_OBJECTS)

fake-link-c43: $(C43_FAKE_OUTPUT)

$(C43_RESERVATION_BIN): $(C43_ARCHIVE) $(C43_C41_SUPPORT_OBJECTS) \
		$(C43_FAKE_SOURCES) $(C43_FAKE_HEADERS) \
		tests/test_c43_reservation.c
	$(CC) $(CPPFLAGS) -Ifakes $(CFLAGS) $(LDFLAGS) -o $@ \
		$(C43_FAKE_SOURCES) tests/test_c43_reservation.c \
		$(C43_ARCHIVE) $(C43_C41_SUPPORT_OBJECTS)

check-c43-reservation: $(C43_RESERVATION_BIN)
	$(C43_RESERVATION_BIN)

check-c43-architecture: $(C43_ARCHIVE) $(C43_PUBLIC_ABI_BIN) \
		$(C43_FAKE_OUTPUT) $(C43_RESERVATION_BIN)
	python3 ../../scripts/check_c43_architecture.py \
		--archive $(C43_ARCHIVE) --abi $(C43_PUBLIC_ABI_BIN) \
		--fake $(C43_FAKE_OUTPUT) --reservation $(C43_RESERVATION_BIN)

check-c43-phase1: check-c43-public-abi check-c43-architecture $(C43_FAKE_OUTPUT)
	$(C43_FAKE_OUTPUT)

check-c43-phase2: check-c43-phase1 check-c43-reservation

check-c43: check-c43-phase2

clean-c43:
	rm -f -- $(C43_ARCHIVE_OBJECTS) $(C43_C41_SUPPORT_OBJECTS) \
		$(C43_ARCHIVE) $(C43_PUBLIC_ABI_BIN) $(C43_FAKE_OUTPUT) \
		$(C43_RESERVATION_BIN)
	@rmdir -- $(C43_BUILD_DIR) 2>/dev/null || true
