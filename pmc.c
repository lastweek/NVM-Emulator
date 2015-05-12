/*
 *	2015/05/08 Created by Shan Yizhou.
 *	pmc.c: Intel x86 PerfMon sample module.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>

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
static u64 pmc_predefined_eventmap[PERF_COUNT_MAX] =
{
	[UNHALTED_CYCLES]		= 0x003c,
	[INSTRUCTIONS_RETIRED]	= 0x00c0,
	[UNHALTED_REF_CYCLES]	= 0x013c,
	[LLC_REFERENCES]		= 0x4f2e,
	[LLC_MISSES]			= 0x412e,
	[BRANCH_INSTRUCTIONS_RETIRED]	= 0x00c4,
	[BRANCH_MISSES_RETIRED]			= 0x00c5,
};


/*
Description from Intel Developer manual 2a [CPUID]:
EAX Bits 07 - 00: Version ID of architectural performance monitoring
	Bits 15 - 08: Number of general-purpose performance monitoring counter per logical processor
	Bits 23 - 16: Bit width of general-purpose, performance monitoring counter
	Bits 31 - 24: Length of EBX bit vector to enumerate architectural performance monitoring events
EBX Bit 00: Core cycle event not available if 1
	Bit 01: Instruction retired event not available if 1
	Bit 02: Reference cycles event not available if 1
	Bit 03: Last-level cache reference event not available if 1
	Bit 04: Last-level cache misses event not available if 1
	Bit 05: Branch instruction retired event not available if 1
	Bit 06: Branch mispredict retired event not available if 1
	Bits 31- 07: Reserved = 0
ECX Reserved = 0
EDX Bits 04 - 00: Number of fixed-function performance counters (if Version ID > 1)
	Bits 12 - 05: Bit width of fixed-function performance counters (if Version ID > 1)
	Reserved = 0
*/
u32  eax_arch_perf_version;
u32  eax_nr_of_perf_counter_per_cpu;
u32  eax_bit_width_of_perf_counter;
u32  eax_len_of_ebx_to_enumerate;
u32  ebx_predefined_event_mask;
u32  edx_nr_of_fixed_func_perf_counter;
u32  edx_bit_width_of_fixed_func_perf_counter;
u64  CPU_BASE_FREQUENCY;
char CPU_BRAND[48];

void pmc_main(void);
int
pmc_init(void)
{
	printk(KERN_INFO "PMC module init.\n");
	pmc_main();
}

int
pmc_exit(void)
{
	printk(KERN_INFO "PMC module exit.\n");
}

void
pmc_err(char *fmt)
{
	printk(KERN_INFO "PMC Error: %s", fmt);
	pmc_exit();
}

/* x86_64 Only */
static void
pmc_cpuid(u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
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
pmc_rdtsc(void)
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
pmc_rdmsr(u32 addr)
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
pmc_wrmsr(u32 addr, u64 value)
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
	pmc_cpuid(&eax, &ebx, &ecx, &edx);

	tmsr = pmc_rdmsr(MSR_IA32_MISC_ENABLE);
	if (!(tmsr & MSR_IA32_MISC_PERFMON_ENABLE)) {
		printk(KERN_INFO "PMC WARNING: MSR_IA32_MISC_PERFMON_ENABLE = 0 \n");
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
	pmc_cpuid(&eax, &ebx, &ecx, &edx);

	if (eax < 0x80000004U)
		pmc_err("CPUID, Extended function Not supported.\n");
	
	s = CPU_BRAND;
	for (i = 0; i < 3; i++) {
		eax = 0x80000002U + i;
		pmc_cpuid(&eax, &ebx, &ecx, &edx);
		memcpy(s, &eax, 4); s += 4;
		memcpy(s, &ebx, 4); s += 4;
		memcpy(s, &ecx, 4); s += 4;
		memcpy(s, &edx, 4); s += 4;
	}
	
	/* Extract frequency from brand string. */
	CPU_BASE_FREQUENCY = 2000000000;
}


/* Get Host CPU Performance Monitoring Information */
static void
cpu_perf_info(void)
{
	u32 eax, ebx, ecx, edx;
	
	eax = 0x0A;
	pmc_cpuid(&eax, &ebx, &ecx, &edx);
	
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
	printk(KERN_INFO "PMC %s\n", CPU_BRAND);
	printk(KERN_INFO "PMC Architectual Performance Monitoring Version ID: %u\n", eax_arch_perf_version);
	printk(KERN_INFO "PMC Number of general-purpose perf counter per cpu: %u\n", eax_nr_of_perf_counter_per_cpu);
	printk(KERN_INFO "PMC Bit width of general-purpose, perf counter reg: %u\n", eax_bit_width_of_perf_counter);
	printk(KERN_INFO "PMC Length of [EBX] bit vector to enumerate events: %u\n", eax_len_of_ebx_to_enumerate);
	printk(KERN_INFO "PMC EBX event not avaliable if 1: %x\n", ebx_predefined_event_mask);
	
	if (eax_arch_perf_version > 1) {
		printk(KERN_INFO "PMC Number of fixed-func perf counters:    %u\n", edx_nr_of_fixed_func_perf_counter);
		printk(KERN_INFO "PMC Bit width of fixed-func perf counters: %u\n", edx_bit_width_of_fixed_func_perf_counter);
	}
}

static void
pmc_clear_msrs(void)
{
	pmc_wrmsr(MSR_IA32_PMC0, 0x0);
	pmc_wrmsr(MSR_IA32_PMC1, 0x0);
	pmc_wrmsr(MSR_IA32_PMC2, 0x0);
	pmc_wrmsr(MSR_IA32_PMC3, 0x0);
	
	pmc_wrmsr(MSR_IA32_PERFEVTSEL0, 0x0);
	pmc_wrmsr(MSR_IA32_PERFEVTSEL1, 0x0);
	pmc_wrmsr(MSR_IA32_PERFEVTSEL2, 0x0);
	pmc_wrmsr(MSR_IA32_PERFEVTSEL3, 0x0);

	pmc_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0x0);
	pmc_wrmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, 0x0);
}

