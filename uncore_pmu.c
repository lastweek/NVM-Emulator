/*
 * NOTE:
 * The scope of the UNCORE PMU is: Package. 
 * Therefore, everything only needs to be done once in a package
 * by a logical core in that package.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <asm/nmi.h>

#define __AC(X,Y)	(X##Y)

/* Architectual MSRs and Control Bit */
#define __MSR_IA32_MISC_ENABLE			0x1a0
#define __MSR_IA32_DEBUG_CTL			0x1d9

#define __MSR_IA32_MISC_ENABLE_PERFMON_ENABLE	(__AC(1, ULL)<<7)
#define __MSR_IA32_DEBUG_CTL_ENABLE_UNCORE_PMI	(__AC(1, ULL)<<13)

/* Nehalem PMC Bit Width */
#define NHM_UNCORE_PMC_BIT_WIDTH		48
#define NHM_UNCORE_PMC_VALUE_MASK		((__AC(1, ULL)<<48) - 1)

/* Nehalem Global Control Registers */
#define NHM_UNCORE_GLOBAL_CTRL			0x391	/* Read/Write */
#define NHM_UNCORE_GLOBAL_STATUS		0x392	/* Read-Only */
#define NHM_UNCORE_GLOBAL_OVF_CTRL		0x393	/* Write-Only */

/* Nehalem Performance Counter and Control Registers */
#define NHM_UNCORE_PMCO					0x3b0	/* Read/Write */
#define NHM_UNCORE_PMC1					0x3b1	/* Read/Write */
#define NHM_UNCORE_PMC2					0x3b2	/* Read/Write */
#define NHM_UNCORE_PMC3					0x3b3	/* Read/Write */
#define NHM_UNCORE_PMC4					0x3b4	/* Read/Write */
#define NHM_UNCORE_PMC5					0x3b5	/* Read/Write */
#define NHM_UNCORE_PMC6					0x3b6	/* Read/Write */
#define NHM_UNCORE_PMC7					0x3b7	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL0			0x3c0	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL1			0x3c1	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL2			0x3c2	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL3			0x3c3	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL4			0x3c4	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL5			0x3c5	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL6			0x3c6	/* Read/Write */
#define NHM_UNCORE_PERFEVTSEL7			0x3c7	/* Read/Write */

#define NHM_UNCORE_PMC_BASE		NHM_UNCORE_PMCO
#define NHM_UNCORE_SEL_BASE		NHM_UNCORE_PERFEVTSEL0

/* Control Bit in PERFEVTSEL */
#define NHM_PEREVTSEL_EVENT_MASK			(__AC(0xff, ULL))
#define NHM_PEREVTSEL_UNIT_MASK				(__AC(0xff, ULL)<<8)
#define NHM_PEREVTSEL_OCC_CTR_RST			(__AC(1, ULL)<<17)
#define NHM_PEREVTSEL_EDGE_DETECT			(__AC(1, ULL)<<18)
#define NHM_PEREVTSEL_PMI_ENABLE			(__AC(1, ULL)<<20)
#define NHM_PEREVTSEL_COUNT_ENABLE			(__AC(1, ULL)<<22)
#define NHM_PEREVTSEL_INVERT				(__AC(1, ULL)<<23)

/* Control Bit in GLOBAL_CTRL */
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

#define NHM_UNCORE_GLOBAL_CTRL_EN_PMI		\
	(NHM_UNCORE_GLOBAL_CTRL_EN_PMI_CORE0 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMI_CORE1 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMI_CORE2 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMI_CORE3 )

/* Control Bit in GLOBAL_CTRL and OVF_CTRL */
#define NHM_UNCORE_GLOBAL_OVF_FC0			(__AC(1, ULL)<<32)
#define NHM_UNCORE_GLOBAL_OVF_PMI			(__AC(1, ULL)<<61)
#define NHM_UNCORE_GLOBAL_OVF_CHG			(__AC(1, ULL)<<63)

#define NHM_UNCORE_PMC_MASK				\
	(NHM_UNCORE_GLOBAL_CTRL_EN_PMC0 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC1 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC2 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC3 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC4 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC5 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC6 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC7 )

/* General Control MSR Mask */
#define NHM_UNCORE_GLOBAL_CTRL_MASK		\
	(NHM_UNCORE_PMC_MASK | NHM_UNCORE_GLOBAL_CTRL_EN_PMI)

#define NHM_UNCORE_GLOBAL_STATUS_MASK	\
	(NHM_UNCORE_PMC_MASK		|		\
	 NHM_UNCORE_GLOBAL_OVF_PMI	|		\
	 NHM_UNCORE_GLOBAL_OVF_CHG  )

/* Clear FC0 in case some bad things happen */
#define NHM_UNCORE_GLOBAL_OVF_CTRL_MASK	\
	(NHM_UNCORE_GLOBAL_STATUS_MASK | NHM_UNCORE_GLOBAL_OVF_FC0)

