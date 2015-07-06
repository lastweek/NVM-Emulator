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

#define _UNCORE_DEBUG_

#define __AC(X,Y)	(X##Y)

/*
 * Architectual MSRs
 * And their control bits
 */

#define __MSR_IA32_MISC_ENABLE			0x1a0
#define __MSR_IA32_DEBUG_CTL			0x1d9
#define __MSR_IA32_PERFMON_ENABLE		(__AC(1, ULL)<<7)
#define __MSR_IA32_ENABLE_UNCORE_PMI	(__AC(1, ULL)<<13)

/*
 * o Nehalem PMC Bit Width
 * o Nehalem PMC Maximum Value Mask
 */

#define NHM_UNCORE_PMC_BIT_WIDTH		48
#define NHM_UNCORE_PMC_VALUE_MASK		((__AC(1, ULL)<<48) - 1)

/*
 * o Nehalem Global Control Registers
 * o Nehalem Performance Counter and Control Registers
 */

#define NHM_UNCORE_GLOBAL_CTRL			0x391	/* Read/Write */
#define NHM_UNCORE_GLOBAL_STATUS		0x392	/* Read-Only */
#define NHM_UNCORE_GLOBAL_OVF_CTRL		0x393	/* Write-Only */
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

/*
 * Control Bit in PERFEVTSEL
 */

#define NHM_PEREVTSEL_EVENT_MASK			(__AC(0xff, ULL))
#define NHM_PEREVTSEL_UNIT_MASK				(__AC(0xff, ULL)<<8)
#define NHM_PEREVTSEL_OCC_CTR_RST			(__AC(1, ULL)<<17)
#define NHM_PEREVTSEL_EDGE_DETECT			(__AC(1, ULL)<<18)
#define NHM_PEREVTSEL_PMI_ENABLE			(__AC(1, ULL)<<20)
#define NHM_PEREVTSEL_COUNT_ENABLE			(__AC(1, ULL)<<22)
#define NHM_PEREVTSEL_INVERT				(__AC(1, ULL)<<23)

/*
 * Control Bit in GLOBAL_CTRL
 */

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

/*
 * Test result shows that the EN_PMI_COREx bit
 * enables _one_ _physical_ core to receive a
 * PMI, which means _two_ _logical_ cores will
 * receive PMI.
 *
 * Also, if one package has more than four physical
 * cores, like Xeon 5600 which has 6, only 4 of 6
 * can receive PMI.
 */
#define NHM_UNCORE_GLOBAL_CTRL_EN_PMI		\
	(NHM_UNCORE_GLOBAL_CTRL_EN_PMI_CORE0 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMI_CORE1 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMI_CORE2 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMI_CORE3 )

/*
 * Control Bit in GLOBAL_CTRL and OVF_CTRL
 */

#define NHM_UNCORE_GLOBAL_OVF_FC0			(__AC(1, ULL)<<32)
#define NHM_UNCORE_GLOBAL_OVF_PMI			(__AC(1, ULL)<<61)
#define NHM_UNCORE_GLOBAL_OVF_CHG			(__AC(1, ULL)<<63)

/*
 * Masks For Three GLOBAL MSRs
 * Used when we wanna read from or write to these
 * three MSRs. Masks help to avoid writing reserved
 * bit in MSR which can bring kernel panic.
 */

#define NHM_UNCORE_PMC_MASK				\
	(NHM_UNCORE_GLOBAL_CTRL_EN_PMC0 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC1 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC2 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC3 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC4 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC5 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC6 |	\
	 NHM_UNCORE_GLOBAL_CTRL_EN_PMC7 )

#define NHM_UNCORE_GLOBAL_CTRL_MASK		\
	(NHM_UNCORE_PMC_MASK | NHM_UNCORE_GLOBAL_CTRL_EN_PMI)

#define NHM_UNCORE_GLOBAL_STATUS_MASK	\
	(NHM_UNCORE_PMC_MASK		|		\
	 NHM_UNCORE_GLOBAL_OVF_PMI	|		\
	 NHM_UNCORE_GLOBAL_OVF_CHG  )

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
	
	/*
	 * The PERFMON_ENABLE bit is Read-Only, every
	 * core in one package has the same value.
	 * Therefore, it is sufficient to check one core.
	 */
	msr = uncore_rdmsr(__MSR_IA32_MISC_ENABLE);
	if (!(msr & __MSR_IA32_PERFMON_ENABLE)) {
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

/**
 * Clear bit @pmcmask(overflow status) in GLOBAL_STATUS
 * Write into OVF_CTRL to clear GLOBAL_STATUS 
 */
static inline void nhm_uncore_clear_ovf(u64 pmcmask)
{
	u64 mask = pmcmask;
	mask |= (NHM_UNCORE_GLOBAL_OVF_PMI | NHM_UNCORE_GLOBAL_OVF_CHG);
	uncore_wrmsr(NHM_UNCORE_GLOBAL_OVF_CTRL, mask);
}

/**
 * Clear entire GLOBAL_STATUS.
 * Write into OVF_CTRL to clear GLOBAL_STATUS 
 */
static inline void nhm_uncore_clear_status(void)
{
	uncore_wrmsr(NHM_UNCORE_GLOBAL_OVF_CTRL, NHM_UNCORE_GLOBAL_OVF_CTRL_MASK);
}

static inline void nhm_uncore_clear_ctrl(void)
{
	uncore_wrmsr(NHM_UNCORE_GLOBAL_CTRL, 0);
}

static inline void nhm_uncore_clear_msrs(void)
{
	int id;
	u64 pmc, sel;

	nhm_uncore_clear_ctrl();
	nhm_uncore_clear_status();
	
	for_each_pmc_pair(id, pmc, sel) {
		uncore_wrmsr(pmc, 0);
		uncore_wrmsr(sel, 0);
	}
}

/**
 * nhm_uncore_set_event - Set event in pmc.
 * @pmcid: 	The id of the pmc pair
 * @event:	pre-defined event id
 * @pmi:	No-zero to enable pmi
 * @pmcval:	the initial value in pmc
 */
static inline void
nhm_uncore_set_event(int pmcid, int event, int pmi, long long pmcval)
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

	uncore_wrmsr(pmcid + NHM_UNCORE_PMC_BASE, (u64)pmcval);
	uncore_wrmsr(pmcid + NHM_UNCORE_SEL_BASE, selval);
}

