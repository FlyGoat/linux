// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2010, 2011, 2012, Lemote, Inc.
 * Author: Chen Huacai, chenhc@lemote.com
 */

#include <irq.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/irq-loongson-ipi.h>
#include <linux/sched.h>
#include <linux/sched/hotplug.h>
#include <linux/sched/task_stack.h>
#include <linux/smp.h>
#include <linux/cpufreq.h>
#include <linux/kexec.h>
#include <asm/ipi.h>
#include <asm/processor.h>
#include <asm/time.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <loongson.h>
#include <loongson_regs.h>
#include <workarounds.h>

#include "smp.h"

#define MBOX_PC_ID	0
#define MBOX_SP_ID	1
#define MBOX_GP_ID	2
#define MBOX_A1_ID	3

DEFINE_PER_CPU(int, cpu_state);

static uint32_t core0_c0count[NR_CPUS];

static void ipi_write_buf(int cpu, struct task_struct *idle)
{
	loongson_ipi_write_buf(cpu, MBOX_A1_ID, 0);
	loongson_ipi_write_buf(cpu, MBOX_GP_ID, (u64)task_thread_info(idle));
	loongson_ipi_write_buf(cpu, MBOX_SP_ID, (u64)__KSTK_TOS(idle));
	loongson_ipi_write_buf(cpu, MBOX_PC_ID, (u64)&smp_bootstrap);
}

static irqreturn_t loongson3_ask_c0count(int irq, void *dev_id)
{
	int i, cpu = smp_processor_id();
	unsigned long c0count;

	BUG_ON(cpu != 0);
	c0count = read_c0_count();
	c0count = c0count ? c0count : 1;
	for (i = 1; i < nr_cpu_ids; i++)
		core0_c0count[i] = c0count;
	nudge_writes(); /* Let others see the result ASAP */

	return IRQ_HANDLED;
}

#define MAX_LOOPS 800
/*
 * SMP init and finish on secondary CPUs
 */
static void loongson3_init_secondary(void)
{
	int i;
	uint32_t initcount;
	unsigned int cpu = smp_processor_id();
	unsigned int imask = STATUSF_IP7 | STATUSF_IP6 |
			     STATUSF_IP3 | STATUSF_IP2;

	/* Set interrupt mask, but don't enable */
	change_c0_status(ST0_IM, imask);

	per_cpu(cpu_state, cpu) = CPU_ONLINE;
	cpu_set_core(&cpu_data[cpu],
		     cpu_logical_map(cpu) % loongson_sysconf.cores_per_package);
	cpu_data[cpu].package =
		cpu_logical_map(cpu) / loongson_sysconf.cores_per_package;

	i = 0;
	core0_c0count[cpu] = 0;
	mips_smp_send_ipi_single(0, IPI_ASK_C0COUNT);
	while (!core0_c0count[cpu]) {
		i++;
		cpu_relax();
	}

	if (i > MAX_LOOPS)
		i = MAX_LOOPS;
	if (cpu_data[cpu].package)
		initcount = core0_c0count[cpu] + i;
	else /* Local access is faster for loops */
		initcount = core0_c0count[cpu] + i/2;

	write_c0_count(initcount);
}

static void loongson3_smp_finish(void)
{
	write_c0_compare(read_c0_count() + mips_hpt_frequency/HZ);
	local_irq_enable();

	pr_info("CPU#%d finished, CP0_ST=%x\n",
			smp_processor_id(), read_c0_status());
}

static void __init loongson3_smp_setup(void)
{
	int i = 0, num = 0; /* i: physical id, num: logical id */

	init_cpu_possible(cpu_none_mask);

	/* For unified kernel, NR_CPUS is the maximum possible value,
	 * loongson_sysconf.nr_cpus is the really present value
	 */
	while (i < loongson_sysconf.nr_cpus) {
		if (loongson_sysconf.reserved_cpus_mask & (1<<i)) {
			/* Reserved physical CPU cores */
			__cpu_number_map[i] = -1;
		} else {
			__cpu_number_map[i] = num;
			__cpu_logical_map[num] = i;
			set_cpu_possible(num, true);
			/* Loongson processors are always grouped by 4 */
			cpu_set_cluster(&cpu_data[num], i / 4);
			num++;
		}
		i++;
	}
	pr_info("Detected %i available CPU(s)\n", num);

	while (num < loongson_sysconf.nr_cpus) {
		__cpu_logical_map[num] = -1;
		num++;
	}

	ipi_handlers[IPI_ASK_C0COUNT] = loongson3_ask_c0count;
	ipi_names[IPI_ASK_C0COUNT] = "Loongson Ask C0 Count";

	cpu_set_core(&cpu_data[0],
		     cpu_logical_map(0) % loongson_sysconf.cores_per_package);
	cpu_data[0].package = cpu_logical_map(0) / loongson_sysconf.cores_per_package;
}

