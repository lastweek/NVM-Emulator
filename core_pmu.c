/*
 *	Copyright (C) 2015 Yizhou Shan <shanyizhou@ict.ac.cn>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License along
 *	with this program; if not, write to the Free Software Foundation, Inc.,
 *	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 *	core_pmu.c - Using LLC_MISS to emulate NVM latency
 *
 *	Intel PMU can interrupt CPU in NMI manner when it overflows.
 *	This module inserts a pmu_nmi_handler to the NMI ISR list,
 *	in which we can do something everytime PMU overflows.
 *
 *	Performance monitoring events are architectural when they behave
 *	consistently across microarchitectures. There are 7 pre-defined
 *	architectural events in Intel cpus, including LLC_MISSES.
 *
 *	Each logical processor has its own set of __MSR_IA32_PEREVTSELx
 *	and __MSR_IA32_PMCx MSRs. Configuration facilities and counters are
 *	not shared between logical processors that sharing a processor core.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/nmi.h>
#include <asm/msr.h>

#define __MSR_IA32_PMC0				0x0C1
#define __MSR_IA32_PERFEVTSEL0			0x186
#define __MSR_CORE_PERF_GLOBAL_STATUS		0x38E
#define __MSR_CORE_PERF_GLOBAL_CTRL		0x38F

#define __MSR_IA32_MISC_PERFMON_ENABLE		(1ULL<<7)
#define __MSR_CORE_PERF_GLOBAL_OVF_CTRL		0x390
#define __MSR_IA32_MISC_ENABLE			0x1A0

/* Bit layout of MSR_IA32_PERFEVTSEL */
#define USR_MODE				(1ULL<<16)
#define OS_MODE 				(1ULL<<17)
#define EDGE_DETECT				(1ULL<<18)
#define PIN_CONTROL				(1ULL<<19)
#define INT_ENABLE				(1ULL<<20)
#define ANY_THREAD				(1ULL<<21)
#define ENABLE					(1ULL<<22)
#define INVERT					(1ULL<<23)
#define CMASK(val)				(u64)(val<<24)

/* 
 * Intel predefined events
 * 
 * LLC_MISSES: This event count each cache miss condition for
 * references to the last level cache. The event count may
 * include speculation and cache line fills due to the first
 * level cache hardware prefetcher, but may exclude cache
 * line fills due to other hardware-prefetchers
 */
enum PERF_EVENT_ID {
	UNHALTED_CYCLES			= 0,
	INSTRUCTIONS_RETIRED		= 1,
	UNHALTED_REF_CYCLES		= 2,
	LLC_REFERENCES			= 3,
	LLC_MISSES			= 4,
	BRANCH_INSTRUCTIONS_RETIRED	= 5,
	BRANCH_MISSES_RETIRED		= 6,

	EVENT_COUNT_MAX,
};

/* UMASK and Event Select */
const static u64 predefined_event_map[EVENT_COUNT_MAX] =
{
	[UNHALTED_CYCLES]		= 0x003c,
	[INSTRUCTIONS_RETIRED]		= 0x00c0,
	[UNHALTED_REF_CYCLES]		= 0x013c,
	[LLC_REFERENCES]		= 0x4f2e,
	[LLC_MISSES]			= 0x412e,
	[BRANCH_INSTRUCTIONS_RETIRED]	= 0x00c4,
	[BRANCH_MISSES_RETIRED]		= 0x00c5,
};

struct pre_event {
	int event;
	u64 threshold;
};

static struct pre_event pre_event_info = {
	.event = 0,
	.threshold = 0
};

/* PMU information from cpuid */
static u32  PERF_VERSION;
static u32  PC_PER_CPU;
static u32  PC_BIT_WIDTH;
static u32  LEN_EBX_TOENUM;
static u32  PRE_EVENT_MASK;

/* Module global variables */
static u64  PMU_LATENCY;
static u64  PMU_PMC0_INIT_VALUE;
static u64  CPU_BASE_FREQUENCY;
static char CPU_BRAND[48];

/* Show in /proc/pmu_info */
DEFINE_PER_CPU(unsigned long long, PMU_EVENT_COUNT);

