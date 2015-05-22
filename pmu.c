#include <linux/init.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/rculist.h>
#include <linux/percpu-defs.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>

#include <asm/nmi.h>
#include <asm/msr.h>


//#define MSR_IA32_MISC_ENABLE	0x000001A0
//#define MSR_IA32_PMC0			0x000000C1
//#define MSR_CORE_PERF_GLOBAL_STATUS	0x0000038E
//#define MSR_CORE_PERF_GLOBAL_CTRL		0x0000038F
#define MSR_IA32_PERFEVTSEL0			0x00000186
#define MSR_IA32_MISC_PERFMON_ENABLE 	1ULL<<7
#define MSR_CORE_PERF_GLOBAL_OVF_CTRL	0x00000390

/**
 * Bit field of MSR_IA32_PERFEVTSEL
 */
#define USR_MODE	1ULL<<16
#define OS_MODE 	1ULL<<17
#define EDGE_DETECT	1ULL<<18
#define PIN_CONTROL 1ULL<<19
#define INT_ENABLE	1ULL<<20
#define ANY_THREAD	1ULL<<21
#define ENABLE		1ULL<<22
#define INVERT		1ULL<<23
#define CMASK(val)  (u64)(val<<24)

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

/**
 * Intel Predefined Events
 */
enum perf_hw_id {
	UNHALTED_CYCLES = 0,
	INSTRUCTIONS_RETIRED = 1,
	UNHALTED_REF_CYCLES = 2,
	LLC_REFERENCES = 3,
	LLC_MISSES = 4,
	BRANCH_INSTRUCTIONS_RETIRED = 5,
	BRANCH_MISSES_RETIRED = 6,

	PERF_COUNT_MAX,
};
static u64 pmu_predefined_eventmap[PERF_COUNT_MAX] =
{
	[UNHALTED_CYCLES]		= 0x003c,
	[INSTRUCTIONS_RETIRED]	= 0x00c0,
	[UNHALTED_REF_CYCLES]	= 0x013c,
	[LLC_REFERENCES]		= 0x4f2e,
	[LLC_MISSES]			= 0x412e,
	[BRANCH_INSTRUCTIONS_RETIRED]	= 0x00c4,
	[BRANCH_MISSES_RETIRED]			= 0x00c5,
};

/**
 * PMU Parameters From CPUID
 */
u32  PERF_VERSION;
u32  PC_PER_CPU;
u32  PC_BIT_WIDTH;
u32  LEN_EBX_TOENUM;
u32  PRE_EVENT_MASK;

/**
 * MODULE GLOBAL VARIABLES
 */
int  PMU_LATENCY;
u64  PMU_PMC0_INIT_VALUE;
u64  TSC1, TSC2, TSC3;
u64  CPU_BASE_FREQUENCY;
char CPU_BRAND[48];
DEFINE_PER_CPU(int, PMU_EVENT_COUNT);


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
 */
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
 */
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
	printk(KERN_INFO "PMU %s\n", CPU_BRAND);
	printk(KERN_INFO "PMU Architectual PerfMon Version ID: %u\n", PERF_VERSION);
	printk(KERN_INFO "PMU General-purpose perf counter per cpu: %u\n", PC_PER_CPU);
	printk(KERN_INFO "PMU Bit width of general perf counter: %u\n", PC_BIT_WIDTH);
	printk(KERN_INFO "PMU Pre-defined events not avaliable if 1: %x\n", PRE_EVENT_MASK);
}


//#################################################
// PMU PART
//#################################################

/**
 * struct remote_function_call - rfc
 * @p:    the task to attach
 * @func: the function to be called
 * @info: the function call argument
 * @ret:  the function return value
 *
 */
struct remote_function_call {
	struct	task_struct *p;
	void	(*func)(void *info);
	void	*info;
	int 	ret;
};

/**
 * A wrapper for remote cpu function call.
 */
