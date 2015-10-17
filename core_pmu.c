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
 *	This module inserts a core_pmu_nmi_handler to the NMI ISR list,
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

#define pr_fmt(fmt) "CORE PMU: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/delay.h>

#include <asm/nmi.h>
#include <asm/msr.h>

int core_pmu_proc_create(void);
void core_pmu_proc_remove(void);

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

/* PMU information from cpuid */
static u32  PERF_VERSION;
static u32  PC_PER_CPU;
static u32  PC_BIT_WIDTH;
static u32  LEN_EBX_TOENUM;
static u32  PRE_EVENT_MASK;

/* Module global variables */
static u64  PMU_LATENCY;
static u64  CPU_BASE_FREQUENCY;
static char CPU_BRAND[48];

struct pre_event {
	int event;
	u64 threshold;
};

static struct pre_event pre_event_info = {
	.event = 0,
	.threshold = 0
};

u64 pre_event_init_value;

/* Show in /proc/core_pmu */
DEFINE_PER_CPU(u64, PERCPU_NMI_TIMES);

//#################################################
//	Assembly Helper Functions
//#################################################

static inline void core_pmu_cpuid(u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
	unsigned int op = *eax;
	asm volatile (
		"cpuid"
		: "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
		: "a"(op)
	);
}

static inline u64 core_pmu_rdtsc(void)
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

static inline u64 core_pmu_rdmsr(u32 addr)
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

static inline void core_pmu_wrmsr(u32 addr, u64 value)
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
	core_pmu_cpuid(&eax, &ebx, &ecx, &edx);

	msr = core_pmu_rdmsr(__MSR_IA32_MISC_ENABLE);
	if (!(msr & __MSR_IA32_MISC_PERFMON_ENABLE)) {
		pr_info("MSR_IA32_MISC: PERFMON_ENABLE=0, PMU Disabled\n");
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
	core_pmu_cpuid(&eax, &ebx, &ecx, &edx);

	if (eax < 0x80000004U)
		pr_info("CPUID Extended Function Not Supported.\n");
	
	s = CPU_BRAND;
	for (i = 0; i < 3; i++) {
		eax = 0x80000002U + i;
		core_pmu_cpuid(&eax, &ebx, &ecx, &edx);
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
	core_pmu_cpuid(&eax, &ebx, &ecx, &edx);
	
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
	
	pr_info("%s\n", CPU_BRAND);
	pr_info("PMU Version: %u\n", PERF_VERSION);
	pr_info("PC per CPU: %u\n", PC_PER_CPU);
	pr_info("PC bit width: %u\n", PC_BIT_WIDTH);
	pr_info("Predefined events mask: %x\n", PRE_EVENT_MASK);
}


//#################################################
//	PMU Driver
//#################################################

/**
 * core_pmu_cpu_function_call - Call a function on specific CPU
 * @func:	the function to be called
 * @info:	the function call argument
 *
 * Wait until function has completed on other CPUs.
 */
static void core_pmu_cpu_function_call(int cpu, void (*func)(void *info), void *info)
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
static void __core_pmu_show_msrs(void *info)
{
	u64 tmsr1, tmsr2, tmsr3;
	
	tmsr1 = core_pmu_rdmsr(__MSR_IA32_PMC0);
	tmsr2 = core_pmu_rdmsr(__MSR_IA32_PERFEVTSEL0);
	pr_info("CPU %d: PMC0=%llx PERFEVTSEL0=%llx\n",
		smp_processor_id(), tmsr1, tmsr2);

	tmsr1 = core_pmu_rdmsr(__MSR_CORE_PERF_GLOBAL_CTRL);
	tmsr2 = core_pmu_rdmsr(__MSR_CORE_PERF_GLOBAL_STATUS);
	tmsr3 = core_pmu_rdmsr(__MSR_CORE_PERF_GLOBAL_OVF_CTRL);
	pr_info("CPU %d: G_CTRL=%llx G_STATUS=%llx OVF_CTRL=%llx\n",
		smp_processor_id(), tmsr1, tmsr2, tmsr3);
}

static void __core_pmu_clear_msrs(void *info)
{
	core_pmu_wrmsr(__MSR_IA32_PMC0, 0x0);
	core_pmu_wrmsr(__MSR_IA32_PERFEVTSEL0, 0x0);
	core_pmu_wrmsr(__MSR_CORE_PERF_GLOBAL_CTRL, 0x0);
	core_pmu_wrmsr(__MSR_CORE_PERF_GLOBAL_OVF_CTRL, 0x0);
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
static void __core_pmu_enable_counting(void *info)
{
	core_pmu_wrmsr(__MSR_CORE_PERF_GLOBAL_CTRL, 1);
}

static void __core_pmu_disable_counting(void *info)
{
	core_pmu_wrmsr(__MSR_CORE_PERF_GLOBAL_CTRL, 0);
}

static void __core_pmu_clear_ovf(void *info)
{
	core_pmu_wrmsr(__MSR_CORE_PERF_GLOBAL_OVF_CTRL, 1);
}

static void __core_pmu_enable_predefined_event(void *info)
{
	int evt;
	u64 val;

	if (!info)
		return;

	/* TODO for core_proc */
	//val = ((struct pre_event *)info)->threshold;
	val = pre_event_init_value;
	evt = ((struct pre_event *)info)->event;
	
	/* 48-bit Mask, in case #GP occurs */
	val &= (1ULL<<48)-1;
	
	core_pmu_wrmsr(__MSR_IA32_PMC0, val);
	core_pmu_wrmsr(__MSR_IA32_PERFEVTSEL0,
				predefined_event_map[evt]
				| USR_MODE
				| INT_ENABLE
				| ENABLE );
}

static void __core_pmu_lapic_init(void *info)
{
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}


/*
 * These set of functions are intended to walk through all online CPUs
 */
static void core_pmu_show_msrs(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		core_pmu_cpu_function_call(cpu, __core_pmu_show_msrs, NULL);
	}
}

static void core_pmu_clear_msrs(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		core_pmu_cpu_function_call(cpu, __core_pmu_clear_msrs, NULL);
	}
}