static void __init loongson3_prepare_cpus(unsigned int max_cpus)
{
	init_cpu_present(cpu_possible_mask);
	per_cpu(cpu_state, smp_processor_id()) = CPU_ONLINE;
}

/*
 * Setup the PC, SP, and GP of a secondary processor and start it running!
 */
static int loongson3_boot_secondary(int cpu, struct task_struct *idle)
{
	pr_info("Booting CPU#%d...\n", cpu);

	ipi_write_buf(cpu, idle);

	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU

static int loongson3_cpu_disable(void)
{
	unsigned long flags;
	unsigned int cpu = smp_processor_id();

	set_cpu_online(cpu, false);
	calculate_cpu_foreign_map();
	local_irq_save(flags);
	clear_c0_status(ST0_IM);
	local_irq_restore(flags);
	local_flush_tlb_all();

	return 0;
}


static void loongson3_cpu_die(unsigned int cpu)
{
	while (per_cpu(cpu_state, cpu) != CPU_DEAD)
		cpu_relax();

	mb();
}

static __always_inline void loongson_mbox_mmio_loop(void __iomem *mbox_addr)
{
	register unsigned long pc, sp, gp, a1;

	while (!readl(mbox_addr))
		cpu_relax();

	pc = readl(mbox_addr + MBOX_PC_ID * 8);
	sp = readl(mbox_addr + MBOX_SP_ID * 8);
	gp = readl(mbox_addr + MBOX_GP_ID * 8);
	a1 = readl(mbox_addr + MBOX_A1_ID * 8);

	__asm__ __volatile__(
		"   .set push                         \n"
		"   .set noreorder                    \n"
		"   move    $sp, %[pc]                \n"
		"   move    $gp, %[gp]                \n"
		"   move    $a1, %[a1]                \n"
		"   jr      %[pc]                     \n"
		"   nop                               \n"
		"   .set pop                          \n"
		:
		: [pc] "r" (pc), [sp] "r" (sp), [gp] "r" (gp), [a1] "r" (a1)
		: "a1");
}

static __always_inline void loongson_mbox_csr_loop(void)
{
	register unsigned long pc, sp, gp, a1;

	while (!csr_readq(LOONGSON_CSR_MAIL_BUF0))
		cpu_relax();

	pc = csr_readq(LOONGSON_CSR_MAIL_BUF0 + MBOX_PC_ID * 8);
	sp = csr_readq(LOONGSON_CSR_MAIL_BUF0 + MBOX_SP_ID * 8);
	gp = csr_readq(LOONGSON_CSR_MAIL_BUF0 + MBOX_GP_ID * 8);
	a1 = csr_readq(LOONGSON_CSR_MAIL_BUF0 + MBOX_A1_ID * 8);

	__asm__ __volatile__(
		"   .set push                         \n"
		"   .set noreorder                    \n"
		"   move    $sp, %[pc]                \n"
		"   move    $gp, %[gp]                \n"
		"   move    $a1, %[a1]                \n"
		"   jr      %[pc]                     \n"
		"   nop                               \n"
		"   .set pop                          \n"
		:
		: [pc] "r" (pc), [sp] "r" (sp), [gp] "r" (gp), [a1] "r" (a1)
		: "a1");
}

/* To shutdown a core in Loongson 3, the target core should go to CKSEG1 and
 * flush all L1 entries at first. Then, another core (usually Core 0) can
 * safely disable the clock of the target core. loongson3_play_dead() is
 * called via CKSEG1 (uncached and unmmaped)
 */
static void loongson3_type1_play_dead(int *state_addr, void __iomem *mbox_addr)
{
	register int val;
	register void *addr;

	__asm__ __volatile__(
		"   .set push                     \n"
		"   .set noreorder                \n"
		"   li %[addr], 0x80000000        \n" /* KSEG0 */
		"1: cache 0, 0(%[addr])           \n" /* flush L1 ICache */
		"   cache 0, 1(%[addr])           \n"
		"   cache 0, 2(%[addr])           \n"
		"   cache 0, 3(%[addr])           \n"
		"   cache 1, 0(%[addr])           \n" /* flush L1 DCache */
		"   cache 1, 1(%[addr])           \n"
		"   cache 1, 2(%[addr])           \n"
		"   cache 1, 3(%[addr])           \n"
		"   addiu %[sets], %[sets], -1    \n"
		"   bnez  %[sets], 1b             \n"
		"   addiu %[addr], %[addr], 0x20  \n"
		"   li    %[val], 0x7             \n" /* *state_addr = CPU_DEAD; */
		"   sw    %[val], (%[state_addr]) \n"
		"   sync                          \n"
		"   cache 21, (%[state_addr])     \n" /* flush entry of *state_addr */
		"   .set pop                      \n"
		: [addr] "=&r" (addr), [val] "=&r" (val)
		: [state_addr] "r" (state_addr),
		  [sets] "r" (cpu_data[smp_processor_id()].dcache.sets));

		loongson_mbox_mmio_loop(mbox_addr);
}

static void loongson3_type2_play_dead(int *state_addr, void __iomem *mbox_addr)
{
	register int val;
	register void *addr;

	__asm__ __volatile__(
		"   .set push                     \n"
		"   .set noreorder                \n"
		"   li %[addr], 0x80000000        \n" /* KSEG0 */
		"1: cache 0, 0(%[addr])           \n" /* flush L1 ICache */
		"   cache 0, 1(%[addr])           \n"
		"   cache 0, 2(%[addr])           \n"
		"   cache 0, 3(%[addr])           \n"
		"   cache 1, 0(%[addr])           \n" /* flush L1 DCache */
		"   cache 1, 1(%[addr])           \n"
		"   cache 1, 2(%[addr])           \n"
		"   cache 1, 3(%[addr])           \n"
		"   addiu %[sets], %[sets], -1    \n"
		"   bnez  %[sets], 1b             \n"
		"   addiu %[addr], %[addr], 0x20  \n"
		"   li    %[val], 0x7             \n" /* *state_addr = CPU_DEAD; */
		"   sw    %[val], (%[state_addr]) \n"
		"   sync                          \n"
		"   cache 21, (%[state_addr])     \n" /* flush entry of *state_addr */
		"   .set pop                      \n"
		: [addr] "=&r" (addr), [val] "=&r" (val)
		: [state_addr] "r" (state_addr),
		  [sets] "r" (cpu_data[smp_processor_id()].dcache.sets));

		loongson_mbox_mmio_loop(mbox_addr);
}

static void loongson3_type3_play_dead(int *state_addr, void __iomem *mbox_addr)
{
	register int val;
	register void *addr;

	__asm__ __volatile__(
		"   .set push                     \n"
		"   .set noreorder                \n"
		"   li %[addr], 0x80000000        \n" /* KSEG0 */
		"1: cache 0, 0(%[addr])           \n" /* flush L1 ICache */
		"   cache 0, 1(%[addr])           \n"
		"   cache 0, 2(%[addr])           \n"
		"   cache 0, 3(%[addr])           \n"
		"   cache 1, 0(%[addr])           \n" /* flush L1 DCache */
		"   cache 1, 1(%[addr])           \n"
		"   cache 1, 2(%[addr])           \n"
		"   cache 1, 3(%[addr])           \n"
		"   addiu %[sets], %[sets], -1    \n"
		"   bnez  %[sets], 1b             \n"
		"   addiu %[addr], %[addr], 0x40  \n"
		"   li %[addr], 0x80000000        \n" /* KSEG0 */
		"2: cache 2, 0(%[addr])           \n" /* flush L1 VCache */
		"   cache 2, 1(%[addr])           \n"
		"   cache 2, 2(%[addr])           \n"
		"   cache 2, 3(%[addr])           \n"
		"   cache 2, 4(%[addr])           \n"
		"   cache 2, 5(%[addr])           \n"
		"   cache 2, 6(%[addr])           \n"
		"   cache 2, 7(%[addr])           \n"
		"   cache 2, 8(%[addr])           \n"
		"   cache 2, 9(%[addr])           \n"
		"   cache 2, 10(%[addr])          \n"
		"   cache 2, 11(%[addr])          \n"
		"   cache 2, 12(%[addr])          \n"
		"   cache 2, 13(%[addr])          \n"
		"   cache 2, 14(%[addr])          \n"
		"   cache 2, 15(%[addr])          \n"
		"   addiu %[vsets], %[vsets], -1  \n"
		"   bnez  %[vsets], 2b            \n"
		"   addiu %[addr], %[addr], 0x40  \n"
		"   li    %[val], 0x7             \n" /* *state_addr = CPU_DEAD; */
		"   sw    %[val], (%[state_addr]) \n"
		"   sync                          \n"
		"   cache 21, (%[state_addr])     \n" /* flush entry of *state_addr */
		"   .set pop                      \n"
		: [addr] "=&r" (addr), [val] "=&r" (val)
		: [state_addr] "r" (state_addr),
		  [sets] "r" (cpu_data[smp_processor_id()].dcache.sets),
		  [vsets] "r" (cpu_data[smp_processor_id()].vcache.sets));

	if (mbox_addr)
		loongson_mbox_mmio_loop(mbox_addr);
	else
		loongson_mbox_csr_loop();
}

void play_dead(void)
{
	int prid_imp, prid_rev, *state_addr;
	unsigned int cpu = smp_processor_id();
	void __iomem *mbox_addr;
	void (*play_dead_at_ckseg1)(int *, void __iomem *);

	idle_task_exit();
	cpuhp_ap_report_dead();

	mbox_addr = loongson_ipi_get_mmio_buf(cpu, 0);
	BUG_ON(!loonsgon_ipi_is_iocsr() && !mbox_addr);

	prid_imp = read_c0_prid() & PRID_IMP_MASK;
	prid_rev = read_c0_prid() & PRID_REV_MASK;

	if (prid_imp == PRID_IMP_LOONGSON_64G) {
		play_dead_at_ckseg1 =
			(void *)CKSEG1ADDR((unsigned long)loongson3_type3_play_dead);
		goto out;
	}

	switch (prid_rev) {
	case PRID_REV_LOONGSON3A_R1:
	default:
		play_dead_at_ckseg1 =
			(void *)CKSEG1ADDR((unsigned long)loongson3_type1_play_dead);
		break;
	case PRID_REV_LOONGSON3B_R1:
	case PRID_REV_LOONGSON3B_R2:
		play_dead_at_ckseg1 =
			(void *)CKSEG1ADDR((unsigned long)loongson3_type2_play_dead);
		break;
	case PRID_REV_LOONGSON3A_R2_0:
	case PRID_REV_LOONGSON3A_R2_1:
	case PRID_REV_LOONGSON3A_R3_0:
	case PRID_REV_LOONGSON3A_R3_1:
		play_dead_at_ckseg1 =
			(void *)CKSEG1ADDR((unsigned long)loongson3_type3_play_dead);
		break;
	}

out:
	state_addr = &per_cpu(cpu_state, cpu);
	mb();
	play_dead_at_ckseg1(state_addr, mbox_addr);
	BUG();
}

static int loongson3_disable_clock(unsigned int cpu)
{
	uint64_t core_id = cpu_core(&cpu_data[cpu]);
	uint64_t package_id = cpu_data[cpu].package;

	if ((read_c0_prid() & PRID_REV_MASK) == PRID_REV_LOONGSON3A_R1) {
		LOONGSON_CHIPCFG(package_id) &= ~(1 << (12 + core_id));
	} else {
		if (!(loongson_sysconf.workarounds & WORKAROUND_CPUHOTPLUG))
			LOONGSON_FREQCTRL(package_id) &= ~(1 << (core_id * 4 + 3));
	}
	return 0;
}

static int loongson3_enable_clock(unsigned int cpu)
{
	uint64_t core_id = cpu_core(&cpu_data[cpu]);
	uint64_t package_id = cpu_data[cpu].package;

	if ((read_c0_prid() & PRID_REV_MASK) == PRID_REV_LOONGSON3A_R1) {
		LOONGSON_CHIPCFG(package_id) |= 1 << (12 + core_id);
	} else {
		if (!(loongson_sysconf.workarounds & WORKAROUND_CPUHOTPLUG))
			LOONGSON_FREQCTRL(package_id) |= 1 << (core_id * 4 + 3);
	}
	return 0;
}

static int register_loongson3_notifier(void)
{
	return cpuhp_setup_state_nocalls(CPUHP_MIPS_SOC_PREPARE,
					 "mips/loongson:prepare",
					 loongson3_enable_clock,
					 loongson3_disable_clock);
}
early_initcall(register_loongson3_notifier);

#endif

const struct plat_smp_ops loongson3_smp_ops = {
	.send_ipi_single = mips_smp_send_ipi_single,
	.send_ipi_mask = mips_smp_send_ipi_mask,
	.init_secondary = loongson3_init_secondary,
	.smp_finish = loongson3_smp_finish,
	.boot_secondary = loongson3_boot_secondary,
	.smp_setup = loongson3_smp_setup,
	.prepare_cpus = loongson3_prepare_cpus,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable = loongson3_cpu_disable,
	.cpu_die = loongson3_cpu_die,
#endif
#ifdef CONFIG_KEXEC_CORE
	.kexec_nonboot_cpu = kexec_nonboot_cpu_jump,
#endif
};
