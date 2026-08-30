# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
AR ?= ar
CLANG ?= clang
LLVM_AR ?= llvm-ar-18
BUILD_DIR ?= build
FWLAB_FAKE_OUTPUT ?= $(BUILD_DIR)/c35_fake_link

CPPFLAGS += -I../../include -I. -Ibindings -Itests \
	-I../../core/c34 -I../../core/c34/fakes -I../../core/fakes \
	-I../../media/c34-file -I../../media/c34-file/tests \
	-I../../nfc/fakes -I../../nfc/tests
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -fno-common
CFLAGS ?= $(BASE_CFLAGS)
FW_CFLAGS := $(CFLAGS) -g0

FW_ARCHIVE := $(BUILD_DIR)/libfwlab_portable_core_c31_c34.a
FW_OBJECTS := $(BUILD_DIR)/fw/c31.o $(BUILD_DIR)/fw/c31_codec.o \
	$(BUILD_DIR)/fw/c32_policy.o $(BUILD_DIR)/fw/c34_codec.o \
	$(BUILD_DIR)/fw/c34_recovery.o $(BUILD_DIR)/fw/c34_mapping.o \
	$(BUILD_DIR)/fw/c34_journal.o $(BUILD_DIR)/fw/c34_checkpoint.o \
	$(BUILD_DIR)/fw/c34_nfc_graph.o $(BUILD_DIR)/fw/c34_coordinator.o \
	$(BUILD_DIR)/fw/c34_drive.o $(BUILD_DIR)/fw/c34_provider.o

HEADLESS_SOURCES := c35_binding.c c35_lifecycle_port.c \
	c35_profile.c c35_headless.c c35_finalizer.c c35_trace.c c35_bundle.c \
	bindings/c35_c34.c bindings/c35_scripted.c
NFC_SOURCES := ../../nfc/nfc_model.c ../../nfc/nfc_scheduler.c \
	../../nfc/nfc_fault.c ../../nfc/nfc_media.c ../../nfc/nfc_codec.c
FIXTURE_SOURCES := ../../core/fakes/c31_fake_dma.c \
	../../core/fakes/c31_fake_provider.c \
	../../core/c34/fakes/c34_buffer.c \
	../../core/c34/fakes/c34_memory_media.c
FILE_SOURCES := ../../media/c34-file/c34_file_codec.c \
	../../media/c34-file/c34_file_recovery.c \
	../../media/c34-file/c34_file_engine.c \
	../../media/c34-file/c34_file_media.c \
	../../media/c34-file/c34_file_posix.c \
	../../media/c34-file/tests/c34_file_test_support.c
TEST_SUPPORT := tests/c35_test_support.c
GRAD_BIN := $(BUILD_DIR)/c35_graduation_tests
RESET_BIN := $(BUILD_DIR)/c35_reset_identity_tests
ISOLATION_BIN := $(BUILD_DIR)/c35_isolation_tests
GEOMETRY_BIN := $(BUILD_DIR)/c35_geometry_tests
POSIX_BIN := $(BUILD_DIR)/c35_posix_tests
MODEL_BIN := $(BUILD_DIR)/c35_model
BROKEN_BIN := $(BUILD_DIR)/c35_broken
THREAD_BIN := $(BUILD_DIR)/c35_thread_tests
OWNERSHIP_BIN := $(BUILD_DIR)/c35_ownership_tests
TRACE_BIN := $(BUILD_DIR)/c35_trace_bounds_tests
TRANSACTION_BIN := $(BUILD_DIR)/c35_transaction_fault_tests
LIMITS_BIN := $(BUILD_DIR)/c35_limits_cleanup_tests
TWO_ATOM_BIN := $(BUILD_DIR)/c35_two_atom_tests
PROFILE_BIN := $(BUILD_DIR)/c35_profile_bundle_tests
MODEL_A_BIN := $(BUILD_DIR)/c35a_model
BROKEN_A_BIN := $(BUILD_DIR)/c35a_broken
POISON_BIN := $(BUILD_DIR)/c35_poison_cleanup_tests
TAKEOVER_BIN := $(BUILD_DIR)/c35_phase_takeover_tests
LANE_S_BIN := $(BUILD_DIR)/c35_lane_s
LANE_M_BIN := $(BUILD_DIR)/c35_lane_m
LANE_B_BIN := $(BUILD_DIR)/c35_lane_b
LANE_P_BIN := $(BUILD_DIR)/c35_lane_p
LANE_BINS := $(LANE_S_BIN) $(LANE_M_BIN) $(LANE_B_BIN) $(LANE_P_BIN)
LANE_BASE_SOURCES := c35_binding.c c35_lifecycle_port.c c35_profile.c \
	c35_headless.c c35_finalizer.c c35_trace.c c35_bundle.c \
	../../core/fakes/c31_fake_dma.c tests/c35_test_support.c \
	tests/c35_lane_main.c
