/**
 *	Description
 *
 *	Intel Uncore Performance Monitoring Module.
 *
 *	Specially, designed for Intel Xeon Processor 5600 Series(Westmere).
 *	Westmere is a subset of Nehalem. Nehalem's facilities apply to Westmere.
 *	(Supported MSRs listed in Table 35-10, Table 35-11, Table 35-13 in SDM.)
 **/

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

#define MSR_UNCORE_PERF_GLOBAL_CTRL		0x391
#define MSR_UNCORE_PERF_GLOBAL_STATUS	0x392
#define MSR_UNCORE_PERF_GLOBAL_OVF_CTRL	0x393

#define MSR_UNCORE_PMCO		0x3b0
#define MSR_UNCORE_PMC1		0x3b1
#define MSR_UNCORE_PMC2		0x3b2

typedef unsigned char		u8;
typedef unsigned short		u16;
typedef unsigned int		u32;
typedef unsigned long long	u64;


//#################################################
// MODULE PART
//#################################################

static int
pmu_init(void)
{
	return 0;
}

static void
pmu_exit(void)
{

}

module_init(pmu_init);
module_exit(pmu_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shan Yizhou");
