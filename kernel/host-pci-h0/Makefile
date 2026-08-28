# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: GPL-2.0-only

obj-m += ssd_fwlab_host_h0.o

KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
