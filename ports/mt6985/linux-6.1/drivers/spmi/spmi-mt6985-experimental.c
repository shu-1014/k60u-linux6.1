// SPDX-License-Identifier: GPL-2.0
/*
 * MT6985 PMIF transport candidate -- NOT a usable PMIC controller.
 *
 * Lifecycle and SWINF algorithm: Linux 6.1 drivers/spmi/spmi-mtk-pmif.c.
 * Register provenance: project/vendor-reference/corot-t-oss/drivers/spmi/
 * spmi-mtk-pmif-core.c, mt6xxx_regs and mt6853_spmi_regs.  Its MT6985
 * match selects mt6xxx_pmif_arb: caps=2, soc_chan=2, SPMI-M.  Only the
 * needed PMIF table entries are retained below; no offsets are inferred.
 * Clock IDs are the six caps=2 IDs in that vendor probe, not caps=1 IDs.
 *
 * SECURITY / REVIEW BLOCKERS:
 * - runtime_review_approved must remain false.  No DT, Kconfig, Makefile,
 *   boot-image or other integration belongs to this experiment.
 * - No caps=2 boot initialization is guessed: firmware ownership, access
 *   permissions, arbitration, clock parents/rates and reset sequencing are
 *   unverified.  INIT_DONE is recorded as an offset, not an initialization
 *   recipe or proof that this controller can be used.
 * - SPMI-P (pmif-p / spmimst-p) is NOT supported.  Retaining all six vendor
 *   clock names does not implement P-channel routing or power management.
 * - RCS SID delivery follows vendor lines 1022..1140: software SID
 *   filtering, four byte-lane W1C registers and a threaded rcs_irq parent.
 *   SID masking is NOT hardware masking.  No transaction lock or IRQ bus
 *   mutex is held over nested callbacks, which can re-enter transport.
 *   Children are removed before the parent IRQ is freed/synchronized,
 *   then SID mappings/domain are disposed before clocks/controller storage.
 *   Wakeup is not enabled and suspend semantics remain unverified.
 * - No AEE, sysfs, debug, event or nack IRQ machinery is imported.  RCS
 *   status read/clear has no error return or timeout/ACK verification;
 *   the vendor 0xff byte clear is retained, not proof against lost events.
 * - Non-data commands are unsupported: the inherited OP_ST_STA BUSY==1
 *   poll is not established completion/ACK semantics for this hardware.
 * - Writes retain the 6.1/vendor submit-only algorithm.  Returning zero
 *   means command submitted, NOT write completion, ACK or PMIC success.
 *   The next transfer waits for IDLE before touching WDATA/ACC.  A timeout
 *   poisons this instance instead of clearing stale state/retrying: late
 *   replies, NACK/error reporting and in-flight hardware quiescence still
 *   need review.  In particular remove cannot prove a submitted write has
 *   drained before clock disable.  This alone forbids runtime approval.
 * - Atomic polls under an IRQ-off raw lock can stall for 10ms per poll;
 *   this is inherited policy, not a measured acceptable latency budget.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/spmi.h>

#define MT6985_PMIF_DELAY_US	10
#define MT6985_PMIF_TIMEOUT_US	10000
#define MT6985_PMIF_IDLE		0
#define MT6985_PMIF_WFVLDCLR	6
#define MT6985_PMIF_STATE(x)	(((x) >> 1) & 0x7)
#define MT6985_PMIF_CMD_REG_0	0
#define MT6985_PMIF_CMD_REG	1
#define MT6985_PMIF_CMD_EXT_LONG	3

/* Not a module parameter, DT property, Kconfig switch or writable override. */
static const bool runtime_review_approved = false;

enum mt6985_pmif_reg {
	MT6985_PMIF_INIT_DONE,
	MT6985_PMIF_CH2_ACC,
	MT6985_PMIF_CH2_WDATA,
	MT6985_PMIF_CH2_RDATA,
	MT6985_PMIF_CH2_VLD_CLR,
	MT6985_PMIF_CH2_STA,
};

static const u32 mt6985_pmif_regs[] = {
	[MT6985_PMIF_INIT_DONE]		= 0x0000,
	[MT6985_PMIF_CH2_ACC]		= 0x0880,
	[MT6985_PMIF_CH2_WDATA]		= 0x0884,
	[MT6985_PMIF_CH2_RDATA]		= 0x0894,
	[MT6985_PMIF_CH2_VLD_CLR]		= 0x08a4,
	[MT6985_PMIF_CH2_STA]		= 0x08a8,
};

