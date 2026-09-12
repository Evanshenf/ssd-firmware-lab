// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2026 Evanshenf

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/unaligned.h>
#include <linux/vmalloc.h>

#include "m4_internal.h"
#include "m4_attach_identity.h"
#include "m4_prp_graph.h"
#include "fwlab/unstable/m4_native.h"
#include "fwlab/unstable/m4_owner_native.h"
#include "fwlab/unstable/m4_canary_native.h"

#define NATIVE_DEPTH 32U
#define NATIVE_QUEUES \
	(FWLAB_M4_HOST_PROFILE == FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL ? 3U : 2U)
#define REG_CAP 0x00
#define REG_VS 0x08
#define REG_CC 0x14
#define REG_CSTS 0x1c
#define REG_AQA 0x24
#define REG_ASQ 0x28
#define REG_ACQ 0x30
#define REG_SQ_DB(q) (0x1000 + (q) * 8)
#define REG_CQ_DB(q) (0x1004 + (q) * 8)
#define CC_ENABLE BIT(0)
#define CC_SHUTDOWN GENMASK(15, 14)
#define CSTS_READY BIT(0)
#define CSTS_FATAL BIT(1)
#define CSTS_SHUTDOWN_COMPLETE BIT(3)

/* Opt-in, one-shot journey cuts, restricted to the native test's
 * 512-byte Q1 origin with captured CDW10=128/CDW11=0. Not a policy engine. */
static unsigned int native_cut;
static unsigned long long native_cut_uid;
static int native_cut_permission_result;
static unsigned int native_cut_permission_bit;
static int native_cut_set(const char *value, const struct kernel_param *parameter)
{
	unsigned int requested;
	int ret = kstrtouint(value, 0, &requested);

	(void)parameter;
	if (ret || requested > 4)
		return -EINVAL;
	if (requested && cmpxchg(&native_cut, 0, requested))
		return -EBUSY;
	if (!requested)
		WRITE_ONCE(native_cut, 0);
	return 0;
}
static const struct kernel_param_ops native_cut_ops = {
	.set = native_cut_set,
	.get = param_get_uint,
};
module_param_cb(native_cut, &native_cut_ops, &native_cut, 0600);
MODULE_PARM_DESC(native_cut, "J1 one-shot: 0 off, 1 DMA-in, 2 DMA-out, 3 pre-CQE, 4 service Read");
module_param(native_cut_uid, ullong, 0400);
MODULE_PARM_DESC(native_cut_uid, "Last native journey cut's captured origin UID");
module_param(native_cut_permission_result, int, 0400);
module_param(native_cut_permission_bit, uint, 0400);

/* Kernel declarations of the two distinct, four-byte link-only anchor types. */
struct fwlab_sq_consumer_anchor_v0 { u32 type_tag; };
struct fwlab_cqe_publisher_anchor_v0 { u32 type_tag; };
const struct fwlab_sq_consumer_anchor_v0 __used
	fwlab_authoritative_sq_consumer_v0 = { 0x53514341 };
const struct fwlab_cqe_publisher_anchor_v0 __used
	fwlab_authoritative_cqe_publisher_v0 = { 0x43515041 };

struct native_queue {
	struct fwlab_m4_mapping mapping;
	u64 incarnation;
	u32 holders;
	u32 vector;
	u16 depth;
	u16 head;
	u16 tail;
	u16 cqid;
	u16 pending;
	u8 phase;
	bool valid;
	bool closing;
};

struct native_request {
	u64 uid;
	u64 sq_incarnation;
	u64 cq_incarnation;
	u64 target_incarnation;
	u64 bus_generation;
	u64 owner_epoch;
	u64 authority_uid;
	u64 dma_uid;
	u64 completion_uid;
	u32 epoch;
	u32 direction;
	u32 bytes;
	u32 bytes_done;
	u32 dma_state;
	u32 publication;
	u32 mappings;
	u32 queue_effect;
	u32 effect_qid;
	u32 queue_entries;
	u32 associated_queue;
	u32 interrupt_vector;
	u32 queue_result_dword0;
	int queue_result;
	int dma_result;
	u16 sqid;
	u16 cqid;
	u16 cid;
	u16 sq_head;
	bool active;
	bool delivered;
	bool shaped;
	bool authority_released;
	bool dma_retired;
	bool queue_done;
	bool queue_started;
	bool queue_refs_held;
	bool frame_reserved;
	u8 frame_class;
	u8 sqe[64];
	u8 completion[16];
	struct fwlab_m4_mapping data_mapping[3];
	u8 data[FWLAB_M4_NATIVE_MAX_BYTES];
};

struct native_frame {
	u8 *data;
	u8 *scratch;
	struct fwlab_m4_mapping *mapping;
	u64 owner_uid;
	u32 owner_epoch;
	u32 map_capacity;
	bool held;
};

struct fwlab_m4_hif {
	struct fwlab_m4_pci_ctx *pci;
	struct mutex lock;
	struct miscdevice misc;
	struct native_queue sq[NATIVE_QUEUES];
	struct native_queue cq[NATIVE_QUEUES];
	struct native_request request[NATIVE_DEPTH];
	struct native_frame frame[2]; /* IO then Admin, only in LARGE_SERIAL */
	struct fwlab_m4_host_limits limits;
	u64 function_nonce;
	u64 next_uid;
	u64 next_queue_incarnation;
	u64 next_authority_uid;
	u64 next_dma_uid;
	u64 delivery_uid;
	u64 bus_generation;
	u32 controller_epoch;
	u32 activation_epoch;
	u32 seen_flr_epoch;
	u32 requested_flr_epoch;
	u32 queue_cursor;
	u32 io_cursor;
	u32 negotiated_pairs;
	u64 next_owner_uid;
	struct fwlab_m4_owner_message revoke_key;
	struct fwlab_m4_owner_message revoke_result;
	struct fwlab_m4_owner_message grant_key;
	struct fwlab_m4_owner_message grant_result;
	struct fwlab_m4_domain_identity owner_domain;
	struct {
		struct fwlab_m4_native_message dma, publication;
		struct fwlab_m4_mapping mapping;
		struct fwlab_m4_irq_ticket irq;
		u64 cq_iova, owner_epoch, new_origin;
		u32 flags;
		bool armed, hold;
	} canary;
	bool registered;
	bool opened;
	bool attached;
	bool firmware_ready;
	bool reset_pending;
	bool reset_drained;
	bool shutdown;
	bool faulted;
	bool quarantined;
	bool stopped;
	struct fwlab_m4_attachment attachment;
	char name[48];
};

static bool native_mq2(struct fwlab_m4_hif *hif)
{
	(void)hif;
	return FWLAB_M4_HOST_PROFILE == FWLAB_M4_HOST_PROFILE_LARGE_MQ2_SERIAL;
}

static bool native_large(struct fwlab_m4_hif *hif)
{
	(void)hif;
	return FWLAB_M4_HOST_PROFILE == FWLAB_M4_HOST_PROFILE_LARGE_SERIAL || native_mq2(hif);
}

static struct native_frame *native_request_frame(struct fwlab_m4_hif *hif,
					struct native_request *request)
{
	struct native_frame *frame;
	if (!request->frame_reserved || request->frame_class >= 2)
		return NULL;
	frame = &hif->frame[request->frame_class];
	return frame->held && frame->owner_uid == request->uid &&
		frame->owner_epoch == request->epoch ? frame : NULL;
}

static u8 *native_request_data(struct fwlab_m4_hif *hif, struct native_request *request)
{
	struct native_frame *frame;
	if (!native_large(hif)) return request->data;
	frame = native_request_frame(hif, request);
	return frame ? frame->data : NULL;
}

static struct fwlab_m4_mapping *native_request_maps(struct fwlab_m4_hif *hif,
						 struct native_request *request)
{
	struct native_frame *frame;
	if (!native_large(hif)) return request->data_mapping;
	frame = native_request_frame(hif, request);
	return frame ? frame->mapping : NULL;
}

static bool native_queue_origin_valid(struct fwlab_m4_hif *hif,
				     const struct native_request *request)
{
	const struct native_queue *sq, *cq;
	if (!native_mq2(hif)) return true;
	if (!request->queue_refs_held || request->sqid >= NATIVE_QUEUES ||
	    request->cqid >= NATIVE_QUEUES) return false;
	sq = &hif->sq[request->sqid];
	cq = &hif->cq[request->cqid];
	return sq->valid && cq->valid && sq->holders && cq->holders &&
	       sq->incarnation == request->sq_incarnation &&
	       cq->incarnation == request->cq_incarnation && sq->cqid == request->cqid;
}

static int native_queues_clear(struct fwlab_m4_hif *hif)
{
	u32 index;
	for (index = 0; index < NATIVE_QUEUES; index++)
		if (hif->sq[index].holders || hif->cq[index].holders) return -EBUSY;
	memset(hif->sq, 0, sizeof(hif->sq));
	memset(hif->cq, 0, sizeof(hif->cq));
	hif->negotiated_pairs = 0;
	hif->io_cursor = 1;
	hif->queue_cursor = 0;
	return 0;
}

/* No active effects may remain at the caller's drained cleanup boundary. */
static int native_request_clear(struct fwlab_m4_hif *hif, struct native_request *request)
{
	struct native_frame *frame = NULL;
	struct native_queue *cq = NULL;
	if (native_mq2(hif) && request->queue_started && !request->queue_done)
		return -EBUSY;
	if (native_mq2(hif) && request->queue_refs_held) {
		if (!native_queue_origin_valid(hif, request)) return -EIO;
		cq = &hif->cq[request->cqid];
		if (request->publication == FWLAB_M4_NATIVE_DISCARDED && !cq->pending)
			return -EIO;
	}
	if (native_large(hif) && request->frame_reserved) {
		frame = native_request_frame(hif, request);
		if (!frame) return -EIO;
	}
	if (cq) {
		hif->sq[request->sqid].holders--;
		cq->holders--;
		if (request->publication == FWLAB_M4_NATIVE_DISCARDED) cq->pending--;
		request->queue_refs_held = false;
	}
	if (frame) {
		frame->held = false;
		frame->owner_uid = 0;
		frame->owner_epoch = 0;
	}
	memset(request, 0, sizeof(*request));
	return 0;
}

static int native_requests_clear(struct fwlab_m4_hif *hif)
{
	u32 index;
	for (index = 0; index < NATIVE_DEPTH; index++)
		if (native_request_clear(hif, &hif->request[index])) return -EIO;
	return hif->frame[0].held || hif->frame[1].held ? -EIO : 0;
}

static int native_reset_cleanup(struct fwlab_m4_hif *hif)
{
	u32 index;

	if (native_large(hif)) {
		for (index = 0; index < NATIVE_DEPTH; index++) {
			struct native_request *held = &hif->request[index];
			if (held->active && (held->publication == FWLAB_M4_NATIVE_UNPUBLISHED ||
			    (native_mq2(hif) && held->queue_started && !held->queue_done) ||
			    (held->shaped && (!held->authority_released || !held->dma_retired))))
				return -EBUSY;
		}
	}
	if (native_requests_clear(hif)) return -EIO;
	if (native_mq2(hif) && native_queues_clear(hif)) return -EIO;
	hif->delivery_uid = 0;
	return 0;
}