//#################################################
//	Assembly Helper Functions
//#################################################

static inline void pmu_cpuid(u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
	unsigned int op = *eax;
	asm volatile (
		"cpuid"
		: "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
		: "a"(op)
	);
}

static inline u64 pmu_rdtsc(void)
{
	u32 edx, eax;
	u64 retval = 0;

	asm volatile (
		"rdtsc"
		:"=a"(eax), "=d"(edx)
	);
	retval = (u64)edx << 32 | (u64)eax;
	return retval;
}

static inline u64 pmu_rdmsr(u32 addr)
{
	u32 edx, eax;
	u64 retval = 0;
	
	asm volatile (
		"rdmsr"
		:"=a"(eax), "=d"(edx)
		:"c"(addr)
	);
	retval = (u64)edx << 32 | (u64)eax;
	return retval;
}

static inline void pmu_wrmsr(u32 addr, u64 value)
{
	asm volatile (
		"wrmsr"
		:
		:"c"(addr), "d"((u32)(value>>32)), "a"((u32)value)
	);
}

//#################################################
//	CPU General Infomation
//#################################################

static void cpu_facility_test(void)
{
	u64 msr;
	u32 eax, ebx, ecx, edx;
	
	eax = 0x01;
	pmu_cpuid(&eax, &ebx, &ecx, &edx);

	msr = pmu_rdmsr(__MSR_IA32_MISC_ENABLE);
	if (!(msr & __MSR_IA32_MISC_PERFMON_ENABLE)) {
		printk(KERN_INFO"MSR_IA32_MISC: PERFMON_ENABLE=0, PMU Disabled\n");
	}
}

/*
 * Get CPU brand and frequency information.
 * Consult Intel SDM Volume2 CPUID for detail.
 */
static void cpu_brand_frequency(void)
{
	char *s;
	int i;
	u32 eax, ebx, ecx, edx;

	eax = 0x80000000;
	pmu_cpuid(&eax, &ebx, &ecx, &edx);

	if (eax < 0x80000004U)
		printk(KERN_INFO"CPUID Extended Function Not Supported.\n");
	
	s = CPU_BRAND;
	for (i = 0; i < 3; i++) {
		eax = 0x80000002U + i;
		pmu_cpuid(&eax, &ebx, &ecx, &edx);
		memcpy(s, &eax, 4); s += 4;
		memcpy(s, &ebx, 4); s += 4;
		memcpy(s, &ecx, 4); s += 4;
		memcpy(s, &edx, 4); s += 4;
	}
	
	/* FIXME */
	CPU_BASE_FREQUENCY = 2270000000ULL;
}


/*
 * Get and initialize PMU information.
 * All cores in one CPU package have the same PMU settings.
 */
static void cpu_perf_info(void)
{
	u32 eax, ebx, ecx, edx;
	
	eax = 0x0A;
	pmu_cpuid(&eax, &ebx, &ecx, &edx);
	
	PERF_VERSION	= (eax & 0xFFU);
	PC_PER_CPU	= (eax & 0xFF00U)>>8;
	PC_BIT_WIDTH	= (eax & 0xFF0000U)>>16;
	LEN_EBX_TOENUM	= (eax & 0xFF000000U)>>24;
	PRE_EVENT_MASK	= (ebx & 0xFFU);
}

static void cpu_print_info(void)
{
	cpu_facility_test();
	cpu_brand_frequency();
	cpu_perf_info();
	
	printk(KERN_INFO"PMU %s\n", CPU_BRAND);
	printk(KERN_INFO"PMU PMU Version: %u\n", PERF_VERSION);
	printk(KERN_INFO"PMU PC per CPU: %u\n", PC_PER_CPU);
	printk(KERN_INFO"PMU PC bit width: %u\n", PC_BIT_WIDTH);
	printk(KERN_INFO"PMU Predefined events mask: %x\n", PRE_EVENT_MASK);
}


//#################################################
//	PMU Driver
//#################################################

/**
 * pmu_cpu_function_call - Call a function on specific CPU
 * @func:	the function to be called
 * @info:	the function call argument
 *
 * Wait until function has completed on other CPUs.
 */