/* Complete vendor mt6853_spmi_regs table selected by mt6xxx_pmif_arb. */
enum mt6985_spmimst_reg {
	MT6985_SPMI_OP_ST_CTRL,
	MT6985_SPMI_GRP_ID_EN,
	MT6985_SPMI_OP_ST_STA,
	MT6985_SPMI_MST_SAMPL,
	MT6985_SPMI_MST_REQ_EN,
	MT6985_SPMI_MST_RCS_CTRL,
	MT6985_SPMI_SLV_3_0_EINT,
	MT6985_SPMI_SLV_7_4_EINT,
	MT6985_SPMI_SLV_B_8_EINT,
	MT6985_SPMI_SLV_F_C_EINT,
	MT6985_SPMI_REC_CTRL,
	MT6985_SPMI_REC0,
	MT6985_SPMI_REC1,
	MT6985_SPMI_REC2,
	MT6985_SPMI_REC3,
	MT6985_SPMI_REC4,
	MT6985_SPMI_REC_CMD_DEC,
	MT6985_SPMI_DEC_DBG,
	MT6985_SPMI_MST_DBG,
};

static const u32 mt6985_spmimst_regs[] = {
	[MT6985_SPMI_OP_ST_CTRL]		= 0x0000,
	[MT6985_SPMI_GRP_ID_EN]		= 0x0004,
	[MT6985_SPMI_OP_ST_STA]		= 0x0008,
	[MT6985_SPMI_MST_SAMPL]		= 0x000c,
	[MT6985_SPMI_MST_REQ_EN]		= 0x0010,
	[MT6985_SPMI_MST_RCS_CTRL]		= 0x0014,
	[MT6985_SPMI_SLV_3_0_EINT]		= 0x0020,
	[MT6985_SPMI_SLV_7_4_EINT]		= 0x0024,
	[MT6985_SPMI_SLV_B_8_EINT]		= 0x0028,
	[MT6985_SPMI_SLV_F_C_EINT]		= 0x002c,
	[MT6985_SPMI_REC_CTRL]		= 0x0040,
	[MT6985_SPMI_REC0]		= 0x0044,
	[MT6985_SPMI_REC1]		= 0x0048,
	[MT6985_SPMI_REC2]		= 0x004c,
	[MT6985_SPMI_REC3]		= 0x0050,
	[MT6985_SPMI_REC4]		= 0x0054,
	[MT6985_SPMI_REC_CMD_DEC]		= 0x005c,
	[MT6985_SPMI_DEC_DBG]		= 0x00f8,
	[MT6985_SPMI_MST_DBG]		= 0x00fc,
};

static const char * const mt6985_pmif_clock_names[] = {
	"pmif_clk_mux",
	"spmimst_m_clk_mux",
	"spmimst_p_clk_mux",
	"vlp_pmif_clk_mux",
	"vlp_spmimst_m_clk_mux",
	"vlp_spmimst_p_clk_mux",
};

struct mt6985_pmif_experimental {
	void __iomem *base;
	void __iomem *spmimst_base;
	const u32 *spmimst_regs;
	struct clk_bulk_data clks[ARRAY_SIZE(mt6985_pmif_clock_names)];
	/* Per-controller, never a shared writable SoC match-data object. */
	raw_spinlock_t lock;
	bool online;
	bool failed;
	struct irq_domain *rcs_domain;
	int rcs_irq;
	bool rcs_irq_requested;
	bool rcs_enabled[16];
};

/* Caller holds lock for the whole transaction, including timeout handling. */
static int mt6985_pmif_wait(struct mt6985_pmif_experimental *pmif, u32 state)
{
	u32 status;
	int ret;

	ret = readl_poll_timeout_atomic(pmif->base +
			mt6985_pmif_regs[MT6985_PMIF_CH2_STA], status,
			MT6985_PMIF_STATE(status) == state,
			MT6985_PMIF_DELAY_US, MT6985_PMIF_TIMEOUT_US);
	if (ret)
		pmif->failed = true;
	return ret;
}

