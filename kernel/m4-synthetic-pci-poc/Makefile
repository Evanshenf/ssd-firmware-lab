# SPDX-FileCopyrightText: 2026 Evanshenf
# SPDX-License-Identifier: GPL-2.0-only

obj-m += ssd_fwlab_sw_iommu_poc.o
ssd_fwlab_sw_iommu_poc-y := m4_iommu.o

obj-m += ssd_fwlab_synth_pci_poc.o
ssd_fwlab_synth_pci_poc-y := m4_pci_main.o m4_pci.o m4_frontend.o \
	m4_nvme.o m4_media_fixture.o

obj-m += ssd_fwlab_bar_aperture_probe.o
ssd_fwlab_bar_aperture_probe-y := m4_bar_probe.o

obj-m += ssd_fwlab_bar_lab.o
ssd_fwlab_bar_lab-y := m4_bar_lab.o

obj-m += ssd_fwlab_dma_lab.o
ssd_fwlab_dma_lab-y := m4_dma_lab.o

obj-m += ssd_fwlab_msix_lab.o
ssd_fwlab_msix_lab-y := m4_msix_lab.o

obj-m += ssd_fwlab_dma_api_lab.o
ssd_fwlab_dma_api_lab-y := m4_dma_api_lab.o

KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
