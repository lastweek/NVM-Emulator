/*
 *	2015/05/08 Created by Shan Yizhou.
 *	pmu.c: Intel x86 PerfMon sample module.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/nmi.h>

/* Architecture MSR Address */
#define MSR_IA32_TSC			0x00000010
#define MSR_IA32_APICBASE		0x0000001B
#define MSR_IA32_MISC_ENABLE	0x000001A0
#define MSR_IA32_MISC_PERFMON_ENABLE 	1ULL<<7

#define MSR_IA32_PMC0			0x000000C1
#define MSR_IA32_PMC1			0x000000C2
#define MSR_IA32_PMC2			0x000000C3
#define MSR_IA32_PMC3			0x000000C4
#define MSR_IA32_PERFEVTSEL0	0x00000186
#define MSR_IA32_PERFEVTSEL1	0x00000187
#define MSR_IA32_PERFEVTSEL2	0x00000188
#define MSR_IA32_PERFEVTSEL3	0x00000189

#define MSR_IA32_PERF_STATUS	0x00000198
#define MSR_IA32_PERF_CTL		0x00000199

/* Core microarchitecture specific MSR */
#define MSR_CORE_PERF_FIXED_CTR0		0x00000309
#define MSR_CORE_PERF_FIXED_CTR1		0x0000030A
#define MSR_CORE_PERF_FIXED_CTR2		0x0000030B
#define MSR_CORE_PERF_FIXED_CTR_CTRL	0x0000038D
#define MSR_CORE_PERF_GLOBAL_STATUS		0x0000038E
#define MSR_CORE_PERF_GLOBAL_CTRL		0x0000038F
#define MSR_CORE_PERF_GLOBAL_OVF_CTRL	0x00000390

/* Bit field of MSR_IA32_PERFEVTSEL */
#define USR_MODE	1ULL<<16
#define OS_MODE 	1ULL<<17
#define EDGE_DETECT	1ULL<<18
#define PIN_CONTROL 1ULL<<19
#define INT_ENABLE	1ULL<<20
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
u64  CPU_BASE_FREQUENCY;
char CPU_BRAND[48];

int LATENCY;
u64 TSC1, TSC2, TSC3;
u64 PMU_PMC0_INIT_VALUE;


void pmu_main(void);

int pmu_init(void)
{
	printk(KERN_INFO "PMU module init.\n");
	pmu_main();
}

int pmu_exit(void)
{
	printk(KERN_INFO "PMU module exit.\n");
}

void pmu_err(char *fmt)
{
	printk(KERN_INFO "PMU Error: %s", fmt);
	pmu_exit();
}

/* x86_64 Only */
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


/* Time Stamp Counter Addr: 0x10 in MSR */
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

static inline void
pmu_wrmsr(u32 addr, u64 value)
{
	asm volatile(
		"wrmsr"
		:
		:"c"(addr), "d"((u32)(value>>32)), "a"((u32)value)
	);
}

/* See whether CPU support the facilities we need. */
static void
cpu_facility_test(void)
{
	u64 tmsr;
	u32 eax, ebx, ecx, edx;
	
	eax = 0x01;
	pmu_cpuid(&eax, &ebx, &ecx, &edx);

	tmsr = pmu_rdmsr(MSR_IA32_MISC_ENABLE);
	if (!(tmsr & MSR_IA32_MISC_PERFMON_ENABLE)) {
		printk(KERN_INFO "PMU WARNING: MSR_IA32_MISC_PERFMON_ENABLE = 0 \n");
	}

}

/* Get CPU brand and frequency */
/* See Intel Developer-Manual 2 [CPUID] for detail. */
static void
cpu_brand_frequency(void)
{
	char *s;
	int i;
	u32 eax, ebx, ecx, edx;

	eax = 0x80000000;
	pmu_cpuid(&eax, &ebx, &ecx, &edx);

	if (eax < 0x80000004U)
		pmu_err("CPUID, Extended function Not supported.\n");
	
	s = CPU_BRAND;
	for (i = 0; i < 3; i++) {
		eax = 0x80000002U + i;
		pmu_cpuid(&eax, &ebx, &ecx, &edx);
		memcpy(s, &eax, 4); s += 4;
		memcpy(s, &ebx, 4); s += 4;
		memcpy(s, &ecx, 4); s += 4;
		memcpy(s, &edx, 4); s += 4;
	}
	
	/* Extract frequency from brand string. */
	/* A lazy guy write the freq manually... */
	CPU_BASE_FREQUENCY = 2000000000;
}


/* Get Host CPU Performance Monitoring Information */
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


/* Get and test Host CPU General Information */
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
	printk(KERN_INFO "PMU Architectual Performance Monitoring Version ID: %u\n", eax_arch_perf_version);
	printk(KERN_INFO "PMU Number of general-purpose perf counter per cpu: %u\n", eax_nr_of_perf_counter_per_cpu);
	printk(KERN_INFO "PMU Bit width of general-purpose, perf counter reg: %u\n", eax_bit_width_of_perf_counter);
	printk(KERN_INFO "PMU Length of [EBX] bit vector to enumerate events: %u\n", eax_len_of_ebx_to_enumerate);
	printk(KERN_INFO "PMU EBX event not avaliable if 1: %x\n", ebx_predefined_event_mask);
	
	if (eax_arch_perf_version > 1) {
		printk(KERN_INFO "PMU Number of fixed-func perf counters:    %u\n", edx_nr_of_fixed_func_perf_counter);
		printk(KERN_INFO "PMU Bit width of fixed-func perf counters: %u\n", edx_bit_width_of_fixed_func_perf_counter);
	}
}

