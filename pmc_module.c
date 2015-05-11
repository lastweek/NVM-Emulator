#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef long long s64;

int  pmc_init(void);
int  pmc_exit(void);
void pmc_err(char *fmt);
void pmc_main(void);
void pmc_cpuid(u32 *eax, u32 *ebx, u32 *ecx, u32 *edx);
u64 pmc_rdtsc(void);
u64 pmc_rdmsr(u32 addr);
void cpu_brand_frequency(void);
void cpu_basic_info(void);
void cpu_perf_info(void);
void cpu_general_info(void);
void cpu_info_print(void);

/*
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

u32 eax_arch_perf_version;
u32 eax_nr_of_perf_counter_per_cpu;
u32 eax_bit_width_of_perf_counter;
u32 eax_len_of_ebx_to_enumerate;
u32 ebx_predefined_event_mask;
u32 edx_nr_of_fixed_func_perf_counter;
u32 edx_bit_width_of_fixed_func_perf_counter;
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
void
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
u64
pmc_rdtsc(void)
{
	unsigned int edx, eax;
	unsigned long long retval = 0;

	asm volatile(
		"rdtsc"
		:"=a"(eax), "=d"(edx)
	);

	retval = ((unsigned long long)(edx) << 32) | (unsigned long long)(eax);
	return retval;
}


/* Privilege 0 or Real-Mode */
u64
pmc_rdmsr(u32 addr)
{
	unsigned int edx, eax;
	unsigned long long retval = 0;
	
	asm volatile(
		"rdmsr"
		:"=a"(eax), "=d"(edx)
		:"c"(addr)
	);
	printk(KERN_INFO "PMC %i edx=%X eax=%X\n", addr, edx, eax);
	retval =((unsigned long long)(edx) << 32) | (unsigned long long)(eax);
	return retval;
}


/* See Intel Developer-Manual 2 [CPUID] */
void
cpu_brand_frequency(void)
{
	char *s;
	unsigned int i;
	unsigned int eax, ebx, ecx, edx;

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

	CPU_BASE_FREQUENCY = 2000000000;
	//CPU_FREQUENCY = 1995381060;
}

void
cpu_basic_info(void)
{
	unsigned int eax, ebx, ecx, edx;
	
	eax = 0x01;
	pmc_cpuid(&eax, &ebx, &ecx, &edx);
	
	/*FIXME Add code*/
}


/* Get Host CPU Performance Monitoring Information */
void
cpu_perf_info(void)
{
	unsigned int eax, ebx, ecx, edx;
	
	eax = 0x0A;
	pmc_cpuid(&eax, &ebx, &ecx, &edx);
	
	eax_arch_perf_version			= eax & 0xFFU;
	eax_nr_of_perf_counter_per_cpu	= (eax & 0xFF00U) >> 8;
	eax_bit_width_of_perf_counter	= (eax & 0xFF0000U) >> 16;
	eax_len_of_ebx_to_enumerate 	= (eax & 0xFF000000U) >> 24;

	ebx_predefined_event_mask		= ebx;

	edx_nr_of_fixed_func_perf_counter			= edx & 0x1FU;
	edx_bit_width_of_fixed_func_perf_counter	= (edx & 0x1FE0U) >> 5;
}
/* Get Host CPU General Information */
void
cpu_general_info(void)
{
	unsigned int eax, ebx, ecx, edx;
	
	/* Get Basic CPU info */
	cpu_basic_info();

	/* Get CPU Brand&Frequency */
	cpu_brand_frequency();
	
	/* Get CPU PM info */
	cpu_perf_info();
}

void
cpu_info_print(void)
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

void pmc_main(void)
{
	u64 ttsc,tmsr;
	int i;

	/* Init and print CPU general infomation */
	cpu_general_info();
	cpu_info_print();
	
	for (i = 0; i < 0x15; i++) {
		tmsr = pmc_rdmsr(i);
	}
}

module_init(pmc_init);
module_exit(pmc_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yizhou Shan");