LANE_C34_SOURCES := bindings/c35_c34.c $(NFC_SOURCES) \
	../../core/c34/fakes/c34_buffer.c
LANE_FILE_SOURCES := ../../media/c34-file/c34_file_codec.c \
	../../media/c34-file/c34_file_recovery.c \
	../../media/c34-file/c34_file_engine.c \
	../../media/c34-file/c34_file_media.c
GEOMETRY_SOURCES := ../../nfc/fakes/nfc_buffer.c \
	../../nfc/fakes/nfc_memory_media.c ../../nfc/tests/c33_test_support.c

.PHONY: all lanes check check-gcc check-clang check-sanitize check-thread \
	check-analysis check-architecture check-determinism check-cross \
	check-all fake-link clean

all: $(GRAD_BIN)

lanes: $(LANE_BINS)

$(BUILD_DIR) $(BUILD_DIR)/fw:
	mkdir -p $@

$(BUILD_DIR)/fw/c31.o: ../../core/c31.c | $(BUILD_DIR)/fw
	$(CC) -I../../include $(FW_CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@
$(BUILD_DIR)/fw/c31_codec.o: ../../core/c31_codec.c | $(BUILD_DIR)/fw
	$(CC) -I../../include $(FW_CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@
$(BUILD_DIR)/fw/c32_policy.o: ../../core/c32/c32_policy.c | $(BUILD_DIR)/fw
	$(CC) -I../../include $(FW_CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@
$(BUILD_DIR)/fw/c34_codec.o: ../../core/c34/c34_codec.c | $(BUILD_DIR)/fw
	$(CC) -I../../include -I../../core/c34 $(FW_CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@
$(BUILD_DIR)/fw/c34_recovery.o: ../../core/c34/c34_recovery.c | $(BUILD_DIR)/fw
	$(CC) -I../../include -I../../core/c34 $(FW_CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@
$(BUILD_DIR)/fw/c34_mapping.o: ../../core/c34/c34_mapping.c | $(BUILD_DIR)/fw
	$(CC) -I../../include -I../../core/c34 $(FW_CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@
$(BUILD_DIR)/fw/c34_journal.o: ../../core/c34/c34_journal.c | $(BUILD_DIR)/fw
	$(CC) -I../../include -I../../core/c34 $(FW_CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@
$(BUILD_DIR)/fw/c34_checkpoint.o: ../../core/c34/c34_checkpoint.c | $(BUILD_DIR)/fw
	$(CC) -I../../include -I../../core/c34 $(FW_CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@
$(BUILD_DIR)/fw/c34_nfc_graph.o: ../../core/c34/c34_nfc_graph.c | $(BUILD_DIR)/fw
	$(CC) -I../../include -I../../core/c34 $(FW_CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@
$(BUILD_DIR)/fw/c34_coordinator.o: ../../core/c34/c34_coordinator.c | $(BUILD_DIR)/fw
	$(CC) -I../../include -I../../core/c34 $(FW_CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@
$(BUILD_DIR)/fw/c34_drive.o: ../../core/c34/c34_drive.c | $(BUILD_DIR)/fw
	$(CC) -I../../include -I../../core/c34 $(FW_CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@
$(BUILD_DIR)/fw/c34_provider.o: ../../core/c34/c34_provider.c | $(BUILD_DIR)/fw
	$(CC) -I../../include -I../../core/c34 $(FW_CFLAGS) -MMD -MF $(@:.o=.d) -c $< -o $@

$(FW_ARCHIVE): $(FW_OBJECTS)
	$(AR) rcsD $@ $(FW_OBJECTS)

$(GRAD_BIN): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		tests/test_graduation.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) tests/test_graduation.c \
		$(FW_ARCHIVE)

$(RESET_BIN): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		tests/test_reset_identity.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) tests/test_reset_identity.c \
		$(FW_ARCHIVE)

$(ISOLATION_BIN): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		tests/test_isolation.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) tests/test_isolation.c \
		$(FW_ARCHIVE)

$(GEOMETRY_BIN): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		$(GEOMETRY_SOURCES) tests/test_geometry_seam.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) $(GEOMETRY_SOURCES) \
		tests/test_geometry_seam.c $(FW_ARCHIVE)

$(POSIX_BIN): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		tests/test_posix.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) tests/test_posix.c \
		$(FW_ARCHIVE)

$(MODEL_BIN): tests/c35_model.c tests/c35_model.h tests/model_c35.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/c35_model.c tests/model_c35.c

$(BROKEN_BIN): tests/c35_model.c tests/c35_model.h tests/broken_c35.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/c35_model.c tests/broken_c35.c

$(THREAD_BIN): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		tests/test_threads.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) tests/test_threads.c \
		$(FW_ARCHIVE) -pthread

$(TRACE_BIN): c35_binding.c c35_trace.c tests/test_trace_bounds.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		c35_binding.c c35_trace.c tests/test_trace_bounds.c

$(TRANSACTION_BIN): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		tests/c35_fault_lifecycle.c tests/c35_fault_binding.c \
		tests/test_transaction_faults.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) tests/c35_fault_lifecycle.c \
		tests/c35_fault_binding.c tests/test_transaction_faults.c \
		$(FW_ARCHIVE)

$(LIMITS_BIN): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		tests/c35_fault_lifecycle.c tests/c35_fault_binding.c \
		tests/test_limits_cleanup.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) tests/c35_fault_lifecycle.c \
		tests/c35_fault_binding.c tests/test_limits_cleanup.c \
		$(FW_ARCHIVE)

$(TWO_ATOM_BIN): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		tests/test_two_atom.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) tests/test_two_atom.c \
		$(FW_ARCHIVE)

$(PROFILE_BIN): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		tests/test_profile_bundle.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) tests/test_profile_bundle.c \
		$(FW_ARCHIVE)

$(MODEL_A_BIN): tests/c35a_model.c tests/c35a_model.h \
		tests/model_c35a.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/c35a_model.c tests/model_c35a.c

$(BROKEN_A_BIN): tests/c35a_model.c tests/c35a_model.h \
		tests/broken_c35a.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		tests/c35a_model.c tests/broken_c35a.c

$(POISON_BIN): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		tests/test_poison_cleanup.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) tests/test_poison_cleanup.c \
		$(FW_ARCHIVE)

