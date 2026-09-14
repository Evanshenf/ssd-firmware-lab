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
WORKER_SOURCES := native_worker.c native_attach.c native_pump.c native_host.c native_owner.c native_owner_socket.c $(FIRMWARE_SOURCES)

# Explicit inventory shared by the offline journey and selected scaled worker.
# The ordinary worker target remains legacy; online qualification is separate.
SCALED_SOURCES := \
	../../core/ftl-scale/ftl_scale_codec.c \
	../../core/ftl-scale/ftl_scale_recovery.c \
	../../core/ftl-scale/ftl_scale_mapping.c \
	../../core/ftl-scale/ftl_scale_heads.c \
	../../core/ftl-scale/ftl_scale_write.c \
	../../core/ftl-scale/ftl_scale_gc.c \
	../../core/ftl-scale/ftl_scale_runtime.c \
	../../core/ftl-scale/ftl_scale_parent.c \
	../../core/ftl-scale/ftl_scale_nfc.c \
	../../core/ftl-scale/ftl_scale_nfc_v2.c \
	../../core/ftl-scale/ftl_scale_window.c \
	../../core/ftl-scale/ftl_scale_read.c \
	../../core/nfc-page-v2/nfc_page_v2.c \
	../../core/nfc-page-v2/nfc_page_v2_lab.c \
	../../core/nfc-page-v2/nfc_channel_v2.c ../../core/nfc-page-v2/nfc_channel_v2_actor.c \
	../../core/nfc-runtime/nfc_scaled_model.c \
	../../media/file-nand-v2/physical_nand.c \
	../../media/file-nand-v2/physical_nand_codec.c \
	../../media/file-nand-v2/physical_nand_posix.c \
	../../media/file-nand-v2/physical_nand_batch.c \
	../../media/file-nand-v2/channel_volume.c \
	../headless-scale/scale_storage.c
SCALED_RUNTIME_SOURCES := tests/test_scaled_runtime.c native_scaled_media.c \
	native_attach.c native_pump.c native_host.c native_owner.c native_owner_socket.c \
	$(FIRMWARE_SOURCES) $(SCALED_SOURCES)
SCALED_BUILD ?= build/scaled-offline
FWLAB_TEST_MEDIA_DIR ?= /run/fwlab-test-media

.PHONY: worker native-io native-io-static native-io-scaled native-io-scaled-static runtime check-runtime clean
.PHONY: scaled-runtime check-scaled-runtime
.PHONY: scaled-worker attach-check
.PHONY: scaled-pump-worker scaled-pump-runtime check-scaled-pump-runtime
.PHONY: large-worker large-runtime check-large-runtime native-io-large native-io-large-static
.PHONY: profile-check
.PHONY: mq2-worker
.PHONY: mq2-replyloss-worker
.PHONY: progress-runtime check-progress-runtime

scaled-worker:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) -DFWLAB_NATIVE_SCALED=1 \
		-I../../core/ftl-scale -I../../media/file-nand-v2 $(CFLAGS) \
		$(WORKER_SOURCES) native_scaled_media.c $(SCALED_SOURCES) \
		-o $(SCALED_BUILD)/fwlab_native_scaled_worker

scaled-pump-worker:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) -DFWLAB_NATIVE_SCALED=1 -DFWLAB_NATIVE_PUMP=1 \
		-I../../core/ftl-scale -I../../media/file-nand-v2 $(CFLAGS) \
		$(WORKER_SOURCES) native_scaled_media.c $(SCALED_SOURCES) \
		-o $(SCALED_BUILD)/fwlab_native_scaled_pump_worker

large-worker:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) -DFWLAB_NATIVE_SCALED=1 -DFWLAB_NATIVE_PUMP=1 -DFWLAB_NATIVE_LARGE=1 \
		-I../../core/ftl-scale -I../../media/file-nand-v2 $(CFLAGS) \
		$(WORKER_SOURCES) native_scaled_media.c $(SCALED_SOURCES) \
		-o $(SCALED_BUILD)/fwlab_native_large_worker

mq2-worker:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) -DFWLAB_NATIVE_SCALED=1 -DFWLAB_NATIVE_PUMP=1 -DFWLAB_NATIVE_LARGE=1 -DFWLAB_NATIVE_MQ2=1 \
		-I../../core/ftl-scale -I../../media/file-nand-v2 $(CFLAGS) \
		$(WORKER_SOURCES) native_scaled_media.c $(SCALED_SOURCES) \
		-o $(SCALED_BUILD)/fwlab_native_mq2_worker

mq2-replyloss-worker:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) -DFWLAB_NATIVE_SCALED=1 -DFWLAB_NATIVE_PUMP=1 -DFWLAB_NATIVE_LARGE=1 -DFWLAB_NATIVE_MQ2=1 \
		-I../../core/ftl-scale -I../../media/file-nand-v2 $(CFLAGS) \
		$(WORKER_SOURCES) native_scaled_media.c $(SCALED_SOURCES) tests/queue_reply_loss.c \
		-Wl,--wrap=ioctl -o $(SCALED_BUILD)/fwlab_native_mq2_replyloss_worker

