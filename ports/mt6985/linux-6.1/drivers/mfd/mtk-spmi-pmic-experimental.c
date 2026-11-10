// SPDX-License-Identifier: GPL-2.0
/*
 * MT6363/MT6373 SPMI MFD compile-review candidate only.
 * Derived from MediaTek's corot-t-oss mtk-spmi-pmic.c (2021).
 *
 * NOT runtime approved: parent PMIF/RCS IRQ routing, trigger/ownership,
 * wake/suspend and recovery after I/O failure still need hardware review.
 * RCS_INT_DONE below is the vendor PMIC-side handshake, NOT proof that the
 * parent controller's RCS IRQ implementation is present or correct.
 * No DTS/build integration or runtime override is intentionally provided.
 */
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/mfd/core.h>
#include <linux/mfd/mt6363/core.h>
#include <linux/mfd/mt6363/registers.h>
#include <linux/mfd/mt6373/core.h>
#include <linux/mfd/mt6373/registers.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spmi.h>

#define MTK_SPMI_PMIC_REG_WIDTH 8
#define PMIC_SWCID 0x0b
#define RCS_INT_DONE 0x41b

static const bool runtime_review_approved = false;

struct irq_top_t {
	unsigned int hwirq_base;
	unsigned int num_int_regs;
	unsigned int en_reg;
	unsigned int en_reg_shift;
	unsigned int sta_reg;
	unsigned int sta_reg_shift;
	unsigned int top_offset;
};

struct mtk_spmi_pmic_data {
	const struct mfd_cell *cells;
	unsigned int num_cells;
	const struct irq_top_t *pmic_ints;
	unsigned int num_top;
	unsigned int num_pmic_irqs;
	unsigned int top_int_status_reg;
};

struct pmic_core {
	struct device *dev;
	struct regmap *regmap;
	int irq;
	bool *enable_hwirq;
	bool *cache_hwirq;
	struct mutex irqlock;
	struct irq_domain *irq_domain;
	const struct mtk_spmi_pmic_data *chip_data;
};

static const struct resource mt6363_regulators_resources[] = {
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VCN15_OC, "VCN15"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VCN13_OC, "VCN13"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VRF09_OC, "VRF09"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VRF12_OC, "VRF12"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VRF13_OC, "VRF13"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VRF18_OC, "VRF18"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VRFIO18_OC, "VRFIO18"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VSRAM_MDFE_OC, "VSRAM_MDFE"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VTREF18_OC, "VTREF18"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VSRAM_APU_OC, "VSRAM_APU"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VAUX18_OC, "VAUX18"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VEMC_OC, "VEMC"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VUFS12_OC, "VUFS12"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VUFS18_OC, "VUFS18"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VIO18_OC, "VIO18"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VIO075_OC, "VIO075"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VA12_1_OC, "VA12_1"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VA12_2_OC, "VA12_2"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VA15_OC, "VA15"),
	DEFINE_RES_IRQ_NAMED(MT6363_IRQ_VM18_OC, "VM18"),
};

static const struct resource mt6373_regulators_resources[] = {
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VUSB_OC, "VUSB"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VAUX18_OC, "VAUX18"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VRF13_AIF_OC, "VRF13_AIF"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VRF18_AIF_OC, "VRF18_AIF"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VRFIO18_AIF_OC, "VRFIO18_AIF"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VCN33_1_OC, "VCN33_1"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VCN33_2_OC, "VCN33_2"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VCN33_3_OC, "VCN33_3"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VCN18IO_OC, "VCN18IO"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VRF09_AIF_OC, "VRF09_AIF"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VRF12_AIF_OC, "VRF12_AIF"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VANT18_OC, "VANT18"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VSRAM_DIGRF_AIF_OC, "VSRAM_DIGRF_AIF"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VEFUSE_OC, "VEFUSE"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VMCH_OC, "VMCH"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VMC_OC, "VMC"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VIBR_OC, "VIBR"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VIO28_OC, "VIO28"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VFP_OC, "VFP"),
	DEFINE_RES_IRQ_NAMED(MT6373_IRQ_VTP_OC, "VTP"),
};

static const struct mfd_cell mt6363_devs[] = {
	{ .name = "mt6363-regulator",
	  .num_resources = ARRAY_SIZE(mt6363_regulators_resources),
	  .resources = mt6363_regulators_resources },
};

static const struct mfd_cell mt6373_devs[] = {
	{ .name = "mt6373-regulator",
	  .num_resources = ARRAY_SIZE(mt6373_regulators_resources),
	  .resources = mt6373_regulators_resources },
};

