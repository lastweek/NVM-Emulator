/*
 *	2015/05/08 Created by Shan Yizhou.
 *	pmu.c: Intel x86 PerfMon sample module.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/rculist.h>

#include <asm/nmi.h>
#include <asm/msr.h>

#define MSR_IA32_MISC_ENABLE	0x000001A0
#define MSR_IA32_MISC_PERFMON_ENABLE 	1ULL<<7

#define MSR_IA32_PMC0			0x000000C1
#define MSR_IA32_PERFEVTSEL0	0x00000186

#define MSR_CORE_PERF_GLOBAL_STATUS		0x0000038E
#define MSR_CORE_PERF_GLOBAL_CTRL		0x0000038F
#define MSR_CORE_PERF_GLOBAL_OVF_CTRL	0x00000390

/* Bit field of MSR_IA32_PERFEVTSEL */
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

/* Intel Predefined Events */
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

/* Global Variables Derived From CPUID */
u32  eax_arch_perf_version;
u32  eax_nr_of_perf_counter_per_cpu;
u32  eax_bit_width_of_perf_counter;
u32  eax_len_of_ebx_to_enumerate;
u32  ebx_predefined_event_mask;
u32  edx_nr_of_fixed_func_perf_counter;
u32  edx_bit_width_of_fixed_func_perf_counter;

u64 TSC1, TSC2, TSC3;
u64  CPU_BASE_FREQUENCY;
char CPU_BRAND[48];
u64 PMU_PMC0_INIT_VALUE;
int PMU_LATENCY;
int PMU_EVENT_COUNT;

void pmu_main(void);

//#################################################
// MODULE PART
//#################################################
static int pmu_init(void)
{
	printk(KERN_INFO "PMU module init.\n");
	pmu_main();
	return 0;
}

static void pmu_exit(void)
{
	unregister_nmi_handler(NMI_LOCAL, "PMU_NMI_HANDLER");
	printk(KERN_INFO "PMU Event occurs %d times.\n", PMU_EVENT_COUNT);
	printk(KERN_INFO "PMU module exit.\n");
}

static void
pmu_err(char *msg)
{
	printk(KERN_INFO "PMU Error: %s", msg);
	pmu_exit();//Dont work
}

//#################################################
// CPU PART
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

/*
 * See whether CPU support facilities we need.
 */
static void
cpu_facility_test(void)
{
	u64 msr;
	u32 eax, ebx, ecx, edx;
	
	/* FIXME Do more things... */
	eax = 0x01;
	pmu_cpuid(&eax, &ebx, &ecx, &edx);

	msr = pmu_rdmsr(MSR_IA32_MISC_ENABLE);
	if (!(msr & MSR_IA32_MISC_PERFMON_ENABLE)) {
		pmu_err("MSR_IA32_MISC_PERFMON_ENABLE = 0, PerfMon Disabled");
	}
}

/*
 * Get CPU brand and frequency.
 * See Intel Developer-Manual 2 [CPUID] for detail.
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
		pmu_err("CPUID Extended Function Not Supported. Fail to get CPU Brand");
	
	s = CPU_BRAND;
	for (i = 0; i < 3; i++) {
		eax = 0x80000002U + i;
		pmu_cpuid(&eax, &ebx, &ecx, &edx);
		memcpy(s, &eax, 4); s += 4;
		memcpy(s, &ebx, 4); s += 4;
		memcpy(s, &ecx, 4); s += 4;
		memcpy(s, &edx, 4); s += 4;
	}
	
	/* FIXME Extract frequency from brand string. */
	/* A lazy guy coded the frequency manually... */
	CPU_BASE_FREQUENCY = 2270000000ULL;
}


/*
 * Get Host CPU Performance Monitoring Information.
 */
static void
cpu_perf_info(void)
{
	u32 eax, ebx, ecx, edx;
	
	eax = 0x0A;
	pmu_cpuid(&eax, &ebx, &ecx, &edx);
	
	ebx_predefined_event_mask		=  ebx;
	eax_arch_perf_version			=  eax & 0xFFU;
	eax_nr_of_perf_counter_per_cpu	= (eax & 0xFF00U) >> 8;
	eax_bit_width_of_perf_counter	= (eax & 0xFF0000U) >> 16;
	eax_len_of_ebx_to_enumerate 	= (eax & 0xFF000000U) >> 24;
	edx_nr_of_fixed_func_perf_counter		 =  edx & 0x1FU;
	edx_bit_width_of_fixed_func_perf_counter = (edx & 0x1FE0U) >> 5;
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
	printk(KERN_INFO "PMU Architectual PerfMon Version ID: %u\n", eax_arch_perf_version);
	printk(KERN_INFO "PMU General-purpose perf counter per cpu: %u\n", eax_nr_of_perf_counter_per_cpu);
	printk(KERN_INFO "PMU Bit width of general perf counter: %u\n", eax_bit_width_of_perf_counter);
	printk(KERN_INFO "PMU Pre-defined events not avaliable if 1: %x\n", ebx_predefined_event_mask);
}


//#################################################
// PMU PART
//#################################################
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
// NMI PART
//#################################################
static void
pmu_lapic_init(void)
{
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}

int pmu_nmi_handler(unsigned int type, struct pt_regs *regs)
{
	u64 delta, tmsr, tmsr1, tmsr2, tmsr3;
	int idx;
	
	/* Get TimeStamp First! */
	TSC2 = pmu_rdtsc();
	
	if (type != NMI_LOCAL)
		return NMI_DONE;

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

	for (idx = 0; idx < PMU_LATENCY; idx++)
		asm("nop");

	PMU_EVENT_COUNT++;
	if (PMU_EVENT_COUNT == 20){
		pmu_clear_msrs();//stop
	}

	return 1;
}

void pmu_main(void)
{
	u64 tmsr1, tmsr2, tmsr3;

	/*
	 * FIXME
	 * The LATENCY is hard to evaluate. A well-designed algorithm needed.
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
	 * We should avoid walking kernel code path as much as possiable.
	 * The flag [NMI_FLAG_FIRST] will tell kernel to put our pmu_nmi_hander
	 * to the head of nmiaction list. Therefore, whenever kernel receives
	 * NMI interrupt, our pmu_nmi_handler will be called first. 
	 * However, the impact to kernel remains unknown. ;)
	 */
	pmu_lapic_init();
	register_nmi_handler(NMI_LOCAL, pmu_nmi_handler, NMI_FLAG_FIRST, "PMU_NMI_HANDLER");

	/*
	 * Clear various MSRs, set initial value of PMC0.
	 * Enable LLC_MISSES event, enable interrupt when overflow.
	 * Enbale counting with MSR_CORE_PERF_GLOBAL_CTRL.
	 * TSC1 can change in pmu_nmi_handler. ;)
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

module_init(pmu_init);
module_exit(pmu_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PerfMon NMI Hander");
MODULE_AUTHOR("Shan Yizhou");
