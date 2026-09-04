# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

override J0_BASE_COMMIT := 924007d3ec185ca641ebab8b3129466203952c43
override J0_S0B_PATH_MANIFEST := 215c94614033a596c03837568e10425c45456d8af364ed31eda6eadd76854d73
override J0_S0B_CONTENT_MANIFEST := 90de973eb65bda98867c725a586e7fcf328f4626990d268105ab85fa1b8addad

override J0_M3P_SOURCES := \
	../../core/m3p/m3p_codec.c \
	../../core/m3p/m3p_mapping.c \
	../../core/m3p/m3p_nfc.c \
	../../core/m3p/m3p_gc.c \
	../../core/m3p/m3p_recovery.c \
	../../core/m3p/m3p_runtime.c
override J0_M3P_MEMBERS := \
	m3p_codec.o m3p_mapping.o m3p_nfc.o m3p_gc.o \
	m3p_recovery.o m3p_runtime.o
override J0_NFC_SOURCES := \
	../../nfc/nfc_model.c \
	../../nfc/nfc_scheduler.c \
	../../nfc/nfc_fault.c \
	../../nfc/nfc_media.c
override J0_NFC_MEMBERS := \
	nfc_model.o nfc_scheduler.o nfc_fault.o nfc_media.o
override J0_FILE_SOURCES := \
	../../media/file-nand-v0/file_nand_codec.c \
	../../media/file-nand-v0/file_nand_engine.c \
	../../media/file-nand-v0/file_nand_media.c \
	../../media/file-nand-v0/file_nand_posix.c
override J0_FILE_MEMBERS := \
	file_nand_codec.o file_nand_engine.o file_nand_media.o file_nand_posix.o

override J0A_BUILD_DIR := build/j0a
override J0A_M3P_OBJECTS := \
	build/j0a/m3p_codec.o \
	build/j0a/m3p_mapping.o \
	build/j0a/m3p_nfc.o \
	build/j0a/m3p_gc.o \
	build/j0a/m3p_recovery.o \
	build/j0a/m3p_runtime.o
override J0A_NFC_OBJECTS := \
	build/j0a/nfc_model.o \
	build/j0a/nfc_scheduler.o \
	build/j0a/nfc_fault.o \
	build/j0a/nfc_media.o
override J0A_FILE_OBJECTS := \
	build/j0a/file_nand_codec.o \
	build/j0a/file_nand_engine.o \
	build/j0a/file_nand_media.o \
	build/j0a/file_nand_posix.o
override J0A_FAKE_SOURCE := ../../core/m3p/fakes/m3p_fake_adjacent.c
override J0A_FAKE_OBJECT := build/j0a/m3p_fake_adjacent.o
override J0A_TEST_SOURCE := ../../core/m3p/tests/test_j0a_lower.c
override J0A_TEST_OBJECT := build/j0a/test_j0a_lower.o
override J0A_M3P_ARCHIVE := build/j0a/libfwlab_m3p_v0.a
override J0A_NFC_ARCHIVE := build/j0a/libfwlab_nfc_v1.a
override J0A_FILE_ARCHIVE := build/j0a/libfwlab_file_nand_v0.a
override J0A_MATRIX := build/j0a/j0a_lower_matrix
override J0A_INTERMEDIATES := \
	build/j0a/m3p_codec.o \
	build/j0a/m3p_mapping.o \
	build/j0a/m3p_nfc.o \
	build/j0a/m3p_gc.o \
	build/j0a/m3p_recovery.o \
	build/j0a/m3p_runtime.o \
	build/j0a/nfc_model.o \
	build/j0a/nfc_scheduler.o \
	build/j0a/nfc_fault.o \
	build/j0a/nfc_media.o \
	build/j0a/file_nand_codec.o \
	build/j0a/file_nand_engine.o \
	build/j0a/file_nand_media.o \
	build/j0a/file_nand_posix.o \
	build/j0a/m3p_fake_adjacent.o \
	build/j0a/test_j0a_lower.o \
	build/j0a/libfwlab_m3p_v0.a \
	build/j0a/libfwlab_nfc_v1.a \
	build/j0a/libfwlab_file_nand_v0.a
override J0A_ARTIFACTS := build/j0a/j0a_lower_matrix
override J0A_OWNED := \
	build/j0a/m3p_codec.o \
	build/j0a/m3p_mapping.o \
	build/j0a/m3p_nfc.o \
	build/j0a/m3p_gc.o \
	build/j0a/m3p_recovery.o \
	build/j0a/m3p_runtime.o \
	build/j0a/nfc_model.o \
	build/j0a/nfc_scheduler.o \
	build/j0a/nfc_fault.o \
	build/j0a/nfc_media.o \
	build/j0a/file_nand_codec.o \
	build/j0a/file_nand_engine.o \
	build/j0a/file_nand_media.o \
	build/j0a/file_nand_posix.o \
	build/j0a/m3p_fake_adjacent.o \
	build/j0a/test_j0a_lower.o \
	build/j0a/libfwlab_m3p_v0.a \
	build/j0a/libfwlab_nfc_v1.a \
	build/j0a/libfwlab_file_nand_v0.a \
	build/j0a/j0a_lower_matrix

override J0_FORBIDDEN_MEMBERS := \
	nfc_codec.o nfc_adapter.o nfc_fake_main.o nfc_memory_media.o \
	spine_fake_adjacent.o tiny_profile_fixture.o \
	m4_frontend.o m4_nvme.o m4_media_fixture.o
override J0_FORBIDDEN_SYMBOL_PREFIXES := c34_ c35_ fwlab_c31_ fwlab_m4_