$(TAKEOVER_BIN): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		tests/test_phase_takeover.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) tests/test_phase_takeover.c \
		$(FW_ARCHIVE)

$(OWNERSHIP_BIN): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		tests/test_ownership.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) tests/test_ownership.c \
		$(FW_ARCHIVE)

$(LANE_S_BIN): $(FW_ARCHIVE) $(LANE_BASE_SOURCES) \
		bindings/c35_scripted.c ../../core/fakes/c31_fake_provider.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
		-DC35_TEST_LANE_MASK=1 -DC35_LANE_KIND=0 $(LDFLAGS) \
		-Wl,--gc-sections -Wl,-Map,$@.map -o $@ \
		$(LANE_BASE_SOURCES) bindings/c35_scripted.c \
		../../core/fakes/c31_fake_provider.c $(FW_ARCHIVE)

$(LANE_M_BIN): $(FW_ARCHIVE) $(LANE_BASE_SOURCES) $(LANE_C34_SOURCES) \
		../../core/c34/fakes/c34_memory_media.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
		-DC35_TEST_LANE_MASK=2 -DC35_LANE_KIND=1 $(LDFLAGS) \
		-Wl,--gc-sections -Wl,-Map,$@.map -o $@ \
		$(LANE_BASE_SOURCES) $(LANE_C34_SOURCES) \
		../../core/c34/fakes/c34_memory_media.c $(FW_ARCHIVE)

