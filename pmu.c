#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <asm/msr.h>

#define __MSR_IA32_MISC_ENABLE		0x1A0
#define __MSR_IA32_MISC_PERFMON_ENABLE 	((1ULL)<<7)

/*
 * The scope of the uncore PMU relative MSRs is: Package.
 */

/* Nehalem Global Control Registers */
#define NHM_UNCORE_PERF_GLOBAL_CTRL	0x391	/* Read/Write */
#define NHM_UNCORE_PERF_GLOBAL_STATUS	0x392	/* Read-Only */
#define NHM_UNCORE_PERF_GLOBAL_OVF_CTRL	0x393	/* Write-Only */

/* Nehalem Performance Control Registers */
#define NHM_UNCORE_PMCO			0x3b0	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL0		0X3c0	/* Read/Write */

#define NHM_CONTROL_REG_BIT_WIDTH	64
#define NHM_COUNTER_REG_BIT_WIDTH	48

/* Control Bit In NHM_UNCORE_PERFEVTSEL */
#define NHM_PEREVTSEL_OCC_CTR_RST	((1ULL)<<17)
#define NHM_PEREVTSEL_EDGE_DETECT	((1ULL)<<18)
#define NHM_PEREVTSEL_PMI_ENABLE	((1ULL)<<20)
#define NHM_PEREVTSEL_COUNT_ENABLE	((1ULL)<<22)
#define NHM_PEREVTSEL_INVERT		((1ULL)<<23)

enum nhm_uncore_event_id {
	nhm_qhl_request_ioh_reads	=	1,
	nhm_qhl_request_ioh_writes	=	2,
	nhm_qhl_request_remote_reads	=	3,
	nhm_qhl_request_remote_writes	=	4,
	nhm_qhl_request_local_reads	=	5,
	nhm_qhl_request_local_writes	=	6,
	
	nhm_qmc_normal_reads_any	=	7,
	nhm_qmc_writes_full_any		=	8,
	nhm_qmc_writes_partial_any	=	9,

	NHM_UNCORE_EVENT_ID_MAX
};

static u64 nhm_uncore_event_map[NHM_UNCORE_EVENT_ID_MAX] = 
{
	/* Event = 0x20, UMASK = 0xxx */
	[nhm_qhl_request_ioh_reads]	=	0x0120,
	[nhm_qhl_request_ioh_writes]	=	0x0220,
	[nhm_qhl_request_remote_reads]	=	0x0420,
	[nhm_qhl_request_remote_writes]	=	0x0820,
	[nhm_qhl_request_local_reads]	=	0x1020,
	[nhm_qhl_request_local_writes]	=	0x2020,
	
	/* Event = 0x2c, UMASK = 0x07 */
	[nhm_qmc_normal_reads_any]	=	0x072c,
	
	/* Event = 0x2f, UMASK = 0xxx */
	[nhm_qmc_writes_full_any]	=	0x072f,
	[nhm_qmc_writes_partial_any]	=	0x382f,
};

static void
uncore_cpuid(u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
	unsigned int op = *eax;
	asm volatile(
		"cpuid"
		: "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
		: "a"(op)
	);
}

static u64 uncore_rdtsc(void)
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

static u64 uncore_rdmsr(u32 addr)
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

static void uncore_wrmsr(u32 addr, u64 value)
{
	asm volatile(
		"wrmsr"
		:
		:"c"(addr), "d"((u32)(value>>32)), "a"((u32)value)
	);
}

static u32  PERF_VERSION;
static u64  CPU_BASE_FREQUENCY;
static char CPU_BRAND[48];

static void cpu_brand_frequency(void)
{
	char *s;
	int i;
	u32 eax, ebx, ecx, edx;

	eax = 0x80000000;
	uncore_cpuid(&eax, &ebx, &ecx, &edx);

	if (eax < 0x80000004U)
		printk(KERN_INFO"CPUID Extended Function Not Supported.\n");
	
	s = CPU_BRAND;
	for (i = 0; i < 3; i++) {
		eax = 0x80000002U + i;
		uncore_cpuid(&eax, &ebx, &ecx, &edx);
		memcpy(s, &eax, 4); s += 4;
		memcpy(s, &ebx, 4); s += 4;
		memcpy(s, &ecx, 4); s += 4;
		memcpy(s, &edx, 4); s += 4;
	}
	printk(KERN_INFO "PMU %s\n", CPU_BRAND);
	
	/* FIXME */
	CPU_BASE_FREQUENCY = 2400000000ULL;
}