static int mt6985_pmif_read_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid,
			       u16 addr, u8 *buf, size_t len)
{
	struct mt6985_pmif_experimental *pmif = spmi_controller_get_drvdata(ctrl);
	unsigned long flags;
	u32 data, cmd, type;
	int ret;
	size_t i;

	if (!buf || !len || len > 4 || sid > 0xf)
		return -EINVAL;
	if (opc >= 0x60 && opc <= 0x7f)
		type = MT6985_PMIF_CMD_REG;
	else if ((opc >= 0x20 && opc <= 0x2f) ||
		 (opc >= 0x38 && opc <= 0x3f))
		type = MT6985_PMIF_CMD_EXT_LONG;
	else
		return -EINVAL;

	cmd = (type << 30) | ((u32)sid << 24) | ((u32)(len - 1) << 16) | addr;
	raw_spin_lock_irqsave(&pmif->lock, flags);
	if (!pmif->online || pmif->failed) {
		ret = -EIO;
		goto out;
	}
	ret = mt6985_pmif_wait(pmif, MT6985_PMIF_IDLE);
	if (ret)
		goto out;
	writel(cmd, pmif->base + mt6985_pmif_regs[MT6985_PMIF_CH2_ACC]);
	ret = mt6985_pmif_wait(pmif, MT6985_PMIF_WFVLDCLR);
	if (ret)
		goto out;
	data = readl(pmif->base + mt6985_pmif_regs[MT6985_PMIF_CH2_RDATA]);
	/* Explicit low-byte-first packing; no host-endian memcpy assumption. */
	for (i = 0; i < len; i++)
		buf[i] = data >> (8 * i);
	writel(1, pmif->base + mt6985_pmif_regs[MT6985_PMIF_CH2_VLD_CLR]);
out:
	raw_spin_unlock_irqrestore(&pmif->lock, flags);
	return ret;
}

static int mt6985_pmif_write_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid,
				u16 addr, const u8 *buf, size_t len)
{
	struct mt6985_pmif_experimental *pmif = spmi_controller_get_drvdata(ctrl);
	unsigned long flags;
	u32 data = 0, cmd, type;
	int ret;
	size_t i;

	if (!buf || !len || len > 4 || sid > 0xf)
		return -EINVAL;
	if (opc >= 0x40 && opc <= 0x5f)
		type = MT6985_PMIF_CMD_REG;
	else if (opc <= 0x0f || (opc >= 0x30 && opc <= 0x37))
		type = MT6985_PMIF_CMD_EXT_LONG;
	else if (opc >= 0x80)
		type = MT6985_PMIF_CMD_REG_0;
	else
		return -EINVAL;

	for (i = 0; i < len; i++)
		data |= (u32)buf[i] << (8 * i);
	cmd = (type << 30) | BIT(29) | ((u32)sid << 24) |
	      ((u32)(len - 1) << 16) | addr;
	raw_spin_lock_irqsave(&pmif->lock, flags);
	if (!pmif->online || pmif->failed) {
		ret = -EIO;
		goto out;
	}
	ret = mt6985_pmif_wait(pmif, MT6985_PMIF_IDLE);
	if (ret)
		goto out;
	writel(data, pmif->base + mt6985_pmif_regs[MT6985_PMIF_CH2_WDATA]);
	writel(cmd, pmif->base + mt6985_pmif_regs[MT6985_PMIF_CH2_ACC]);
	/* Submit-only, as in 6.1: not an ACK/completion check (see blockers). */
out:
	raw_spin_unlock_irqrestore(&pmif->lock, flags);
	return ret;
}

static int mt6985_pmif_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid)
{
	/* No register access: do not guess OP_ST_STA completion semantics. */
	if (sid > 0xf || opc < SPMI_CMD_RESET || opc > SPMI_CMD_WAKEUP)
		return -EINVAL;
	return -EOPNOTSUPP;
}

/* RCS IRQ implementation begins. */
static void mt6985_pmif_rcs_mask(struct irq_data *data)
{
	struct mt6985_pmif_experimental *pmif = irq_data_get_irq_chip_data(data);
	irq_hw_number_t sid = irqd_to_hwirq(data);

	/* Software dispatch filter only; no hardware SID mask is known.
	 * Status is still cleared while masked; replay is not guaranteed.
	 */
	if (sid < ARRAY_SIZE(pmif->rcs_enabled))
		WRITE_ONCE(pmif->rcs_enabled[sid], false);
}

static void mt6985_pmif_rcs_unmask(struct irq_data *data)
{
	struct mt6985_pmif_experimental *pmif = irq_data_get_irq_chip_data(data);
	irq_hw_number_t sid = irqd_to_hwirq(data);

	if (sid < ARRAY_SIZE(pmif->rcs_enabled))
		WRITE_ONCE(pmif->rcs_enabled[sid], true);
}

static const struct irq_chip mt6985_pmif_rcs_chip = {
	.name = "mt6985-pmif-rcs",
	.irq_mask = mt6985_pmif_rcs_mask,
	.irq_unmask = mt6985_pmif_rcs_unmask,
};

