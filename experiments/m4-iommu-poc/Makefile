# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: BSD-3-Clause

KERNEL_SOURCE ?= ../../kernel/m4-synthetic-pci-poc
KDIR ?= /lib/modules/$(shell uname -r)/build
USER_CC ?= cc
USER_CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Werror

.PHONY: all architecture check clean user

all:
	$(MAKE) -C $(KERNEL_SOURCE) KDIR=$(KDIR)

user: build/vfio_handoff_probe build/nvme_unaligned_probe

architecture:
	python3 check_frontend_architecture.py

check: architecture all user

build/vfio_handoff_probe: tools/vfio_handoff_probe.c
	mkdir -p build
	$(USER_CC) $(USER_CFLAGS) -o $@ $<

build/nvme_unaligned_probe: tools/nvme_unaligned_probe.c
	mkdir -p build
	$(USER_CC) $(USER_CFLAGS) -o $@ $<

clean:
	$(MAKE) -C $(KERNEL_SOURCE) KDIR=$(KDIR) clean
	rm -rf build