static void
remote_function(void *info)
{
	struct remote_function_call *rfc = info;
	struct task_struct *p = rfc->p;

	// Do Nothing. For future use...
	if (p) {
		rfc->ret = -EAGAIN;
	}
	
	rfc->ret = tfc->func(info)
}


/**
 * pmu_cpu_function_call - call a function on the cpu
 * @cpu :	the cpu to run
 * @func:	the function to be called
 * @info:	the function call argument
 *
 * Calls the function @func on the remote cpu.
 * Wait until the function finish.
 *
 * returns: @func return value or -ENXIO when the cpu is offline
 */
static int
pmu_cpu_function_call(int cpu, int (*func)(void *info), void *info)
{
	struct remote_function_call data = {
		.p	= NULL,
		.func	= func,
		.info	= info,
		.ret	= -ENXIO, // No such CPU
	};
	smp_call_function_single(cpu, remote_function, &data, 1);
	return data.ret;
}

static void
pmu_test(void *info)
{
	printk(KERN_INFO "PMU this is cpu %d\n", smp_processor_id());
}


static void
pmu_clear_msrs(void)
{
	pmu_wrmsr(MSR_IA32_PMC0, 0x0);
	pmu_wrmsr(MSR_IA32_PERFEVTSEL0, 0x0);
	pmu_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0x0);
	pmu_wrmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, 0x0);
}

static inline void
pmu_enable_counting(void)
{
	pmu_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 1);
}
static inline void
pmu_disable_counting(void)
{
	pmu_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0);
}

static inline void
pmu_clear_ovf(void)
{
	pmu_wrmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, 1);
}

static void
pmu_enable_predefined_event(int event, u64 init_val)
{
	pmu_clear_ovf();
	pmu_wrmsr(MSR_IA32_PMC0, init_val);
	pmu_wrmsr(MSR_IA32_PERFEVTSEL0,
				pmu_predefined_eventmap[event]
				| USR_MODE
				| INT_ENABLE
				| ENABLE );
}

//#################################################
// PMU NMI PART
//#################################################
static void
pmu_lapic_init(void)
{
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}

int pmu_nmi_handler(unsigned int type, struct pt_regs *regs)
{
	u64 delta, tmsr, tmsr1, tmsr2, tmsr3;
	int idx, per_cpu_count;
	
	/* Get TimeStamp First! */
	TSC2 = pmu_rdtsc();
	
	tmsr = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_STATUS);
	if (!(tmsr & 0x1))	// PMC0 isn't Overflowed, dont handle
		return NMI_DONE;

	/*
	 * Some chipsets need to unmask the LVTPC in a particular spot
	 * inside the nmi handler.  As a result, the unmasking was pushed
	 * into all the nmi handlers.
	 *
	 * This generic handler doesn't seem to have any issues where the
	 * unmasking occurs so it was left at the top.
	 */
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	
	delta = TSC2 - TSC1;
	TSC1 = TSC2;
	printk(KERN_INFO "PMU NMI: Overflow Catched! TSC2=%llu, period=%llu\n", TSC2, delta);
	
	tmsr1 = pmu_rdmsr(MSR_IA32_PMC0);
	tmsr2 = pmu_rdmsr(MSR_IA32_PERFEVTSEL0);
	printk(KERN_INFO "PMU NMI: PMC0=%llx PERFEVTSEL0=%llx\n", tmsr1, tmsr2);
	tmsr1 = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_CTRL);
	tmsr2 = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_STATUS);
	tmsr3 = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL);
	printk(KERN_INFO "PMU NMI: G_CTRL=%llx G_STATUS=%llx OVF_CTRL=%llx\n", tmsr1, tmsr2, tmsr3);

	/*
	 * Start Counting Again.
	 */
	pmu_clear_msrs();
	pmu_enable_predefined_event(LLC_REFERENCES, PMU_PMC0_INIT_VALUE);
	pmu_enable_counting();

