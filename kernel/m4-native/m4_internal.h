/* SPDX-FileCopyrightText: 2026 Evanshenf */
/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SSD_FWLAB_M4_INTERNAL_H
#define SSD_FWLAB_M4_INTERNAL_H

#include <linux/device.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/irq_work.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/sizes.h>
#include <linux/spinlock.h>
#include <linux/version.h>

#ifdef CONFIG_ARM64
#include <linux/pci-ecam.h>
#elif defined(CONFIG_X86)
#include <asm/pci.h>
#else
#error "Native M4 platform binding currently supports x86 and ARM64 only"
#endif

#include "m4_hif.h"
#include "m4_dma_api.h"
#include "fwlab/unstable/m4_attach_native.h"
#include "fwlab/unstable/m4_profile_native.h"

#ifndef FWLAB_M4_PRODUCER
#define FWLAB_M4_PRODUCER FWLAB_M4_PRODUCER_BAR
#endif
#if FWLAB_M4_PRODUCER != FWLAB_M4_PRODUCER_BAR && \
    FWLAB_M4_PRODUCER != FWLAB_M4_PRODUCER_PUMP
#error "FWLAB_M4_PRODUCER must be 1 (BAR) or 2 (PUMP)"
#endif
#ifndef FWLAB_M4_HOST_PROFILE
#define FWLAB_M4_HOST_PROFILE FWLAB_M4_HOST_PROFILE_SMALL
#endif
#if FWLAB_M4_HOST_PROFILE != FWLAB_M4_HOST_PROFILE_SMALL && \
    FWLAB_M4_HOST_PROFILE != FWLAB_M4_HOST_PROFILE_LARGE_SERIAL && \
    FWLAB_M4_HOST_PROFILE != FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL
#error "FWLAB_M4_HOST_PROFILE must select SMALL, LARGE_SERIAL or LARGE_MQ2_SERIAL"
#endif
#if FWLAB_M4_HOST_PROFILE != FWLAB_M4_HOST_PROFILE_SMALL && \
    FWLAB_M4_PRODUCER != FWLAB_M4_PRODUCER_PUMP
#error "Large profiles require the PUMP producer"
#endif
#if FWLAB_M4_HOST_PROFILE == FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL && \
    LINUX_VERSION_CODE < KERNEL_VERSION(7, 0, 0)
#error "MQ2 requires the qualified Linux 7 MSI parent-domain interface"
#endif

#define FWLAB_M4_PCI_NAME "ssd_fwlab_native_pci"
#define FWLAB_M4_IOMMU_NAME "ssd_fwlab_native_iommu"
#define FWLAB_M4_VENDOR_ID 0xfffa
#define FWLAB_M4_DEVICE_ID 0x0002
#define FWLAB_M4_SUBSYSTEM_ID 0x0002
#define FWLAB_M4_CLASS_CODE 0xff0000U
#define FWLAB_M4_NVME_CLASS_CODE PCI_CLASS_STORAGE_EXPRESS
#define FWLAB_M4_REVISION 0x01
#define FWLAB_M4_BUS_NR 0
#define FWLAB_M4_DEVFN PCI_DEVFN(0, 0)

/* Stay inside the 16-bit PCI segment field consumed by VFIO and QEMU. */
#define FWLAB_M4_DOMAIN_MIN 0x7000U
#define FWLAB_M4_DOMAIN_MAX 0x7fffU

#define FWLAB_M4_BAR_DEFAULT_START 0ULL
#define FWLAB_M4_BAR_SIZE SZ_16K
#define FWLAB_M4_BAR_MAP_SIZE SZ_16K

#define FWLAB_M4_BAR_SIGNATURE 0x53534657U
#define FWLAB_M4_BAR_VERSION 0x00010000U
#define FWLAB_M4_BAR_ACK_XOR 0xa5a55a5aU

