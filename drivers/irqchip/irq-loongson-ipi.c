#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/irq-loongson-ipi.h>
#include <linux/percpu.h>
#include <linux/smp.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#ifdef CONFIG_LOONGARCH
#include <loongarch.h>
#endif
#ifdef CONFIG_MIPS
#include <loongson_regs.h>
#endif

#define MAX_NODES	(NR_CPUS / 4)
#define NR_IPIS		32

#define IPI_CORE_OFFSET	0x100
#define IPI_REG_STATUS 	0x00
#define IPI_REG_EN	0x04
#define IPI_REG_SET	0x08
#define IPI_REG_CLR	0x0c
#define IPI_REG_BUF     0x20

#ifdef CONFIG_LOONGARCH
#define IPI_IOCSR_REG_STATUS	LOONGARCH_IOCSR_IPI_STATUS
#define IPI_IOCSR_REG_EN	LOONGARCH_IOCSR_IPI_EN
#define IPI_IOCSR_REG_SET	LOONGARCH_IOCSR_IPI_SET
#define IPI_IOCSR_REG_CLR	LOONGARCH_IOCSR_IPI_CLEAR
#define IPI_IOCSR_REG_BUF	LOONGARCH_IOCSR_MBUF0

#define IPI_IOCSR_REG_SEND	LOONGARCH_IOCSR_IPI_SEND
#define IPI_IOCSR_SEND_IP_SHF	IOCSR_IPI_SEND_IP_SHIFT
#define IPI_IOCSR_SEND_CPU_SHF	IOCSR_IPI_SEND_CPU_SHIFT
#define IPI_IOCSR_SEND_BLOCKING	IOCSR_IPI_SEND_BLOCKING

#define IPI_IOCSR_REG_MAIL_SEND	LOONGARCH_IOCSR_MBUF_SEND
#define IPI_IOCSR_MAIL_SEND_BLOCK	IOCSR_MBUF_SEND_BLOCKING
#define IPI_IOCSR_MAIL_SEND_BOX_LO(box)	IOCSR_MBUF_SEND_BOX_LO(box)
#define IPI_IOCSR_MAIL_SEND_BOX_HI(box)	IOCSR_MBUF_SEND_BOX_HI(box)
#define IPI_IOCSR_MAIL_SEND_BOX_SHF	IOCSR_MBUF_SEND_BOX_SHIFT
#define IPI_IOCSR_MAIL_SEND_CPU_SHF	IOCSR_MBUF_SEND_CPU_SHIFT
#define IPI_IOCSR_MAIL_SEND_BUF_SHF	IOCSR_MBUF_SEND_BUF_SHIFT
#define IPI_IOCSR_MAIL_SEND_H32_MASK	IOCSR_MBUF_SEND_H32_MASK

#define ipi_iocsr_writel(val, reg)	iocsr_write32(val, reg)
#define ipi_iocsr_readl(reg)		iocsr_read32(reg)
#define ipi_iocsr_writeq(val, reg)	csr_writeq(val, reg)
#define ipi_iocsr_readq(reg)		iocsr_write64(reg)
#endif

#ifdef CONFIG_MIPS
#define IPI_IOCSR_REG_STATUS	LOONGSON_CSR_IPI_STATUS
#define IPI_IOCSR_REG_EN	LOONGSON_CSR_IPI_EN
#define IPI_IOCSR_REG_SET	LOONGSON_CSR_IPI_SET
#define IPI_IOCSR_REG_CLR	LOONGSON_CSR_IPI_CLEAR
#define IPI_IOCSR_REG_BUF	LOONGSON_CSR_MAIL_BUF0

#define IPI_IOCSR_REG_SEND	LOONGSON_CSR_IPI_SEND
#define IPI_IOCSR_SEND_IP_SHF	CSR_IPI_SEND_IP_SHIFT
#define IPI_IOCSR_SEND_CPU_SHF	CSR_IPI_SEND_IP_SHIFT
#define IPI_IOCSR_SEND_BLOCKING	CSR_IPI_SEND_BLOCK