attach-check:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_native_attach.c native_attach.c native_pump.c \
		-Wl,--wrap=ioctl -o $(SCALED_BUILD)/native_attach_check
	$(SCALED_BUILD)/native_attach_check

profile-check:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_native_profile.c $(FIRMWARE_SOURCES) \
		-o $(SCALED_BUILD)/native_profile_check
	$(SCALED_BUILD)/native_profile_check

scaled-runtime:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) -I../../core/ftl-scale -I../../media/file-nand-v2 \
		$(CFLAGS) $(SCALED_RUNTIME_SOURCES) -Wl,--wrap=ioctl \
		-o $(SCALED_BUILD)/native_scaled_offline

check-scaled-runtime: scaled-runtime
	FWLAB_TEST_MEDIA_DIR="$(FWLAB_TEST_MEDIA_DIR)" $(SCALED_BUILD)/native_scaled_offline

scaled-pump-runtime:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) -DFWLAB_NATIVE_SCALED=1 -DFWLAB_NATIVE_PUMP=1 \
		-I../../core/ftl-scale -I../../media/file-nand-v2 \
		$(CFLAGS) $(SCALED_RUNTIME_SOURCES) -Wl,--wrap=ioctl \
		-o $(SCALED_BUILD)/native_scaled_pump_offline

check-scaled-pump-runtime: scaled-pump-runtime
	FWLAB_TEST_MEDIA_DIR="$(FWLAB_TEST_MEDIA_DIR)" $(SCALED_BUILD)/native_scaled_pump_offline

large-runtime:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) -DFWLAB_NATIVE_SCALED=1 -DFWLAB_NATIVE_PUMP=1 -DFWLAB_NATIVE_LARGE=1 \
		-I../../core/ftl-scale -I../../media/file-nand-v2 \
		$(CFLAGS) $(SCALED_RUNTIME_SOURCES) -Wl,--wrap=ioctl \
		-o $(SCALED_BUILD)/native_large_offline

check-large-runtime: large-runtime
	FWLAB_TEST_MEDIA_DIR="$(FWLAB_TEST_MEDIA_DIR)" $(SCALED_BUILD)/native_large_offline

progress-runtime:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) -DFWLAB_NATIVE_SCALED=1 -DFWLAB_NATIVE_PUMP=1 -DFWLAB_NATIVE_LARGE=1 -DFWLAB_NATIVE_MQ2=1 \
		-I../../core/ftl-scale -I../../media/file-nand-v2 \
		$(CFLAGS) $(SCALED_RUNTIME_SOURCES) -Wl,--wrap=ioctl -Wl,--wrap=nanosleep \
		-o $(SCALED_BUILD)/native_progress_offline

check-progress-runtime: progress-runtime
	FWLAB_TEST_MEDIA_DIR="$(FWLAB_TEST_MEDIA_DIR)" $(SCALED_BUILD)/native_progress_offline

worker:
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WORKER_SOURCES) -o build/fwlab_native_worker

native-io:
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_native_io.c tests/owner_journey.c tests/vfio_epoch.c -o build/j1_native_io

native-io-static:
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -static tests/test_native_io.c tests/owner_journey.c tests/vfio_epoch.c -o build/j1_native_io_static

native-io-scaled:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) -DFWLAB_NATIVE_TEST_SCALED=1 $(CFLAGS) \
		tests/test_native_io.c tests/owner_journey.c tests/vfio_epoch.c \
		-o $(SCALED_BUILD)/j1_native_scaled_io

native-io-scaled-static:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) -DFWLAB_NATIVE_TEST_SCALED=1 $(CFLAGS) -static \
		tests/test_native_io.c tests/owner_journey.c tests/vfio_epoch.c \
		-o $(SCALED_BUILD)/j1_native_scaled_io_static

native-io-large:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) -DFWLAB_NATIVE_TEST_SCALED=1 -DFWLAB_NATIVE_TEST_LARGE=1 $(CFLAGS) \
		tests/test_native_io.c tests/owner_journey.c tests/vfio_epoch.c \
		-o $(SCALED_BUILD)/j1_native_large_io

native-io-large-static:
	mkdir -p $(SCALED_BUILD)
	$(CC) $(CPPFLAGS) -DFWLAB_NATIVE_TEST_SCALED=1 -DFWLAB_NATIVE_TEST_LARGE=1 $(CFLAGS) -static \
		tests/test_native_io.c tests/owner_journey.c tests/vfio_epoch.c \
		-o $(SCALED_BUILD)/j1_native_large_io_static

runtime:
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(RUNTIME_SOURCES) -o build/j1_runtime_matrix

check-runtime: runtime
	./build/j1_runtime_matrix

clean:
	rm -f -- build/j1_runtime_matrix build/fwlab_native_worker build/j1_native_io build/j1_native_io_static
	@rmdir build 2>/dev/null || true
