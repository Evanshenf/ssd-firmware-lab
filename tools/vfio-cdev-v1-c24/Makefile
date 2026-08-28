# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CFLAGS ?= -O2 -g
CPPFLAGS += -I../../kernel/vfio-cdev-v1/uapi/unstable
WARNINGS := -Wall -Wextra -Werror -Wpedantic
BUILD_DIR ?= build
TARGET := $(BUILD_DIR)/vfio_cdev_v1_c24

.PHONY: all clean

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $@

$(TARGET): c24.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -pthread -o $@ $<

clean:
	rm -f $(TARGET)
	@rmdir $(BUILD_DIR) 2>/dev/null || true