static int mt6985_pmif_rcs_map(struct irq_domain *domain, unsigned int virq,
			      irq_hw_number_t sid)
{
	struct mt6985_pmif_experimental *pmif = domain->host_data;
	int ret;

	if (sid >= ARRAY_SIZE(pmif->rcs_enabled))
		return -EINVAL;
	ret = irq_set_parent(virq, pmif->rcs_irq);
	if (ret)
		return ret;
	WRITE_ONCE(pmif->rcs_enabled[sid], false);
	irq_set_chip_data(virq, pmif);
	irq_set_chip(virq, &mt6985_pmif_rcs_chip);
	irq_set_nested_thread(virq, 1);
	irq_set_noprobe(virq);
	return 0;
}

static void mt6985_pmif_rcs_unmap(struct irq_domain *domain, unsigned int virq)
{
	struct irq_data *data = irq_get_irq_data(virq);

	if (data)
		mt6985_pmif_rcs_mask(data);
	irq_set_nested_thread(virq, 0);
	irq_set_parent(virq, 0);
	irq_set_chip(virq, NULL);
	irq_set_chip_data(virq, NULL);
}

static const struct irq_domain_ops mt6985_pmif_rcs_domain_ops = {
	.map = mt6985_pmif_rcs_map,
	.unmap = mt6985_pmif_rcs_unmap,
	.xlate = irq_domain_xlate_onetwocell,
};

static irqreturn_t mt6985_pmif_rcs_thread(int irq, void *data)
{
	struct mt6985_pmif_experimental *pmif = data;
	unsigned int sid, shift, status, virq;
	void __iomem *reg;

	for (sid = 0; sid < ARRAY_SIZE(pmif->rcs_enabled); sid++) {
		reg = pmif->spmimst_base + pmif->spmimst_regs[
			MT6985_SPMI_SLV_3_0_EINT + sid / 4];
		shift = (sid % 4) * 8;
		status = (readl(reg) >> shift) & 0xff;
		/* Vendor policy: clear the entire byte, even if status was zero.
		 * This can discard a newly arriving event; no ACK is available.
		 */
		writel(0xffU << shift, reg);
		if (!status || !READ_ONCE(pmif->rcs_enabled[sid]))
			continue;
		virq = irq_find_mapping(pmif->rcs_domain, sid);
		/* Independent RCS registers; never hold transport/IRQ bus locks
		 * across callbacks, which may transact or mask/unmask a SID.
		 */
		if (virq)
			handle_nested_irq(virq);
	}
	return IRQ_HANDLED;
}

static void mt6985_pmif_rcs_exit(struct mt6985_pmif_experimental *pmif)
{
	unsigned int sid, virq;

	/* Caller has removed controller children. free_irq waits for threads. */
	if (pmif->rcs_irq_requested) {
		free_irq(pmif->rcs_irq, pmif);
		pmif->rcs_irq_requested = false;
	}
	if (!pmif->rcs_domain)
		return;
	for (sid = 0; sid < ARRAY_SIZE(pmif->rcs_enabled); sid++) {
		WRITE_ONCE(pmif->rcs_enabled[sid], false);
		virq = irq_find_mapping(pmif->rcs_domain, sid);
		if (virq)
			irq_dispose_mapping(virq);
	}
	irq_domain_remove(pmif->rcs_domain);
	pmif->rcs_domain = NULL;
}

static int mt6985_pmif_rcs_init(struct platform_device *pdev,
			       struct mt6985_pmif_experimental *pmif)
{
	unsigned int sid;
	int ret;

	ret = of_irq_get_byname(pdev->dev.of_node, "rcs_irq");
	if (ret < 0)
		return ret; /* In particular, preserve -EPROBE_DEFER. */
	if (!ret)
		return -EINVAL;
	pmif->rcs_irq = ret;
	pmif->rcs_domain = irq_domain_add_linear(pdev->dev.of_node,
		ARRAY_SIZE(pmif->rcs_enabled), &mt6985_pmif_rcs_domain_ops, pmif);
	if (!pmif->rcs_domain)
		return -ENOMEM;

	/* Vendor initialization discards pending bootloader events.  MMIO
	 * clear has no timeout/error/ACK result; it is not verified quiescence.
	 */
	for (sid = 0; sid < ARRAY_SIZE(pmif->rcs_enabled); sid++)
		writel(0xffU << ((sid % 4) * 8), pmif->spmimst_base +
		       pmif->spmimst_regs[MT6985_SPMI_SLV_3_0_EINT + sid / 4]);
	ret = request_threaded_irq(pmif->rcs_irq, NULL, mt6985_pmif_rcs_thread,
				   IRQF_ONESHOT, "mt6985-pmif-rcs", pmif);
	if (ret) {
		mt6985_pmif_rcs_exit(pmif);
		return ret;
	}
	pmif->rcs_irq_requested = true;
	/* Do not guess IRQF_NO_SUSPEND or enable parent wakeup by default. */
	return 0;
}