static void pmu_cpu_function_call(int cpu, void (*func)(void *info), void *info)
{
	int err;
	err = smp_call_function_single(cpu, func, info, 1);
	
	if (err) {
		/* TODO why fail */
	}
}

/*
 * These set of functions are intended for a single CPU
 */
static void __pmu_show_msrs(void *info)
{
	u64 tmsr1, tmsr2, tmsr3;
	
	tmsr1 = pmu_rdmsr(__MSR_IA32_PMC0);
	tmsr2 = pmu_rdmsr(__MSR_IA32_PERFEVTSEL0);
	printk(KERN_INFO "PMU CPU %d: PMC0=%llx PERFEVTSEL0=%llx\n",
					smp_processor_id(), tmsr1, tmsr2);

	tmsr1 = pmu_rdmsr(__MSR_CORE_PERF_GLOBAL_CTRL);
	tmsr2 = pmu_rdmsr(__MSR_CORE_PERF_GLOBAL_STATUS);
	tmsr3 = pmu_rdmsr(__MSR_CORE_PERF_GLOBAL_OVF_CTRL);
	printk(KERN_INFO "PMU CPU %d: G_CTRL=%llx G_STATUS=%llx OVF_CTRL=%llx\n",
					smp_processor_id(), tmsr1, tmsr2, tmsr3);
}

static void __pmu_clear_msrs(void *info)
{
	pmu_wrmsr(__MSR_IA32_PMC0, 0x0);
	pmu_wrmsr(__MSR_IA32_PERFEVTSEL0, 0x0);
	pmu_wrmsr(__MSR_CORE_PERF_GLOBAL_CTRL, 0x0);
	pmu_wrmsr(__MSR_CORE_PERF_GLOBAL_OVF_CTRL, 0x0);
}

/*
 * Each enable bit in __MSR_CORE_PERF_GLOBAL_CTRL is
 * AND'ed with the enable bits for all privilege levels
 * in the __MSR_IA32_PEREVTSELx to start/stop the counting
 * counters. Counting is enabled if the AND'ed results is
 * true; counting is disabled when the result is false.
 *
 * Bit 0 in __MSR_CORE_PERF_GLOBAL_CTRL is responsiable
 * for enable/disable __MSR_IA32_PMC0
 */
static void __pmu_enable_counting(void *info)
{
	pmu_wrmsr(__MSR_CORE_PERF_GLOBAL_CTRL, 1);
}

static void __pmu_disable_counting(void *info)
{
	pmu_wrmsr(__MSR_CORE_PERF_GLOBAL_CTRL, 0);
}

static void __pmu_clear_ovf(void *info)
{
	pmu_wrmsr(__MSR_CORE_PERF_GLOBAL_OVF_CTRL, 1);
}

static void __pmu_enable_predefined_event(void *info)
{
	int evt;
	u64 val;

	if (!info)
		return;

	val = ((struct pre_event *)info)->threshold;
	evt = ((struct pre_event *)info)->event;
	
	/* 48-bit */
	val &= 0xffffffffffff;
	
	pmu_wrmsr(__MSR_IA32_PMC0, val);
	pmu_wrmsr(__MSR_IA32_PERFEVTSEL0,
				predefined_event_map[evt]
				| USR_MODE
				| INT_ENABLE
				| ENABLE );
}

static void __pmu_lapic_init(void *info)
{
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}


/*
 * These set of functions are intended to walk through all online CPUs
 */
static void pmu_show_msrs(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_show_msrs, NULL);
	}
}

static void pmu_clear_msrs(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_clear_msrs, NULL);
	}
}

static void pmu_enable_counting(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_enable_counting, NULL);
	}
}

static void pmu_disable_counting(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_disable_counting, NULL);
	}
}

static void pmu_clear_ovf(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_clear_ovf, NULL);
	}
}

static void pmu_enable_predefined_event(int event, u64 threshold)
{
	int cpu;
	pre_event_info.event = event;
	pre_event_info.threshold = threshold;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_enable_predefined_event, &pre_event_info);
	}
}

static void pmu_lapic_init(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_lapic_init, NULL);
	}
}

//#################################################
//	PMU NMI Handler
//#################################################