static void cpu_version_info(void)
{
	u32 eax, ebx, ecx, edx;
	u32 fid, model, sid;
	
	eax = 0x01;
	uncore_cpuid(&eax, &ebx, &ecx, &edx);
	
	/* If Family ID = 0xF, then use Extended Family ID
	   If Family ID = 0xF or 0x6, then use Extended Model ID */
	fid = (eax & 0xf00) >> 8;
	model = (eax & 0xf0) >> 4;
	sid = eax & 0xf;

	if (fid == 0xf || fid == 0x6)
		model |= ((eax & 0xf0000) >> 12);
	if (fid == 0xf)
		fid += ((eax & 0xff00000) >> 20);
	
	printk(KERN_INFO "PMU Intel Family ID=%x, Model=%x, Stepping ID=%d\n", fid, model, sid);
}

static void cpu_perf_info(void)
{
	u32 msr;
	u32 eax, ebx, ecx, edx;
	
	eax = 0x0a;
	uncore_cpuid(&eax, &ebx, &ecx, &edx);
	
	PERF_VERSION = eax & 0xff;
	printk(KERN_INFO "PMU CPU_PMU Version ID: %u\n", PERF_VERSION);

	msr = uncore_rdmsr(__MSR_IA32_MISC_ENABLE);
	if (!(msr & __MSR_IA32_MISC_PERFMON_ENABLE)) {
		printk(KERN_INFO"PMU ERROR! CPU_PMU Disabled!\n");
	}
}

static void cpu_print_info(void)
{
	cpu_brand_frequency();
	cpu_version_info();
	cpu_perf_info();
}

static void nhm_uncore_show_msrs(void)
{
	u64 v1, v2, v3;

	v1 = uncore_rdmsr(NHM_UNCORE_PERF_GLOBAL_CTRL);
	v2 = uncore_rdmsr(NHM_UNCORE_PMCO);
	v3 = uncore_rdmsr(NHM_UNCORE_PERFEVTSEL0);
	
	printk(KERN_INFO "PMU GLOBAL_CTRL=%llx PMC=%lld SEL=%llx\n", v1,v2,v3);
}

/* Only one set uncore PMU in one package!
   Hence everything only needs to be done ONCE */
static inline void nhm_uncore_clear_msrs(void)
{
	uncore_wrmsr(NHM_UNCORE_PERF_GLOBAL_CTRL, 0);
	uncore_wrmsr(NHM_UNCORE_PMCO, 0);
	uncore_wrmsr(NHM_UNCORE_PERFEVTSEL0, 0);
}

static inline void nhm_uncore_enable_event(int event, u64 init_val)
{
	u64 selval;
	
	selval = nhm_uncore_event_map[event] |
		 //NHM_PEREVTSEL_PMI_ENABLE    |
		 NHM_PEREVTSEL_COUNT_ENABLE  ;
	
	uncore_wrmsr(NHM_UNCORE_PMCO, init_val);
	uncore_wrmsr(NHM_UNCORE_PERFEVTSEL0, selval);
}

/* Enable PMC0 Only */
static inline void nhm_uncore_enable_counting(void)
{
	uncore_wrmsr(NHM_UNCORE_PERF_GLOBAL_CTRL, 0x1);
}

/* Disable PMC0 Only */
static inline void nhm_uncore_disable_counting(void)
{
	uncore_wrmsr(NHM_UNCORE_PERF_GLOBAL_CTRL, 0x0);
}

void pmu_main(void)
{
	cpu_print_info();

	nhm_uncore_clear_msrs();
	nhm_uncore_show_msrs();
	
	nhm_uncore_enable_event(nhm_qhl_request_remote_writes, 0);
	nhm_uncore_show_msrs();

	
	nhm_uncore_enable_counting();
	mdelay(1);
	nhm_uncore_disable_counting();
	
	nhm_uncore_show_msrs();
	nhm_uncore_clear_msrs();
}

int pmu_init(void)
{
	printk(KERN_INFO "PMU INIT ON CPU %d\n", smp_processor_id());
	printk(KERN_INFO "PMU ONLINE CPUS: %d\n", num_online_cpus());
	pmu_main();
	return 0;
}

void pmu_exit(void)
{
	int cpu;
	/*
	for_each_online_cpu(cpu) {
		printk(KERN_INFO "PMU CPU %d\n", cpu);
	}
	*/
	//unregister_nmi_handler(NMI_LOCAL, "PMU_NMI_HANDLER");
	printk(KERN_INFO "PMU module exit.\n");
}

module_init(pmu_init);
module_exit(pmu_exit);
MODULE_LICENSE("GPL");
