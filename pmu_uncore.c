/*
 *	Description
 *
 *	Intel Uncore Performance Monitoring Module.
 *
 *	Specially, it's designed for Xeon 5600 Series(Westmere).
 *	Westmere(Tick 32nm) is the second edition of Nehalem(Tock 45nm).
 */

/*
	NOTE:
	Check PCI configuration space to see uncore clock ratio?
	How to access pci device?
*/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/percpu-defs.h>
#include <linux/percpu.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <asm/nmi.h>
#include <asm/msr.h>

/* Nehalem Global Control Registers */
#define NHM_UNCORE_PERF_GLOBAL_CTRL		0x391
#define NHM_UNCORE_PERF_GLOBAL_STATUS	0x392
#define NHM_UNCORE_PERF_GLOBAL_OVF_CTRL	0x393

/* Nehalem Performance Control Registers */
#define NHM_UNCORE_PMCO					0x3b0
#define NHM_UNCORE_PERFEVTSEL0			0X3C0

#define NHM_CONTROL_REG_BIT_WIDTH		64
#define NHM_COUNTER_REG_BIT_WIDTH		48

/* Control Bit In NHM_UNCORE_PERFEVTSEL */
#define NHM_OCC_CTR_RST		((1ULL)<<17)
#define NHM_EDGE_DETECT		((1ULL)<<18)
#define NHM_PMI_ENABLE		((1ULL)<<20)
#define NHM_COUNT_ENABLE	((1ULL)<<22)
#define NHM_INVERT			((1ULL)<<23)

typedef unsigned char		u8;
typedef unsigned short		u16;
typedef unsigned int		u32;
typedef unsigned long long	u64;

enum nhm_uncore_event_id {
	nmh_qhl_request_ioh_reads		=	1,
	nmh_qhl_request_ioh_writes		=	2,
	nmh_qhl_request_remote_reads	=	3,
	nmh_qhl_request_remote_writes	=	4,
	nmh_qhl_request_local_reads		=	5,
	nmh_qhl_request_local_writes	=	6,
	
	nmh_qmc_normal_reads_any		=	7,
	nmh_qmc_writes_full_any			=	8,
	nmh_qmc_writes_partial_any		=	9,

	NHM_UNCORE_EVENT_ID_MAX
};

static u64 nmh_uncore_event_map[NMH_UNCORE_EVENT_ID_MAX] = 
{
	/* Event = 0x20, UMASK = 0xxx */
	[nmh_qhl_request_ioh_reads]		=	0x0120,
	[nmh_qhl_request_ioh_writes]	=	0x0220,
	[nmh_qhl_request_remote_reads]	=	0x0420,
	[nmh_qhl_request_remote_writes]	=	0x0820,
	[nmh_qhl_request_local_reads]	=	0x1020,
	[nmh_qhl_request_local_writes]	=	0x2020,
	
	/* Event = 0x2c, UMASK = 0x07 */
	[nmh_qmc_normal_reads_any]		=	0x072c,
	
	/* Event = 0x2f, UMASK = 0xxx */
	[nmh_qmc_writes_full_any]		=	0x072f,
	[nmh_qmc_writes_partial_any]	=	0x382f,
};





//#################################################
// MODULE PART
//#################################################

static int
pmu_uncore_init(void)
{
	printk(KERN_INFO "INIT PMU_UNCORE\n");
	return 0;
}

static void
pmu_uncore_exit(void)
{
	printk(KERN_INFO "EXIT PMU_UNCORE\n");
	
}

module_init(pmu_uncore_init);
module_exit(pmu_uncore_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shan Yizhou");