$(LANE_B_BIN): $(FW_ARCHIVE) $(LANE_BASE_SOURCES) $(LANE_C34_SOURCES) \
		$(LANE_FILE_SOURCES) ../../media/c34-file/tests/c34_file_test_support.c \
		| $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
		-DC35_TEST_LANE_MASK=4 -DC35_LANE_KIND=2 $(LDFLAGS) \
		-Wl,--gc-sections -Wl,-Map,$@.map -o $@ \
		$(LANE_BASE_SOURCES) $(LANE_C34_SOURCES) $(LANE_FILE_SOURCES) \
		../../media/c34-file/tests/c34_file_test_support.c $(FW_ARCHIVE)

$(LANE_P_BIN): $(FW_ARCHIVE) $(LANE_BASE_SOURCES) $(LANE_C34_SOURCES) \
		$(LANE_FILE_SOURCES) ../../media/c34-file/c34_file_posix.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
		-DC35_TEST_LANE_MASK=8 -DC35_LANE_KIND=3 $(LDFLAGS) \
		-Wl,--gc-sections -Wl,-Map,$@.map -o $@ \
		$(LANE_BASE_SOURCES) $(LANE_C34_SOURCES) $(LANE_FILE_SOURCES) \
		../../media/c34-file/c34_file_posix.c $(FW_ARCHIVE)

check-gcc: $(GRAD_BIN) $(RESET_BIN) $(ISOLATION_BIN) $(GEOMETRY_BIN) \
	$(POSIX_BIN) $(MODEL_BIN) $(BROKEN_BIN) $(THREAD_BIN) $(OWNERSHIP_BIN) \
	$(TRACE_BIN) $(TRANSACTION_BIN) $(LIMITS_BIN) $(TWO_ATOM_BIN) \
	$(PROFILE_BIN) $(MODEL_A_BIN) $(BROKEN_A_BIN) $(POISON_BIN) \
	$(TAKEOVER_BIN) \
	$(LANE_BINS)
	$(GRAD_BIN)
	$(RESET_BIN)
	$(ISOLATION_BIN)
	$(GEOMETRY_BIN)
	$(POSIX_BIN)
	$(MODEL_BIN)
	$(BROKEN_BIN)
	$(THREAD_BIN)
	$(OWNERSHIP_BIN)
	$(TRACE_BIN)
	$(TRANSACTION_BIN)
	$(LIMITS_BIN)
	$(TWO_ATOM_BIN)
	$(PROFILE_BIN)
	$(MODEL_A_BIN)
	$(BROKEN_A_BIN)
	$(POISON_BIN)
	$(TAKEOVER_BIN)
	$(LANE_S_BIN) life >/dev/null
	$(LANE_M_BIN) life >/dev/null
	$(LANE_B_BIN) life >/dev/null
	$(LANE_P_BIN) life >/dev/null
	$(LANE_M_BIN) semantic >/dev/null
	$(LANE_B_BIN) semantic >/dev/null
	$(LANE_P_BIN) semantic >/dev/null
	$(LANE_B_BIN) container >/dev/null
	$(LANE_P_BIN) container >/dev/null
	$(LANE_M_BIN) two-atom >/dev/null
	$(LANE_B_BIN) two-atom >/dev/null
	$(LANE_P_BIN) two-atom >/dev/null