/*
	for (idx = 0; idx < PMU_LATENCY; idx++)
		asm("nop");
*/

	per_cpu_count = ++get_cpu_var(PMU_EVENT_COUNT);
	put_cpu_var(PMU_EVENT_COUNT);
	if (per_cpu_count == 20){
		//FIXME Each CPU
		pmu_clear_msrs();//stop
	}

	return NMI_HANDLED;
}

void pmu_main(void)
{
	u64 tmsr1, tmsr2, tmsr3;

	/*
	 * The LATENCY is hard to evaluate. 
	 * The PMU_PMC0_INIT_VALUE is also tentative.
	 */
	PMU_EVENT_COUNT = 0;
	PMU_LATENCY = 0;
	PMU_PMC0_INIT_VALUE = -9999;
	
	/*
	 * Check CPU status and PMU parameters. Pay attention to the status of
	 * pre-defined performance events. Not all Intel CPUs support all 7 events.
	 * For us, LLC_MISSES matters only.
	 */
	cpu_general_info();
	cpu_print_info();

	/*
	 * We should AVOID walking kernel code path as much as possiable.
	 * Flag [NMI_FLAG_FIRST] will tell kernel to put our pmu_nmi_hander
	 * to the head of nmiaction list. Therefore, whenever kernel receives
	 * NMI interrupt, our pmu_nmi_handler will be called first. 
	 * However, the impact to kernel remains unknown. ;)
	 */
	pmu_lapic_init();
	register_nmi_handler(NMI_LOCAL, pmu_nmi_handler, NMI_FLAG_FIRST, "PMU_NMI_HANDLER");

	/*
	 * Clear various MSRs, set initial value of PMC0.
	 * Enable LLC_MISSES event, enable interrupt when overflow.
	 * Enbale counting on MSR_CORE_PERF_GLOBAL_CTRL.
	 * And, TSC1 changes in pmu_nmi_handler. ;)
	 */
	pmu_clear_msrs();
	pmu_enable_predefined_event(LLC_MISSES, PMU_PMC0_INIT_VALUE);
	pmu_enable_counting();
	
	TSC1 = pmu_rdtsc();
	printk(KERN_INFO "PMU Event Start Counting TSC: %llu\n", TSC1);
	
	/*
	 * We have set USR_MODE in MSR_IA32_PERFEVTSEL0,
	 * so the performance event counter will count User-Level only.
	 * Don't worry about the latency this code can bring. ;)
	 */
	tmsr1 = pmu_rdmsr(MSR_IA32_PMC0);
	tmsr2 = pmu_rdmsr(MSR_IA32_PERFEVTSEL0);
	printk(KERN_INFO "PMU Event Start Counting PMC0=%lld PERFEVTSEL0=%llx\n", tmsr1|(0xFF<<48), tmsr2);
	tmsr1 = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_CTRL);
	tmsr2 = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_STATUS);
	tmsr3 = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL);
	printk(KERN_INFO "PMU Event Start Counting G_CTRL=%llx G_STATUS=%llx OVF_CTRL=%llx\n", tmsr1, tmsr2, tmsr3);
}


//#################################################
// MODULE PART
//#################################################

static int
pmu_init(void)
{
	printk(KERN_INFO "PMU module init.\n");
	printk(KERN_INFO "PMU online cpus: %d.\n", num_online_cpus());
	//pmu_main();
	
	for_each_online_cpu(cpu) {
		pmu_cpu_function_call(cpu, pmu_test, NULL);
	}
	return 0;
}

static void
pmu_exit(void)
{
	int cpu;
	for_each_online_cpu(cpu) {
		printk(KERN_INFO "PMU cpu %d, count=%d\n", cpu, per_cpu(PMU_EVENT_COUNT, cpu));
	}

	unregister_nmi_handler(NMI_LOCAL, "PMU_NMI_HANDLER");
	
	printk(KERN_INFO "PMU module exit.\n");
}

module_init(pmu_init);
module_exit(pmu_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shan Yizhou");