#define FWLAB_M4_REG_SIGNATURE 0x0000
#define FWLAB_M4_REG_VERSION 0x0004
#define FWLAB_M4_REG_EPOCH 0x0008
#define FWLAB_M4_REG_DOORBELL 0x0100
#define FWLAB_M4_REG_ACK 0x0104
#define FWLAB_M4_REG_IRQ_TRIGGER 0x0110
#define FWLAB_M4_REG_IRQ_ACK 0x0114

#define FWLAB_M4_MSIX_TABLE_OFFSET 0x2000
#define FWLAB_M4_MSIX_PBA_OFFSET 0x3000

#define FWLAB_M4_PCIE_CAP 0x40
#define FWLAB_M4_MSIX_CAP 0xa0
#define FWLAB_M4_VECTOR_COUNT \
    (FWLAB_M4_HOST_PROFILE == FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL ? 3U : 1U)

struct fwlab_m4_irq_ticket {
	u64 owner_epoch;
	u64 bus_generation;
	u64 effects_generation;
	u64 route_generation;
	u32 bar_epoch;
	u32 virq;
	u32 vector;
};

struct fwlab_m4_irq_route {
	struct irq_work work;
	struct fwlab_m4_pci_ctx *owner;
	struct fwlab_m4_irq_ticket ticket;
	u64 generation;
	unsigned int virq;
	bool allocated;
	bool pending;
	bool queued;
};

struct fwlab_m4_pci_ctx {
	/* Architecture hooks consume sysdata; our callbacks use bridge private. */
#ifdef CONFIG_ARM64
	struct pci_config_window sysdata;
#else
	struct pci_sysdata sysdata;
#endif
	struct pci_host_bridge *bridge;
	struct device *root_dev;
	struct pci_dev *pdev;
	struct resource busn_res;
	struct resource mem_window;
	struct resource *reserved_parent;
	u8 config[PCI_CFG_SPACE_SIZE];
	spinlock_t config_lock;
	void __iomem *bar_mapping;
	struct task_struct *bar_thread;
	struct irq_domain *msi_domain;
	struct fwnode_handle *msi_fwnode;
	struct fwlab_m4_irq_route route[FWLAB_M4_VECTOR_COUNT];
	u64 effects_generation;
	bool effects_open;
	u64 owner_epoch;
	u32 owner_kind;
	u32 owner_phase;
	phys_addr_t bar_start;
	resource_size_t bar_size;
	u32 bar_epoch;
	u64 access_generation;
	struct fwlab_m4_hif *hif;

	u32 requester_id;
	int iommu_group_id;

	bool bus_registered;
	bool mem_window_inserted;
};

int fwlab_m4_prepare_msix(struct fwlab_m4_pci_ctx *ctx, u64 owner_epoch,
			  u64 bus_generation, u32 bar_epoch,
			  struct fwlab_m4_irq_ticket *ticket);
int fwlab_m4_prepare_msix_vector(struct fwlab_m4_pci_ctx *ctx, u64 owner_epoch,
			  u64 bus_generation, u32 bar_epoch, u32 vector,
			  struct fwlab_m4_irq_ticket *ticket);
int fwlab_m4_retire_msix_route(struct fwlab_m4_pci_ctx *ctx, u32 vector);
int fwlab_m4_raise_msix(struct fwlab_m4_pci_ctx *ctx,
			const struct fwlab_m4_irq_ticket *ticket);
void fwlab_m4_flush_msix(struct fwlab_m4_pci_ctx *ctx);
void fwlab_m4_clear_msix(struct fwlab_m4_pci_ctx *ctx);
void fwlab_m4_close_effects(struct fwlab_m4_pci_ctx *ctx);

int fwlab_m4_pci_prepare(struct device *root_dev,
			 struct fwlab_m4_pci_ctx **out_ctx);
int fwlab_m4_pci_scan(struct fwlab_m4_pci_ctx *ctx);
int fwlab_m4_pci_publish(struct fwlab_m4_pci_ctx *ctx);
void fwlab_m4_pci_remove(struct fwlab_m4_pci_ctx *ctx);
void fwlab_m4_pci_free(struct fwlab_m4_pci_ctx *ctx);

#endif /* SSD_FWLAB_M4_INTERNAL_H */