#define IPI_IOCSR_REG_MAIL_SEND	LOONGSON_CSR_MAIL_SEND
#define IPI_IOCSR_MAIL_SEND_BLOCK	CSR_MAIL_SEND_BLOCK
#define IPI_IOCSR_MAIL_SEND_BOX_LO(box)	CSR_MAIL_SEND_BOX_LOW(box)
#define IPI_IOCSR_MAIL_SEND_BOX_HI(box)	CSR_MAIL_SEND_BOX_HIGH(box)
#define IPI_IOCSR_MAIL_SEND_BOX_SHF	CSR_MAIL_SEND_BOX_SHIFT
#define IPI_IOCSR_MAIL_SEND_CPU_SHF	CSR_MAIL_SEND_CPU_SHIFT
#define IPI_IOCSR_MAIL_SEND_BUF_SHF	CSR_MAIL_SEND_BUF_SHIFT
#define IPI_IOCSR_MAIL_SEND_H32_MASK	CSR_MAIL_SEND_H32_MASK

#define ipi_iocsr_writel(val, reg)	csr_writel(val, reg)
#define ipi_iocsr_readl(reg)		csr_readl(reg)
#define ipi_iocsr_writeq(val, reg)	csr_writeq(val, reg)
#define ipi_iocsr_readq(reg)		csr_readq(reg)
#endif

static void __iomem *ipi_base[NR_CPUS] __read_mostly;
static struct irq_domain *ipi_domain __ro_after_init;
static DECLARE_BITMAP(ipi_allocated, NR_IPIS);
static int ipi_parent_irq;

DEFINE_STATIC_KEY_TRUE(loongson_ipi_iocsr);

void __iomem *loongson_ipi_get_mmio_buf(int cpu, int id)
{
	if (cpu >= NR_CPUS || !ipi_base[cpu])
		return NULL;

	return ipi_base[cpu] + IPI_REG_BUF + id * 8;
}

static int ipi_mmio_write_buf(int cpu, int id, u64 data)
{
	void __iomem *buf;

	buf = loongson_ipi_get_mmio_buf(cpu, id);
	if (!buf)
		return -ENXIO;

	writeq(data, buf);
	return 0;
}

static int ipi_iocsr_write_buf(int cpu, int id, u64 data)
{
	u64 val;

	/* Send high 32 bits */
	val = IPI_IOCSR_MAIL_SEND_BLOCK;
	val |= (IPI_IOCSR_MAIL_SEND_BOX_HI(id) << IPI_IOCSR_MAIL_SEND_BOX_SHF);
	val |= (cpu << IPI_IOCSR_MAIL_SEND_CPU_SHF);
	val |= (data & IPI_IOCSR_MAIL_SEND_H32_MASK);
	ipi_iocsr_writeq(val, IPI_IOCSR_REG_MAIL_SEND);

	/* Send low 32 bits */
	val = IPI_IOCSR_MAIL_SEND_BLOCK;
	val |= (IPI_IOCSR_MAIL_SEND_BOX_LO(id) << IPI_IOCSR_MAIL_SEND_BOX_SHF);
	val |= (cpu << IPI_IOCSR_MAIL_SEND_CPU_SHF);
	val |= (data << IPI_IOCSR_MAIL_SEND_BUF_SHF);
	ipi_iocsr_writeq(val, IPI_IOCSR_REG_MAIL_SEND);

	return 0;
}

int loongson_ipi_write_buf(int cpu, int id, u64 data)
{
	if (!ipi_domain)
		return -ENXIO;

	if (loonsgon_ipi_is_iocsr())
		return ipi_iocsr_write_buf(cpu, id, data);
	else
		return ipi_mmio_write_buf(cpu, id, data);
}