/*
 *	Each PMCx and PERFEVTSELx forms a pair.
 *	When we manipulate PMU, we usually handle a PMC/SEL pair.
 *	Hence, in the code below, we operate PMCx and PERFEVTSELx
 *	through their corresponding pair id.
 */
enum NHM_UNCORE_PMC_PAIR_ID {
	PMC_PID0, PMC_PID1, PMC_PID2, PMC_PID3,
	PMC_PID4, PMC_PID5, PMC_PID6, PMC_PID7,
	PMC_PID_MAX
};

/**
 * for_each_pmc_pair - loop each pmc pair in NHM PMU
 * @id:  pair id
 * @pmc: pmc msr address
 * @sel: perfevtsel msr address
 */
#define for_each_pmc_pair(id, pmc, sel) \
	for ((id) = 0, (pmc) = NHM_UNCORE_PMC_BASE, (sel) = NHM_UNCORE_SEL_BASE; \
		(id) < PMC_PID_MAX; (id)++, (pmc)++, (sel)++)


//#################################################
// NHM UNCORE EVENT
//#################################################

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
	[nhm_qhl_request_ioh_reads]		=	0x0120,
	[nhm_qhl_request_ioh_writes]	=	0x0220,
	[nhm_qhl_request_remote_reads]	=	0x0420,
	[nhm_qhl_request_remote_writes]	=	0x0820,
	[nhm_qhl_request_local_reads]	=	0x1020,
	[nhm_qhl_request_local_writes]	=	0x2020,
	[nhm_qmc_normal_reads_any]		=	0x072c,
	[nhm_qmc_writes_full_any]		=	0x072f,
	[nhm_qmc_writes_partial_any]	=	0x382f,
};


//#################################################
//  ASSEMBLY PART
//#################################################

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
		: :"c"(addr), "d"((u32)(value>>32)), "a"((u32)value)
	);
}

//#################################################
// CPU INFO PART
//#################################################

static u32  PERF_VERSION;
static u64  CPU_BASE_FREQUENCY;
static char CPU_BRAND[48];

static void cpu_brand_info(void)
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
	if (!(msr & __MSR_IA32_MISC_ENABLE_PERFMON_ENABLE)) {
		printk(KERN_INFO"PMU ERROR! CPU_PMU Disabled!\n");
	}
}

static void uncore_cpu_info(void)
{
	cpu_brand_info();
	cpu_version_info();
	cpu_perf_info();
}

//#################################################
// NHM UNCORE PMU PART
//#################################################

static void nhm_uncore_show_msrs(void)
{
	int id;
	u64 pmc, sel;
	u64 a, b;
	
	printk(KERN_INFO "PMU\n");
	
	a = uncore_rdmsr(NHM_UNCORE_GLOBAL_CTRL);
	printk(KERN_INFO "PMU SHOW --> GLOBAL_CTRL = 0x%llx\n", a);

	a = uncore_rdmsr(NHM_UNCORE_GLOBAL_STATUS);
	printk(KERN_INFO "PMU SHOW --> GLOBAL_STATUS = 0x%llx\n", a);

	for_each_pmc_pair(id, pmc, sel) {
		a = uncore_rdmsr(pmc);
		b = uncore_rdmsr(sel);
		if (b) {/* active */
			printk(KERN_INFO "PMU SHOW --> PMC%d = %-20lld SEL%d = 0x%llx\n",id,a,id,b);
		}
	}

	printk(KERN_INFO "PMU\n");
}

static inline void nhm_uncore_clear_msrs(void)
{
	int id;
	u64 pmc, sel;

	/* A little weird.
	   Write into OVF_CTRL to clear GLOBAL_STATUS ... */
	uncore_wrmsr(NHM_UNCORE_GLOBAL_OVF_CTRL, NHM_UNCORE_GLOBAL_OVF_CTRL_MASK);
	
	uncore_wrmsr(NHM_UNCORE_GLOBAL_CTRL, 0);
	
	for_each_pmc_pair(id, pmc, sel) {
		uncore_wrmsr(pmc, 0);
		uncore_wrmsr(sel, 0);
	}
}

/**
 * nhm_uncore_set_event - Set event in pmc.
 * @pid: 	The id of the pmc pair
 * @event:	pre-defined event id
 * @pmi:	No-zero to enable pmi
 * @pmcval:	the initial value in pmc
 */
static inline void
nhm_uncore_set_event(int pid, int event, int pmi, long long pmcval)
{
	u64 selval;
	
	selval = nhm_uncore_event_map[event] |
			 NHM_PEREVTSEL_COUNT_ENABLE  ;

	if (pmi)
		selval |= NHM_PEREVTSEL_PMI_ENABLE;
	
	/* ok ok ok, this is a very very very safty mask!
	   Writing to reserved bits in MSR cause CPU to
	   generate #GP fault. #GP handler will make you die */
	pmcval &= NHM_UNCORE_PMC_VALUE_MASK;

	uncore_wrmsr(pid + NHM_UNCORE_PMC_BASE, (u64)pmcval);
	uncore_wrmsr(pid + NHM_UNCORE_SEL_BASE, selval);
}

