#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <asm/msr.h>

#define __AC(X,Y)	(X##Y)

#define __MSR_IA32_MISC_ENABLE			0x1A0
#define __MSR_IA32_MISC_PERFMON_ENABLE 	(__AC(1, ULL)<<7)

/*
 * NOTE:
 * The scope of the uncore PMU relative MSRs is: Package. 
 */
/* Nehalem Uncore PMU MSR Bit Width */
#define NHM_CONTROL_REG_BIT_WIDTH		64
#define NHM_COUNTER_REG_BIT_WIDTH		48

/* Nehalem Global Control Registers */
#define NHM_UNCORE_PERF_GLOBAL_CTRL		0x391	/* Read/Write */
#define NHM_UNCORE_PERF_GLOBAL_STATUS	0x392	/* Read-Only */
#define NHM_UNCORE_PERF_GLOBAL_OVF_CTRL	0x393	/* Write-Only */

/* Nehalem Performance Control Registers */
#define NHM_UNCORE_PMCO				0x3b0	/* Read/Write */
#define NHM_UNCORE_PMC1				0x3b1	/* Read/Write */
#define NHM_UNCORE_PMC2				0x3b2	/* Read/Write */
#define NHM_UNCORE_PMC3				0x3b3	/* Read/Write */
#define NHM_UNCORE_PMC4				0x3b4	/* Read/Write */
#define NHM_UNCORE_PMC5				0x3b5	/* Read/Write */
#define NHM_UNCORE_PMC6				0x3b6	/* Read/Write */
#define NHM_UNCORE_PMC7				0x3b7	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL0		0x3c0	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL1		0x3c1	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL2		0x3c2	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL3		0x3c3	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL4		0x3c4	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL5		0x3c5	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL6		0x3c6	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL7		0x3c7	/* Read/Write */

#define NHM_UNCORE_PMC_BASE			NHM_UNCORE_PMCO
#define NHM_UNCORE_SEL_BASE			NHM_UNCORE_PERFEVTSEL0

/*
 *	Each PMCx and PERFEVTSELx forms a pair.
 *	When we manipulate PMU, we usually handle a pmc pair.
 *	Hence, in the code below, we operate PMCx and PERFEVTSELx
 *	through their corresponding pair id.
 */
enum NHM_UNCORE_PMC_PAIR_ID {
	PMC_PID0, PMC_PID1, PMC_PID2, PMC_PID3,
	PMC_PID4, PMC_PID5, PMC_PID6, PMC_PID7,
	PMC_PID_MAX
};

/**
 * for_each_pmc_pair - loop each pmc pair in PMU
 * @id:  pair id
 * @pmc: pmc msr address
 * @sel: perfevtsel msr address
 */
#define for_each_pmc_pair(id, pmc, sel) \
	for ((id) = 0, (pmc) = NHM_UNCORE_PMC_BASE, (sel) = NHM_UNCORE_SEL_BASE; \
		(id) < PMC_PID_MAX; (id)++, (pmc)++, (sel)++)
		 
/* Control Bit in NHM_UNCORE_PERF_GLOBAL_CTRL */
#define NHM_UNCORE_GLOBAL_CTRL_EN_PMC0		(__AC(1, ULL)<<0)
#define NHM_UNCORE_GLOBAL_CTRL_EN_PMC1		(__AC(1, ULL)<<1)
#define NHM_UNCORE_GLOBAL_CTRL_EN_PMC2		(__AC(1, ULL)<<2)
#define NHM_UNCORE_GLOBAL_CTRL_EN_PMC3		(__AC(1, ULL)<<3)
#define NHM_UNCORE_GLOBAL_CTRL_EN_PMC4		(__AC(1, ULL)<<4)
#define NHM_UNCORE_GLOBAL_CTRL_EN_PMC5		(__AC(1, ULL)<<5)
#define NHM_UNCORE_GLOBAL_CTRL_EN_PMC6		(__AC(1, ULL)<<6)
#define NHM_UNCORE_GLOBAL_CTRL_EN_PMC7		(__AC(1, ULL)<<7)