static void ipi_mmio_handle(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	int cpu = smp_processor_id();
	irq_hw_number_t hwirq;
	unsigned long status;

	chained_irq_enter(chip, desc);

	status = readl_relaxed(ipi_base[cpu] + IPI_REG_STATUS);

	for_each_set_bit(hwirq, &status, NR_IPIS)
		generic_handle_domain_irq(ipi_domain, hwirq);

	chained_irq_exit(chip, desc);
}

static void ipi_mmio_mask(struct irq_data *d)
{
	uint32_t val;

	val = readl(ipi_base[raw_smp_processor_id()] + IPI_REG_EN);
	val &= ~BIT(irqd_to_hwirq(d));
	writel(val, ipi_base[raw_smp_processor_id()] + IPI_REG_EN);
}

static void ipi_mmio_unmask(struct irq_data *d)
{
	uint32_t val;

	val = readl_relaxed(ipi_base[raw_smp_processor_id()] + IPI_REG_EN);
	val |= BIT(irqd_to_hwirq(d));
	writel_relaxed(val, ipi_base[raw_smp_processor_id()] + IPI_REG_EN);
}

static void ipi_mmio_ack(struct irq_data *d)
{
	uint32_t val;

	val = BIT(irqd_to_hwirq(d));
	writel_relaxed(val, ipi_base[raw_smp_processor_id()] + IPI_REG_CLR);
}

static void ipi_mmio_send_mask(struct irq_data *d,
				const struct cpumask *mask)
{
	int cpu;

	/*
	 * Ensure that stores to normal memory are visible to the
	 * other CPUs before issuing the IPI.
	 */
	smp_wmb();

	for_each_cpu(cpu, mask)
		writel_relaxed(BIT(d->hwirq), ipi_base[cpu] + IPI_REG_SET);

}

static const struct irq_chip ipi_mmio_chip = {
	.name		= "Loongson IPI MMIO",
	.irq_mask	= ipi_mmio_mask,
	.irq_unmask	= ipi_mmio_unmask,
	.irq_ack	= ipi_mmio_ack,
	.ipi_send_mask	= ipi_mmio_send_mask,
};

static inline bool ipi_iocsr_present(void)
{
	return TRUE;
#ifdef CONFIG_LOONGARCH
	if (cpu_has_csripi())
		return TRUE;
#endif

#ifdef CONFIG_MIPS
	if (cpu_has_csr()) {
		u32 val = csr_readl(LOONGSON_CSR_FEATURES);

		return val & LOONGSON_CSRF_IPI;
	}
#endif
	return FALSE;
}

static void ipi_iocsr_handle(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	irq_hw_number_t hwirq;
	unsigned long status;

	chained_irq_enter(chip, desc);

	status = ipi_iocsr_readl(IPI_IOCSR_REG_STATUS);

	for_each_set_bit(hwirq, &status, 32)
		generic_handle_domain_irq(ipi_domain, hwirq);

	chained_irq_exit(chip, desc);
}

static void ipi_iocsr_mask(struct irq_data *d)
{
	uint32_t val;

	val = ipi_iocsr_readl(IPI_IOCSR_REG_EN);
	val &= ~BIT(irqd_to_hwirq(d));
	ipi_iocsr_writel(val, IPI_IOCSR_REG_EN);
}

static void ipi_iocsr_unmask(struct irq_data *d)
{
	uint32_t val;

	val = ipi_iocsr_readl(IPI_IOCSR_REG_EN);
	val |= BIT(irqd_to_hwirq(d));
	ipi_iocsr_writel(val, IPI_IOCSR_REG_EN);
}

static void ipi_iocsr_ack(struct irq_data *d)
{
	uint32_t val;

	val = BIT(irqd_to_hwirq(d));
	ipi_iocsr_writel(val, IPI_IOCSR_REG_CLR);
}

static void ipi_iocsr_send_mask(struct irq_data *d,
				const struct cpumask *mask)
{
	int cpu;
	uint32_t val;

	/*
	 * Ensure that stores to normal memory are visible to the
	 * other CPUs before issuing the IPI.
	 */
	smp_wmb();

	for_each_cpu(cpu, mask) {
		val = IPI_IOCSR_SEND_BLOCKING | irqd_to_hwirq(d);
		val |= (cpu << IPI_IOCSR_SEND_CPU_SHF);
		ipi_iocsr_writel(val, IPI_IOCSR_REG_SEND);
	}
}

