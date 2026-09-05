# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CPPFLAGS += -I../../include -I../../core/command-spine -I../../core/m3p \
	-I../../media/file-nand-v0 -I../../nfc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic -fno-common

FIRMWARE_SOURCES := \
	../headless-j0/j0_controller_buffer.c \
	../headless-j0/j0_host_data.c \
	../headless-j0/j0_action_drivers.c \
	../headless-j0/j0_construction.c \
	../../core/c4-nvme/c41_codec.c \
	../../core/command-spine/spine_contracts.c \
	../../core/command-spine/spine_lifecycle.c \
	../../core/command-spine/profiles/c43_p1_adapter.c \
	../../core/command-spine/profiles/linux_profile_v1_adapter.c \
	../../core/m3p/m3p_codec.c ../../core/m3p/m3p_mapping.c \
	../../core/m3p/m3p_nfc.c ../../core/m3p/m3p_gc.c \
	../../core/m3p/m3p_recovery.c ../../core/m3p/m3p_runtime.c \
	../../core/nfc-runtime/nfc_trace_window.c \
	../../nfc/nfc_model.c ../../nfc/nfc_scheduler.c \
	../../nfc/nfc_fault.c ../../nfc/nfc_media.c \
	../../media/file-nand-v0/file_nand_codec.c \
	../../media/file-nand-v0/file_nand_engine.c \
	../../media/file-nand-v0/file_nand_media.c \
	../../media/file-nand-v0/file_nand_posix.c

RUNTIME_SOURCES := tests/test_runtime.c $(FIRMWARE_SOURCES)
WORKER_SOURCES := native_worker.c native_host.c native_owner.c native_owner_socket.c $(FIRMWARE_SOURCES)

.PHONY: worker native-io native-io-static runtime check-runtime clean

worker:
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WORKER_SOURCES) -o build/fwlab_native_worker

native-io:
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_native_io.c tests/owner_journey.c tests/vfio_epoch.c -o build/j1_native_io

native-io-static:
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -static tests/test_native_io.c tests/owner_journey.c tests/vfio_epoch.c -o build/j1_native_io_static

runtime:
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(RUNTIME_SOURCES) -o build/j1_runtime_matrix

check-runtime: runtime
	./build/j1_runtime_matrix

clean:
	rm -f -- build/j1_runtime_matrix build/fwlab_native_worker build/j1_native_io build/j1_native_io_static
	@rmdir build 2>/dev/null || true