#define NHM_UNCORE_GLOBAL_CTRL_EN_PMI_CORE0	(__AC(1, ULL)<<48)
#define NHM_UNCORE_GLOBAL_CTRL_EN_PMI_CORE1	(__AC(1, ULL)<<49)
#define NHM_UNCORE_GLOBAL_CTRL_EN_PMI_CORE2	(__AC(1, ULL)<<50)
#define NHM_UNCORE_GLOBAL_CTRL_EN_PMI_CORE3	(__AC(1, ULL)<<51)
#define NHM_UNCORE_GLOBAL_CTRL_EN_FRZ		(__AC(1, ULL)<<63)

/* Control Bit in NHM_UNCORE_PERFEVTSEL */
#define NHM_PEREVTSEL_EVENT_MASK	(__AC(0xf, ULL))
#define NHM_PEREVTSEL_UNIT_MASK		(__AC(0xf, ULL)<<8)
#define NHM_PEREVTSEL_OCC_CTR_RST	(__AC(1, ULL)<<17)
#define NHM_PEREVTSEL_EDGE_DETECT	(__AC(1, ULL)<<18)
#define NHM_PEREVTSEL_PMI_ENABLE	(__AC(1, ULL)<<20)
#define NHM_PEREVTSEL_COUNT_ENABLE	(__AC(1, ULL)<<22)
#define NHM_PEREVTSEL_INVERT		(__AC(1, ULL)<<23)

enum nhm_uncore_event_id {
	nhm_qhl_request_ioh_reads		=	1,
	nhm_qhl_request_ioh_writes		=	2,
	nhm_qhl_request_remote_reads	=	3,
	nhm_qhl_request_remote_writes	=	4,
	nhm_qhl_request_local_reads		=	5,
	nhm_qhl_request_local_writes	=	6,
	
	nhm_qmc_normal_reads_any		=	7,
	nhm_qmc_writes_full_any			=	8,
	nhm_qmc_writes_partial_any		=	9,

	NHM_UNCORE_EVENT_ID_MAX
};

