// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

/*
 * Deliberately small native-NVMe fixture for the M4 transport PoC.
 *
 * This is not the portable firmware protocol core and must not be used as
 * C4.x conformance evidence.  It exists to prove that the synthetic PCI,
 * software-IOMMU/DMA and isolated MSI-X paths can drive Linux nvme-pci.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvme.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "m4_frontend.h"
#include "m4_media.h"
#include "m4_nvme.h"

#define FWLAB_NVME_MQES 31U
#define FWLAB_NVME_MAX_Q_DEPTH (FWLAB_NVME_MQES + 1U)
#define FWLAB_NVME_NS_BYTES SZ_1M
#define FWLAB_NVME_LBA_SHIFT 9U
#define FWLAB_NVME_LBA_BYTES (1U << FWLAB_NVME_LBA_SHIFT)
#define FWLAB_NVME_NS_LBAS (FWLAB_NVME_NS_BYTES / FWLAB_NVME_LBA_BYTES)
#define FWLAB_NVME_MAX_XFER SZ_8K
#define FWLAB_NVME_MAX_COMMANDS_PER_POLL 128U
#define FWLAB_NVME_BAR_IMPLEMENTED_BYTES SZ_16K
#define FWLAB_NVME_VENDOR_ID 0xfffa
#define FWLAB_NVME_FIXTURE_NAME "ssd_fwlab_nvme_fixture_poc"

#define FWLAB_NVME_CAP_CQR BIT_ULL(16)
#define FWLAB_NVME_CAP_TO (1ULL << 24)
#define FWLAB_NVME_CAP_CSS_NVM BIT_ULL(37)
#define FWLAB_NVME_CC_MPS_MASK GENMASK(10, 7)
#define FWLAB_NVME_CC_AMS_MASK GENMASK(13, 11)
#define FWLAB_NVME_CC_IOSQES_MASK GENMASK(19, 16)
#define FWLAB_NVME_CC_IOCQES_MASK GENMASK(23, 20)

struct fwlab_nvme_queue {
	bool sq_valid;
	bool cq_valid;
	u64 sq_base;
	u64 cq_base;
	u16 sq_depth;
	u16 cq_depth;
	u16 sq_head;
	u16 cq_tail;
	bool cq_phase;
};

struct fwlab_m4_nvme {
	struct mutex lock;
	const struct fwlab_m4_frontend_services *services;
	struct fwlab_nvme_queue adminq;
	struct fwlab_nvme_queue ioq;
	struct fwlab_m4_media media;
	u8 *xfer;
	u32 observed_epoch;
	u32 last_cc;
	bool enabled;
	bool aer_outstanding;
};

static char *backend_path;
module_param(backend_path, charp, 0444);
MODULE_PARM_DESC(backend_path,
	"Existing exact-size regular file for the disposable NVMe media fixture");

static u32 fwlab_nvme_read32(struct fwlab_m4_nvme *nvme, u32 offset)
{
	return nvme->services->bar_read32(nvme->services->context, offset);
}

static u64 fwlab_nvme_read64(struct fwlab_m4_nvme *nvme, u32 offset)
{
	return nvme->services->bar_read64(nvme->services->context, offset);
}

static void fwlab_nvme_write32(struct fwlab_m4_nvme *nvme, u32 offset,
			       u32 value)
{
	nvme->services->bar_write32(nvme->services->context, offset, value);
}

static void fwlab_nvme_write64(struct fwlab_m4_nvme *nvme, u32 offset,
			       u64 value)
{
	nvme->services->bar_write64(nvme->services->context, offset, value);
}

static int fwlab_nvme_dma(struct fwlab_m4_nvme *nvme, dma_addr_t iova,
			  void *buffer, size_t length,
			  enum fwlab_m4_dma_direction direction)
{
	return nvme->services->dma(nvme->services->context, iova, buffer,
				   length, direction);
}

static unsigned int fwlab_nvme_sq_db(unsigned int qid)
{
	return NVME_REG_DBS + qid * 8U;
}

static unsigned int fwlab_nvme_cq_db(unsigned int qid)
{
	return NVME_REG_DBS + qid * 8U + 4U;
}

static void fwlab_nvme_clear_doorbells(struct fwlab_m4_nvme *nvme)
{
	fwlab_nvme_write32(nvme, fwlab_nvme_sq_db(0), 0);
	fwlab_nvme_write32(nvme, fwlab_nvme_cq_db(0), 0);
	fwlab_nvme_write32(nvme, fwlab_nvme_sq_db(1), 0);
	fwlab_nvme_write32(nvme, fwlab_nvme_cq_db(1), 0);
	wmb();
}

static void fwlab_nvme_queue_clear(struct fwlab_nvme_queue *queue)
{
	memset(queue, 0, sizeof(*queue));
	queue->cq_phase = true;
}

static void fwlab_nvme_disable_locked(struct fwlab_m4_nvme *nvme)
{
	fwlab_nvme_queue_clear(&nvme->adminq);
	fwlab_nvme_queue_clear(&nvme->ioq);
	nvme->enabled = false;
	nvme->aer_outstanding = false;
}

static void fwlab_nvme_reset(void *instance, u32 controller_epoch)
{
	struct fwlab_m4_nvme *nvme = instance;
	u64 cap = FWLAB_NVME_MQES | FWLAB_NVME_CAP_CQR |
		  FWLAB_NVME_CAP_TO | FWLAB_NVME_CAP_CSS_NVM;

	(void)controller_epoch;
	nvme->services->bar_zero(nvme->services->context, 0,
				 FWLAB_NVME_BAR_IMPLEMENTED_BYTES);
	fwlab_nvme_write64(nvme, NVME_REG_CAP, cap);
	fwlab_nvme_write32(nvme, NVME_REG_VS, NVME_VS(1, 0, 0));
	wmb();
}

static int fwlab_nvme_create(
	const struct fwlab_m4_frontend_services *services, void **instance)
{
	struct fwlab_m4_nvme *nvme;
	int ret;

	if (!services || !instance)
		return -EINVAL;
	*instance = NULL;
	nvme = kzalloc(sizeof(*nvme), GFP_KERNEL);
	if (!nvme)
		return -ENOMEM;
	nvme->services = services;
	ret = fwlab_m4_media_fixture_create(backend_path, FWLAB_NVME_NS_BYTES,
					    &nvme->media);
	if (ret)
		goto err_nvme;
	nvme->xfer = kmalloc(FWLAB_NVME_MAX_XFER, GFP_KERNEL);
	if (!nvme->xfer) {
		ret = -ENOMEM;
		goto err_backend;
	}

	mutex_init(&nvme->lock);
	fwlab_nvme_disable_locked(nvme);
	*instance = nvme;
	return 0;

err_backend:
	fwlab_m4_media_unbind(&nvme->media);
err_nvme:
	kfree(nvme);
	return ret;
}

static void fwlab_nvme_destroy(void *instance)
{
	struct fwlab_m4_nvme *nvme = instance;

	if (!nvme)
		return;
	kfree(nvme->xfer);
	fwlab_m4_media_unbind(&nvme->media);
	kfree(nvme);
}

static int fwlab_nvme_prp_transfer(struct fwlab_m4_nvme *nvme,
				    const struct nvme_command *cmd,
				    void *buffer, size_t length,
				    enum fwlab_m4_dma_direction direction)
{
	dma_addr_t prp1 = le64_to_cpu(cmd->common.dptr.prp1);
	dma_addr_t prp2 = le64_to_cpu(cmd->common.dptr.prp2);
	__le64 list[2];
	size_t remaining;
	size_t copied;
	size_t first;
	unsigned int entries;
	unsigned int i;
	int ret;

	if (!prp1 || !length || length > FWLAB_NVME_MAX_XFER ||
	    (cmd->common.flags & NVME_CMD_SGL_ALL))
		return -EINVAL;

	first = min_t(size_t, length, PAGE_SIZE - offset_in_page(prp1));
	ret = fwlab_nvme_dma(nvme, prp1, buffer, first, direction);
	if (ret || first == length)
		return ret;
	remaining = length - first;
	if (!prp2 || !IS_ALIGNED(prp2, PAGE_SIZE))
		return -EINVAL;
	if (remaining <= PAGE_SIZE)
		return fwlab_nvme_dma(nvme, prp2, (u8 *)buffer + first,
				       remaining, direction);

	/* At the 8-KiB profile ceiling, PRP2 can name at most two pages. */
	entries = DIV_ROUND_UP(remaining, PAGE_SIZE);
	if (entries > ARRAY_SIZE(list))
		return -EINVAL;
	ret = fwlab_nvme_dma(nvme, prp2, list, entries * sizeof(list[0]),
			     FWLAB_M4_DMA_READ_HOST);
	if (ret)
		return ret;

	copied = first;
	for (i = 0; i < entries; i++) {
		dma_addr_t page = le64_to_cpu(list[i]);
		size_t chunk = min_t(size_t, remaining, PAGE_SIZE);

		if (!page || !IS_ALIGNED(page, PAGE_SIZE))
			return -EINVAL;
		ret = fwlab_nvme_dma(nvme, page, (u8 *)buffer + copied, chunk,
				     direction);
		if (ret)
			return ret;
		copied += chunk;
		remaining -= chunk;
	}
	return remaining ? -EINVAL : 0;
}