static const struct irq_chip ipi_iocsr_chip = {
	.name		= "Loongson IPI IOCSR",
	.irq_mask	= ipi_iocsr_mask,
	.irq_unmask	= ipi_iocsr_unmask,
	.irq_ack	= ipi_iocsr_ack,
	.ipi_send_mask	= ipi_iocsr_send_mask,
};

static int ipi_cpu_starting(unsigned int cpu)
{
	if (loonsgon_ipi_is_iocsr()) {
		ipi_iocsr_writel(0x0, IPI_IOCSR_REG_EN);
		ipi_iocsr_writel(0xffffffff, IPI_IOCSR_REG_CLR);
	} else {
		writel_relaxed(0x0, ipi_base[cpu] + IPI_REG_EN);
		writel_relaxed(0xffffffff, ipi_base[cpu] + IPI_REG_CLR);
	}

	enable_percpu_irq(ipi_parent_irq, IRQ_TYPE_NONE);

	return 0;
}

static int ipi_cpu_dying(unsigned int cpu)
{
	disable_percpu_irq(ipi_parent_irq);

	return 0;
}

static int ipi_domain_alloc(struct irq_domain *d, unsigned int virq,
			    unsigned int nr_irqs, void *arg)
{
	int i;
	const struct irq_chip *ipi_chip = loonsgon_ipi_is_iocsr() ?
		&ipi_iocsr_chip : &ipi_mmio_chip;

	i = bitmap_weight(ipi_allocated, NR_IPIS);
	if (i + nr_irqs > NR_IPIS)
		return -ENOSPC;

	for (i = 0; i < nr_irqs; i++) {
		int hwirq = find_first_zero_bit(ipi_allocated, NR_IPIS);

		bitmap_set(ipi_allocated, hwirq, 1);
		irq_set_percpu_devid(virq + i);
		irq_domain_set_info(d, virq + i, hwirq, ipi_chip, NULL,
				    handle_percpu_devid_irq, NULL, NULL);
	}

	return 0;
}

static int ipi_domain_match(struct irq_domain *d, struct device_node *node,
			    enum irq_domain_bus_token bus_token)
{
	bool is_ipi;

	switch (bus_token) {
	case DOMAIN_BUS_IPI:
		is_ipi = d->bus_token == DOMAIN_BUS_IPI;
		return (!node || (to_of_node(d->fwnode) == node)) && is_ipi;
	default:
		return 0;
	}
}

static const struct irq_domain_ops ipi_mux_domain_ops = {
	.alloc		= ipi_domain_alloc,
	.match		= ipi_domain_match,
};

static int __init ipi_irq_domain_init(struct fwnode_handle *fwnode)
{
	struct irq_domain *domain;
	int rc;

	domain = irq_domain_create_linear(fwnode, NR_IPIS,
					  &ipi_mux_domain_ops, NULL);
	if (!domain)
		return -ENOMEM;

	domain->flags |= IRQ_DOMAIN_FLAG_IPI_SINGLE;
	irq_domain_update_bus_token(domain, DOMAIN_BUS_IPI);
	
	ipi_domain = domain;

	return 0;
}

int __init loongson_ipi_iocsr_init(struct fwnode_handle *fwnode,
				   int parent_irq)
{
	int rc;
	
	if (ipi_domain)
		return -EEXIST;

	if (!ipi_iocsr_present())
		return -ENODEV;

	rc = ipi_irq_domain_init(fwnode);
	if (rc)
		return rc;

	irq_set_percpu_devid(parent_irq);
	irq_set_chained_handler(parent_irq, ipi_iocsr_handle);
	static_branch_enable(&loongson_ipi_iocsr);
	ipi_parent_irq = parent_irq;

	cpuhp_setup_state(CPUHP_AP_IRQ_LOONGSON_IPI_STARTING,
			  "irqchip/loongson-ipi:starting", ipi_cpu_starting,
			  ipi_cpu_starting);

	return 0;
}