/* Enable pmc0 */
static inline void
pmc_enable_counting(void)
{
	if (eax_arch_perf_version > 1)
		pmc_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 1);
}
/* Disable pmc0 */
static inline void
pmc_disable_counting(void)
{
	if (eax_arch_perf_version > 1)
		pmc_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0);
}
/* Clear ovf_pmc0 */
static inline void
pmc_clear_ovf(void)
{
	if (eax_arch_perf_version > 1)
		pmc_wrmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, 1);
}

static void
pmc_enable_predefined_event(int event, u64 init_val)
{
	pmc_clear_ovf();
	pmc_wrmsr(MSR_IA32_PMC0, init_val);
	pmc_wrmsr(MSR_IA32_PERFEVTSEL0,
				pmc_predefined_eventmap[event]
				| OS_MODE
				| ENABLE );
}

static void
pmc_enable_general_event(u64 event, u64 umask, u64 init_val)
{
	pmc_clear_ovf();
	pmc_wrmsr(MSR_IA32_PMC0, init_val);
	pmc_wrmsr(MSR_IA32_PERFEVTSEL0,
				event
				| umask
				| OS_MODE
				| ENABLE );
}

/*
static void
pmc_init_handler(void)
{
	apic_write(APIC_LVTPC, APIC_DM_NMI);
	register_die_notifier(&..);
	register_nmi_handler(...);
}
*/

void
pmc_main(void)
{
	u64 tsc1, tsc2, tmsr;
	int a[10000];
	int i;

	/* Initial Step */
	cpu_general_info();
	cpu_print_info();
	pmc_clear_msrs();
	
	/* Enable Event Step */
	tsc1 = pmc_rdtsc();
	//pmc_enable_general_event((u64)0x3, 0, 0xF);
	pmc_enable_predefined_event(LLC_REFERENCES, -999);
	pmc_enable_counting();
	for (i = 0; i < 10000; i++)
			a[i] = i*100;
	pmc_disable_counting();
	tsc2 = pmc_rdtsc();
	tmsr = pmc_rdmsr(MSR_IA32_PMC0);
	printk(KERN_INFO "PMC tsc1=%llu tsc2=%llu, time=%llu\n", tsc1, tsc2, (tsc2-tsc1));
	printk(KERN_INFO "PMC MSR=0x%x pmc0=%llu\n", MSR_IA32_PMC0, tmsr);
	tmsr = pmc_rdmsr(MSR_IA32_PERFEVTSEL0);
	printk(KERN_INFO "PMC MSR=0x%x perfevtsel0=0x%llx\n", MSR_IA32_PERFEVTSEL0, tmsr);
	
	/* Why write fail?????? version == 1 will fail*/
	pmc_wrmsr(MSR_IA32_MISC_ENABLE, (1ULL<<16));
	pmc_wrmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, 0x3);
	pmc_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0x3);
	
	tmsr = pmc_rdmsr(MSR_CORE_PERF_GLOBAL_STATUS);
	printk(KERN_INFO "PMC MSR=%x status=%llx \n", MSR_CORE_PERF_GLOBAL_STATUS, tmsr);

	tmsr = pmc_rdmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL);
	printk(KERN_INFO "PMC MSR=%X ovf_ctrl=%llx \n", MSR_CORE_PERF_GLOBAL_OVF_CTRL, tmsr);
	
	tmsr = pmc_rdmsr(MSR_CORE_PERF_GLOBAL_CTRL);
	printk(KERN_INFO "PMC MSR=%x ctrl=%llx \n", MSR_CORE_PERF_GLOBAL_CTRL, tmsr);

	tmsr = pmc_rdmsr(MSR_IA32_MISC_ENABLE);
	printk(KERN_INFO "PMC MSR=%x MISC=%llx \n", MSR_IA32_MISC_ENABLE, tmsr);
}

module_init(pmc_init);
module_exit(pmc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PerfMon module");
MODULE_AUTHOR("Yizhou Shan");