static void fwlab_nvme_fill_id_ctrl(struct nvme_id_ctrl *id)
{
	static const char serial[] = "FWLABM4POC000000001";
	static const char model[] = "SSD Firmware Lab M4 Native NVMe PoC";
	static const char firmware[] = "M4POC001";

	memset(id, 0, sizeof(*id));
	id->vid = cpu_to_le16(FWLAB_NVME_VENDOR_ID);
	id->ssvid = cpu_to_le16(FWLAB_NVME_VENDOR_ID);
	memset(id->sn, ' ', sizeof(id->sn));
	memcpy(id->sn, serial, min(sizeof(id->sn), sizeof(serial) - 1));
	memset(id->mn, ' ', sizeof(id->mn));
	memcpy(id->mn, model, min(sizeof(id->mn), sizeof(model) - 1));
	memset(id->fr, ' ', sizeof(id->fr));
	memcpy(id->fr, firmware, min(sizeof(id->fr), sizeof(firmware) - 1));
	id->mdts = 1; /* 8 KiB at MPSMIN=4 KiB. */
	id->cntlid = cpu_to_le16(1);
	id->ver = cpu_to_le32(NVME_VS(1, 0, 0));
	id->sqes = (NVME_NVM_IOSQES << 4) | NVME_NVM_IOSQES;
	id->cqes = (NVME_NVM_IOCQES << 4) | NVME_NVM_IOCQES;
	id->nn = cpu_to_le32(1);
	id->vwc = 0;
}