/* Keep the real vendor top tables, including gaps in top-status bit numbers. */
static const struct irq_top_t mt6363_ints[] = {
	MT6363_TOP_GEN(BUCK), MT6363_TOP_GEN(LDO), MT6363_TOP_GEN(PSC),
	MT6363_TOP_GEN(MISC), MT6363_TOP_GEN(HK), MT6363_TOP_GEN(BM),
};
static const struct irq_top_t mt6373_ints[] = {
	MT6373_TOP_GEN(BUCK), MT6373_TOP_GEN(LDO), MT6373_TOP_GEN(MISC),
};

static const struct mtk_spmi_pmic_data mt6363_data = {
	.cells = mt6363_devs, .num_cells = ARRAY_SIZE(mt6363_devs),
	.pmic_ints = mt6363_ints, .num_top = ARRAY_SIZE(mt6363_ints),
	.num_pmic_irqs = MT6363_IRQ_NR,
	.top_int_status_reg = MT6363_TOP_INT_STATUS1,
};
static const struct mtk_spmi_pmic_data mt6373_data = {
	.cells = mt6373_devs, .num_cells = ARRAY_SIZE(mt6373_devs),
	.pmic_ints = mt6373_ints, .num_top = ARRAY_SIZE(mt6373_ints),
	.num_pmic_irqs = MT6373_IRQ_NR,
	.top_int_status_reg = MT6373_TOP_INT_STATUS1,
};

/* Restrict mappings to the regulator OC resources actually exposed here. */
static bool mtk_spmi_pmic_valid_hwirq(struct pmic_core *core,
				    irq_hw_number_t hw)
{
	const struct mfd_cell *cell = &core->chip_data->cells[0];
	unsigned int i;

	for (i = 0; i < cell->num_resources; i++)
		if (cell->resources[i].start == hw)
			return true;
	return false;
}

static void mtk_spmi_pmic_irq_enable(struct irq_data *data)
{
	struct pmic_core *core = irq_data_get_irq_chip_data(data);

	core->enable_hwirq[irqd_to_hwirq(data)] = true;
}

static void mtk_spmi_pmic_irq_disable(struct irq_data *data)
{
	struct pmic_core *core = irq_data_get_irq_chip_data(data);

	core->enable_hwirq[irqd_to_hwirq(data)] = false;
}

static void mtk_spmi_pmic_irq_lock(struct irq_data *data)
{
	struct pmic_core *core = irq_data_get_irq_chip_data(data);

	mutex_lock(&core->irqlock);
}

static void mtk_spmi_pmic_irq_sync_unlock(struct irq_data *data)
{
	struct pmic_core *core = irq_data_get_irq_chip_data(data);
	const struct mtk_spmi_pmic_data *chip = core->chip_data;
	unsigned int i, group, offset, reg, shift;
	const struct irq_top_t *top;
	int ret;

	for (i = 0; i < chip->num_pmic_irqs; i++) {
		if (core->enable_hwirq[i] == core->cache_hwirq[i])
			continue;
		group = 0;
		while (group + 1 < chip->num_top &&
		       i >= chip->pmic_ints[group + 1].hwirq_base)
			group++;
		top = &chip->pmic_ints[group];
		offset = i - top->hwirq_base;
		shift = offset % MTK_SPMI_PMIC_REG_WIDTH;
		reg = top->en_reg + top->en_reg_shift *
			(offset / MTK_SPMI_PMIC_REG_WIDTH);
		ret = regmap_update_bits(core->regmap, reg, BIT(shift),
					core->enable_hwirq[i] ? BIT(shift) : 0);
		if (ret) {
			/* Void IRQ callback: cannot propagate; leave cache dirty. */
			dev_err_ratelimited(core->dev, "IRQ %u mask sync failed: %d\n",
					    i, ret);
			continue;
		}
		core->cache_hwirq[i] = core->enable_hwirq[i];
	}
	mutex_unlock(&core->irqlock);
}

static struct irq_chip mtk_spmi_pmic_irq_chip = {
	.name = "spmi-pmic-experimental",
	.flags = IRQCHIP_SKIP_SET_WAKE,
	.irq_enable = mtk_spmi_pmic_irq_enable,
	.irq_disable = mtk_spmi_pmic_irq_disable,
	.irq_bus_lock = mtk_spmi_pmic_irq_lock,
	.irq_bus_sync_unlock = mtk_spmi_pmic_irq_sync_unlock,
};

