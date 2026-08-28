# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

CC ?= cc
CFLAGS ?= -O2 -g
CPPFLAGS ?=
WARNINGS := -Wall -Wextra -Werror -Wpedantic

.PHONY: all clean

all: vfio_cdev_v0_smoke

vfio_cdev_v0_smoke: vfio_cdev_v0_smoke.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -std=c11 -o $@ $<

clean:
	rm -f vfio_cdev_v0_smoke