static void core_pmu_enable_counting(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		core_pmu_cpu_function_call(cpu, __core_pmu_enable_counting, NULL);
	}
}

static void core_pmu_disable_counting(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		core_pmu_cpu_function_call(cpu, __core_pmu_disable_counting, NULL);
	}
}

static void core_pmu_clear_ovf(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		core_pmu_cpu_function_call(cpu, __core_pmu_clear_ovf, NULL);
	}
}

static void core_pmu_enable_predefined_event(int event, u64 threshold)
{
	int cpu;
	pre_event_info.event = event;
	pre_event_info.threshold = threshold;
	for_each_online_cpu(cpu) {
		core_pmu_cpu_function_call(cpu, __core_pmu_enable_predefined_event, &pre_event_info);
	}
}

static void core_pmu_lapic_init(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		core_pmu_cpu_function_call(cpu, __core_pmu_lapic_init, NULL);
	}
}

//#################################################
//	PMU NMI Handler
//#################################################

static int core_pmu_nmi_handler(unsigned int type, struct pt_regs *regs)
{
	u64 tmsr;
	
	tmsr = core_pmu_rdmsr(__MSR_CORE_PERF_GLOBAL_STATUS);
	if (!(tmsr & 0x1)) /* No overflow on *this* CPU */
		return NMI_DONE;
	
#ifdef CORE_PMU_DEBUG
	pr_info("NMI CPU %d", smp_processor_id());
#endif
	
	/* Restart counting on *this* cpu. */
	__core_pmu_clear_msrs(NULL);
	__core_pmu_enable_predefined_event(&pre_event_info);
	__core_pmu_enable_counting(NULL);

	this_cpu_inc(PERCPU_NMI_TIMES);
	return NMI_HANDLED;
}

static void core_pmu_regitser_nmi_handler(void)
{
	register_nmi_handler(NMI_LOCAL, core_pmu_nmi_handler,
		NMI_FLAG_FIRST, "CORE_PMU_NMI_HANDLER");
	pr_info("NMI handler registed...");
}

static void core_pmu_unregister_nmi_handler(void)
{
	unregister_nmi_handler(NMI_LOCAL, "CORE_PMU_NMI_HANDLER");
	pr_info("NMI handler unregisted...");
}

static void core_pmu_main(void)
{
	/* Initial value of counter: -256
	 * The init value can be changed via /proc interface
	 */
	pre_event_init_value	= -256;
	PMU_LATENCY		= CPU_BASE_FREQUENCY*10;

	/* We must *avoid* walking kernel code path as much as possiable.
	 * [NMI_FLAG_FIRST] tells kernel to put our core_pmu_nmi_handler to the head
	 * of nmiaction list. Therefore, whenever kernel receives NMI
	 * interrupts, our core_pmu_nmi_handler will be called first!
	 */
	core_pmu_lapic_init();
	core_pmu_regitser_nmi_handler();

	/* Enable PMU on all online CPUs */
	core_pmu_clear_msrs();
	core_pmu_enable_predefined_event(LLC_MISSES, pre_event_init_value);
	core_pmu_enable_counting();
}

static int core_pmu_init(void)
{
	int ret;

	pr_info("INIT ON CPU %2d (NODE %2d)",
		smp_processor_id(), numa_node_id());
	
	/* We rely on this proc file */
	ret = core_pmu_proc_create();
	if (ret)
		return ret;
	
	/*
	 * Pay attention to the output messages:
	 * A processor that supports architectural performance
	 * monitoring may not support all the predefined architectural
	 * performance events. The non-zero bits in CPUID.0AH:EBX
	 * indicate that the events are not available.
	 */
	cpu_print_info();

	/* Start core PMU */
	core_pmu_main();
	
	return 0;
}

static void core_pmu_exit(void)
{
	/* Remove proc file*/
	core_pmu_proc_remove();

	/* Clear PMU of all CPUs */
	core_pmu_clear_msrs();
	core_pmu_unregister_nmi_handler();
	
	pr_info("EXIT ON CPU %2d (NODE %2d)",
		smp_processor_id(), numa_node_id());
}

module_init(core_pmu_init);
module_exit(core_pmu_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("shanyizhou@ict.ac.cn");
