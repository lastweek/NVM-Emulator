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
#define EVTMASK(val) val
#define UMASK(val)  (val<<8)
#define USR_MODE	1ULL<<16
#define OS_MODE 	1ULL<<17
#define EDGE_DETECT	1ULL<<18
#define PIN_CONTROL 1ULL<<19
#define INT_ENABLE	1ULL<<20
#define ENABLE		1ULL<<22
#define INVERT		1ULL<<23
#define CMASK(val)  (val<<24)

/* UMASK and EVTMASK for Pre-defined events */
#define INST_RETIRED_UMASK	 0x00
#define INST_RETIRED_EVTMASK 0xC0
#define LLC_REFE_UMASK		0x4F
#define LLC_REFE_EVTMASK	0x2E
#define LLC_MISS_UMASK		0x41
#define LLC_MISS_EVTMASK	0x2E

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef unsigned long long u64;

int  pmc_init(void);
int  pmc_exit(void);
void pmc_main(void);
void pmc_err(char *fmt);

static u64  pmc_rdtsc(void);
static void pmc_cpuid(u32 *eax, u32 *ebx, u32 *ecx, u32 *edx);
static u64  pmc_rdmsr(u32 addr);
static void pmc_wrmsr(u32 addr, u64 value);

static void cpu_brand_frequency(void);
static void cpu_facility_test(void);
static void cpu_perf_info(void);
static void cpu_general_info(void);
static void cpu_print_info(void);

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
	u32 eax, ebx, ecx, edx;
	
	eax = 0x01;
	pmc_cpuid(&eax, &ebx, &ecx, &edx);

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
	pmc_wrmsr(MSR_CORE_PERF_GLOBAL_STATUS, 0x0);
	pmc_wrmsr(MSR_CORE_PERF_GLOBAL_OVF_CTRL, 0x0);
}

/* Set bit 0 in MSR_CORE_PERF_GLOBAL_CTRL to 1 */
static void
pmc_start_counting(void)
{
	pmc_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 1);
}

/* Set bit 0 in MSR_CORE_PERF_GLOBAL_CTRL to 0 */
static void
pmc_end_counting(void)
{
	pmc_wrmsr(MSR_CORE_PERF_GLOBAL_CTRL, 0);
}


static void
pmc_enable_LLC_MISS(void)
{
	u64 counterVal = (u64)(-999);
	u64 tmsr;
	
	pmc_wrmsr(MSR_IA32_PMC0, counterVal);/* set initial value */
	pmc_wrmsr(MSR_IA32_PERFEVTSEL0,		 /* enable pmc */
			  (u64)EVTMASK(INST_RETIRED_EVTMASK) |
			  	   UMASK(INST_RETIRED_UMASK) |
				   OS_MODE |
				   ENABLE
			  );
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

void pmc_main(void)
{
	u64 tsc1, tsc2, tmsr;
	int a[10000];
	int i;

	cpu_general_info();
	cpu_print_info();
	
	tsc1 = pmc_rdtsc();
	pmc_enable_LLC_MISS();
	pmc_start_counting();
	for (i = 0; i < 10000; i++)
			a[i] = i*100;
	pmc_end_counting();
	tsc2 = pmc_rdtsc();

	printk(KERN_INFO "PMC tsc1=%llu tsc2=%llu, period = %llu \n", tsc1,tsc2,(tsc2-tsc1));
	tmsr = pmc_rdmsr(MSR_IA32_PMC0);
	printk(KERN_INFO "PMC MSR=%X pmc0=%llu %X %X \n", MSR_IA32_PMC0, tmsr, (u32)tmsr>>32, tmsr);
	tmsr = pmc_rdmsr(MSR_IA32_PERFEVTSEL0);
	printk(KERN_INFO "PMC MSR=%X perf0=%llu %X %X \n", MSR_IA32_PERFEVTSEL0,tmsr, (u32)tmsr>>32, tmsr);
	tmsr = pmc_rdmsr(MSR_IA32_PERF_STATUS);
	printk(KERN_INFO "PMC MSR=%X val=%llu %X %X \n", MSR_IA32_PERF_STATUS, (u32)tmsr>>32, tmsr);
}

module_init(pmc_init);
module_exit(pmc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PerfMon module");
MODULE_AUTHOR("Yizhou Shan");
