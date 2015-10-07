/*
 *	Copyright (C) 2015 Yizhou Shan <shanyizhou@ict.ac.cn>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License along
 *	with this program; if not, write to the Free Software Foundation, Inc.,
 *	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * Support Xeon:
 * O	Platform:		Xeon® E5 v3 and Xeon® E7 v3
 *	Microarchitecture:	Haswell-EP, Haswell-EX
 *
 *
 * Precious Xeon:
 * O	Platform:		Xeon® E5 v2 and Xeon® E7 v2
 *	Microarchitecture:	Ivy Bridge-EP, Ivy Bridge-EX
 *
 * O	Platform:		Xeon® E5 v1 and Xeon® E7 v1
 *	Microarchitecture:	Sandy Bridge-EP, Westmere-EX
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/percpu.h>
#include <linux/string.h>
#include <linux/sched.h>
#include "uncore_pmu.h"

/* HSWEP Box-Level Control MSR Bit Layout */
#define HSWEP_MSR_BOX_CTL_RST_CTRL		(1 << 0)
#define HSWEP_MSR_BOX_CTL_RST_CTRS		(1 << 1)
#define HSWEP_MSR_BOX_CTL_FRZ			(1 << 8)
#define HSWEP_MSR_BOX_CTL_INIT			(HSWEP_MSR_BOX_CTL_RST_CTRL | \
						 HSWEP_MSR_BOX_CTL_RST_CTRS )

/* HSWEP Event Select MSR Bit Layout */
#define HSWEP_MSR_EVNTSEL_EVENT			0x000000FF
#define HSWEP_MSR_EVNTSEL_UMASK			0x0000FF00
#define HSWEP_MSR_EVNTSEL_RST			(1 << 17)
#define HSWEP_MSR_EVNTSEL_EDGE_DET		(1 << 18)
#define HSWEP_MSR_EVNTSEL_TID_EN		(1 << 19)
#define HSWEP_MSR_EVNTSEL_EN			(1 << 22)
#define HSWEP_MSR_EVNTSEL_INVERT		(1 << 23)
#define HSWEP_MSR_EVNTSEL_THRESHOLD		0xFF000000
#define HSWEP_MSR_RAW_EVNTSEL_MASK		(HSWEP_MSR_EVNTSEL_EVENT	| \
						 HSWEP_MSR_EVNTSEL_UMASK	| \
						 HSWEP_MSR_EVNTSEL_EDGE_DET	| \
						 HSWEP_MSR_EVNTSEL_INVERT	| \
						 HSWEP_MSR_EVNTSEL_THRESHOLD)

/* HSWEP Uncore Global Per-Socket MSRs */
#define HSWEP_MSR_PMON_GLOBAL_CTL		0x700
#define HSWEP_MSR_PMON_GLOBAL_STATUS		0x701
#define HSWEP_MSR_PMON_GLOBAL_CONFIG		0x702

/* HSWEP Uncore U-box */
#define HSWEP_MSR_U_PMON_BOX_STATUS		0x708
#define HSWEP_MSR_U_PMON_UCLK_FIXED_CTL		0x703
#define HSWEP_MSR_U_PMON_UCLK_FIXED_CTR		0x704
#define HSWEP_MSR_U_PMON_EVNTSEL0		0x705
#define HSWEP_MSR_U_PMON_CTR0			0x709

/* HSWEP Uncore PCU-box */
#define HSWEP_MSR_PCU_PMON_BOX_CTL		0x710
#define HSWEP_MSR_PCU_PMON_BOX_FILTER		0x715
#define HSWEP_MSR_PCU_PMON_BOX_STATUS		0x716
#define HSWEP_MSR_PCU_PMON_EVNTSEL0		0x711
#define HSWEP_MSR_PCU_PMON_CTR0			0x717

/* HSWEP Uncore S-box */
#define HSWEP_MSR_S_PMON_BOX_CTL		0x720
#define HSWEP_MSR_S_PMON_BOX_FILTER		0x725
#define HSWEP_MSR_S_PMON_EVNTSEL0		0x721
#define HSWEP_MSR_S_PMON_CTR0			0x726
#define HSWEP_MSR_S_MSR_OFFSET			0xa