static void native_frames_free(struct fwlab_m4_hif *hif)
{
	u32 index;
	for (index = 0; index < 2; index++) {
		kvfree(hif->frame[index].data);
		kvfree(hif->frame[index].mapping);
		kfree(hif->frame[index].scratch);
	}
}

struct native_guard {
	struct fwlab_m4_hif *hif;
	u64 bus_generation;
	u32 epoch;
	u64 owner_epoch;
};

static bool native_access_locked(void *context)
{
	struct native_guard *guard = context;
	struct fwlab_m4_hif *hif = guard->hif;
	u16 command = get_unaligned_le16(&hif->pci->config[PCI_COMMAND]);
	u32 cc = readl(hif->pci->bar_mapping + REG_CC);

	return hif->pci->effects_open && hif->firmware_ready && !hif->reset_pending &&
	       hif->pci->owner_phase == FWLAB_M4_OWNER_OWNED &&
	       guard->owner_epoch == hif->pci->owner_epoch &&
	       !hif->quarantined && !hif->stopped &&
	       guard->epoch == hif->controller_epoch &&
	       guard->bus_generation == hif->pci->access_generation &&
	       READ_ONCE(hif->requested_flr_epoch) == hif->seen_flr_epoch &&
	       (command & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
		       (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) &&
	       (cc & CC_ENABLE) && !(cc & CC_SHUTDOWN);
}

static bool native_access(struct fwlab_m4_hif *hif, u64 generation, u32 epoch,
			  u64 owner_epoch)
{
	struct native_guard guard = { hif, generation, epoch, owner_epoch };
	unsigned long flags;
	bool allowed;

	spin_lock_irqsave(&hif->pci->config_lock, flags);
	allowed = native_access_locked(&guard);
	spin_unlock_irqrestore(&hif->pci->config_lock, flags);
	return allowed;
}

static int native_copy(struct fwlab_m4_hif *hif,
		       const struct fwlab_m4_mapping *mapping, u32 offset,
		       void *bytes, u32 length, u64 generation, u32 epoch,
		       u64 owner_epoch)
{
	struct native_guard context = { hif, generation, epoch, owner_epoch };
	struct fwlab_m4_copy_guard guard = {
		.lock = &hif->pci->config_lock,
		.valid_locked = native_access_locked,
		.context = &context,
	};

	if (mapping->domain_nonce != hif->owner_domain.nonce ||
	    mapping->attach_generation != hif->owner_domain.attach_generation)
		return -ESTALE;
	return fwlab_m4_mapping_copy(&hif->pci->pdev->dev, mapping, offset,
				     bytes, length, &guard);
}

static void native_registers_init(struct fwlab_m4_hif *hif)
{
	u64 cap = 0x000000200101001fULL;

#if FWLAB_M4_HOST_PROFILE != FWLAB_M4_HOST_PROFILE_SMALL
	/*
	 * LARGE/MQ2 reconstruct the full FTL before RESET_ACK. Advertise a
	 * fixed 60-second readiness budget for that construction, independent
	 * of namespace geometry. The SMALL reference keeps its original TO.
	 * CAP.TO does not extend Linux's separate shutdown-complete timeout.
	 */
	cap = (cap & ~GENMASK_ULL(31, 24)) | (120ULL << 24);
#endif
	memset_io(hif->pci->bar_mapping, 0, FWLAB_M4_BAR_MAP_SIZE);
	writeq(cap, hif->pci->bar_mapping + REG_CAP);
	writel(0x00010000, hif->pci->bar_mapping + REG_VS);
	wmb();
}

static void native_cancel_transport(struct fwlab_m4_hif *hif)
{
	u32 index;

	fwlab_m4_close_effects(hif->pci);
	hif->firmware_ready = false;
	hif->reset_drained = false;
	hif->delivery_uid = 0;
	for (index = 0; index < NATIVE_DEPTH; index++) {
		struct native_request *request = &hif->request[index];

		if (!request->active)
			continue;
		if (request->dma_state == FWLAB_M4_NATIVE_DMA_RESERVED)
			request->dma_state = FWLAB_M4_NATIVE_DMA_CANCELLED;
		if (request->publication == FWLAB_M4_NATIVE_UNPUBLISHED)
			request->publication = FWLAB_M4_NATIVE_DISCARDED;
	}
	fwlab_m4_clear_msix(hif->pci);
	if (native_mq2(hif)) {
		/* Old origins still own these incarnations while firmware drains.
		 * No new queue can be constructed before RESET_ACK/CERTIFY cleanup. */
		for (index = 0; index < NATIVE_QUEUES; index++) {
			hif->sq[index].closing = true;
			hif->cq[index].closing = true;
		}
	} else {
		memset(hif->sq, 0, sizeof(hif->sq));
		memset(hif->cq, 0, sizeof(hif->cq));
	}
	/* Queue memory may be reused without being zeroed by Linux. Old doorbells
	 * must not make a new epoch consume those retired SQEs before its first
	 * submission. Clear them before RESET_ACK publishes RDY=0. */
	for (index = 0; index < NATIVE_QUEUES; index++) {
		writel(0, hif->pci->bar_mapping + REG_SQ_DB(index));
		writel(0, hif->pci->bar_mapping + REG_CQ_DB(index));
	}
	wmb();
	memset_io(hif->pci->bar_mapping + FWLAB_M4_MSIX_PBA_OFFSET, 0, 8);
}

static void native_begin_reset(struct fwlab_m4_hif *hif, bool shutdown,
			       bool flr)
{
	if (hif->reset_pending || hif->pci->owner_phase != FWLAB_M4_OWNER_OWNED)
		return;
	if (hif->controller_epoch == U32_MAX) {
		hif->quarantined = true;
		fwlab_m4_close_effects(hif->pci);
		writel(CSTS_FATAL, hif->pci->bar_mapping + REG_CSTS);
		return;
	}
	native_cancel_transport(hif);
	hif->controller_epoch++;
	hif->reset_pending = true;
	hif->shutdown = shutdown;
	if (flr)
		native_registers_init(hif);
}

static int native_queue_mapping(struct fwlab_m4_hif *hif,
				struct native_queue *queue, u64 address,
				u32 depth, bool submission)
{
	struct native_queue candidate = {};
	int ret;

	if (!address || !IS_ALIGNED(address, PAGE_SIZE) || depth < 2 ||
	    depth > NATIVE_DEPTH)
		return -EINVAL;
	ret = fwlab_m4_mapping_capture(&hif->pci->pdev->dev, address,
		depth * (submission ? 64 : 16), submission ? FWLAB_M4_DMA_READ_HOST :
		FWLAB_M4_DMA_WRITE_HOST, &candidate.mapping);
	if (ret)
		return ret;
	if (candidate.mapping.domain_nonce != hif->owner_domain.nonce ||
	    candidate.mapping.attach_generation != hif->owner_domain.attach_generation)
		return -ESTALE;
	candidate.depth = depth;
	candidate.phase = 1;
	candidate.valid = true;
	*queue = candidate;
	return 0;
}

static u32 native_fault_status(struct fwlab_m4_hif *hif)
{
	return CSTS_FATAL | ((readl(hif->pci->bar_mapping + REG_CC) & CC_ENABLE)
			    ? CSTS_READY : 0);
}

/* Old work is drained; this never makes the successor firmware ready. */
static void native_drained_status(struct fwlab_m4_hif *hif)
{
	u32 cc = readl(hif->pci->bar_mapping + REG_CC);

	if (!(cc & CC_ENABLE)) {
		hif->faulted = false;
		hif->shutdown = false;
		writel(0, hif->pci->bar_mapping + REG_CSTS);
	} else if (hif->faulted) {
		writel(native_fault_status(hif), hif->pci->bar_mapping + REG_CSTS);
	} else if (hif->shutdown) {
		writel(CSTS_READY | CSTS_SHUTDOWN_COMPLETE,
		       hif->pci->bar_mapping + REG_CSTS);
	}
}

static void native_fault(struct fwlab_m4_hif *hif)
{
	hif->faulted = true;
	native_begin_reset(hif, false, false);
	writel(native_fault_status(hif), hif->pci->bar_mapping + REG_CSTS);
}

static bool native_cut_point(struct fwlab_m4_hif *hif,
			     struct native_request *request, unsigned int point)
{
	if (request->sqid != 1 || request->bytes != 512 ||
	    get_unaligned_le32(request->sqe + 40) != 128 ||
	    get_unaligned_le32(request->sqe + 44) != 0 ||
	    (point == 3 && request->direction != 1) ||
	    cmpxchg(&native_cut, point, 0) != point)
		return false;
	WRITE_ONCE(native_cut_uid, request->uid);
	WRITE_ONCE(native_cut_permission_result, 0);
	WRITE_ONCE(native_cut_permission_bit, 0);
	if (point == 1 || point == 2) {
		u16 command;
		u16 bit = point == 1 ? PCI_COMMAND_MASTER : PCI_COMMAND_MEMORY;
		int ret = -EIO;

		/* Exercise the real config-write generation/permission gate with
		 * this already owned mapping, not a reminted authority. */
		if (!pci_read_config_word(hif->pci->pdev, PCI_COMMAND, &command)) {
			if (!pci_write_config_word(hif->pci->pdev, PCI_COMMAND, command & ~bit))
				ret = native_copy(hif, &native_request_maps(hif, request)[0], 0,
					native_request_data(hif, request), native_request_maps(hif, request)[0].length,
					request->bus_generation, request->epoch, request->owner_epoch);
			if (pci_write_config_word(hif->pci->pdev, PCI_COMMAND, command))
				ret = -EIO;
		}
		WRITE_ONCE(native_cut_permission_result, ret);
		WRITE_ONCE(native_cut_permission_bit, bit);
	}
	native_fault(hif);
	pr_info(FWLAB_M4_PCI_NAME
		": J1_CUT point=%u uid=%llu old_epoch=%u next_epoch=%u bytes_done=%u publication=%u\n",
		point, request->uid, request->epoch, hif->controller_epoch,
		request->bytes_done, request->publication);
	return true;
}

static int native_enable(struct fwlab_m4_hif *hif, u32 cc)
{
	u32 aqa = readl(hif->pci->bar_mapping + REG_AQA);
	struct fwlab_m4_domain_identity domain;
	unsigned long flags;
	u16 command;
	int ret;

	if (hif->pci->effects_generation == U64_MAX)
		return -EOVERFLOW;
	if ((cc & ~CC_SHUTDOWN) != 0x00460001 ||
	    (aqa & 0xf000f000) || hif->faulted)
		return -EINVAL;
	ret = fwlab_m4_domain_identity(&hif->pci->pdev->dev, &domain);
	if (ret || domain.kind != hif->pci->owner_kind)
		return ret ? ret : -EACCES;
	if (!hif->owner_domain.nonce && hif->pci->owner_epoch == 1 &&
	    hif->pci->owner_kind == 1)
		hif->owner_domain = domain;
	if (domain.nonce != hif->owner_domain.nonce)
		return -ESTALE;
	/* PCI reset may temporarily attach the blocking domain, then restore
	 * the same IOAS. Only a freshly drained controller epoch may adopt that
	 * new attachment generation; a different IOAS still requires owner grant. */
	if (domain.attach_generation != hif->owner_domain.attach_generation) {
		if (hif->controller_epoch <= hif->activation_epoch)
			return -ESTALE;
		hif->owner_domain.attach_generation = domain.attach_generation;
	}
	spin_lock_irqsave(&hif->pci->config_lock, flags);
	command = get_unaligned_le16(&hif->pci->config[PCI_COMMAND]);
	hif->bus_generation = hif->pci->access_generation;
	spin_unlock_irqrestore(&hif->pci->config_lock, flags);
	if ((command & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) !=
	    (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER))
		return -EACCES;
	ret = native_queue_mapping(hif, &hif->sq[0],
		readq(hif->pci->bar_mapping + REG_ASQ), (aqa & 0xfff) + 1, true);
	if (!ret)
		ret = native_queue_mapping(hif, &hif->cq[0],
			readq(hif->pci->bar_mapping + REG_ACQ),
			((aqa >> 16) & 0xfff) + 1, false);
	if (ret)
		return ret;
	if (native_mq2(hif)) {
		if (hif->next_queue_incarnation >= U64_MAX - 1) return -EOVERFLOW;
		hif->sq[0].incarnation = ++hif->next_queue_incarnation;
		hif->cq[0].incarnation = ++hif->next_queue_incarnation;
		hif->io_cursor = 1;
	}
	pr_info(FWLAB_M4_PCI_NAME ": enable epoch=%u owner=%llu stale_db=%u/%u\n",
		hif->controller_epoch, hif->pci->owner_epoch,
		readl(hif->pci->bar_mapping + REG_SQ_DB(0)),
		readl(hif->pci->bar_mapping + REG_CQ_DB(0)));
	/* Old software may still write a doorbell after an earlier failure cut.
	 * The new controller may consume nothing before publishing RDY, so reset
	 * its new queue doorbells at that activation boundary as well. */
	writel(0, hif->pci->bar_mapping + REG_SQ_DB(0));
	writel(0, hif->pci->bar_mapping + REG_CQ_DB(0));
	writel(0, hif->pci->bar_mapping + REG_SQ_DB(1));
	writel(0, hif->pci->bar_mapping + REG_CQ_DB(1));
	if (native_mq2(hif)) {
		writel(0, hif->pci->bar_mapping + REG_SQ_DB(2));
		writel(0, hif->pci->bar_mapping + REG_CQ_DB(2));
	}
	wmb();
	spin_lock_irqsave(&hif->pci->config_lock, flags);
	hif->pci->effects_open = true;
	spin_unlock_irqrestore(&hif->pci->config_lock, flags);
	hif->activation_epoch = hif->controller_epoch;
	writel(CSTS_READY, hif->pci->bar_mapping + REG_CSTS);
	wmb();
	return 0;
}

static struct native_request *native_find(struct fwlab_m4_hif *hif,
					  u64 uid, u32 epoch)
{
	u32 index;

	for (index = 0; index < NATIVE_DEPTH; index++)
		if (hif->request[index].active && hif->request[index].uid == uid &&
		    hif->request[index].epoch == epoch)
			return &hif->request[index];
	return NULL;
}

static int native_capture(struct fwlab_m4_hif *hif, u32 qid, u32 *captured)
{
	struct native_queue *sq = &hif->sq[qid];
	struct native_queue *cq;
	struct native_request *request = NULL;
	u32 tail, head, used, index;
	int ret;

	*captured = 0;
	if (native_mq2(hif) && sq->closing) return 0;
	if (native_large(hif) && hif->frame[qid == 0 ? 1 : 0].held)
		return 0;
	if (!sq->valid || sq->cqid >= NATIVE_QUEUES)
		return 0;
	cq = &hif->cq[sq->cqid];
	if (native_mq2(hif) && cq->closing) return 0;
	if (!cq->valid)
		return -EINVAL;
	tail = readl(hif->pci->bar_mapping + REG_SQ_DB(qid));
	head = readl(hif->pci->bar_mapping + REG_CQ_DB(sq->cqid));
	if (tail >= sq->depth || head >= cq->depth)
		return -EINVAL;
	if (tail == sq->head)
		return 0;
	used = (cq->tail + cq->depth - head) % cq->depth;
	if (used + cq->pending >= cq->depth - 1)
		return 0;
	for (index = 0; index < NATIVE_DEPTH; index++)
		if (!hif->request[index].active) {
			request = &hif->request[index];
			break;
		}
	if (!request)
		return 0;
	if (hif->next_uid == U64_MAX)
		return -EOVERFLOW;
	memset(request, 0, sizeof(*request));
	ret = native_copy(hif, &sq->mapping, sq->head * 64, request->sqe, 64,
			  hif->bus_generation, hif->controller_epoch, hif->pci->owner_epoch);
	if (ret)
		return ret;
	request->uid = hif->next_uid++;
	request->epoch = hif->controller_epoch;
	request->owner_epoch = hif->pci->owner_epoch;
	request->bus_generation = hif->bus_generation;
	request->sqid = qid;
	if (native_mq2(hif)) {
		request->cqid = sq->cqid;
		request->sq_incarnation = sq->incarnation;
		request->cq_incarnation = cq->incarnation;
		request->queue_refs_held = true;
		sq->holders++;
		cq->holders++;
	}
	request->cid = get_unaligned_le16(request->sqe + 2);
	request->sq_head = (sq->head + 1) % sq->depth;
	if (native_large(hif)) {
		struct native_frame *frame = &hif->frame[qid == 0 ? 1 : 0];
		request->frame_class = qid == 0 ? 1 : 0;
		request->frame_reserved = true;
		frame->held = true;
		frame->owner_uid = request->uid;
		frame->owner_epoch = request->epoch;
	}
	request->active = true;
	sq->head = request->sq_head;
	cq->pending++;
	*captured = 1;
	pr_debug(FWLAB_M4_PCI_NAME ": capture q=%u uid=%llu op=%#x cdw10=%#x\n",
		 qid, request->uid, request->sqe[0],
		 get_unaligned_le32(request->sqe + 40));
	/* One actual service-error return, before any userspace delivery/DMA.
	 * Reuse the existing root-only cut and exact 512-byte Q1/LBA128 Read.
	 * The normal caller's fault path, not this hook, performs reset closure. */
	if (unlikely(READ_ONCE(native_cut) == 4) && qid == 1 &&
	    request->sqe[0] == 2 && get_unaligned_le32(request->sqe + 4) == 1 &&
	    get_unaligned_le32(request->sqe + 40) == 128 &&
	    !get_unaligned_le32(request->sqe + 44) &&
	    !get_unaligned_le32(request->sqe + 48) && cmpxchg(&native_cut, 4, 0) == 4) {
		WRITE_ONCE(native_cut_uid, request->uid);
		pr_info(FWLAB_M4_PCI_NAME ": J1_SERVICE_CUT uid=%llu epoch=%u result=%d\n",
			request->uid, request->epoch, -EIO);
		return -EIO;
	}
	return 0;
}

int fwlab_m4_hif_request_reset(struct fwlab_m4_hif *hif, u32 epoch)
{
	if (!hif || !epoch)
		return -EINVAL;
	WRITE_ONCE(hif->requested_flr_epoch, epoch);
	return 0;
}

/* Caller owns the HIF mutex. Both producers use this one semantic engine;
 * the private pump visits at most two queues and retains at most one capture. */
static int native_service_locked(struct fwlab_m4_hif *hif, u32 visits, u32 *captured)
{
	u32 cc, flr, visited;
	int ret = 0;

	*captured = 0;
	if (hif->stopped || hif->quarantined || !hif->pci->pdev)
		goto out;
	cc = readl(hif->pci->bar_mapping + REG_CC);
	flr = READ_ONCE(hif->requested_flr_epoch);
	if (flr != hif->seen_flr_epoch) {
		hif->seen_flr_epoch = flr;
		if (hif->pci->owner_phase == FWLAB_M4_OWNER_OWNED &&
		    hif->reset_pending && hif->reset_drained) {
			/* Coalesce another FLR while no old references or effects exist. */
			native_registers_init(hif);
			hif->shutdown = false;
			hif->faulted = false;
			cc = readl(hif->pci->bar_mapping + REG_CC);
		} else if (hif->pci->owner_phase == FWLAB_M4_OWNER_OWNED)
			native_begin_reset(hif, false, true);
		else
			native_registers_init(hif);
	}
	if (hif->pci->owner_phase != FWLAB_M4_OWNER_OWNED)
		goto out;
	if (hif->pci->effects_open && (!(cc & CC_ENABLE) || (cc & CC_SHUTDOWN)))
		native_begin_reset(hif, !!(cc & CC_SHUTDOWN), false);
	if (!(cc & CC_ENABLE)) {
		hif->faulted = false;
		if (hif->reset_pending && hif->reset_drained) {
			hif->shutdown = false;
			writel(0, hif->pci->bar_mapping + REG_CSTS);
		} else if (!hif->reset_pending && !hif->pci->effects_open)
			writel(0, hif->pci->bar_mapping + REG_CSTS);
	}
	if (hif->reset_pending || !hif->firmware_ready)
		goto out;
	/* A local fault waits for a real Host disable edge. Firmware recovery
	 * alone must not reopen the same enabled controller or mint new epochs. */
	if (hif->faulted)
		goto out;
	if (hif->shutdown) {
		if (cc & CC_ENABLE)
			goto out;
		hif->shutdown = false;
		writel(0, hif->pci->bar_mapping + REG_CSTS);
	}
	if (!hif->pci->effects_open && (cc & CC_ENABLE)) {
		ret = native_enable(hif, cc);
		if (ret)
			goto fault;
	}
	if (!hif->pci->effects_open)
		goto out;
	if (!native_access(hif, hif->bus_generation, hif->controller_epoch,
			   hif->pci->owner_epoch)) {
		ret = -ESTALE;
		goto fault;
	}
	for (visited = 0; visited < visits; visited++) {
		u32 qid = hif->queue_cursor;
		if (native_mq2(hif) && qid)
			qid = qid == 1 ? hif->io_cursor : 3 - hif->io_cursor;
		ret = native_capture(hif, qid, captured);
		hif->queue_cursor = (hif->queue_cursor + 1) % NATIVE_QUEUES;
		if (native_mq2(hif) && *captured) {
			if (qid) {
				hif->io_cursor = qid == 1 ? 2 : 1;
				hif->queue_cursor = 0; /* Offer Admin after an I/O grant. */
			} else {
				hif->queue_cursor = 1; /* Then offer the preferred I/O queue. */
			}
		}
		if (ret || *captured)
			break;
	}
	fwlab_m4_flush_msix(hif->pci);
	if (!ret)
		goto out;
fault:
	native_fault(hif);
out:
	return ret;
}

int fwlab_m4_hif_step(struct fwlab_m4_hif *hif)
{
	u32 captured;
	int ret;

	if (!hif)
		return -EINVAL;
	mutex_lock(&hif->lock);
	ret = native_service_locked(hif, 1, &captured);
	mutex_unlock(&hif->lock);
	return ret;
}

struct native_graph_context {
	struct fwlab_m4_hif *hif;
	struct native_request *request;
	struct native_frame *frame;
	u32 count;
	enum fwlab_m4_dma_direction direction;
};

static int native_graph_read(void *opaque, u64 address, u32 bytes, void *output)
{
	struct native_graph_context *context = opaque;
	struct fwlab_m4_hif *hif = context->hif;
	struct native_request *request = context->request;
	struct fwlab_m4_mapping list;
	int ret = fwlab_m4_mapping_capture(&hif->pci->pdev->dev, address, bytes,
		FWLAB_M4_DMA_READ_HOST, &list);
	if (ret) return ret;
	if (list.domain_nonce != hif->owner_domain.nonce ||
	    list.attach_generation != hif->owner_domain.attach_generation)
		return -ESTALE;
	return native_copy(hif, &list, 0, output, bytes,
		request->bus_generation, request->epoch, request->owner_epoch);
}

static int native_graph_capture(void *opaque, u64 address, u32 bytes)
{
	struct native_graph_context *context = opaque;
	struct fwlab_m4_hif *hif = context->hif;
	struct fwlab_m4_mapping *mapping;
	int ret;
	if (context->count >= context->frame->map_capacity) return -E2BIG;
	mapping = &context->frame->mapping[context->count];
	ret = fwlab_m4_mapping_capture(&hif->pci->pdev->dev, address, bytes,
		context->direction, mapping);
	if (ret) return ret;
	if (mapping->domain_nonce != hif->owner_domain.nonce ||
	    mapping->attach_generation != hif->owner_domain.attach_generation)
		return -ESTALE;
	++context->count;
	return 0;
}

static int native_shape(struct fwlab_m4_hif *hif,
			 struct native_request *request,
			 struct fwlab_m4_native_message *message)
{
	struct fwlab_m4_mapping list;
	struct fwlab_m4_mapping *maps = native_request_maps(hif, request);
	u64 address[3], prp1, prp2;
	__le64 entries[2];
	u32 length[3], remaining, count = 1, index;
	int direction, ret;

	if (!maps || !message->bytes || message->bytes >
	    (request->sqid ? hif->limits.max_io_bytes : hif->limits.max_admin_bytes) ||
	    (message->direction != 1 && message->direction != 2))
		return -EINVAL;
	if (request->shaped) {
		if (request->authority_released || request->bytes != message->bytes ||
		    request->direction != message->direction)
			return -EINVAL;
		goto result;
	}
	if (!native_access(hif, request->bus_generation, request->epoch, request->owner_epoch))
		return -ESTALE;
	prp1 = get_unaligned_le64(request->sqe + 24);
	prp2 = get_unaligned_le64(request->sqe + 32);
	if (!prp1 || !IS_ALIGNED(prp1, 4))
		return -EINVAL;
	if (native_large(hif)) {
		struct native_graph_context context = { hif, request,
			native_request_frame(hif, request), 0,
			message->direction == 1 ? FWLAB_M4_DMA_READ_HOST : FWLAB_M4_DMA_WRITE_HOST };
		struct fwlab_m4_prp_walk walk = { .context = &context,
			.read_list = native_graph_read, .capture = native_graph_capture,
			.scratch = context.frame->scratch,
			.scratch_bytes = FWLAB_M4_CONTROL_PAGE_BYTES };
		ret = fwlab_m4_prp_build(&walk, &hif->limits, prp1, prp2, message->bytes);
		if (ret) return ret;
		count = context.count;
		goto graph_ready;
	}
	address[0] = prp1;
	length[0] = min_t(u32, message->bytes, PAGE_SIZE - offset_in_page(prp1));
	remaining = message->bytes - length[0];
	if (remaining) {
		if (!prp2 || !IS_ALIGNED(prp2, PAGE_SIZE))
			return -EINVAL;
		if (remaining <= PAGE_SIZE) {
			address[1] = prp2;
			length[1] = remaining;
			count = 2;
		} else {
			ret = fwlab_m4_mapping_capture(&hif->pci->pdev->dev, prp2,
				sizeof(entries), FWLAB_M4_DMA_READ_HOST, &list);
			if (!ret)
				ret = native_copy(hif, &list, 0, entries, sizeof(entries),
					request->bus_generation, request->epoch, request->owner_epoch);
			if (ret)
				return ret;
			address[1] = le64_to_cpu(entries[0]);
			address[2] = le64_to_cpu(entries[1]);
			length[1] = PAGE_SIZE;
			length[2] = remaining - PAGE_SIZE;
			count = 3;
		}
	}
	direction = message->direction == 1 ? FWLAB_M4_DMA_READ_HOST :
					      FWLAB_M4_DMA_WRITE_HOST;
	for (index = 0; index < count; index++) {
		if (!address[index] || (index && !IS_ALIGNED(address[index], PAGE_SIZE)))
			return -EINVAL;
		ret = fwlab_m4_mapping_capture(&hif->pci->pdev->dev, address[index],
			length[index], direction, &maps[index]);
		if (ret)
			return ret;
		if (maps[index].domain_nonce != hif->owner_domain.nonce ||
		    maps[index].attach_generation != hif->owner_domain.attach_generation)
			return -ESTALE;
	}
graph_ready:
	if (!native_access(hif, request->bus_generation, request->epoch, request->owner_epoch))
		return -ESTALE;
	if (hif->next_authority_uid == U64_MAX || hif->next_dma_uid == U64_MAX)
		return -EOVERFLOW;
	request->authority_uid = hif->next_authority_uid++;
	request->dma_uid = hif->next_dma_uid++;
	request->direction = message->direction;
	request->bytes = message->bytes;
	request->mappings = count;
	request->dma_state = FWLAB_M4_NATIVE_DMA_RESERVED;
	request->shaped = true;
	if (hif->canary.armed && !hif->canary.flags && request->sqid == 0 &&
	    request->sqe[0] == 6 && request->direction == 2 && request->bytes == PAGE_SIZE) {
		struct fwlab_m4_native_message *old = &hif->canary.dma;
		memset(old, 0, sizeof(*old));
		old->version = FWLAB_M4_NATIVE_VERSION;
		old->size = sizeof(*old);
		old->operation = FWLAB_M4_NATIVE_DMA;
		old->function_nonce = hif->function_nonce;
		old->origin_uid = request->uid;
		old->controller_epoch = request->epoch;
		old->authority_uid = request->authority_uid;
		old->dma_uid = request->dma_uid;
		old->bytes = request->bytes;
		old->direction = request->direction;
		hif->canary.mapping = maps[0];
		hif->canary.owner_epoch = request->owner_epoch;
		hif->canary.flags = 1;
	}
result:
	message->authority_uid = request->authority_uid;
	message->dma_uid = request->dma_uid;
	message->dma_state = request->dma_state;
	return 0;
}

static int native_dma(struct fwlab_m4_hif *hif, struct native_request *request,
		       struct fwlab_m4_native_message *message, bool query)
{
	u32 index;
	int ret = 0;
	u8 *data = native_request_data(hif, request);
	struct fwlab_m4_mapping *maps = native_request_maps(hif, request);

	if (!data || !maps || !request->shaped || message->authority_uid != request->authority_uid ||
	    message->dma_uid != request->dma_uid || message->bytes != request->bytes ||
	    message->direction != request->direction)
		return -ESTALE;
	if (!query && (request->authority_released || request->dma_retired))
		return -ESTALE;
	if (!query && request->dma_state == FWLAB_M4_NATIVE_DMA_RESERVED) {
		if (!native_access(hif, request->bus_generation, request->epoch, request->owner_epoch)) {
			request->dma_state = FWLAB_M4_NATIVE_DMA_CANCELLED;
			goto result;
		}
		if (native_cut_point(hif, request, request->direction == 1 ? 1 : 2))
			goto result;
		if (request->direction == 2 &&
		    copy_from_user(data, u64_to_user_ptr(message->data_pointer),
				   request->bytes))
			return -EFAULT;
		for (index = 0; index < request->mappings; index++) {
			ret = native_copy(hif, &maps[index], 0,
				data + request->bytes_done,
				maps[index].length,
				request->bus_generation, request->epoch, request->owner_epoch);
			if (ret)
				break;
			request->bytes_done += maps[index].length;
		}
		request->dma_result = ret;
		request->dma_state = ret ? FWLAB_M4_NATIVE_DMA_FAILED :
					 FWLAB_M4_NATIVE_DMA_DONE;
	}
result:
	message->dma_state = request->dma_state;
	message->bytes_done = request->bytes_done;
	if (request->direction == 1 && request->bytes_done &&
	    copy_to_user(u64_to_user_ptr(message->data_pointer), data,
			 request->bytes_done))
		return -EFAULT;
	return 0;
}

static bool native_queue_arguments_equal(const struct native_request *request,
					const struct fwlab_m4_native_message *message)
{
	return request->queue_effect == message->queue_effect &&
	       request->effect_qid == message->queue_id &&
	       request->queue_entries == message->queue_entries &&
	       request->associated_queue == message->associated_queue &&
	       request->interrupt_vector == message->interrupt_vector;
}

static int native_queue_effect_mq2(struct fwlab_m4_hif *hif,
				 struct native_request *request,
				 struct fwlab_m4_native_message *message)
{
	struct native_queue candidate = {}, *sq, *cq;
	struct native_guard guard = { hif, request->bus_generation, request->epoch,
				    request->owner_epoch };
	u32 qid = message->queue_id, i;
	unsigned long flags;
	bool deleting = message->queue_effect == FWLAB_M4_NATIVE_DELETE_SQ ||
			message->queue_effect == FWLAB_M4_NATIVE_DELETE_CQ;
	bool retire_route = false;
	int ret = 0;

	if (request->queue_started) {
		if (!native_queue_arguments_equal(request, message)) return -EINVAL;
		if (request->queue_done) {
			message->result_dword0 = request->queue_result_dword0;
			return request->queue_result;
		}
	} else {
		request->queue_started = true;
		request->queue_effect = message->queue_effect;
		request->effect_qid = qid;
		request->queue_entries = message->queue_entries;
		request->associated_queue = message->associated_queue;
		request->interrupt_vector = message->interrupt_vector;
	}
	/* An accepted deletion may finish metadata/route cleanup after reset or
	 * owner revoke. It cannot admit new work, mint DMA, or publish a CQE. */
	if (request->sqid || (!native_access(hif, request->bus_generation,
		request->epoch, request->owner_epoch) &&
		!(deleting && request->target_incarnation))) { ret = -ESTALE; goto done; }
	if (message->queue_effect == FWLAB_M4_NATIVE_NUMBER_OF_QUEUES) {
		if (qid || !message->queue_entries || !message->associated_queue ||
		    message->queue_entries > U16_MAX || message->associated_queue > U16_MAX ||
		    message->interrupt_vector) { ret = -EINVAL; goto done; }
	} else if (!qid || qid >= NATIVE_QUEUES || qid > hif->negotiated_pairs) {
		ret = -EINVAL;
		goto done;
	}
	if (message->queue_effect == FWLAB_M4_NATIVE_CREATE_CQ ||
	    message->queue_effect == FWLAB_M4_NATIVE_CREATE_SQ) {
		if (message->queue_entries != NATIVE_DEPTH) { ret = -EINVAL; goto done; }
		ret = native_queue_mapping(hif, &candidate, get_unaligned_le64(request->sqe + 24),
			message->queue_entries, message->queue_effect == FWLAB_M4_NATIVE_CREATE_SQ);
		if (ret) goto done;
	}
	sq = &hif->sq[qid];
	cq = &hif->cq[qid];
	spin_lock_irqsave(&hif->pci->config_lock, flags);
	if (!native_access_locked(&guard) && !(deleting && request->target_incarnation)) {
		ret = -ESTALE;
		goto unlock;
	}
	switch (message->queue_effect) {
	case FWLAB_M4_NATIVE_NUMBER_OF_QUEUES:
		for (i = 1; i < NATIVE_QUEUES; i++)
			if (hif->sq[i].valid || hif->cq[i].valid) { ret = -EBUSY; break; }
		if (!ret) {
			u32 n = min_t(u32, hif->limits.io_queue_pairs,
				min(message->queue_entries, message->associated_queue));
			hif->negotiated_pairs = n;
			request->queue_result_dword0 = (n - 1) | ((n - 1) << 16);
		}
		break;
	case FWLAB_M4_NATIVE_CREATE_CQ:
		if (cq->valid || cq->holders || message->interrupt_vector != qid)
			ret = -EINVAL;
		else if (hif->next_queue_incarnation == U64_MAX)
			ret = -EOVERFLOW;
		else {
			candidate.incarnation = ++hif->next_queue_incarnation;
			candidate.vector = qid;
			writel(0, hif->pci->bar_mapping + REG_CQ_DB(qid));
			*cq = candidate;
		}
		break;
	case FWLAB_M4_NATIVE_CREATE_SQ:
		if (sq->valid || sq->holders || !cq->valid || cq->closing ||
		    message->associated_queue != qid || message->interrupt_vector)
			ret = -EINVAL;
		else if (hif->next_queue_incarnation == U64_MAX)
			ret = -EOVERFLOW;
		else {
			candidate.incarnation = ++hif->next_queue_incarnation;
			candidate.cqid = qid;
			writel(0, hif->pci->bar_mapping + REG_SQ_DB(qid));
			*sq = candidate;
		}
		break;
	case FWLAB_M4_NATIVE_DELETE_SQ:
		if (!sq->valid || (request->target_incarnation &&
		    request->target_incarnation != sq->incarnation)) { ret = -ESTALE; break; }
		if (!request->target_incarnation) {
			if (sq->closing) { ret = -EBUSY; break; }
			request->target_incarnation = sq->incarnation;
			sq->closing = true;
		}
		if (sq->holders) ret = -EINPROGRESS;
		else memset(sq, 0, sizeof(*sq));
		break;
	case FWLAB_M4_NATIVE_DELETE_CQ:
		if (!cq->valid || (request->target_incarnation &&
		    request->target_incarnation != cq->incarnation)) { ret = -ESTALE; break; }
		for (i = 1; i < NATIVE_QUEUES; i++)
			if (hif->sq[i].valid && hif->sq[i].cqid == qid) { ret = -EBUSY; break; }
		if (ret) break;
		if (!request->target_incarnation) {
			if (cq->closing) { ret = -EBUSY; break; }
			request->target_incarnation = cq->incarnation;
			cq->closing = true;
		}
		if (cq->holders || cq->pending) ret = -EINPROGRESS;
		else retire_route = true;
		break;
	default:
		ret = -EINVAL;
		break;
	}
unlock:
	spin_unlock_irqrestore(&hif->pci->config_lock, flags);
	/* Topology stays under hif->lock, but route synchronization cannot hold
	 * config_lock while waiting for an IRQ callback which also takes it. */
	if (retire_route && !ret) {
		ret = fwlab_m4_retire_msix_route(hif->pci, cq->vector);
		if (!ret) memset(cq, 0, sizeof(*cq));
	}
done:
	if (ret != -EINPROGRESS) {
		request->queue_done = true;
		request->queue_result = ret;
	}
	message->result_dword0 = request->queue_result_dword0;
	return ret;
}

static int native_queue_effect(struct fwlab_m4_hif *hif,
			       struct native_request *request,
			       struct fwlab_m4_native_message *message)
{
	int ret = 0;
	u64 address;
	unsigned long flags;
	struct native_queue candidate = {};
	struct native_guard guard = { hif, request->bus_generation, request->epoch,
				    request->owner_epoch };

	if (native_mq2(hif)) return native_queue_effect_mq2(hif, request, message);
	if (request->queue_done) {
		if (request->queue_effect != message->queue_effect ||
		    request->effect_qid != message->queue_id ||
		    request->queue_entries != message->queue_entries ||
		    request->associated_queue != message->associated_queue ||
		    request->interrupt_vector != message->interrupt_vector)
			return -EINVAL;
		return request->queue_result;
	}
	if (!native_access(hif, request->bus_generation, request->epoch, request->owner_epoch))
		return -ESTALE;
	if (message->queue_effect != FWLAB_M4_NATIVE_NUMBER_OF_QUEUES &&
	    message->queue_id != 1)
		return -EINVAL;
	address = get_unaligned_le64(request->sqe + 24);
	if (message->queue_effect == FWLAB_M4_NATIVE_CREATE_CQ ||
	    message->queue_effect == FWLAB_M4_NATIVE_CREATE_SQ) {
		ret = native_queue_mapping(hif, &candidate, address, message->queue_entries,
			message->queue_effect == FWLAB_M4_NATIVE_CREATE_SQ);
		if (ret)
			return ret;
	}
	spin_lock_irqsave(&hif->pci->config_lock, flags);
	if (!native_access_locked(&guard)) {
		spin_unlock_irqrestore(&hif->pci->config_lock, flags);
		return -ESTALE;
	}
	switch (message->queue_effect) {
	case FWLAB_M4_NATIVE_NUMBER_OF_QUEUES:
		break;
	case FWLAB_M4_NATIVE_CREATE_CQ:
		if (hif->cq[1].valid || message->interrupt_vector != 0)
			ret = -EINVAL;
		else {
			writel(0, hif->pci->bar_mapping + REG_CQ_DB(1));
			hif->cq[1] = candidate;
		}
		break;
	case FWLAB_M4_NATIVE_CREATE_SQ:
		if (hif->sq[1].valid || !hif->cq[1].valid ||
		    message->associated_queue != 1)
			ret = -EINVAL;
		else {
			writel(0, hif->pci->bar_mapping + REG_SQ_DB(1));
			hif->sq[1] = candidate;
			hif->sq[1].cqid = 1;
		}
		break;
	case FWLAB_M4_NATIVE_DELETE_SQ:
		if (!hif->sq[1].valid || hif->cq[1].pending)
			ret = -EBUSY;
		else
			memset(&hif->sq[1], 0, sizeof(hif->sq[1]));
		break;
	case FWLAB_M4_NATIVE_DELETE_CQ:
		if (!hif->cq[1].valid || hif->sq[1].valid)
			ret = -EBUSY;
		else
			memset(&hif->cq[1], 0, sizeof(hif->cq[1]));
		break;
	default:
		ret = -EINVAL;
		break;
	}
	spin_unlock_irqrestore(&hif->pci->config_lock, flags);
	request->queue_done = true;
	request->queue_effect = message->queue_effect;
	request->effect_qid = message->queue_id;
	request->queue_entries = message->queue_entries;
	request->associated_queue = message->associated_queue;
	request->interrupt_vector = message->interrupt_vector;
	request->queue_result = ret;
	return ret;
}

static int native_publish(struct fwlab_m4_hif *hif,
			  struct native_request *request,
			  struct fwlab_m4_native_message *message, bool query)
{
	struct native_queue *cq;
	struct fwlab_m4_irq_ticket irq;
	u8 bytes[16] = {};
	u16 status;
	int ret;

	if (query) {
		if (request->completion_uid &&
		    request->completion_uid != message->completion_uid)
			return -ESTALE;
		message->publication = request->publication;
		return 0;
	}
	if (!message->completion_uid || message->status_code > 255 ||
	    message->status_code_type > 7 || message->do_not_retry > 1 ||
	    message->more > 1 || message->retry_delay > 3)
		return -EINVAL;
	status = (message->status_code << 1) | (message->status_code_type << 9) |
		 (message->retry_delay << 12) | (message->more << 14) |
		 (message->do_not_retry << 15);
	put_unaligned_le32(message->result_dword0, bytes);
	put_unaligned_le16(request->sq_head, bytes + 8);
	put_unaligned_le16(request->sqid, bytes + 10);
	put_unaligned_le16(request->cid, bytes + 12);
	put_unaligned_le16(status, bytes + 14);
	if (request->completion_uid) {
		if (request->completion_uid != message->completion_uid ||
		    memcmp(request->completion, bytes, sizeof(bytes)))
			return -ESTALE;
		message->publication = request->publication;
		return 0;
	}
	if (!native_access(hif, request->bus_generation, request->epoch, request->owner_epoch)) {
		native_begin_reset(hif,
			!!(readl(hif->pci->bar_mapping + REG_CC) & CC_SHUTDOWN), false);
		message->publication = request->publication;
		return 0;
	}
	if (native_cut_point(hif, request, 3)) {
		message->publication = request->publication;
		return 0;
	}
	if (!native_queue_origin_valid(hif, request)) return -ESTALE;
	cq = &hif->cq[native_mq2(hif) ? request->cqid : hif->sq[request->sqid].cqid];
	if (!cq->valid || !cq->pending)
		return -EINVAL;
	ret = fwlab_m4_prepare_msix_vector(hif->pci, request->owner_epoch,
		request->bus_generation, hif->seen_flr_epoch, cq->vector, &irq);
	if (ret)
		return ret;
	request->completion_uid = message->completion_uid;
	memcpy(request->completion, bytes, sizeof(bytes));
	put_unaligned_le16(status | cq->phase, bytes + 14);
	ret = native_copy(hif, &cq->mapping, cq->tail * 16, bytes, 14,
			  request->bus_generation, request->epoch, request->owner_epoch);
	if (!ret) {
		wmb();
		ret = native_copy(hif, &cq->mapping, cq->tail * 16 + 14, bytes + 14, 2,
				  request->bus_generation, request->epoch, request->owner_epoch);
	}
	if (ret) {
		native_fault(hif);
		message->publication = request->publication;
		return 0;
	}
	request->publication = FWLAB_M4_NATIVE_COMMITTED;
	cq->pending--;
	if (++cq->tail == cq->depth) {
		cq->tail = 0;
		cq->phase ^= 1;
	}
	wmb();
	(void)fwlab_m4_raise_msix(hif->pci, &irq);
	if (hif->canary.armed && hif->canary.flags == 1 &&
	    request->uid == hif->canary.dma.origin_uid) {
		hif->canary.publication = *message;
		hif->canary.publication.operation = FWLAB_M4_NATIVE_PUBLISH;
		hif->canary.cq_iova = cq->mapping.iova;
		hif->canary.irq = irq;
		hif->canary.flags = 7;
		hif->canary.armed = false;
	}
	message->publication = request->publication;
	return 0;
}

static int native_attach_locked(struct fwlab_m4_hif *hif, u32 producer, u32 profile,
				u32 format,
				const u8 uuid[16], const u8 binding[32])
{
	int ret;

	if (hif->quarantined)
		return -EBUSY;
	ret = fwlab_m4_attach_pin_host_profile(&hif->attachment, FWLAB_M4_HOST_PROFILE,
		FWLAB_M4_PRODUCER, profile, producer, format, uuid, binding);
	if (!ret)
		hif->attached = true;
	return ret;
}

static int native_exchange(struct fwlab_m4_hif *hif,
			   struct fwlab_m4_native_message *message)
{
	struct native_request *request;
	u32 index;

	if (message->operation == FWLAB_M4_NATIVE_ATTACH) {
		int ret;

		if (hif->quarantined ||
		    !memchr_inv(message->media_uuid, 0, sizeof(message->media_uuid)) ||
		    !memchr_inv(message->binding_sha256, 0, sizeof(message->binding_sha256)))
			return -EBUSY;
		ret = native_attach_locked(hif, FWLAB_M4_PRODUCER_BAR,
			FWLAB_M4_HOST_PROFILE_SMALL, FWLAB_M4_MEDIA_LEGACY,
			message->media_uuid, message->binding_sha256);
		if (ret)
			return ret;
		memcpy(message->media_uuid, hif->attachment.media_uuid, sizeof(message->media_uuid));
		memcpy(message->binding_sha256, hif->attachment.binding_sha256,
		       sizeof(message->binding_sha256));
		message->function_nonce = hif->function_nonce;
		message->controller_epoch = hif->controller_epoch;
		return 0;
	}
	if (!hif->attached || message->function_nonce != hif->function_nonce)
		return -ESTALE;
	if (message->operation == FWLAB_M4_NATIVE_REVOKE) {
		hif->faulted = true;
		native_begin_reset(hif, false, false);
		writel(CSTS_FATAL, hif->pci->bar_mapping + REG_CSTS);
		message->controller_epoch = hif->controller_epoch;
		return 0;
	}
	if (message->operation == FWLAB_M4_NATIVE_STATUS ||
	    message->operation == FWLAB_M4_NATIVE_NEXT) {
		message->controller_epoch = hif->controller_epoch;
		message->event = hif->reset_pending ? FWLAB_M4_NATIVE_RESET :
						   FWLAB_M4_NATIVE_IDLE;
		if (hif->reset_pending || message->operation == FWLAB_M4_NATIVE_STATUS)
			return 0;
		request = NULL;
		for (index = 0; index < NATIVE_DEPTH; index++) {
			struct native_request *candidate = &hif->request[index];

			if (candidate->active &&
			    (candidate->uid == hif->delivery_uid || !candidate->delivered) &&
			    (!request || candidate->uid < request->uid))
				request = candidate;
		}
		if (!request)
			return 0;
		request->delivered = true;
		hif->delivery_uid = request->uid;
		message->origin_uid = request->uid;
		message->controller_epoch = request->epoch;
		message->queue_id = request->sqid;
		message->command_id = request->cid;
		message->event = FWLAB_M4_NATIVE_COMMAND;
		memcpy(message->sqe, request->sqe, sizeof(message->sqe));
		return 0;
	}
	if (message->operation == FWLAB_M4_NATIVE_DRAIN_ACK) {
		int ret;

		if (!native_large(hif)) return -EOPNOTSUPP;
		if (message->controller_epoch != hif->controller_epoch || hif->quarantined ||
		    hif->pci->owner_phase != FWLAB_M4_OWNER_OWNED || !hif->reset_pending ||
		    hif->firmware_ready || READ_ONCE(hif->pci->effects_open))
			return -ESTALE;
		if (hif->reset_drained) return 0;
		ret = native_reset_cleanup(hif);
		if (ret) return ret;
		hif->reset_drained = true;
		native_drained_status(hif);
		return 0;
	}
	if (message->operation == FWLAB_M4_NATIVE_RESET_ACK) {
		int ret;

		if (message->controller_epoch != hif->controller_epoch || hif->quarantined ||
		    hif->pci->owner_phase != FWLAB_M4_OWNER_OWNED)
			return -ESTALE;
		if (!hif->reset_pending && hif->firmware_ready)
			return 0;
		ret = native_reset_cleanup(hif);
		if (ret) return ret;
		hif->reset_pending = false;
		hif->reset_drained = false;
		hif->firmware_ready = true;
		/* SHST completion is not a controller-disable acknowledgement.
		 * Keep RDY set until the Host clears CC.EN, so a polling BAR backend
		 * cannot miss a short disable/re-enable sequence during rebind. */
		writel(hif->faulted ? native_fault_status(hif) :
		       hif->shutdown ? CSTS_READY | CSTS_SHUTDOWN_COMPLETE : 0,
		       hif->pci->bar_mapping + REG_CSTS);
		return 0;
	}
	request = native_find(hif, message->origin_uid, message->controller_epoch);
	if (!request)
		return -ESTALE;
	if (hif->delivery_uid == request->uid)
		hif->delivery_uid = 0;
	if (!native_queue_origin_valid(hif, request)) return -ESTALE;
	switch (message->operation) {
	case FWLAB_M4_NATIVE_SHAPE:
		return native_shape(hif, request, message);
	case FWLAB_M4_NATIVE_DMA:
		return native_dma(hif, request, message, false);
	case FWLAB_M4_NATIVE_DMA_QUERY:
		return native_dma(hif, request, message, true);
	case FWLAB_M4_NATIVE_DMA_CANCEL:
	case FWLAB_M4_NATIVE_DMA_RETIRE:
	case FWLAB_M4_NATIVE_AUTHORITY_RELEASE:
		if (!request->shaped || message->authority_uid != request->authority_uid ||
		    message->dma_uid != request->dma_uid)
			return -ESTALE;
		if (message->operation == FWLAB_M4_NATIVE_AUTHORITY_RELEASE) {
			/* SHAPE reserves an operation even if lifecycle never submits
			 * DMA. Reset can cancel that reservation first. Both states
			 * have no Host effect and may release without a submitted-op
			 * retire; DONE/FAILED still require explicit retirement. */
			if (request->dma_state == FWLAB_M4_NATIVE_DMA_RESERVED ||
			    request->dma_state == FWLAB_M4_NATIVE_DMA_CANCELLED) {
				request->dma_state = FWLAB_M4_NATIVE_DMA_CANCELLED;
				request->dma_retired = true;
			}
			if (!request->dma_retired)
				return -EBUSY;
			request->authority_released = true;
		} else if (message->operation == FWLAB_M4_NATIVE_DMA_CANCEL) {
			if (request->dma_state == FWLAB_M4_NATIVE_DMA_RESERVED)
				request->dma_state = FWLAB_M4_NATIVE_DMA_CANCELLED;
		} else {
			if (request->dma_state == FWLAB_M4_NATIVE_DMA_RESERVED)
				return -EBUSY;
			request->dma_retired = true;
		}
		message->dma_state = request->dma_state;
		message->bytes_done = request->bytes_done;
		return 0;
	case FWLAB_M4_NATIVE_QUEUE:
		return native_queue_effect(hif, request, message);
	case FWLAB_M4_NATIVE_PUBLISH:
		return native_publish(hif, request, message, false);
	case FWLAB_M4_NATIVE_PUBLISH_QUERY:
		return native_publish(hif, request, message, true);
	case FWLAB_M4_NATIVE_RETIRE:
		if (request->publication == FWLAB_M4_NATIVE_UNPUBLISHED)
			return -EBUSY;
		if (native_large(hif) && request->shaped &&
		    (!request->authority_released || !request->dma_retired))
			return -EBUSY;
		return native_request_clear(hif, request);
	default:
		return -EINVAL;
	}
}

static void native_owner_observe(struct fwlab_m4_hif *hif,
				 struct fwlab_m4_owner_message *message)
{
	message->function_nonce = hif->function_nonce;
	message->owner_epoch = hif->pci->owner_epoch;
	message->owner_kind = hif->pci->owner_kind;
	message->phase = hif->pci->owner_phase;
	message->controller_epoch = hif->controller_epoch;
	message->execution_epoch = hif->controller_epoch;
	message->generation = 1;
	message->media_format_version = hif->attachment.media_format_version;
	memcpy(message->media_uuid, hif->attachment.media_uuid, sizeof(message->media_uuid));
	memcpy(message->binding_sha256, hif->attachment.binding_sha256, sizeof(message->binding_sha256));
}

static bool native_owner_revoke_equal(const struct fwlab_m4_owner_message *left,
				      const struct fwlab_m4_owner_message *right)
{
	struct fwlab_m4_owner_message a = *left, b = *right;

	a.operation = b.operation = 0;
	a.result = b.result = 0;
	return !memcmp(&a, &b, sizeof(a));
}

static bool native_owner_grant_equal(const struct fwlab_m4_owner_message *left,
				     const struct fwlab_m4_owner_message *right)
{
	return native_owner_revoke_equal(left, right);
}

static bool native_owner_zero(const struct fwlab_m4_owner_message *message)
{
	return message->proof_flags == FWLAB_M4_OWNER_PROOFS &&
	       !message->host_dma_authorities && !message->mapping_refs &&
	       !message->pin_refs && !message->dma_operations &&
	       !message->controller_buffer_leases && !message->lifecycle_commands &&
	       !message->aggregate_block_operations && !message->completion_leases &&
	       !message->cqe_workers && !message->irq_workers && !message->pba_pending_vectors &&
	       (message->ftl_epoch_proof[0] || message->ftl_epoch_proof[1]) &&
	       (message->nfc_epoch_proof[0] || message->nfc_epoch_proof[1]);
}

static int native_owner_exchange(struct fwlab_m4_hif *hif,
				 struct fwlab_m4_owner_message *message)
{
	struct fwlab_m4_owner_message input = *message;
	struct fwlab_m4_owner_message *retained = &hif->revoke_result;
	unsigned long flags;
	u32 index;

	if (!hif->attached || message->function_nonce != hif->function_nonce)
		return -ESTALE;
	if (message->operation == FWLAB_M4_OWNER_OBSERVE) {
		native_owner_observe(hif, message);
		return 0;
	}
	if (message->operation == FWLAB_M4_OWNER_QUARANTINE) {
		native_cancel_transport(hif);
		hif->quarantined = true;
		spin_lock_irqsave(&hif->pci->config_lock, flags);
		hif->pci->owner_kind = 0;
		hif->pci->owner_phase = FWLAB_M4_OWNER_QUARANTINED;
		spin_unlock_irqrestore(&hif->pci->config_lock, flags);
		writel(CSTS_FATAL, hif->pci->bar_mapping + REG_CSTS);
		native_owner_observe(hif, message);
		return 0;
	}
	if (hif->quarantined)
		return -EIO;
	if (message->operation == FWLAB_M4_OWNER_REVOKE ||
	    message->operation == FWLAB_M4_OWNER_REVOKE_QUERY) {
		if (hif->revoke_key.client_uid == message->client_uid && message->client_uid) {
			if (!native_owner_revoke_equal(message, &hif->revoke_key))
				return -ESTALE;
			*message = *retained;
			return 0;
		}
		if (message->operation == FWLAB_M4_OWNER_REVOKE_QUERY)
			return -ESTALE;
		if (!message->client_uid || message->policy != 1 ||
		    hif->reset_pending || !hif->firmware_ready ||
		    hif->pci->owner_phase != FWLAB_M4_OWNER_OWNED ||
		    message->owner_epoch != hif->pci->owner_epoch ||
		    message->owner_kind != hif->pci->owner_kind ||
		    message->controller_epoch != hif->controller_epoch ||
		    message->execution_epoch != hif->controller_epoch ||
		    memcmp(message->binding_sha256, hif->attachment.binding_sha256,
			   sizeof(hif->attachment.binding_sha256)))
			return -EINVAL;
		if (hif->pci->owner_epoch == U64_MAX || hif->next_owner_uid >= U64_MAX - 1)
			return -EOVERFLOW;
		/* Close data and IRQ entry, then drain handlers before publishing the
		 * owner-revoke LP. Internal firmware work drains after this LP. */
		native_cancel_transport(hif);
		hif->revoke_key = input;
		memset(retained, 0, sizeof(*retained));
		retained->version = FWLAB_M4_OWNER_VERSION;
		retained->size = sizeof(*retained);
		retained->transition_uid = hif->next_owner_uid++;
		retained->old_owner_epoch = hif->pci->owner_epoch;
		retained->old_controller_epoch = hif->controller_epoch;
		retained->old_execution_epoch = hif->controller_epoch;
		retained->old_owner_kind = hif->pci->owner_kind;
		spin_lock_irqsave(&hif->pci->config_lock, flags);
		hif->pci->owner_epoch++;
		hif->pci->owner_kind = 0;
		hif->pci->owner_phase = FWLAB_M4_OWNER_DRAINING;
		spin_unlock_irqrestore(&hif->pci->config_lock, flags);
		memset(&hif->grant_key, 0, sizeof(hif->grant_key));
		memset(&hif->grant_result, 0, sizeof(hif->grant_result));
		native_owner_observe(hif, retained);
		*message = *retained;
		return 0;
	}
	if (message->operation == FWLAB_M4_OWNER_CERTIFY) {
		if (!native_owner_zero(message) || !retained->transition_uid ||
		    message->transition_uid != retained->transition_uid ||
		    message->owner_epoch != retained->owner_epoch ||
		    memcmp(message->binding_sha256, hif->attachment.binding_sha256,
			   sizeof(hif->attachment.binding_sha256)))
			return -ESTALE;
		if (retained->certificate_uid) {
			if (memcmp(message->ftl_epoch_proof, retained->ftl_epoch_proof,
				   sizeof(retained->ftl_epoch_proof)) ||
			    memcmp(message->nfc_epoch_proof, retained->nfc_epoch_proof,
				   sizeof(retained->nfc_epoch_proof)))
				return -ESTALE;
			*message = *retained;
			return 0;
		}
		if (hif->pci->owner_phase != FWLAB_M4_OWNER_DRAINING || hif->pci->effects_open)
			return -EBUSY;
		for (index = 0; index < NATIVE_DEPTH; index++) {
			struct native_request *request = &hif->request[index];
			if (request->active && (request->publication == FWLAB_M4_NATIVE_UNPUBLISHED ||
			    (native_mq2(hif) && request->queue_started && !request->queue_done) ||
			    (request->shaped && (!request->authority_released || !request->dma_retired))))
				return -EBUSY;
		}
		fwlab_m4_close_effects(hif->pci);
		native_registers_init(hif);
		if (native_requests_clear(hif)) return -EIO;
		if (native_mq2(hif) && native_queues_clear(hif)) return -EIO;
		hif->controller_epoch = 0;
		hif->faulted = false;
		hif->shutdown = false;
		hif->reset_drained = false;
		spin_lock_irqsave(&hif->pci->config_lock, flags);
		hif->pci->owner_phase = FWLAB_M4_OWNER_NONE;
		spin_unlock_irqrestore(&hif->pci->config_lock, flags);
		retained->certificate_uid = hif->next_owner_uid++;
		retained->proof_flags = message->proof_flags;
		memcpy(retained->ftl_epoch_proof, message->ftl_epoch_proof, sizeof(retained->ftl_epoch_proof));
		memcpy(retained->nfc_epoch_proof, message->nfc_epoch_proof, sizeof(retained->nfc_epoch_proof));
		native_owner_observe(hif, retained);
		*message = *retained;
		return 0;
	}
	if (message->operation == FWLAB_M4_OWNER_GRANT ||
	    message->operation == FWLAB_M4_OWNER_GRANT_QUERY) {
		struct fwlab_m4_domain_identity domain;
		struct device *device = &hif->pci->pdev->dev;
		int ret;
		if (hif->grant_key.client_uid == message->client_uid && message->client_uid) {
			if (!native_owner_grant_equal(message, &hif->grant_key))
				return -ESTALE;
			*message = hif->grant_result;
			return 0;
		}
		if (message->operation == FWLAB_M4_OWNER_GRANT_QUERY)
			return -ESTALE;
		if (!message->client_uid || !retained->certificate_uid ||
		    hif->pci->owner_phase != FWLAB_M4_OWNER_NONE ||
		    message->transition_uid != retained->transition_uid ||
		    message->certificate_uid != retained->certificate_uid ||
		    message->owner_epoch != hif->pci->owner_epoch ||
		    (message->target_owner != 1 && message->target_owner != 2) ||
		    memcmp(message->binding_sha256, hif->attachment.binding_sha256,
			   sizeof(hif->attachment.binding_sha256)))
			return -ESTALE;
		if (retained->old_controller_epoch == U32_MAX)
			return -EOVERFLOW;
		/* Firmware has already prepared the successor under the closed gate.
		 * Kernel grant validates its exact successor and retained certificate. */
		if (message->controller_epoch != retained->old_controller_epoch + 1 ||
		    message->execution_epoch != retained->old_execution_epoch + 1)
			return -EINVAL;
		device_lock(device);
		ret = fwlab_m4_domain_identity(device, &domain);
		if (!ret && (domain.kind != message->target_owner ||
		    (message->target_owner == 1 && device->driver) ||
		    (message->target_owner == 2 &&
		     (!device->driver || strcmp(device->driver->name, "vfio-pci")))))
			ret = -EACCES;
		device_unlock(device);
		if (ret)
			return ret;
		native_registers_init(hif);
		hif->owner_domain = domain;
		hif->controller_epoch = message->controller_epoch;
		hif->firmware_ready = true;
		hif->reset_pending = false;
		hif->reset_drained = false;
		hif->grant_key = input;
		spin_lock_irqsave(&hif->pci->config_lock, flags);
		hif->pci->owner_kind = message->target_owner;
		hif->pci->owner_phase = FWLAB_M4_OWNER_OWNED;
		spin_unlock_irqrestore(&hif->pci->config_lock, flags);
		native_owner_observe(hif, message);
		hif->grant_result = *message;
		return 0;
	}
	return -EINVAL;
}

static noinline_for_stack int native_canary_exchange(struct fwlab_m4_hif *hif,
				  struct fwlab_m4_canary_message *message)
{
	struct native_request *new_request = NULL;
	u32 index;

	if (!hif->attached || message->function_nonce != hif->function_nonce)
		return -ESTALE;
	if (message->operation == FWLAB_M4_CANARY_ARM) {
		if (hif->canary.armed || hif->canary.hold)
			return -EBUSY;
		memset(&hif->canary, 0, sizeof(hif->canary));
		hif->canary.armed = true;
	} else if (message->operation == FWLAB_M4_CANARY_HOLD) {
		if (hif->canary.flags != 7 || hif->canary.owner_epoch == hif->pci->owner_epoch)
			return -ESTALE;
		hif->canary.hold = true;
	} else if (message->operation == FWLAB_M4_CANARY_HELD) {
		new_request = native_find(hif, message->origin_uid, message->controller_epoch);
		if (!hif->canary.hold || !new_request || new_request->sqid ||
		    !new_request->shaped || new_request->bytes != PAGE_SIZE || new_request->direction != 2)
			return -ESTALE;
		hif->canary.new_origin = new_request->uid;
	} else if (message->operation == FWLAB_M4_CANARY_RELEASE) {
		hif->canary.hold = false;
	} else if (message->operation == FWLAB_M4_CANARY_DISARM) {
		memset(&hif->canary, 0, sizeof(hif->canary));
	} else if (message->operation != FWLAB_M4_CANARY_QUERY &&
		   message->operation != FWLAB_M4_CANARY_PROBE) {
		return -EINVAL;
	}
	for (index = 0; index < NATIVE_DEPTH; index++)
		if (hif->request[index].active && hif->request[index].uid == hif->canary.new_origin)
			new_request = &hif->request[index];
	message->flags = hif->canary.flags;
	message->held = new_request && hif->canary.hold;
	message->old_origin = hif->canary.dma.origin_uid;
	message->old_owner_epoch = hif->canary.owner_epoch;
	message->old_controller = hif->canary.dma.controller_epoch;
	message->old_domain = hif->canary.mapping.domain_nonce;
	message->old_data_iova = hif->canary.mapping.iova;
	message->old_cq_iova = hif->canary.cq_iova;
	message->old_completion_uid = hif->canary.publication.completion_uid;
	message->old_route_generation = hif->canary.irq.route_generation;
	message->old_virq = hif->canary.irq.virq;
	if (new_request) {
		struct fwlab_m4_irq_ticket irq;
		message->new_origin = new_request->uid;
		message->new_owner_epoch = new_request->owner_epoch;
		message->new_controller = new_request->epoch;
		message->new_domain = native_request_maps(hif, new_request)[0].domain_nonce;
		message->new_data_iova = native_request_maps(hif, new_request)[0].iova;
		message->new_cq_iova = hif->cq[0].mapping.iova;
		if (!fwlab_m4_prepare_msix(hif->pci, new_request->owner_epoch,
			new_request->bus_generation, hif->seen_flr_epoch, &irq)) {
			message->new_route_generation = irq.route_generation;
			message->new_virq = irq.virq;
		}
	}
	if (message->operation == FWLAB_M4_CANARY_PROBE) {
		struct fwlab_m4_native_message replay = hif->canary.dma;
		struct native_guard context;
		struct fwlab_m4_copy_guard guard;
		u8 *bytes;

		if (!new_request || !hif->canary.hold || hif->canary.flags != 7 ||
		    new_request->owner_epoch == hif->canary.owner_epoch ||
		    message->old_data_iova != message->new_data_iova ||
		    message->old_cq_iova != message->new_cq_iova)
			return -EINVAL;
		bytes = kmalloc(PAGE_SIZE, GFP_KERNEL);
		if (!bytes)
			return -ENOMEM;
		memset(bytes, 0x3c, PAGE_SIZE);
		replay.data_pointer = message->data_pointer;
		message->dma_result = native_exchange(hif, &replay);
		context = (struct native_guard){ hif, new_request->bus_generation,
			new_request->epoch, new_request->owner_epoch };
		guard = (struct fwlab_m4_copy_guard){ &hif->pci->config_lock,
			native_access_locked, &context };
		message->mapping_result = fwlab_m4_mapping_copy(&hif->pci->pdev->dev,
			&hif->canary.mapping, 0, bytes, PAGE_SIZE, &guard);
		replay = hif->canary.publication;
		message->publication_result = native_exchange(hif, &replay);
		message->irq_result = fwlab_m4_raise_msix(hif->pci, &hif->canary.irq);
		kfree(bytes);
	}
	return 0;
}

static long native_ioctl(struct file *file, unsigned int command, unsigned long arg)
{
	struct fwlab_m4_hif *hif = file->private_data;
	struct fwlab_m4_native_message message;

	if (command == FWLAB_M4_ATTACH_PROFILE) {
		struct fwlab_m4_attach_profile_message attach;
		if (copy_from_user(&attach, (void __user *)arg, sizeof(attach)))
			return -EFAULT;
		if (!fwlab_m4_attach_profile_request_valid(&attach))
			return -EINVAL;
		mutex_lock(&hif->lock);
		if (hif->stopped)
			attach.result = -ENODEV;
		else if (attach.host_profile_id != FWLAB_M4_HOST_PROFILE ||
			 memcmp(&attach.limits, &hif->limits, sizeof(attach.limits)))
			attach.result = -EOPNOTSUPP;
		else
			attach.result = native_attach_locked(hif, attach.producer_mode,
				attach.host_profile_id, attach.media_format_version,
				attach.media_uuid, attach.binding_sha256);
		if (!attach.result) {
			attach.producer_mode = FWLAB_M4_PRODUCER;
			attach.host_profile_id = FWLAB_M4_HOST_PROFILE;
			attach.limits = hif->limits;
			attach.function_nonce = hif->function_nonce;
			attach.controller_epoch = hif->controller_epoch;
		}
		mutex_unlock(&hif->lock);
		return copy_to_user((void __user *)arg, &attach, sizeof(attach)) ? -EFAULT : 0;
	}
	if (command == FWLAB_M4_ATTACH_MODE) {
		struct fwlab_m4_attach_mode_message attach;

		if (copy_from_user(&attach, (void __user *)arg, sizeof(attach)))
			return -EFAULT;
		if (!fwlab_m4_attach_mode_request_valid(&attach))
			return -EINVAL;
		mutex_lock(&hif->lock);
		attach.result = hif->stopped ? -ENODEV : native_attach_locked(hif,
			attach.producer_mode, FWLAB_M4_HOST_PROFILE_SMALL, attach.media_format_version,
			attach.media_uuid, attach.binding_sha256);
		if (!attach.result) {
			attach.producer_mode = FWLAB_M4_PRODUCER;
			attach.function_nonce = hif->function_nonce;
			attach.controller_epoch = hif->controller_epoch;
		}
		mutex_unlock(&hif->lock);
		return copy_to_user((void __user *)arg, &attach, sizeof(attach)) ? -EFAULT : 0;
	}
	if (command == FWLAB_M4_PUMP) {
		struct fwlab_m4_pump_message pump;

		if (copy_from_user(&pump, (void __user *)arg, sizeof(pump)))
			return -EFAULT;
		if (!fwlab_m4_pump_request_valid(&pump))
			return -EINVAL;
		mutex_lock(&hif->lock);
		pump.result = 0;
		if (hif->stopped)
			pump.result = -ENODEV;
		else if (FWLAB_M4_PRODUCER != FWLAB_M4_PRODUCER_PUMP)
			pump.result = -EOPNOTSUPP;
		else if (!hif->attached || pump.function_nonce != hif->function_nonce)
			pump.result = -ESTALE;
		else if (hif->quarantined)
			pump.result = -EIO;
		else
			pump.service_result = native_service_locked(hif, NATIVE_QUEUES, &pump.captured);
		mutex_unlock(&hif->lock);
		/* A repeat after lost copyout is another tick. Retained NEXT and
		 * keyed effects, not this observation, own delivery/completion. */
		return copy_to_user((void __user *)arg, &pump, sizeof(pump)) ? -EFAULT : 0;
	}
	if (command == FWLAB_M4_ATTACH_IDENTITY) {
		struct fwlab_m4_attach_message attach;

		if (copy_from_user(&attach, (void __user *)arg, sizeof(attach)))
			return -EFAULT;
		if (!fwlab_m4_attach_request_valid(&attach))
			return -EINVAL;
		mutex_lock(&hif->lock);
		attach.result = hif->stopped ? -ENODEV : native_attach_locked(hif,
			FWLAB_M4_PRODUCER_BAR, FWLAB_M4_HOST_PROFILE_SMALL, attach.media_format_version,
			attach.media_uuid, attach.binding_sha256);
		if (!attach.result) {
			attach.media_format_version = hif->attachment.media_format_version;
			memcpy(attach.media_uuid, hif->attachment.media_uuid, sizeof(attach.media_uuid));
			memcpy(attach.binding_sha256, hif->attachment.binding_sha256,
			       sizeof(attach.binding_sha256));
			attach.function_nonce = hif->function_nonce;
			attach.controller_epoch = hif->controller_epoch;
		}
		mutex_unlock(&hif->lock);
		/* Exact immutable attachment retry is safe even after failed copyout. */
		return copy_to_user((void __user *)arg, &attach, sizeof(attach)) ? -EFAULT : 0;
	}
	if (command == FWLAB_M4_CANARY_EXCHANGE) {
		struct fwlab_m4_canary_message canary;
		if (copy_from_user(&canary, (void __user *)arg, sizeof(canary)))
			return -EFAULT;
		if (canary.version != FWLAB_M4_CANARY_VERSION || canary.size != sizeof(canary) ||
		    memchr_inv(canary.reserved, 0, sizeof(canary.reserved)))
			return -EINVAL;
		mutex_lock(&hif->lock);
		canary.result = hif->stopped ? -ENODEV : native_canary_exchange(hif, &canary);
		mutex_unlock(&hif->lock);
		return copy_to_user((void __user *)arg, &canary, sizeof(canary)) ? -EFAULT : 0;
	}
	if (command == FWLAB_M4_OWNER_EXCHANGE) {
		struct fwlab_m4_owner_message owner;
		u32 operation;
		if (copy_from_user(&owner, (void __user *)arg, sizeof(owner)))
			return -EFAULT;
		if (owner.version != FWLAB_M4_OWNER_VERSION || owner.size != sizeof(owner) ||
		    owner.reserved0 || memchr_inv(owner.reserved, 0, sizeof(owner.reserved)))
			return -EINVAL;
		operation = owner.operation;
		mutex_lock(&hif->lock);
		owner.result = hif->stopped ? -ENODEV : native_owner_exchange(hif, &owner);
		mutex_unlock(&hif->lock);
		owner.operation = operation;
		return copy_to_user((void __user *)arg, &owner, sizeof(owner)) ? -EFAULT : 0;
	}
	if (command != FWLAB_M4_NATIVE_EXCHANGE)
		return -ENOTTY;
	if (copy_from_user(&message, (void __user *)arg, sizeof(message)))
		return -EFAULT;
	if (message.version != FWLAB_M4_NATIVE_VERSION || message.size != sizeof(message) ||
	    message.reserved0 || memchr_inv(message.reserved, 0, sizeof(message.reserved)))
		return -EINVAL;
	mutex_lock(&hif->lock);
	message.result = hif->stopped ? -ENODEV : native_exchange(hif, &message);
	mutex_unlock(&hif->lock);
	return copy_to_user((void __user *)arg, &message, sizeof(message)) ? -EFAULT : 0;
}

static int native_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct fwlab_m4_hif *hif = container_of(misc, struct fwlab_m4_hif, misc);
	int ret = 0;

	mutex_lock(&hif->lock);
	if (hif->opened || hif->stopped || hif->quarantined)
		ret = -EBUSY;
	else {
		hif->opened = true;
		file->private_data = hif;
	}
	mutex_unlock(&hif->lock);
	return ret ? ret : nonseekable_open(inode, file);
}