//#define PMU_DEBUG

int pmu_nmi_handler(unsigned int type, struct pt_regs *regs)
{
	u64 tmsr;
	
	tmsr = pmu_rdmsr(__MSR_CORE_PERF_GLOBAL_STATUS);
	if (!(tmsr & 0x1)) /* No overflow on *this* CPU */
		return NMI_DONE;
	
#ifdef PMU_DEBUG
	printk(KERN_INFO"PMU NMI CPU %d", smp_processor_id());
#endif
	
	/* Restart counting on *this* cpu. */
	__pmu_clear_msrs(NULL);
	__pmu_enable_predefined_event(&pre_event_info);
	__pmu_enable_counting(NULL);

	this_cpu_inc(PMU_EVENT_COUNT);
	return NMI_HANDLED;
}

static void pmu_regitser_nmi_handler(void)
{
	register_nmi_handler(NMI_LOCAL, pmu_nmi_handler,
		NMI_FLAG_FIRST, "PMU_NMI_HANDLER");
	printk(KERN_INFO "PMU NMI handler registed...");
}

static void pmu_unregister_nmi_handler(void)
{
	unregister_nmi_handler(NMI_LOCAL, "PMU_NMI_HANDLER");
	printk(KERN_INFO "PMU NMI handler unregisted...");
}

static void pmu_main(void)
{
	/*
	 * FIXME
	 * It seems that we do NOT need pmu_latency
	 */
	PMU_PMC0_INIT_VALUE	= -128;
	PMU_LATENCY		= CPU_BASE_FREQUENCY*10;

	/*
	 * We must *avoid* walking kernel code path as much as possiable.
	 * [NMI_FLAG_FIRST] tells kernel to put our pmu_nmi_handler
	 * to the head of nmiaction list. Therefore, whenever kernel receives
	 * NMI interrupts, our pmu_nmi_handler will be called first!
	 * Please see arch/x86/kernel/nmi.c for more information.
	 */
	pmu_lapic_init();
	pmu_regitser_nmi_handler();

	/*
	 * Enable every cpus PMU
	 */
	pmu_clear_msrs();
	pmu_enable_predefined_event(LLC_MISSES, PMU_PMC0_INIT_VALUE);
	pmu_enable_counting();
}

const char pmu_proc_format[] = "CPU %2d, PMU_EVENT_COUNT = %lld\n";
static int pmu_proc_show(struct seq_file *m, void *v)
{
	int cpu;
	for_each_online_cpu(cpu) {
		seq_printf(m, pmu_proc_format, cpu,
			per_cpu(PMU_EVENT_COUNT, cpu));
	}
	
	/* Reset each counter.
	 * So it will be easy to use: cat /proc/pmu_info 
	 * to store count numbers */
	get_cpu();
	for_each_online_cpu(cpu) {
		*per_cpu_ptr(&PMU_EVENT_COUNT, cpu) = 0;
	}
	put_cpu();
	
	return 0;
}

static int pmu_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmu_proc_show, NULL);
}

const struct file_operations pmu_proc_fops = {
	.open		= pmu_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release
};

static int pmu_init(void)
{
	int this_cpu = get_cpu();

	printk(KERN_INFO "PMU init on CPU %2d\n", this_cpu);
	
	/*
	 * A processor that supports architectural performance
	 * monitoring may not support all the predefined architectural
	 * performance events. The non-zero bits in CPUID.0AH:EBX
	 * indicate that the events are not available.
	 */
	cpu_print_info();
	
	/*
	 * Create /proc/pmu_info file
	 * User-Kernel space interface
	 */
	proc_create("pmu_info", 0, NULL, &pmu_proc_fops);

	pmu_main();
	put_cpu();
	
	return 0;
}

static void pmu_exit(void)
{
	/* Remove proc/pmu_info */
	remove_proc_entry("pmu_info", NULL);
	
	/* Clear PMU of all CPUs */
	pmu_clear_msrs();
	pmu_unregister_nmi_handler();
	printk(KERN_INFO "PMU exit on CPU %2d\n", smp_processor_id());
}

module_init(pmu_init);
module_exit(pmu_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("shanyizhou@ict.ac.cn");
