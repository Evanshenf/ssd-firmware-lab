# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CFLAGS ?= -O2 -g
CPPFLAGS += -I../../kernel/vfio-cdev-v1/uapi/unstable
WARNINGS := -Wall -Wextra -Werror -Wpedantic
BUILD_DIR ?= build
TARGET := $(BUILD_DIR)/vfio_cdev_v1_c2_2

.PHONY: all clean

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $@

$(TARGET): vfio_cdev_v1_c2_2.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $<

clean:
	rm -f $(TARGET)
	@rmdir $(BUILD_DIR) 2>/dev/null || true