/**
 * nhm_uncore_enable_counting - enable counting
 *
 * o Enable all pmc pairs to start counting
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

static void __enable_pmi(void *info)
{
	u64 m;
	
	m = uncore_rdmsr(__MSR_IA32_DEBUG_CTL);
	if (!(m & __MSR_IA32_ENABLE_UNCORE_PMI)) {
#ifdef _UNCORE_DEBUG_
		printk(KERN_INFO "PMU CPU%d Set ENABLE_UNCORE_PMI to 1\n", smp_processor_id());
#endif
		m |= __MSR_IA32_ENABLE_UNCORE_PMI;
		uncore_wrmsr(__MSR_IA32_DEBUG_CTL, m);
	}
}

/**
 * nmh_uncore_pmi_init - enable uncore pmi
 *
 * Enable every core to receive the uncore
 * pmi generated when pmc overflow. Every
 * core in system should do this.
 */
static void nhm_uncore_pmi_init(void)
{
	int cpu, err;
	for_each_online_cpu(cpu) {
		err = smp_call_function_single(cpu, __enable_pmi, NULL, 1);
	}
}

static int is_nmi_registed = 0;

int nhm_uncore_nmi_handler(unsigned int type, struct pt_regs *regs)
{
	u64 status, ovfpmc;

	status = uncore_rdmsr(NHM_UNCORE_GLOBAL_STATUS);
	ovfpmc = status & NHM_UNCORE_PMC_MASK;
	if (!ovfpmc) /* No PMC Overflows */
		return NMI_DONE;
	
#ifdef _UNCORE_DEBUG_
	printk(KERN_INFO "PMU NMI CPU %2d, GLOBAL_STATUS=%llx, OVF_PMC_MASK=%llx\n",
		smp_processor_id(), status, ovfpmc);
#endif
	
	//nhm_uncore_clear_ovf(ovfpmc);

	return NMI_HANDLED;
}

static void nhm_uncore_register_nmi_handler(void)
{
	int ret;
	ret = register_nmi_handler(NMI_LOCAL, nhm_uncore_nmi_handler,
			NMI_FLAG_FIRST, "__UNCORE_NMI_HANDLER");
	if (!ret) {
		is_nmi_registed = 1;
#ifdef _UNCORE_DEBUG_
		printk(KERN_INFO "PMU NMI Handler Installed\n");
#endif
	}
}

static void nhm_uncore_unregister_nmi_handler(void)
{
	if (is_nmi_registed) {
		unregister_nmi_handler(NMI_LOCAL, "__UNCORE_NMI_HANDLER");
#ifdef _UNCORE_DEBUG_
		printk(KERN_INFO "PMU NMI Handler Removed\n");
#endif
	} else {
#ifdef _UNCORE_DEBUG_
		printk(KERN_INFO "PMU NMI Try to remove a unregisted handler\n");
#endif
	}
}


//#################################################
// GENERAL MODULE PART
//#################################################

#define __START_COUNTING() \
	nhm_uncore_enable_counting();

#define __END_COUNTING() \
	nhm_uncore_disable_counting();

#define uncore_set_event(pmcid, event, pmi, pmcval) \
	nhm_uncore_set_event(pmcid, event, pmi, pmcval)

#define uncore_pmi_init() \
	nhm_uncore_pmi_init()

#define uncore_nmi_register() \
	nhm_uncore_register_nmi_handler()

#define uncore_nmi_unregister() \
	nhm_uncore_unregister_nmi_handler()

#define show() 	nhm_uncore_show_msrs()
#define clear()	nhm_uncore_clear_msrs()

void uncore_pmu_main(void)
{
	uncore_pmi_init();
	uncore_nmi_register();
	
	clear();
	//uncore_set_event(PMC_PID0, nhm_qhl_request_remote_writes, 1, -100);
	//uncore_set_event(PMC_PID1, nhm_qhl_request_remote_reads,  1, -100);
	uncore_set_event(PMC_PID2, nhm_qhl_request_local_reads,   1, -1000);
	//uncore_set_event(PMC_PID3, nhm_qhl_request_local_writes,  1, -1000);
	show();
	
	__START_COUNTING();
	
	mdelay(1000);
	show();
	mdelay(1000);
	show();
	
	__END_COUNTING();
	show();
}

const char *beybanner = "PMU --------> EXIT <--------";
const char *welbanner = "PMU <-------- INIT -------->";

int uncore_pmu_init(void)
{
	printk(KERN_INFO "%s ON CPU %d\nPMU\n", welbanner, smp_processor_id());
	printk(KERN_INFO "PMU ONLINE CPUS: %d\n", num_online_cpus());
	
	uncore_cpu_info();
	uncore_pmu_main();
	
	return 0;
}

void uncore_pmu_exit(void)
{
	/* Clean up, avoid endless counting */
	clear();
	uncore_nmi_unregister();

	printk(KERN_INFO "%s ON CPU %d\n", beybanner, smp_processor_id());
}

module_init(uncore_pmu_init);
module_exit(uncore_pmu_exit);
MODULE_LICENSE("GPL");