static int mtk_spmi_pmic_irq_group(struct pmic_core *core, unsigned int group)
{
	const struct irq_top_t *top = &core->chip_data->pmic_ints[group];
	unsigned int i, reg, status, pending, bit, hw, virq;
	int ret;

	for (i = 0; i < top->num_int_regs; i++) {
		reg = top->sta_reg + top->sta_reg_shift * i;
		ret = regmap_read(core->regmap, reg, &status);
		if (ret)
			return ret;
		pending = status;
		while (pending) {
			bit = __ffs(pending);
			hw = top->hwirq_base + MTK_SPMI_PMIC_REG_WIDTH * i + bit;
			virq = hw < core->chip_data->num_pmic_irqs ?
				irq_find_mapping(core->irq_domain, hw) : 0;
			if (virq)
				handle_nested_irq(virq);
			pending &= ~BIT(bit);
		}
		/* Vendor order: dispatch nested IRQs, then W1C this status byte. */
		if (status) {
			ret = regmap_write(core->regmap, reg, status);
			if (ret)
				return ret;
		}
	}
	return 0;
}

static irqreturn_t mtk_spmi_pmic_irq_handler(int irq, void *data)
{
	struct pmic_core *core = data;
	const struct mtk_spmi_pmic_data *chip = core->chip_data;
	unsigned int status, i;
	int ret;

	ret = regmap_read(core->regmap, chip->top_int_status_reg, &status);
	if (ret)
		goto io_error;
	for (i = 0; i < chip->num_top; i++) {
		if (!(status & BIT(chip->pmic_ints[i].top_offset)))
			continue;
		ret = mtk_spmi_pmic_irq_group(core, i);
		if (ret)
			goto io_error;
	}
	/* Vendor order: all group dispatch/W1C completes before RCS done. */
	ret = regmap_write(core->regmap, RCS_INT_DONE, 1);
	if (ret)
		goto io_error;
	return IRQ_HANDLED;
io_error:
	/* Do not claim completion or guess a recovery/ack after failed I/O. */
	dev_err_ratelimited(core->dev, "IRQ read/ack failed: %d\n", ret);
	return IRQ_NONE;
}

static int mtk_spmi_pmic_irq_domain_map(struct irq_domain *domain,
				      unsigned int irq, irq_hw_number_t hw)
{
	struct pmic_core *core = domain->host_data;

	if (!mtk_spmi_pmic_valid_hwirq(core, hw))
		return -EINVAL;
	irq_set_chip_data(irq, core);
	irq_set_chip_and_handler(irq, &mtk_spmi_pmic_irq_chip, handle_level_irq);
	irq_set_nested_thread(irq, 1);
	irq_set_noprobe(irq);
	return 0;
}

static const struct irq_domain_ops pmic_irq_domain_ops = {
	.map = mtk_spmi_pmic_irq_domain_map,
	.xlate = irq_domain_xlate_twocell,
};

static void mtk_spmi_pmic_remove_domain(void *data)
{
	struct pmic_core *core = data;
	unsigned int hw, virq;

	/* MFD children and the parent threaded handler are already gone. */
	for (hw = 0; hw < core->chip_data->num_pmic_irqs; hw++) {
		virq = irq_find_mapping(core->irq_domain, hw);
		if (virq)
			irq_dispose_mapping(virq);
	}
	irq_domain_remove(core->irq_domain);
}

static int mtk_spmi_pmic_irq_init(struct pmic_core *core)
{
	const struct mtk_spmi_pmic_data *chip = core->chip_data;
	const struct irq_top_t *top;
	unsigned int i, j;
	int ret;

	mutex_init(&core->irqlock);
	core->enable_hwirq = devm_kcalloc(core->dev, chip->num_pmic_irqs,
					 sizeof(bool), GFP_KERNEL);
	core->cache_hwirq = devm_kcalloc(core->dev, chip->num_pmic_irqs,
					sizeof(bool), GFP_KERNEL);
	if (!core->enable_hwirq || !core->cache_hwirq)
		return -ENOMEM;
	/* Vendor initialization order, but stop at the first failed write. */
	for (i = 0; i < chip->num_top; i++) {
		top = &chip->pmic_ints[i];
		for (j = 0; j < top->num_int_regs; j++) {
			ret = regmap_write(core->regmap,
					top->en_reg + top->en_reg_shift * j, 0);
			if (ret)
				return ret;
			ret = regmap_write(core->regmap,
					top->sta_reg + top->sta_reg_shift * j, 0xff);
			if (ret)
				return ret;
		}
	}
	ret = regmap_write(core->regmap, RCS_INT_DONE, 1);
	if (ret)
		return ret;
	core->irq_domain = irq_domain_add_linear(core->dev->of_node,
			chip->num_pmic_irqs, &pmic_irq_domain_ops, core);
	if (!core->irq_domain)
		return -ENOMEM;
	ret = devm_add_action_or_reset(core->dev,
				      mtk_spmi_pmic_remove_domain, core);
	if (ret)
		return ret;
	ret = devm_request_threaded_irq(core->dev, core->irq, NULL,
			mtk_spmi_pmic_irq_handler, IRQF_ONESHOT,
			mtk_spmi_pmic_irq_chip.name, core);
	if (ret)
		return ret;
	return 0;
}

