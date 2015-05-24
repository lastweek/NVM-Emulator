/**
 *	Desciption:
 *	Intel PMU can interrupt CPU in NMI manner when it overflows.
 *	This module inserts a pmu_nmi_handler to the NMI ISR list,
 *	in which we can do something everytime PMU overflows.
 *
 *	Performance monitoring events are architectural when they
 *	behave consistently across microarchitectures. There are 7
 *	pre-defined architectural events in Intel cpus, including
 *	LLC_MISSES.
 *
 *	Each logical processor has its own set of MSR_IA32_PEREVTSELx
 *	and MSR_IA32_PMCx MSRs. Configuration facilities and counters
 *	are not shared between logical processors that sharing a
 *	processor core.
 **/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/percpu-defs.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <asm/nmi.h>
#include <asm/msr.h>

#define MSR_IA32_PMC0					0x000000C1
#define MSR_IA32_MISC_ENABLE			0x000001A0
#define MSR_CORE_PERF_GLOBAL_STATUS		0x0000038E
#define MSR_CORE_PERF_GLOBAL_CTRL		0x0000038F

#define MSR_IA32_PERFEVTSEL0			0x00000186
#define MSR_IA32_MISC_PERFMON_ENABLE 	1ULL<<7
#define MSR_CORE_PERF_GLOBAL_OVF_CTRL	0x00000390

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

/**
 * BIT FIELD OF MSR_IA32_PERFEVTSEL0
 **/
#define USR_MODE	1ULL<<16
#define OS_MODE 	1ULL<<17
#define EDGE_DETECT	1ULL<<18
#define PIN_CONTROL 1ULL<<19
#define INT_ENABLE	1ULL<<20
#define ANY_THREAD	1ULL<<21
#define ENABLE		1ULL<<22
#define INVERT		1ULL<<23
#define CMASK(val)  (u64)(val<<24)

/**
 * INTEL PREDEFINED EVENTS
 **/
enum perf_hw_id {
	UNHALTED_CYCLES				=	0,
	INSTRUCTIONS_RETIRED		=	1,
	UNHALTED_REF_CYCLES			=	2,
	LLC_REFERENCES				=	3,
	LLC_MISSES					=	4,
	BRANCH_INSTRUCTIONS_RETIRED	=	5,
	BRANCH_MISSES_RETIRED		=	6,

	EVENT_COUNT_MAX,
};

static u64 intel_predefined_eventmap[EVENT_COUNT_MAX] =
{
	[UNHALTED_CYCLES]				= 0x003c,
	[INSTRUCTIONS_RETIRED]			= 0x00c0,
	[UNHALTED_REF_CYCLES]			= 0x013c,
	[LLC_REFERENCES]				= 0x4f2e,
	[LLC_MISSES]					= 0x412e,
	[BRANCH_INSTRUCTIONS_RETIRED]	= 0x00c4,
	[BRANCH_MISSES_RETIRED]			= 0x00c5,
};

struct pre_event {
	int event;
	u64 threshold;
};

static struct pre_event pre_event_info = {
	.event = 0,
	.threshold = 0
};

/**
 * PMU INFORMATION FROM CPUID
 **/
static u32  PERF_VERSION;
static u32  PC_PER_CPU;
static u32  PC_BIT_WIDTH;
static u32  LEN_EBX_TOENUM;
static u32  PRE_EVENT_MASK;

/**
 * MODULE GLOBAL VARIABLES
 **/
static u32  PMU_LATENCY;
static u64  PMU_PMC0_INIT_VALUE;
static u64  CPU_BASE_FREQUENCY;
static char CPU_BRAND[48];

/**
 * PER CPU EXCLUSIVE VARIABLES
 **/
DEFINE_PER_CPU(int, PMU_EVENT_COUNT);
DEFINE_PER_CPU(u64, TSC1);
DEFINE_PER_CPU(u64, TSC2);


//#################################################
// ASSEMBLER PART
//#################################################

static void
pmu_cpuid(u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
	unsigned int op = *eax;
	asm volatile(
		"cpuid"
		: "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
		: "a"(op)
	);
}

static u64
pmu_rdtsc(void)
{
	u32 edx, eax;
	u64 retval = 0;

	asm volatile(
		"rdtsc"
		:"=a"(eax), "=d"(edx)
	);
	retval = (u64)edx << 32 | (u64)eax;
	return retval;
}

static u64
pmu_rdmsr(u32 addr)
{
	u32 edx, eax;
	u64 retval = 0;
	
	asm volatile(
		"rdmsr"
		:"=a"(eax), "=d"(edx)
		:"c"(addr)
	);
	retval = (u64)edx << 32 | (u64)eax;
	return retval;
}

static void
pmu_wrmsr(u32 addr, u64 value)
{
	asm volatile(
		"wrmsr"
		:
		:"c"(addr), "d"((u32)(value>>32)), "a"((u32)value)
	);
}

//#################################################
// CPU PART
//#################################################