check: check-gcc

check-clang:
	$(MAKE) CC=$(CLANG) AR=$(LLVM_AR) BUILD_DIR=build/clang check

check-sanitize:
	$(MAKE) CC=$(CC) BUILD_DIR=build/sanitize \
		CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -fno-common -fno-omit-frame-pointer -fsanitize=address,undefined' \
		LDFLAGS='-fsanitize=address,undefined' check

check-thread:
	$(MAKE) CC=$(CLANG) AR=$(LLVM_AR) BUILD_DIR=build/tsan \
		CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -fno-common -fno-omit-frame-pointer -fsanitize=thread' \
		LDFLAGS='-fsanitize=thread' build/tsan/c35_thread_tests
	build/tsan/c35_thread_tests

check-analysis:
	python3 ../../scripts/check_c35_analysis.py

check-architecture: lanes
	python3 ../../scripts/check_c35_architecture.py

check-determinism: lanes
	$(MAKE) CC=$(CLANG) AR=$(LLVM_AR) BUILD_DIR=build/clang lanes
	python3 ../../scripts/check_c35_determinism.py

check-cross: lanes
	python3 ../../scripts/check_c35_cross.py

check-all: check check-clang check-sanitize check-thread check-analysis \
	check-architecture check-determinism check-cross

$(FWLAB_FAKE_OUTPUT): $(FW_ARCHIVE) $(HEADLESS_SOURCES) $(NFC_SOURCES) \
		$(FIXTURE_SOURCES) $(FILE_SOURCES) $(TEST_SUPPORT) \
		tests/c35_fake_main.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
		$(HEADLESS_SOURCES) $(NFC_SOURCES) $(FIXTURE_SOURCES) \
		$(FILE_SOURCES) $(TEST_SUPPORT) tests/c35_fake_main.c \
		$(FW_ARCHIVE)

fake-link: check $(FWLAB_FAKE_OUTPUT)

