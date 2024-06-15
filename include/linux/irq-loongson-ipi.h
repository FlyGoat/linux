#ifndef _IRQ_LOONGSON_IPI_H
#define _IRQ_LOONGSON_IPI_H

#include <linux/irqdomain.h>
#include <linux/jump_label.h>

DECLARE_STATIC_KEY_TRUE(loongson_ipi_iocsr);

static inline bool loonsgon_ipi_is_iocsr(void)
{
	return static_branch_likely(&loongson_ipi_iocsr);
}

extern void __iomem *loongson_ipi_get_mmio_buf(int cpu, int id);

extern int __init loongson_ipi_iocsr_init(struct fwnode_handle *fwnode,
                			  int parent_irq);

extern int loongson_ipi_write_buf(int cpu, int id, u64 data);

#endif