static void
cpu_facility_test(void)
{
	u64 msr;
	u32 eax, ebx, ecx, edx;
	
	eax = 0x01;
	pmu_cpuid(&eax, &ebx, &ecx, &edx);

	msr = pmu_rdmsr(MSR_IA32_MISC_ENABLE);
	if (!(msr & MSR_IA32_MISC_PERFMON_ENABLE)) {
		printk(KERN_INFO"MSR_IA32_MISC_PERFMON_ENABLE=0, PMU Disabled\n");
	}
}

/**
 * Get cpu brand and frequency information.
 * Consult Intel Dev-Manual2 CPUID for detail.
 **/
static void
cpu_brand_frequency(void)
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


/**
 * Get and initialize PMU information.
 * All cores in one CPU package have the same PMU settings.
 **/
static void
cpu_perf_info(void)
{
	u32 eax, ebx, ecx, edx;
	
	eax = 0x0A;
	pmu_cpuid(&eax, &ebx, &ecx, &edx);
	
	PERF_VERSION	=  eax & 0xFFU;
	PC_PER_CPU		= (eax & 0xFF00U)>>8;
	PC_BIT_WIDTH	= (eax & 0xFF0000U)>>16;
	LEN_EBX_TOENUM	= (eax & 0xFF000000U)>>24;
	PRE_EVENT_MASK	=  ebx & 0xFFU;
}

static void
cpu_general_info(void)
{
	cpu_facility_test();
	cpu_brand_frequency();
	cpu_perf_info();
}

static void
cpu_print_info(void)
{
	printk(KERN_INFO"PMU %s\n", CPU_BRAND);
	printk(KERN_INFO"PMU PMU Version: %u\n", PERF_VERSION);
	printk(KERN_INFO"PMU PC per cpu: %u\n", PC_PER_CPU);
	printk(KERN_INFO"PMU PC bit width: %u\n", PC_BIT_WIDTH);
	printk(KERN_INFO"PMU Predefined events mask: %x\n", PRE_EVENT_MASK);
}


//#################################################
// PMU PART
//#################################################

/**
 * pmu_cpu_function_call - Call a function on specific CPU
 * @func:	the function to be called
 * @info:	the function call argument
 *
 * Wait until function has completed on other CPUs.
 */
static void
pmu_cpu_function_call(int cpu, void (*func)(void *info), void *info)
{
	int err;
	err = smp_call_function_single(cpu, func, info, 1);
	
	if (err) {
		//why fail
	}
}

/**
 * pmu_task_function_call - call a function on cpu on which a task runs
 * @p:		the task to evaluate
 * @func:	the function to be called
 * @info:	the function call argument
 *
 * Calls the function @func when the task is currently running. This might
 * be on the current CPU, which just calls the function directly
 *
 * returns: @func return value, or
 *	    -ESRCH  - when the process isn't running
 *	    -EAGAIN - when the process moved away
 **/
static void
pmu_task_function_call(struct task_struct *p, int (*func) (void *info), void *info)
{
	int err;
	if (task_curr(p))
		err = smp_call_function_single(task_cpu(p), func, info, 1);

	if (err) {
		//why fail
	}
}

/**
 * These set of functions are intended for a single cpu.
 **/
static void
__pmu_show_msrs(void *info)
{
	u64 tmsr1, tmsr2, tmsr3;
	
	tmsr1 = pmu_rdmsr(MSR_IA32_PMC0);
	tmsr2 = pmu_rdmsr(MSR_IA32_PERFEVTSEL0);
	printk(KERN_INFO "PMU CPU %d: PMC0=%llx PERFEVTSEL0=%llx\n",
					smp_processor_id(), tmsr1, tmsr2);

	tmsr1 = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_CTRL);
	tmsr2 = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_STATUS);
	tmsr3 = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL);
	printk(KERN_INFO "PMU CPU %d: G_CTRL=%llx G_STATUS=%llx OVF_CTRL=%llx\n",
					smp_processor_id(), tmsr1, tmsr2, tmsr3);
}

static void
__pmu_clear_msrs(void *info)
{
	pmu_wrmsr(MSR_IA32_PMC0, 0x0);
	pmu_wrmsr(MSR_IA32_PERFEVTSEL0, 0x0);
	pmu_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0x0);
	pmu_wrmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, 0x0);
}

/**
 *  Each enable bit in MSR_CORE_PERF_GLOBAL_CTRL is
 *	AND'ed with the enable bits for all privilege levels
 *	in the MSR_IA32_PEREVTSELx to start/stop the counting
 *	counters. Counting is enabled if the AND'ed results is
 *	true; counting is disabled when the result is false.
 *
 *	Bit 0 in MSR_CORE_PERF_GLOBAL_CTRL is responsiable
 *	for enable/disable MSR_IA32_PMC0
 **/
static void
__pmu_enable_counting(void *info)
{
	pmu_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 1);
}
static void
__pmu_disable_counting(void *info)
{
	pmu_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0);
}