/**
 * nhm_uncore_enable_counting - enable counting
 * o Enable all pmc pairs
 * o Set all cores to receive PMI
 */
static inline void nhm_uncore_enable_counting(void)
{
	uncore_wrmsr(NHM_UNCORE_GLOBAL_CTRL, NHM_UNCORE_GLOBAL_CTRL_MASK);
}

static inline void nhm_uncore_disable_counting(void)
{
	uncore_wrmsr(NHM_UNCORE_GLOBAL_CTRL, 0);
}


//#################################################
// NMI PART
//#################################################

int is_nmi_handler_registed = 0;
char *nmi_handler_name = "NHM_UNCORE_NMI_HANDLER";

int nhm_uncore_nmi_handler(unsigned int type, struct pt_regs *regs)
{
	u64 msrval;

	printk(KERN_INFO "PMU NMI!!!! CPU %d\n", smp_processor_id());
	
	msrval = uncore_rdmsr(NHM_UNCORE_GLOBAL_STATUS);
	printk(KERN_INFO "PMU NMI status = %llx\n", 
		msrval & NHM_UNCORE_GLOBAL_STATUS_MASK);
	
	if (!(msrval & 0xff))
		return NMI_DONE;

	
	return NMI_HANDLED;
}

static void nhm_uncore_register_nmi_handler(void)
{
	int retval;
	
	retval = register_nmi_handler(NMI_LOCAL, nhm_uncore_nmi_handler,
			NMI_FLAG_FIRST, nmi_handler_name);
	if (!retval)
		is_nmi_handler_registed = 1;
}

static void nhm_uncore_unregister_nmi_handler(void)
{
	if (is_nmi_handler_registed)
		unregister_nmi_handler(NMI_LOCAL, nmi_handler_name);
}

/**
 * nmh_uncore_pmi_init - Init PMU Interrupt
 * Set Enable_Uncore_PMI in  MSR_IA32_DEBUG_CTL.
 * Init apic.(Is that necessary for uncore?)
 */
static void nhm_uncore_pmi_init(void)
{
	u64 mval;
	
	mval = uncore_rdmsr(__MSR_IA32_DEBUG_CTL);
	mval |= __MSR_IA32_DEBUG_CTL_ENABLE_UNCORE_PMI;
	uncore_wrmsr(__MSR_IA32_DEBUG_CTL, mval);
}


//#################################################
// GENERAL MODULE PART
//#################################################

#define __START_COUNTING__ \
	nhm_uncore_enable_counting();

#define __END_COUNTING__ \
	nhm_uncore_disable_counting();

#define uncore_set_event(pid, event, pmi, pmcval) \
	nhm_uncore_set_event(pid, event, pmi, pmcval)

#define uncore_pmi_init() \
	nhm_uncore_pmi_init()

#define uncore_register_nmi_handler() \
	nhm_uncore_register_nmi_handler()

#define uncore_unregister_nmi_handler() \
	nhm_uncore_unregister_nmi_handler()

#define show() 	nhm_uncore_show_msrs()
#define clear()	nhm_uncore_clear_msrs()

void uncore_pmu_main(void)
{
	uncore_pmi_init();
	uncore_register_nmi_handler();

	clear();
	uncore_set_event(PMC_PID0, nhm_qhl_request_remote_writes, 1, -100);
	uncore_set_event(PMC_PID1, nhm_qhl_request_remote_reads,  1, -100);
	uncore_set_event(PMC_PID2, nhm_qhl_request_local_reads,   1, -100);
	uncore_set_event(PMC_PID3, nhm_qhl_request_local_writes,  1, -100);
	show();
	
	__START_COUNTING__
	
	mdelay(1000);
	show();
	mdelay(1000);
	show();
	
	__END_COUNTING__
	
}

int uncore_pmu_init(void)
{
	char *banner = "PMU <-------- INIT -------->";
	printk(KERN_INFO "%s ON CPU %d\nPMU\n", banner, smp_processor_id());
	printk(KERN_INFO "PMU ONLINE CPUS: %d\n", num_online_cpus());
	
	uncore_cpu_info();
	uncore_pmu_main();
	
	return 0;
}

void uncore_pmu_exit(void)
{
	char *banner = "PMU --------> EXIT <--------";

	/* Do some regular cleaning up */
	clear();
	//uncore_unregister_nmi_handler();

	printk(KERN_INFO "%s ON CPU %d\n", banner, smp_processor_id());
}

module_init(uncore_pmu_init);
module_exit(uncore_pmu_exit);
MODULE_LICENSE("GPL");