/* HSWEP Uncore C-box */
#define HSWEP_MSR_C_PMON_BOX_CTL		0xE00
#define HSWEP_MSR_C_PMON_BOX_FILTER0		0xE05
#define HSWEP_MSR_C_PMON_BOX_FILTER1		0xE06
#define HSWEP_MSR_C_PMON_BOX_STATUS		0xE07
#define HSWEP_MSR_C_PMON_EVNTSEL0		0xE01
#define HSWEP_MSR_C_PMON_CTR0			0xE08
#define HSWEP_MSR_C_MSR_OFFSET			0x10
#define HSWEP_MSR_C_EVENTSEL_MASK		(HSWEP_MSR_RAW_EVNTSEL_MASK | \
						 HSWEP_MSR_EVNTSEL_TID_EN)

static void hswep_uncore_msr_init_box(struct uncore_box *box)
{
	unsigned int msr;

	msr = uncore_msr_box_ctl(box);
	if (msr)
		wrmsrl(msr, HSWEP_MSR_BOX_CTL_INIT);
}

static void hswep_uncore_msr_enable_box(struct uncore_box *box)
{
	unsigned long long config;
	unsigned int msr;

	msr = uncore_msr_box_ctl(box);
	if (msr) {
		rdmsrl(msr, config);
		config &= ~HSWEP_MSR_BOX_CTL_FRZ;
		wrmsrl(msr, config);
	}
}

static void hswep_uncore_msr_disable_box(struct uncore_box *box)
{
	unsigned long long config;
	unsigned int msr;

	msr = uncore_msr_box_ctl(box);
	if (msr) {
		rdmsrl(msr, config);
		config |= HSWEP_MSR_BOX_CTL_FRZ;
		wrmsrl(msr, config);
	}
}

static void hswep_uncore_msr_enable_event(struct uncore_box *box,
					struct uncore_event *event)
{
	wrmsrl(event->ctl, event->disable);
}

static void hswep_uncore_msr_disable_event(struct uncore_box *box,
					struct uncore_event *event)
{
	wrmsrl(event->ctl, event->enable | HSWEP_MSR_EVNTSEL_EN);
}

const struct uncore_box_ops HSWEP_UNCORE_UBOX_OPS = {
	.init_box	= hswep_uncore_msr_init_box,
	.enable_box	= hswep_uncore_msr_enable_box,
	.disable_box	= hswep_uncore_msr_disable_box,
	.enable_event	= hswep_uncore_msr_enable_event,
	.disable_event	= hswep_uncore_msr_disable_event
};

const struct uncore_box_ops HSWEP_UNCORE_PCUBOX_OPS = {
	.init_box	= hswep_uncore_msr_init_box,
	.enable_box	= hswep_uncore_msr_enable_box,
	.disable_box	= hswep_uncore_msr_disable_box,
	.enable_event	= hswep_uncore_msr_enable_event,
	.disable_event	= hswep_uncore_msr_disable_event
};

struct uncore_box_ops HSWEP_UNCORE_SBOX_OPS = {
	.init_box	= hswep_uncore_msr_init_box,
	.enable_box	= hswep_uncore_msr_enable_box,
	.disable_box	= hswep_uncore_msr_disable_box,
	.enable_event	= hswep_uncore_msr_enable_event,
	.disable_event	= hswep_uncore_msr_disable_event
};

struct uncore_box_ops HSWEP_UNCORE_CBOX_OPS = {
	.init_box	= hswep_uncore_msr_init_box,
	.enable_box	= hswep_uncore_msr_enable_box,
	.disable_box	= hswep_uncore_msr_disable_box,
	.enable_event	= hswep_uncore_msr_enable_event,
	.disable_event	= hswep_uncore_msr_disable_event
};

struct uncore_box_type HSWEP_UNCORE_UBOX = {
	.name		= "U-BOX",
	.num_counters	= 2,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.perf_ctr	= HSWEP_MSR_U_PMON_CTR0,
	.perf_ctl	= HSWEP_MSR_U_PMON_EVNTSEL0,
	.event_mask	= 0,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= HSWEP_MSR_U_PMON_UCLK_FIXED_CTR,
	.fixed_ctl	= HSWEP_MSR_U_PMON_UCLK_FIXED_CTL,
	.ops		= &HSWEP_UNCORE_UBOX_OPS
};