static void fwlab_nvme_fill_id_ns(struct nvme_id_ns *id)
{
	memset(id, 0, sizeof(*id));
	id->nsze = cpu_to_le64(FWLAB_NVME_NS_LBAS);
	id->ncap = cpu_to_le64(FWLAB_NVME_NS_LBAS);
	id->nuse = cpu_to_le64(FWLAB_NVME_NS_LBAS);
	id->nlbaf = 0;
	id->flbas = 0;
	id->noiob = cpu_to_le16(SZ_4K / FWLAB_NVME_LBA_BYTES);
	id->lbaf[0].ms = 0;
	id->lbaf[0].ds = FWLAB_NVME_LBA_SHIFT;
}

static u16 fwlab_nvme_identify(struct fwlab_m4_nvme *nvme,
				const struct nvme_command *cmd)
{
	u8 cns = cmd->identify.cns;
	u32 nsid = le32_to_cpu(cmd->identify.nsid);

	memset(nvme->xfer, 0, NVME_IDENTIFY_DATA_SIZE);
	if (cns == NVME_ID_CNS_CTRL) {
		fwlab_nvme_fill_id_ctrl((struct nvme_id_ctrl *)nvme->xfer);
	} else if (cns == NVME_ID_CNS_NS && nsid == 1) {
		fwlab_nvme_fill_id_ns((struct nvme_id_ns *)nvme->xfer);
	} else {
		return NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;
	}

	if (fwlab_nvme_prp_transfer(nvme, cmd, nvme->xfer,
				    NVME_IDENTIFY_DATA_SIZE,
				    FWLAB_M4_DMA_WRITE_HOST))
		return NVME_SC_DATA_XFER_ERROR | NVME_STATUS_DNR;
	return NVME_SC_SUCCESS;
}

