#include <linux/cpumask.h>
#include <linux/interrupt.h>

#ifndef __ASM_IPI_H
#define __ASM_IPI_H

#ifdef CONFIG_SMP
extern const char *ipi_names[];
extern irq_handler_t ipi_handlers[];

#ifdef CONFIG_GENERIC_IRQ_IPI
/*
 * This function will set up the necessary IPIs for Linux to communicate
 * with the CPUs in mask.
 * Return 0 on success.
 */
int mips_smp_ipi_allocate(const struct cpumask *mask);

/*
 * This function will free up IPIs allocated with mips_smp_ipi_allocate to the
 * CPUs in mask, which must be a subset of the IPIs that have been configured.
 * Return 0 on success.
 */
int mips_smp_ipi_free(const struct cpumask *mask);

void mips_smp_ipi_enable(void);
void mips_smp_ipi_disable(void);
extern bool mips_smp_ipi_have_virq_range(void);
extern void mips_smp_ipi_set_virq_range(int virq, int nr);
#else
static inline void mips_smp_ipi_enable(void)
{
}

static inline void mips_smp_ipi_disable(void)
{
}
#endif /* CONFIG_GENERIC_IRQ_IPI */
#else
void mips_smp_ipi_set_virq_range(int virq, int nr)
{
}

static inline bool mips_smp_ipi_have_virq_range(void)
{
	return false;
}
#endif /* CONFIG_SMP */
#endif