struct uncore_box_type HSWEP_UNCORE_PCUBOX = {
	.name		= "PCU-BOX",
	.num_counters	= 4,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.perf_ctr	= HSWEP_MSR_PCU_PMON_CTR0,
	.perf_ctl	= HSWEP_MSR_PCU_PMON_EVNTSEL0,
	.event_mask	= 0,
	.box_ctl	= HSWEP_MSR_PCU_PMON_BOX_CTL,
	.box_status	= HSWEP_MSR_PCU_PMON_BOX_STATUS,
	.ops		= &HSWEP_UNCORE_PCUBOX_OPS
};

struct uncore_box_type HSWEP_UNCORE_SBOX = {
	.name		= "S-BOX",
	.num_counters	= 4,
	.num_boxes	= 4,
	.perf_ctr_bits	= 48,
	.perf_ctr	= HSWEP_MSR_S_PMON_CTR0,
	.perf_ctl	= HSWEP_MSR_S_PMON_EVNTSEL0,
	.event_mask	= 0,
	.box_ctl	= HSWEP_MSR_S_PMON_BOX_CTL,
	.msr_offset	= HSWEP_MSR_S_MSR_OFFSET,
	.ops		= &HSWEP_UNCORE_SBOX_OPS
};

struct uncore_box_type HSWEP_UNCORE_CBOX = {
	.name		= "C-BOX",
	.num_counters	= 4,
	.num_boxes	= 18,
	.perf_ctr_bits	= 48,
	.perf_ctr	= HSWEP_MSR_C_PMON_CTR0,
	.perf_ctl	= HSWEP_MSR_C_PMON_EVNTSEL0,
	.event_mask	= HSWEP_MSR_C_EVENTSEL_MASK,
	.box_ctl	= HSWEP_MSR_C_PMON_BOX_CTL,
	.box_status	= HSWEP_MSR_C_PMON_BOX_STATUS,
	.msr_offset	= HSWEP_MSR_C_MSR_OFFSET,
	.ops		= &HSWEP_UNCORE_CBOX_OPS
};

/* MSR Boxes */
struct uncore_box_type *HSWEP_UNCORE_MSR_BOXES[] = {
	&HSWEP_UNCORE_UBOX,
	&HSWEP_UNCORE_PCUBOX,
	&HSWEP_UNCORE_SBOX,
	&HSWEP_UNCORE_CBOX,
	NULL
};

/* PCI Boxes */
struct uncore_box_type *HSWEP_UNCORE_PCI_BOXES[] = {
	
};

static void hswep_cpu_init(void)
{

}

static void hswep_pci_init(void)
{

}

static void uncore_event_show(struct uncore_event *event)
{
	unsigned long long v1, v2;

	if (!event | !event->ctl | !event->ctr)
		return;
	
	rdmsrl(event->ctl, v1);
	rdmsrl(event->ctr, v2);
	printk(KERN_INFO "SEL=%llx CNT=%llx", v1, v2);
}

static int hswep_init(void)
{
	struct uncore_box cbox = {
		.idx = 0,
		.name = "C0",
		.box_type = &HSWEP_UNCORE_CBOX
	};

	struct uncore_event event = {
		.ctl = 0xe01,
		.ctr = 0xe08,
		.enable = (1<<22) | 0x0000 | 0x0000,
		.disable = 0
	};
	
	uncore_event_show(&event);

	uncore_init_box(&cbox);
	uncore_enable_box(&cbox);
	uncore_enable_event(&cbox, &event);
	udelay(100);
	uncore_disable_event(&cbox, &event);
	uncore_disable_box(&cbox);

	uncore_event_show(&event);
	
	return 0;
}

static void hswep_exit(void)
{
	pr_info("exit");
}

module_init(hswep_init);
module_exit(hswep_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("shanyizhou@ict.ac.cn");