clean:
	rm -f build/c35_graduation_tests build/c35_reset_identity_tests \
		build/c35_isolation_tests \
		build/c35_geometry_tests \
		build/c35_posix_tests \
		build/c35_model build/c35_broken \
		build/c35_thread_tests \
		build/c35_ownership_tests \
		build/c35_trace_bounds_tests build/c35_transaction_fault_tests \
		build/c35_limits_cleanup_tests \
		build/c35_two_atom_tests \
		build/c35_profile_bundle_tests \
		build/c35a_model build/c35a_broken \
		build/c35_poison_cleanup_tests \
		build/c35_phase_takeover_tests \
		build/c35_lane_s build/c35_lane_m build/c35_lane_b build/c35_lane_p \
		build/c35_lane_s.map build/c35_lane_m.map \
		build/c35_lane_b.map build/c35_lane_p.map \
		build/c35_fake_link \
		build/libfwlab_portable_core_c31_c34.a build/fw/*.o build/fw/*.d \
		build/clang/c35_graduation_tests \
		build/clang/c35_reset_identity_tests \
		build/clang/c35_isolation_tests \
		build/clang/c35_geometry_tests \
		build/clang/c35_posix_tests \
		build/clang/c35_model build/clang/c35_broken \
		build/clang/c35_thread_tests \
		build/clang/c35_ownership_tests \
		build/clang/c35_trace_bounds_tests \
		build/clang/c35_transaction_fault_tests \
		build/clang/c35_limits_cleanup_tests \
		build/clang/c35_two_atom_tests \
		build/clang/c35_profile_bundle_tests \
		build/clang/c35a_model build/clang/c35a_broken \
		build/clang/c35_poison_cleanup_tests \
		build/clang/c35_phase_takeover_tests \
		build/clang/c35_lane_s build/clang/c35_lane_m \
		build/clang/c35_lane_b build/clang/c35_lane_p \
		build/clang/c35_lane_s.map build/clang/c35_lane_m.map \
		build/clang/c35_lane_b.map build/clang/c35_lane_p.map \
		build/clang/libfwlab_portable_core_c31_c34.a build/clang/fw/*.o \
		build/clang/fw/*.d \
		build/sanitize/c35_graduation_tests \
		build/sanitize/c35_reset_identity_tests \
		build/sanitize/c35_isolation_tests \
		build/sanitize/c35_geometry_tests \
		build/sanitize/c35_posix_tests \
		build/sanitize/c35_model build/sanitize/c35_broken \
		build/sanitize/c35_thread_tests \
		build/sanitize/c35_ownership_tests \
		build/sanitize/c35_trace_bounds_tests \
		build/sanitize/c35_transaction_fault_tests \
		build/sanitize/c35_limits_cleanup_tests \
		build/sanitize/c35_two_atom_tests \
		build/sanitize/c35_profile_bundle_tests \
		build/sanitize/c35a_model build/sanitize/c35a_broken \
		build/sanitize/c35_poison_cleanup_tests \
		build/sanitize/c35_phase_takeover_tests \
		build/sanitize/c35_lane_s build/sanitize/c35_lane_m \
		build/sanitize/c35_lane_b build/sanitize/c35_lane_p \
		build/sanitize/c35_lane_s.map build/sanitize/c35_lane_m.map \
		build/sanitize/c35_lane_b.map build/sanitize/c35_lane_p.map \
		build/tsan/c35_thread_tests \
		build/tsan/libfwlab_portable_core_c31_c34.a build/tsan/fw/*.o \
		build/tsan/fw/*.d \
		build/cross/aarch64/c35_lane_s \
		build/cross/aarch64/c35_lane_m \
		build/cross/aarch64/c35_lane_b \
		build/cross/aarch64/c35_lane_p \
		build/cross/aarch64/c35_lane_s.map \
		build/cross/aarch64/c35_lane_m.map \
		build/cross/aarch64/c35_lane_b.map \
		build/cross/aarch64/c35_lane_p.map \
		build/cross/aarch64/libfwlab_portable_core_c31_c34.a \
		build/cross/aarch64/fw/*.o build/cross/aarch64/fw/*.d \
		build/cross/riscv64/c35_lane_s \
		build/cross/riscv64/c35_lane_m \
		build/cross/riscv64/c35_lane_b \
		build/cross/riscv64/c35_lane_p \
		build/cross/riscv64/c35_lane_s.map \
		build/cross/riscv64/c35_lane_m.map \
		build/cross/riscv64/c35_lane_b.map \
		build/cross/riscv64/c35_lane_p.map \
		build/cross/riscv64/libfwlab_portable_core_c31_c34.a \
		build/cross/riscv64/fw/*.o build/cross/riscv64/fw/*.d \
		build/cross/s390x/c35_lane_s \
		build/cross/s390x/c35_lane_m \
		build/cross/s390x/c35_lane_b \
		build/cross/s390x/c35_lane_p \
		build/cross/s390x/c35_lane_s.map \
		build/cross/s390x/c35_lane_m.map \
		build/cross/s390x/c35_lane_b.map \
		build/cross/s390x/c35_lane_p.map \
		build/cross/s390x/libfwlab_portable_core_c31_c34.a \
		build/cross/s390x/fw/*.o build/cross/s390x/fw/*.d \
		build/sanitize/libfwlab_portable_core_c31_c34.a build/sanitize/fw/*.o \
		build/sanitize/fw/*.d
	@rmdir build/fw build/clang/fw build/sanitize/fw \
		build/tsan/fw build/tsan \
		build/cross/aarch64/fw build/cross/aarch64 \
		build/cross/riscv64/fw build/cross/riscv64 \
		build/cross/s390x/fw build/cross/s390x build/cross \
		build/clang build/sanitize build 2>/dev/null || true