static u64 nhm_uncore_event_map[NHM_UNCORE_EVENT_ID_MAX] = 
{
	/* Event = 0x20, UMASK = 0xxx */
	[nhm_qhl_request_ioh_reads]		=	0x0120,
	[nhm_qhl_request_ioh_writes]	=	0x0220,
	[nhm_qhl_request_remote_reads]	=	0x0420,
	[nhm_qhl_request_remote_writes]	=	0x0820,
	[nhm_qhl_request_local_reads]	=	0x1020,
	[nhm_qhl_request_local_writes]	=	0x2020,
	
	/* Event = 0x2c, UMASK = 0x07 */
	[nhm_qmc_normal_reads_any]		=	0x072c,
	
	/* Event = 0x2f, UMASK = 0xxx */
	[nhm_qmc_writes_full_any]		=	0x072f,
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

/**
 * nhm_uncore_show_msrs - Show registers current state
 * Only show active pmc and perfsel register pairs.
 */
static void nhm_uncore_show_msrs(void)
{
	int id;
	u64 pmc, sel;
	u64 a, b;
	
	a = uncore_rdmsr(NHM_UNCORE_PERF_GLOBAL_CTRL);
	printk(KERN_INFO "PMU SHOW --> UNCORE_GLOBAL_CTRL = 0x%llx\n", a);

	for_each_pmc_pair(id, pmc, sel) {
		a = uncore_rdmsr(pmc);
		b = uncore_rdmsr(sel);
		if (b) {/* active */
			printk(KERN_INFO "PMU SHOW --> PMC%d = %lld SEL%d = 0x%llx\n",id,a,id,b);
		}
	}
}

static inline void nhm_uncore_clear_msrs(void)
{
	int id;
	u64 pmc, sel;

	for_each_pmc_pair(id, pmc, sel) {
		uncore_wrmsr(pmc, 0);
		uncore_wrmsr(sel, 0);
	}

	uncore_wrmsr(NHM_UNCORE_PERF_GLOBAL_CTRL, 0);
}

/**
 * nhm_uncore_set_event - Set event in specified pmc.
 * @pid: 	The id of the pmc pair
 * @event:	pre-defined event id
 * @pmcval:	the initial value in pmc
 */
static inline void nhm_uncore_set_event(int pid, int event, u64 pmcval)
{
	u64 selval;
	
	selval = nhm_uncore_event_map[event] |
			 //NHM_PEREVTSEL_PMI_ENABLE    |
			 NHM_PEREVTSEL_COUNT_ENABLE  ;
	
	uncore_wrmsr(pid + NHM_UNCORE_PMC_BASE, pmcval);
	uncore_wrmsr(pid + NHM_UNCORE_SEL_BASE, selval);
}

static inline void nhm_uncore_enable_counting(void)
{
	uncore_wrmsr(NHM_UNCORE_PERF_GLOBAL_CTRL,
				 NHM_UNCORE_GLOBAL_CTRL_EN_PMC0 |
				 NHM_UNCORE_GLOBAL_CTRL_EN_PMC1 |
				 NHM_UNCORE_GLOBAL_CTRL_EN_PMC2 |
				 NHM_UNCORE_GLOBAL_CTRL_EN_PMC3 |
				 NHM_UNCORE_GLOBAL_CTRL_EN_PMC4 |
				 NHM_UNCORE_GLOBAL_CTRL_EN_PMC5 |
				 NHM_UNCORE_GLOBAL_CTRL_EN_PMC6 |
				 NHM_UNCORE_GLOBAL_CTRL_EN_PMC7 );
}

static inline void nhm_uncore_disable_counting(void)
{
	uncore_wrmsr(NHM_UNCORE_PERF_GLOBAL_CTRL, 0);
}

#define __START_COUNTING__()	nhm_uncore_enable_counting()
#define __END_COUNTING__()		nhm_uncore_disable_counting()
#define show_msrs() 	nhm_uncore_show_msrs()
#define clear_msrs()	nhm_uncore_clear_msrs()
#define set_event(pid, event, pmcval) nhm_uncore_set_event(pid, event, pmcval)

void pmu_main(void)
{
	nhm_uncore_clear_msrs();
	
	set_event(PMC_PID0, nhm_qhl_request_remote_writes, 0);
	set_event(PMC_PID1, nhm_qhl_request_remote_reads, 0);
	set_event(PMC_PID2, nhm_qhl_request_local_reads, 0);
	set_event(PMC_PID3, nhm_qhl_request_local_writes, 0);
	show_msrs();
	
	__START_COUNTING__();
	
	mdelay(1000);
	show_msrs();
	mdelay(1000);
	show_msrs();
	mdelay(1000);
	show_msrs();
	
	__END_COUNTING__();
}

int pmu_init(void)
{
	printk(KERN_INFO "PMU <--- INIT ---> ON CPU %d\n", smp_processor_id());
	printk(KERN_INFO "PMU ONLINE CPUS: %d\n", num_online_cpus());
	cpu_print_info();
	pmu_main();
	return 0;
}

void pmu_exit(void)
{
	/*
	for_each_online_cpu(cpu) {
		printk(KERN_INFO "PMU CPU %d\n", cpu);
	}
	*/
	//unregister_nmi_handler(NMI_LOCAL, "PMU_NMI_HANDLER");
	printk(KERN_INFO "PMU <--- EXIT ---> ON CPU %d\n", smp_processor_id());
}

module_init(pmu_init);
module_exit(pmu_exit);
MODULE_LICENSE("GPL");