/*
 * Linux 6.1 devm_regmap_init_spmi_ext exists, but its bus has no max_raw
 * limit and regmap ignores config.max_raw_* when a bus is supplied.
 * Single-value raw/bulk splitting with REGCACHE_NONE instead limits ordinary
 * read/write transactions to one byte (PMIF <=4). No noinc operations here.
 * This may split child multi-byte unlock sequences: hardware semantics remain
 * unverified. Do not treat this as runtime approval or lift the probe gate.
 */
static const struct regmap_config spmi_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xffff,
	.cache_type = REGCACHE_NONE,
	.use_single_read = true,
	.use_single_write = true,
	.can_multi_write = false,
};

static int mtk_spmi_pmic_probe(struct spmi_device *sdev)
{
	struct pmic_core *core;
	const struct mtk_spmi_pmic_data *chip;
	unsigned int swcid, i;
	int ret;

	/* First executable operation: no allocation, bus access or IRQ setup. */
	if (!runtime_review_approved)
		return -EOPNOTSUPP;
	chip = of_device_get_match_data(&sdev->dev);
	if (!chip)
		return -ENODEV;
	core = devm_kzalloc(&sdev->dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;
	core->dev = &sdev->dev;
	core->chip_data = chip;
	core->irq = of_irq_get(sdev->dev.of_node, 0);
	if (core->irq < 0)
		return core->irq; /* Includes -EPROBE_DEFER; never continue. */
	if (!core->irq)
		return -EINVAL;
	core->regmap = devm_regmap_init_spmi_ext(sdev, &spmi_regmap_config);
	if (IS_ERR(core->regmap))
		return PTR_ERR(core->regmap);
	ret = regmap_read(core->regmap, PMIC_SWCID, &swcid);
	if (ret)
		return ret;
	/* A single SWCID byte is a revision, not a complete chip identity. */
	dev_dbg(core->dev, "SWCID revision byte: %#x (not full chip ID)\n", swcid);
	ret = mtk_spmi_pmic_irq_init(core);
	if (ret)
		return ret;
	/*
	 * mfd-core does not reject irq_create_mapping() == 0. Pre-create and
	 * validate every OC mapping before it can register/probe a child.
	 * The domain action also disposes a partially populated mapping set.
	 */
	for (i = 0; i < chip->cells[0].num_resources; i++)
		if (!irq_create_mapping(core->irq_domain,
					chip->cells[0].resources[i].start))
			return -ENOMEM;
	/*
	 * devres LIFO, success AND probe failure:
	 * children -> free_irq (waits for running parent handler) ->
	 * dispose child mappings/remove domain -> arrays/regmap/core.
	 * No manual domain removal can race the threaded handler. Request
	 * failure also unwinds the already registered domain action.
	 */
	return devm_mfd_add_devices(core->dev, PLATFORM_DEVID_AUTO, chip->cells,
				    chip->num_cells, NULL, 0, core->irq_domain);
}

static const struct of_device_id mtk_spmi_pmic_of_match[] = {
	{ .compatible = "mediatek,mt6363", .data = &mt6363_data },
	{ .compatible = "mediatek,mt6373", .data = &mt6373_data },
	{ }
};
MODULE_DEVICE_TABLE(of, mtk_spmi_pmic_of_match);

static struct spmi_driver mtk_spmi_pmic_driver = {
	.driver = {
		.name = "mtk-spmi-pmic-experimental",
		.of_match_table = mtk_spmi_pmic_of_match,
	},
	.probe = mtk_spmi_pmic_probe,
};
module_spmi_driver(mtk_spmi_pmic_driver);

MODULE_DESCRIPTION("Compile-review-only MT6363/MT6373 SPMI MFD candidate");
MODULE_LICENSE("GPL");