static u16 fwlab_nvme_get_log(struct fwlab_m4_nvme *nvme,
			      const struct nvme_command *cmd)
{
	u64 offset = le64_to_cpu(cmd->get_log_page.lpo);
	u64 dwords = (u64)le16_to_cpu(cmd->get_log_page.numdl) |
		     ((u64)le16_to_cpu(cmd->get_log_page.numdu) << 16);
	u64 bytes;

	if (check_add_overflow(dwords, 1ULL, &dwords) ||
	    check_mul_overflow(dwords, 4ULL, &bytes) || !bytes ||
	    bytes > FWLAB_NVME_MAX_XFER || offset)
		return NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;

	memset(nvme->xfer, 0, bytes);
	switch (cmd->get_log_page.lid) {
	case NVME_LOG_ERROR:
		break;
	case NVME_LOG_SMART:
		if (bytes >= sizeof(struct nvme_smart_log)) {
			struct nvme_smart_log *log =
				(struct nvme_smart_log *)nvme->xfer;

			/* 300 Kelvin, no warning, 100% spare. */
			log->temperature[0] = 300 & 0xff;
			log->temperature[1] = 300 >> 8;
			log->avail_spare = 100;
			log->spare_thresh = 10;
		}
		break;
	case NVME_LOG_FW_SLOT:
		if (bytes >= sizeof(struct nvme_fw_slot_info_log)) {
			struct nvme_fw_slot_info_log *log =
				(struct nvme_fw_slot_info_log *)nvme->xfer;

			log->afi = 1;
			memcpy(&log->frs[0], "M4POC001", sizeof(log->frs[0]));
		}
		break;
	default:
		return NVME_SC_INVALID_LOG_PAGE | NVME_STATUS_DNR;
	}

	if (fwlab_nvme_prp_transfer(nvme, cmd, nvme->xfer, bytes,
				    FWLAB_M4_DMA_WRITE_HOST))
		return NVME_SC_DATA_XFER_ERROR | NVME_STATUS_DNR;
	return NVME_SC_SUCCESS;
}

static u16 fwlab_nvme_features(const struct nvme_command *cmd, u32 *result)
{
	u32 fid = le32_to_cpu(cmd->features.fid) & 0xff;
	u32 value = le32_to_cpu(cmd->features.dword11);

	*result = 0;
	switch (fid) {
	case NVME_FEAT_NUM_QUEUES:
		/* One submission queue and one completion queue, both zero based. */
		*result = 0;
		return NVME_SC_SUCCESS;
	case NVME_FEAT_IRQ_CONFIG:
		return (value & 0xffff) == 0 ? NVME_SC_SUCCESS :
			NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;
	case NVME_FEAT_IRQ_COALESCE:
	case NVME_FEAT_ASYNC_EVENT:
	case NVME_FEAT_POWER_MGMT:
	case NVME_FEAT_VOLATILE_WC:
		return NVME_SC_SUCCESS;
	default:
		return NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;
	}
}

static u16 fwlab_nvme_create_cq(struct fwlab_m4_nvme *nvme,
				 const struct nvme_command *cmd)
{
	u16 qid = le16_to_cpu(cmd->create_cq.cqid);
	u16 depth = le16_to_cpu(cmd->create_cq.qsize) + 1;
	u16 flags = le16_to_cpu(cmd->create_cq.cq_flags);
	u16 vector = le16_to_cpu(cmd->create_cq.irq_vector);
	u64 base = le64_to_cpu(cmd->create_cq.prp1);

	if (qid != 1)
		return NVME_SC_QID_INVALID | NVME_STATUS_DNR;
	if (depth < 2 || depth > FWLAB_NVME_MAX_Q_DEPTH)
		return NVME_SC_QUEUE_SIZE | NVME_STATUS_DNR;
	if (!(flags & NVME_QUEUE_PHYS_CONTIG) ||
	    !(flags & NVME_CQ_IRQ_ENABLED) || vector != 0 ||
	    !IS_ALIGNED(base, PAGE_SIZE))
		return NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;

	nvme->ioq.cq_valid = true;
	nvme->ioq.cq_base = base;
	nvme->ioq.cq_depth = depth;
	nvme->ioq.cq_tail = 0;
	nvme->ioq.cq_phase = true;
	return NVME_SC_SUCCESS;
}