static void
__pmu_clear_ovf(void *info)
{
	pmu_wrmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, 1);
}

static void
__pmu_enable_predefined_event(void *info)
{
	int evt;
	u64 val;

	if (!info)
		return;
	evt = ((struct pre_event *)info)->event;
	val = ((struct pre_event *)info)->threshold;
	if (evt >= EVENT_COUNT_MAX || evt < 0)
		return;
	
	pmu_wrmsr(MSR_IA32_PMC0, val);
	pmu_wrmsr(MSR_IA32_PERFEVTSEL0,
				intel_predefined_eventmap[evt]
				| USR_MODE
				| INT_ENABLE
				| ENABLE );
}

static void
__pmu_lapic_init(void *info)
{
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}


/**
 * These set of functions are intended
 * to walk through all online cpus.
 **/
static void
pmu_show_msrs(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_show_msrs, NULL);
	}
}

static void
pmu_clear_msrs(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_clear_msrs, NULL);
	}
}

static void
pmu_enable_counting(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_enable_counting, NULL);
	}
}

static void
pmu_disable_counting(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_disable_counting, NULL);
	}
}

static void
pmu_clear_ovf(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_clear_ovf, NULL);
	}
}

static void
pmu_enable_predefined_event(int event, u64 threshold)
{
	int cpu;
	pre_event_info.event = event;
	pre_event_info.threshold = threshold;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_enable_predefined_event, &pre_event_info);
	}
}

static void
pmu_lapic_init(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, __pmu_lapic_init, NULL);
	}
}

//#################################################
// NMI HANDLER PART
//#################################################

int pmu_nmi_handler(unsigned int type, struct pt_regs *regs)
{
	int i;
	u64 tmsr;
	
	/**
	 * GET TIMESTAMP FIRST 
	 **/
	this_cpu_write(TSC2, pmu_rdtsc());
	
	/**
	 * PMC0 IS NOT OVERFLOW. RETURN 0
	 **/
	tmsr = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_STATUS);
	if (!(tmsr & 0x1))
		return NMI_DONE;

	/**
	 * Some chipsets need to unmask the LVTPC in a particular
	 * spot inside the nmi handler.  As a result, the unmasking
	 * was pushed into all the nmi handlers.
	 **/
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	
	/**
	 * Restart counting on THIS cpu.
	 **/
	//__pmu_clear_msrs(NULL);
	//__pmu_enable_predefined_event(&pre_event_info);
	//__pmu_enable_counting(NULL);
	__pmu_disable_counting(NULL);

	printk(KERN_INFO"PMU NMI CPU %d OVF! TSC=%llu\n",
					smp_processor_id(), this_cpu_read(TSC2));
	/**
	 * Simulate Lantency
	 **/
	for (i = 0; i < PMU_LATENCY; i++)
		asm("nop");

	this_cpu_inc(PMU_EVENT_COUNT);
	return NMI_HANDLED;
}

static void
pmu_main(void)
{
	u64 tsc1;

	PMU_EVENT_COUNT		=	0;
	PMU_LATENCY			=	0;
	PMU_PMC0_INIT_VALUE	=	-9999;

	/**
	 * Pay attention to the status of predefined performance events.
	 * Not all Intel CPUs support all 7 events. The non-zero bits in
	 * CPUID.0AH:EBX indicate the events that are not available.
	 **/
	cpu_general_info();
	cpu_print_info();
	
	/**
	 * We MUST AVOID walking kernel code path as much as possiable.
	 * [NMI_FLAG_FIRST] tell kernel to put our pmu_nmi_hander
	 * to the head of nmiaction list. Therefore, whenever kernel receives
	 * NMI interrupts, our pmu_nmi_handler will be called first!
	 **/
	pmu_lapic_init();
	register_nmi_handler(NMI_LOCAL, pmu_nmi_handler, NMI_FLAG_FIRST, "PMU_NMI_HANDLER");

	/**
	 * Enable every cpus PMU
	 **/
	pmu_clear_msrs();
	pmu_enable_predefined_event(LLC_MISSES, PMU_PMC0_INIT_VALUE);
	pmu_enable_counting();
	
	tsc1 = pmu_rdtsc();
	printk(KERN_INFO "PMU Event Start Counting TSC: %llu\n", tsc1);
}


//#################################################
// MODULE PART
//#################################################

static int
pmu_init(void)
{
	printk(KERN_INFO "PMU INIT ON CPU %d\n", smp_processor_id());
	printk(KERN_INFO "PMU ONLINE CPUS: %d\n", num_online_cpus());
	pmu_main();
	return 0;
}

static void
pmu_exit(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		printk(KERN_INFO "PMU CPU %d, count=%d\n", cpu, per_cpu(PMU_EVENT_COUNT, cpu));
	}

	pmu_show_msrs();
	unregister_nmi_handler(NMI_LOCAL, "PMU_NMI_HANDLER");
	printk(KERN_INFO "PMU module exit.\n");
}

module_init(pmu_init);
module_exit(pmu_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shan Yizhou");
