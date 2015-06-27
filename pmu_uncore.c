/*
 *	Description
 *
 *	Intel Uncore Performance Monitoring Module.
 *
 *	Specially, it's designed for Xeon 5600 Series(Westmere).
 *	Westmere is a subset of Nehalem(?).
 *	Nehalem's facilities apply to Westmere.
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

/* Nehalem Perf Control Registers */
#define NHM_UNCORE_PMCO					0x3b0
#define NHM_UNCORE_PERFEVTSEL0			0X3C0

//#################################################
// MODULE PART
//#################################################

static int
pmu_uncore_init(void)
{
	return 0;
}

static void
pmu_uncore_exit(void)
{

}

module_init(pmu_uncore_init);
module_exit(pmu_uncore_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shan Yizhou");