static u16 fwlab_nvme_create_sq(struct fwlab_m4_nvme *nvme,
				 const struct nvme_command *cmd)
{
	u16 qid = le16_to_cpu(cmd->create_sq.sqid);
	u16 cqid = le16_to_cpu(cmd->create_sq.cqid);
	u16 depth = le16_to_cpu(cmd->create_sq.qsize) + 1;
	u16 flags = le16_to_cpu(cmd->create_sq.sq_flags);
	u64 base = le64_to_cpu(cmd->create_sq.prp1);

	if (qid != 1 || cqid != 1 || !nvme->ioq.cq_valid)
		return NVME_SC_QID_INVALID | NVME_STATUS_DNR;
	if (depth < 2 || depth > FWLAB_NVME_MAX_Q_DEPTH)
		return NVME_SC_QUEUE_SIZE | NVME_STATUS_DNR;
	if (!(flags & NVME_QUEUE_PHYS_CONTIG) ||
	    !IS_ALIGNED(base, PAGE_SIZE))
		return NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;

	nvme->ioq.sq_valid = true;
	nvme->ioq.sq_base = base;
	nvme->ioq.sq_depth = depth;
	nvme->ioq.sq_head = 0;
	return NVME_SC_SUCCESS;
}

static u16 fwlab_nvme_admin(struct fwlab_m4_nvme *nvme,
			     const struct nvme_command *cmd, u32 *result,
			     bool *defer)
{
	u16 qid;

	*result = 0;
	*defer = false;
	switch (cmd->common.opcode) {
	case nvme_admin_identify:
		return fwlab_nvme_identify(nvme, cmd);
	case nvme_admin_get_log_page:
		return fwlab_nvme_get_log(nvme, cmd);
	case nvme_admin_set_features:
	case nvme_admin_get_features:
		return fwlab_nvme_features(cmd, result);
	case nvme_admin_create_cq:
		return fwlab_nvme_create_cq(nvme, cmd);
	case nvme_admin_create_sq:
		return fwlab_nvme_create_sq(nvme, cmd);
	case nvme_admin_delete_sq:
		qid = le16_to_cpu(cmd->delete_queue.qid);
		if (qid != 1 || !nvme->ioq.sq_valid)
			return NVME_SC_QID_INVALID | NVME_STATUS_DNR;
		nvme->ioq.sq_valid = false;
		return NVME_SC_SUCCESS;
	case nvme_admin_delete_cq:
		qid = le16_to_cpu(cmd->delete_queue.qid);
		if (qid != 1 || !nvme->ioq.cq_valid || nvme->ioq.sq_valid)
			return NVME_SC_QID_INVALID | NVME_STATUS_DNR;
		nvme->ioq.cq_valid = false;
		return NVME_SC_SUCCESS;
	case nvme_admin_async_event:
		if (nvme->aer_outstanding)
			return NVME_SC_ASYNC_LIMIT | NVME_STATUS_DNR;
		nvme->aer_outstanding = true;
		*defer = true;
		return NVME_SC_SUCCESS;
	default:
		pr_info(FWLAB_NVME_FIXTURE_NAME
			 ": unsupported admin opcode=%#x\n",
			cmd->common.opcode);
		return NVME_SC_INVALID_OPCODE | NVME_STATUS_DNR;
	}
}

static u16 fwlab_nvme_io(struct fwlab_m4_nvme *nvme,
			  const struct nvme_command *cmd)
{
	u64 slba;
	u64 end_lba;
	u32 blocks;
	size_t length;
	size_t offset;
	int ret;

	if (cmd->common.opcode == nvme_cmd_flush) {
		if (le32_to_cpu(cmd->common.nsid) != 1)
			return NVME_SC_INVALID_NS | NVME_STATUS_DNR;
		return !fwlab_m4_media_flush(&nvme->media) ?
			NVME_SC_SUCCESS : NVME_SC_INTERNAL | NVME_STATUS_DNR;
	}
	if (cmd->common.opcode != nvme_cmd_read &&
	    cmd->common.opcode != nvme_cmd_write)
		return NVME_SC_INVALID_OPCODE | NVME_STATUS_DNR;
	if (le32_to_cpu(cmd->rw.nsid) != 1)
		return NVME_SC_INVALID_NS | NVME_STATUS_DNR;

	slba = le64_to_cpu(cmd->rw.slba);
	blocks = le16_to_cpu(cmd->rw.length) + 1U;
	if (check_add_overflow(slba, (u64)blocks, &end_lba) ||
	    end_lba > FWLAB_NVME_NS_LBAS)
		return NVME_SC_LBA_RANGE | NVME_STATUS_DNR;
	length = (size_t)blocks << FWLAB_NVME_LBA_SHIFT;
	if (length > FWLAB_NVME_MAX_XFER)
		return NVME_SC_INVALID_FIELD | NVME_STATUS_DNR;
	offset = (size_t)slba << FWLAB_NVME_LBA_SHIFT;

	if (cmd->common.opcode == nvme_cmd_write) {
		ret = fwlab_nvme_prp_transfer(nvme, cmd, nvme->xfer, length,
					      FWLAB_M4_DMA_READ_HOST);
		if (!ret)
			ret = fwlab_m4_media_write(&nvme->media, nvme->xfer,
						   length, offset);
	} else {
		ret = fwlab_m4_media_read(&nvme->media, nvme->xfer, length,
					  offset);
		if (!ret)
			ret = fwlab_nvme_prp_transfer(nvme, cmd, nvme->xfer,
						      length,
						      FWLAB_M4_DMA_WRITE_HOST);
	}
	return ret ? NVME_SC_INTERNAL | NVME_STATUS_DNR :
		NVME_SC_SUCCESS;
}