static int native_release(struct inode *inode, struct file *file)
{
	struct fwlab_m4_hif *hif = file->private_data;

	(void)inode;
	mutex_lock(&hif->lock);
	hif->opened = false;
	hif->quarantined = hif->attached;
	hif->firmware_ready = false;
	fwlab_m4_close_effects(hif->pci);
	writel(CSTS_FATAL, hif->pci->bar_mapping + REG_CSTS);
	mutex_unlock(&hif->lock);
	return 0;
}

static const struct file_operations native_fops = {
	.owner = THIS_MODULE,
	.open = native_open,
	.release = native_release,
	.unlocked_ioctl = native_ioctl,
};

int fwlab_m4_hif_create(struct fwlab_m4_pci_ctx *pci, struct fwlab_m4_hif **out)
{
	struct fwlab_m4_hif *hif;
	u32 index;

	if (!pci || !pci->bar_mapping || !out)
		return -EINVAL;
	hif = kvzalloc(sizeof(*hif), GFP_KERNEL);
	if (!hif)
		return -ENOMEM;
	hif->limits = fwlab_m4_host_limits_for(FWLAB_M4_HOST_PROFILE);
	if (native_large(hif)) {
		if (PAGE_SIZE != FWLAB_M4_CONTROL_PAGE_BYTES) {
			kvfree(hif);
			return -EOPNOTSUPP;
		}
		for (index = 0; index < 2; index++) {
			struct native_frame *frame = &hif->frame[index];
			frame->map_capacity = index ? 2 : hif->limits.max_data_pages;
			frame->data = kvzalloc(index ? hif->limits.max_admin_bytes :
				hif->limits.max_io_bytes, GFP_KERNEL);
			frame->mapping = kvcalloc(frame->map_capacity, sizeof(*frame->mapping), GFP_KERNEL);
			frame->scratch = kzalloc(FWLAB_M4_CONTROL_PAGE_BYTES, GFP_KERNEL);
			if (!frame->data || !frame->mapping || !frame->scratch) {
				native_frames_free(hif);
				kvfree(hif);
				return -ENOMEM;
			}
		}
	}
	hif->pci = pci;
	mutex_init(&hif->lock);
	hif->function_nonce = get_random_u64() ?: 1;
	hif->next_uid = 1;
	hif->next_authority_uid = 1000001;
	hif->next_dma_uid = 2000001;
	hif->controller_epoch = 1;
	hif->next_owner_uid = 1;
	pci->owner_epoch = 1;
	pci->owner_kind = 1;
	pci->owner_phase = FWLAB_M4_OWNER_OWNED;
	pci->effects_generation = 1;
	hif->seen_flr_epoch = pci->bar_epoch;
	hif->requested_flr_epoch = pci->bar_epoch;
	native_registers_init(hif);
	*out = hif;
	return 0;
}

int fwlab_m4_hif_publish(struct fwlab_m4_hif *hif)
{
	int ret;

	if (!hif || !hif->pci->pdev)
		return -EINVAL;
	snprintf(hif->name, sizeof(hif->name), "fwlab-native-%s", pci_name(hif->pci->pdev));
	hif->misc.minor = MISC_DYNAMIC_MINOR;
	hif->misc.name = hif->name;
	hif->misc.fops = &native_fops;
	hif->misc.mode = 0600;
	hif->misc.parent = &hif->pci->pdev->dev;
	ret = misc_register(&hif->misc);
	hif->registered = !ret;
	return ret;
}

int fwlab_m4_hif_stop(struct fwlab_m4_hif *hif)
{
	if (!hif)
		return 0;
	mutex_lock(&hif->lock);
	hif->stopped = true;
	fwlab_m4_close_effects(hif->pci);
	mutex_unlock(&hif->lock);
	return 0;
}

void fwlab_m4_hif_destroy(struct fwlab_m4_hif *hif)
{
	if (!hif)
		return;
	if (hif->registered)
		misc_deregister(&hif->misc);
	native_frames_free(hif);
	kvfree(hif);
}