static int mt6985_pmif_experimental_probe(struct platform_device *pdev)
{
	struct spmi_controller *ctrl;
	struct mt6985_pmif_experimental *pmif;
	int ret, i;

	/* FIRST executable statement: no allocation, clocks, MMIO or children. */
	if (!runtime_review_approved)
		return -EOPNOTSUPP;

	ctrl = spmi_controller_alloc(&pdev->dev, sizeof(*pmif));
	if (!ctrl)
		return -ENOMEM;
	pmif = spmi_controller_get_drvdata(ctrl);
	raw_spin_lock_init(&pmif->lock);
	pmif->spmimst_regs = mt6985_spmimst_regs;
	pmif->base = devm_platform_ioremap_resource_byname(pdev, "pmif");
	if (IS_ERR(pmif->base)) {
		ret = PTR_ERR(pmif->base);
		goto put_ctrl;
	}
	pmif->spmimst_base = devm_platform_ioremap_resource_byname(pdev, "spmimst");
	if (IS_ERR(pmif->spmimst_base)) {
		ret = PTR_ERR(pmif->spmimst_base);
		goto put_ctrl;
	}
	for (i = 0; i < ARRAY_SIZE(mt6985_pmif_clock_names); i++)
		pmif->clks[i].id = mt6985_pmif_clock_names[i];
	ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(pmif->clks), pmif->clks);
	if (ret)
		goto put_ctrl;
	ret = clk_bulk_prepare_enable(ARRAY_SIZE(pmif->clks), pmif->clks);
	if (ret)
		goto put_ctrl;

	ctrl->cmd = mt6985_pmif_cmd;
	ctrl->read_cmd = mt6985_pmif_read_cmd;
	ctrl->write_cmd = mt6985_pmif_write_cmd;
	pmif->online = true;
	/* RCS domain and parent must exist before children can request IRQs. */
	ret = mt6985_pmif_rcs_init(pdev, pmif);
	if (ret)
		goto disable_clks;
	platform_set_drvdata(pdev, ctrl);
	ret = spmi_controller_add(ctrl);
	if (ret)
		goto exit_rcs;
	return 0;

exit_rcs:
	/* 6.1 add failure has not registered controller children. */
	mt6985_pmif_rcs_exit(pmif);
disable_clks:
	platform_set_drvdata(pdev, NULL);
	clk_bulk_disable_unprepare(ARRAY_SIZE(pmif->clks), pmif->clks);
put_ctrl:
	spmi_controller_put(ctrl);
	return ret;
}

static int mt6985_pmif_experimental_remove(struct platform_device *pdev)
{
	struct spmi_controller *ctrl = platform_get_drvdata(pdev);
	struct mt6985_pmif_experimental *pmif = spmi_controller_get_drvdata(ctrl);
	unsigned long flags;

	/* Children may need working transport during their remove callbacks. */
	spmi_controller_remove(ctrl);
	mt6985_pmif_rcs_exit(pmif);
	raw_spin_lock_irqsave(&pmif->lock, flags);
	pmif->online = false;
	raw_spin_unlock_irqrestore(&pmif->lock, flags);
	/* Hardware write drain remains unproven; runtime gate MUST stay closed. */
	clk_bulk_disable_unprepare(ARRAY_SIZE(pmif->clks), pmif->clks);
	platform_set_drvdata(pdev, NULL);
	spmi_controller_put(ctrl);
	return 0;
}

static const struct of_device_id mt6985_pmif_experimental_of_match[] = {
	{ .compatible = "mediatek,mt6985-spmi-experimental" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6985_pmif_experimental_of_match);

static struct platform_driver mt6985_pmif_experimental_driver = {
	.probe = mt6985_pmif_experimental_probe,
	.remove = mt6985_pmif_experimental_remove,
	.driver = {
		.name = "mt6985-spmi-experimental",
		.of_match_table = mt6985_pmif_experimental_of_match,
	},
};
module_platform_driver(mt6985_pmif_experimental_driver);

MODULE_DESCRIPTION("MT6985 PMIF experimental transport; runtime prohibited");
MODULE_LICENSE("GPL");