static int fwlab_nvme_post_cqe(struct fwlab_m4_nvme *nvme,
				struct fwlab_nvme_queue *queue, u16 qid,
				const struct nvme_command *cmd, u32 result,
				u16 status)
{
	struct nvme_completion cqe = { };
	dma_addr_t address;
	int ret;

	if (!queue->cq_valid || !queue->cq_depth)
		return -EINVAL;
	cqe.result.u32 = cpu_to_le32(result);
	cqe.sq_head = cpu_to_le16(queue->sq_head);
	cqe.sq_id = cpu_to_le16(qid);
	cqe.command_id = cmd->common.command_id;
	cqe.status = cpu_to_le16((status << 1) | queue->cq_phase);
	address = queue->cq_base + queue->cq_tail * sizeof(cqe);
	ret = fwlab_nvme_dma(nvme, address, &cqe, sizeof(cqe),
			     FWLAB_M4_DMA_WRITE_HOST);
	if (ret)
		return ret;

	queue->cq_tail++;
	if (queue->cq_tail == queue->cq_depth) {
		queue->cq_tail = 0;
		queue->cq_phase = !queue->cq_phase;
	}
	dma_wmb();
	nvme->services->notify(nvme->services->context, 0);
	return 0;
}

static int fwlab_nvme_process_queue(struct fwlab_m4_nvme *nvme,
				     struct fwlab_nvme_queue *queue,
				     u16 qid, bool admin)
{
	u32 tail;
	unsigned int processed = 0;

	if (!queue->sq_valid || !queue->cq_valid)
		return 0;
	tail = fwlab_nvme_read32(nvme, fwlab_nvme_sq_db(qid));
	if (tail >= queue->sq_depth)
		return -EINVAL;

	while (queue->sq_head != tail &&
	       processed++ < FWLAB_NVME_MAX_COMMANDS_PER_POLL) {
		struct nvme_command cmd;
		dma_addr_t address = queue->sq_base +
			queue->sq_head * sizeof(cmd);
		u32 result = 0;
		u16 status;
		bool defer = false;
		int ret;

		dma_rmb();
		ret = fwlab_nvme_dma(nvme, address, &cmd, sizeof(cmd),
				     FWLAB_M4_DMA_READ_HOST);
		if (ret)
			return ret;
		queue->sq_head++;
		if (queue->sq_head == queue->sq_depth)
			queue->sq_head = 0;

		if (admin)
			status = fwlab_nvme_admin(nvme, &cmd, &result, &defer);
		else
			status = fwlab_nvme_io(nvme, &cmd);
		if (!defer && fwlab_nvme_post_cqe(nvme, queue, qid, &cmd,
						 result, status))
			return -EIO;
	}
	return 0;
}