static void
pmu_clear_msrs(void)
{
	pmu_wrmsr(MSR_IA32_PMC0, 0x0);
	pmu_wrmsr(MSR_IA32_PMC1, 0x0);
	pmu_wrmsr(MSR_IA32_PERFEVTSEL0, 0x0);
	pmu_wrmsr(MSR_IA32_PERFEVTSEL1, 0x0);
	if (eax_arch_perf_version > 1) {
		pmu_wrmsr(MSR_IA32_PMC2, 0x0);
		pmu_wrmsr(MSR_IA32_PMC3, 0x0);
		pmu_wrmsr(MSR_IA32_PERFEVTSEL2, 0x0);
		pmu_wrmsr(MSR_IA32_PERFEVTSEL3, 0x0);
		pmu_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0x0);
		pmu_wrmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, 0x0);
	}
}

/* Enable PMC0 */
static inline void
pmu_enable_counting(void)
{
	if (eax_arch_perf_version > 1)
		pmu_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 1);
}
/* Disable PMC0 */
static inline void
pmu_disable_counting(void)
{
	if (eax_arch_perf_version > 1)
		pmu_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0);
}
/* Clear ovf_pmc0 */
static inline void
pmu_clear_ovf(void)
{
	if (eax_arch_perf_version > 1)
		pmu_wrmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, 1);
}

/* Pre-Defined event enable routine */
static void
pmu_enable_predefined_event(int event, u64 init_val)
{
	pmu_clear_ovf();
	pmu_wrmsr(MSR_IA32_PMC0, init_val);
	pmu_wrmsr(MSR_IA32_PERFEVTSEL0,
				pmu_predefined_eventmap[event]
				| OS_MODE
				| ENABLE );
}

/* General-purpose event enable routine */
static void
pmu_enable_general_event(u64 event, u64 umask, u64 init_val)
{
	pmu_clear_ovf();
	pmu_wrmsr(MSR_IA32_PMC0, init_val);
	pmu_wrmsr(MSR_IA32_PERFEVTSEL0,
				event
				| umask
				| OS_MODE
				| ENABLE );
}

/* Always use NMI for PMU */
static void
pmu_lapic_init(void)
{
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}

/* FIXME */
static int
pmu_nmi_handler(unsigned int cmd, struct pt_regs *regs)
{
	u64 delta, tmsr;
	
	pmu_clear_ovf();
	pmu_wrmsr(MSR_IA32_PMC0, PMU_PMC0_INIT_VALUE);
	pmu_enable_counting();

	TSC2 = pmu_rdtsc();
	delta = TSC2 - TSC1;
	printk(KERN_INFO "PMU Event Overflowed TSC: %lld", TSC2);
	
	/* How to measure the cycles accurate??? */
	for (int i = 0; i < LATENCY; i++)
		asm("nop");

	return NMI_HANDLED;
}

void pmu_main(void)
{
	u64 tsc1, tsc2, tmsr;
	int i;
	
	LATENCY = 100;
	PMU_PMC0_INIT_VALUE = -999;
	
	/* Check CPU status */
	cpu_general_info();
	cpu_print_info();
	pmu_clear_msrs();

	/* Register NMI handler */
	pmu_lapic_init();
	register_nmi_handler(NMI_LOCAL, pmu_nmi_handler, 0, "PMU");
	
	/* Start Counting */
	pmu_enable_predefined_event(LLC_REFERENCES, PMU_PMC0_INIT_VALUE);
	pmu_enable_counting();
	TSC1 = pmu_rdtsc();
	printk(KERN_INFO "PMU Event Start Counting TimeStamp: %lld\n", TSC1);
	
	/* Some fake code */
	for (i = 0; i < 10000; i++)
			a[i] = i*100;
	pmu_disable_counting();
	
	tsc2 = pmu_rdtsc();
	printk(KERN_INFO "PMU Event End TSC: %lld", tsc2);
	tmsr = pmu_rdmsr(MSR_IA32_PMC0);
	printk(KERN_INFO "PMU Event End PMC0: 0x%llx", tmsr);
	tmsr = pmu_rdmsr(MSR_IA32_PERFEVTSEL0);
	printk(KERN_INFO "PMU MSR PERFEVTSEL0: 0x%llx", tmsr);
	
	/*
	pmu_wrmsr(MSR_IA32_MISC_ENABLE, (1ULL<<16));
	pmu_wrmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, 0x3);
	pmu_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0x3);
	
	tmsr = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_STATUS);
	printk(KERN_INFO "PMU MSR=%x status=%llx \n", MSR_CORE_PERF_GLOBAL_STATUS, tmsr);

	tmsr = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL);
	printk(KERN_INFO "PMU MSR=%X ovf_ctrl=%llx \n", MSR_CORE_PERF_GLOBAL_OVF_CTRL, tmsr);
	
	tmsr = pmu_rdmsr(MSR_CORE_PERF_GLOBAL_CTRL);
	printk(KERN_INFO "PMU MSR=%x ctrl=%llx \n", MSR_CORE_PERF_GLOBAL_CTRL, tmsr);

	tmsr = pmu_rdmsr(MSR_IA32_MISC_ENABLE);
	printk(KERN_INFO "PMU MSR=%x MISC=%llx \n", MSR_IA32_MISC_ENABLE, tmsr);
	*/
}

module_init(pmu_init);
module_exit(pmu_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PerfMon module");
MODULE_AUTHOR("Yizhou Shan");
