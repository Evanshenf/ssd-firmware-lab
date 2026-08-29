# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CLANG ?= clang
SAN_CC ?= $(CC)
BUILD_DIR ?= build

UAPI := ../../kernel/vfio-cdev-v1/uapi/unstable
CPPFLAGS += -I$(UAPI)
BASE_CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror -Wpedantic
CFLAGS ?= $(BASE_CFLAGS)
LDLIBS += -pthread

SOURCES := c25_oracle.c c25_session.c
HEADERS := c25_session.h $(UAPI)/fwlab_c21_a1.h
TARGET := $(BUILD_DIR)/vfio_cdev_v1_c25

.PHONY: all check check-clang check-sanitize clean

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $@

$(TARGET): $(SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)

check: $(TARGET)
	$(RUN_ENV) $(TARGET) --selftest

check-clang:
	$(MAKE) CC=$(CLANG) BUILD_DIR=build/clang check

check-sanitize:
	$(MAKE) CC=$(SAN_CC) BUILD_DIR=build/sanitize \
		CFLAGS='-std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -fno-omit-frame-pointer -fsanitize=address,undefined' \
		LDFLAGS='-fsanitize=address,undefined' \
		RUN_ENV='ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1' \
		check

clean:
	rm -f build/vfio_cdev_v1_c25 \
		build/clang/vfio_cdev_v1_c25 \
		build/sanitize/vfio_cdev_v1_c25
	@rmdir build/clang build/sanitize build 2>/dev/null || true