static int fwlab_nvme_enable_locked(struct fwlab_m4_nvme *nvme, u32 cc)
{
	struct fwlab_nvme_queue *adminq = &nvme->adminq;
	u32 aqa = fwlab_nvme_read32(nvme, NVME_REG_AQA);
	u16 sq_depth = (aqa & 0xfff) + 1;
	u16 cq_depth = ((aqa >> 16) & 0xfff) + 1;
	u64 asq = fwlab_nvme_read64(nvme, NVME_REG_ASQ);
	u64 acq = fwlab_nvme_read64(nvme, NVME_REG_ACQ);

	if ((cc & NVME_CC_CSS_MASK) != NVME_CC_CSS_NVM ||
	    (cc & FWLAB_NVME_CC_MPS_MASK) ||
	    (cc & FWLAB_NVME_CC_AMS_MASK) != NVME_CC_AMS_RR ||
	    (cc & FWLAB_NVME_CC_IOSQES_MASK) != NVME_CC_IOSQES ||
	    (cc & FWLAB_NVME_CC_IOCQES_MASK) != NVME_CC_IOCQES ||
	    sq_depth < 2 || cq_depth < 2 ||
	    sq_depth > FWLAB_NVME_MAX_Q_DEPTH ||
	    cq_depth > FWLAB_NVME_MAX_Q_DEPTH ||
	    !IS_ALIGNED(asq, PAGE_SIZE) || !IS_ALIGNED(acq, PAGE_SIZE))
		return -EINVAL;

	fwlab_nvme_queue_clear(adminq);
	adminq->sq_valid = true;
	adminq->cq_valid = true;
	adminq->sq_base = asq;
	adminq->cq_base = acq;
	adminq->sq_depth = sq_depth;
	adminq->cq_depth = cq_depth;
	nvme->enabled = true;
	nvme->aer_outstanding = false;
	return 0;
}

static void fwlab_nvme_poll(void *instance, u32 controller_epoch)
{
	struct fwlab_m4_nvme *nvme = instance;
	u32 csts;
	u32 cc;
	int ret = 0;

	if (!nvme)
		return;
	mutex_lock(&nvme->lock);
	if (nvme->observed_epoch != controller_epoch) {
		fwlab_nvme_disable_locked(nvme);
		nvme->observed_epoch = controller_epoch;
		nvme->last_cc = 0;
	}

	cc = fwlab_nvme_read32(nvme, NVME_REG_CC);
	csts = fwlab_nvme_read32(nvme, NVME_REG_CSTS);
	if (!(cc & NVME_CC_ENABLE)) {
		if (nvme->enabled) {
			fwlab_nvme_disable_locked(nvme);
			/* CC.EN 1->0 is a controller reset; old tails are dead. */
			fwlab_nvme_clear_doorbells(nvme);
		}
		csts = 0;
	} else {
		if (!nvme->enabled) {
			ret = fwlab_nvme_enable_locked(nvme, cc);
			if (ret)
				csts = NVME_CSTS_CFS;
			else
				csts = NVME_CSTS_RDY;
		}
		if (nvme->enabled && !ret) {
			csts = NVME_CSTS_RDY;
			if ((cc & NVME_CC_SHN_MASK) != NVME_CC_SHN_NONE)
				csts |= NVME_CSTS_SHST_CMPLT;
			else {
				ret = fwlab_nvme_process_queue(nvme,
							&nvme->adminq, 0, true);
				if (!ret)
					ret = fwlab_nvme_process_queue(nvme,
								&nvme->ioq, 1,
								false);
				if (ret)
					csts |= NVME_CSTS_CFS;
			}
		}
	}
	fwlab_nvme_write32(nvme, NVME_REG_CSTS, csts);
	nvme->last_cc = cc;
	mutex_unlock(&nvme->lock);
}

static bool fwlab_nvme_quiescent(void *instance)
{
	struct fwlab_m4_nvme *nvme = instance;
	bool quiescent;

	if (!nvme)
		return true;
	mutex_lock(&nvme->lock);
	quiescent = !nvme->enabled && !nvme->adminq.sq_valid &&
		    !nvme->adminq.cq_valid && !nvme->ioq.sq_valid &&
		    !nvme->ioq.cq_valid && !nvme->aer_outstanding;
	mutex_unlock(&nvme->lock);
	return quiescent;
}

static void fwlab_nvme_stop(void *instance, u32 controller_epoch)
{
	struct fwlab_m4_nvme *nvme = instance;

	if (!nvme)
		return;
	mutex_lock(&nvme->lock);
	fwlab_nvme_disable_locked(nvme);
	nvme->observed_epoch = controller_epoch;
	nvme->last_cc = 0;
	mutex_unlock(&nvme->lock);
}

const struct fwlab_m4_frontend_ops fwlab_m4_nvme_fixture_ops = {
	.version = FWLAB_M4_FRONTEND_OPS_VERSION,
	.size = sizeof(struct fwlab_m4_frontend_ops),
	.create = fwlab_nvme_create,
	.destroy = fwlab_nvme_destroy,
	.reset = fwlab_nvme_reset,
	.poll = fwlab_nvme_poll,
	.stop = fwlab_nvme_stop,
	.quiescent = fwlab_nvme_quiescent,
};