static int __init ipi_iocsr_of_init(struct device_node *node,
				    struct device_node *parent)
{
	int rc;
	int parent_irq;

	if (ipi_domain)
		return -EEXIST;

	parent_irq = irq_of_parse_and_map(node, 0);
	if (!parent_irq) {
		pr_err("Failed to map parent IPI interrupt\n");
		return -ENXIO;
	}

	rc = loongson_ipi_iocsr_init(of_node_to_fwnode(node), parent_irq);
	if (rc) {
		pr_err("Failed to create IPI domain\n");
		return rc;
	}

	return 0;
}

static int __init ipi_mmio_of_init(struct device_node *node,
				   struct device_node *parent)
{
	int cpu = smp_processor_id();
	int core_per_node = 4;
	int max_nodes = MAX_NODES;
	int num_nodes, parent_irq, i, j, rc;
	void __iomem *node_base[MAX_NODES] = {NULL};

	if (ipi_domain)
		return -EEXIST;

	parent_irq = irq_of_parse_and_map(node, 0);
	if (!parent_irq) {
		pr_err("Failed to map parent IPI interrupt\n");
		return -ENXIO;
	}

	if (of_device_is_compatible(node, "loongson,ls2k-ipi")) {
		core_per_node = 2;
		max_nodes = 1;
	}

	num_nodes = of_address_count(node);
	num_nodes = min(num_nodes, max_nodes);

	for (i = 0; i < num_nodes; i++) {
		node_base[i] = of_iomap(node, i);
		if (!node_base[i]) {
			pr_err("Failed to map IPI registers\n");
			goto out_unmap;
		}
		for (j = 0; j < core_per_node; j++) {
			int cpuid = i * core_per_node + j;
			int cpu;
			if (cpuid >= num_possible_cpus())
				break;

			cpu = cpu_logical_map(cpuid);
			if (cpu < 0)
				continue;

			ipi_base[cpu] = node_base[i] + j * 0x100;
		}
	}

	writel_relaxed(0x0, ipi_base[cpu] + IPI_REG_EN);
	writel_relaxed(0xffffffff, ipi_base[cpu] + IPI_REG_CLR);

	irq_set_percpu_devid(parent_irq);
	irq_set_chained_handler(parent_irq, ipi_mmio_handle);

	rc = ipi_irq_domain_init(of_node_to_fwnode(node));
	if (rc) {
		pr_err("Failed to create IPI domain\n");
		goto out_unmap;
	}

	static_branch_disable(&loongson_ipi_iocsr);
	ipi_parent_irq = parent_irq;

	cpuhp_setup_state(CPUHP_AP_IRQ_LOONGSON_IPI_STARTING,
			  "irqchip/loongson-ipi:starting", ipi_cpu_starting,
			  ipi_cpu_dying);

	return 0;
out_unmap:
	for (i = 0; i < num_nodes; i++)
		iounmap(node_base[i]);

	return -ENXIO;
}

static int __init ipi_ls3a4000_of_init(struct device_node *node,
				       struct device_node *parent)
{
	int rc;

	if (ipi_domain)
		return -EEXIST;

	rc = ipi_iocsr_of_init(node, parent);
	if (rc) {
		rc = ipi_mmio_of_init(node, parent);
	}

	return rc;
}

IRQCHIP_DECLARE(ls3a_ipi, "loongson,ls3a-ipi", ipi_mmio_of_init);
IRQCHIP_DECLARE(ls2k_ipi, "loongson,ls2k-ipi", ipi_mmio_of_init);
IRQCHIP_DECLARE(iocsr_ipi, "loongson,iocsr-ipi", ipi_iocsr_of_init);
IRQCHIP_DECLARE(ls3a4000_ipi, "loongson,ls3a4000-ipi", ipi_ls3a4000_of_init);
