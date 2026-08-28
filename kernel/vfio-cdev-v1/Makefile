# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: GPL-2.0-only

obj-m += ssd_fwlab_vfio_v1.o

ssd_fwlab_vfio_v1-y := \
	v1_main.o \
	providers/vfio_rw.o \
	contract/c21_state.o \
	contract/c21_wire.o

ccflags-y += -I$(src)/contract -I$(src)/providers \
	-I$(src)/uapi/unstable

KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
