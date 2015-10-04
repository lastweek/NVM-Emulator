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

#include "uncore_pmu.h"

/* HSWEP Uncore Per-Socket MSRs */
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

const struct uncore_box_ops HSWEP_UNCORE_UBOX_OPS = {
	.init_box	= hswep_uncore_msr_init_box,
	.enable_box	= hswep_uncore_msr_enable_box,
	.disable_box	= hswep_uncore_msr_disable_box,
	.enable_box	= hswep_uncore_msr_enable_event,
	.disable_box	= hswep_uncore_msr_disable_event
};

const struct uncore_box_ops HSWEP_UNCORE_PCUBOX_OPS = {
	.init_box	= hswep_uncore_msr_init_box,
	.enable_box	= hswep_uncore_msr_enable_box,
	.disable_box	= hswep_uncore_msr_disable_box,
	.enable_box	= hswep_uncore_msr_enable_event,
	.disable_box	= hswep_uncore_msr_disable_event
};

const struct uncore_box_ops HSWEP_UNCORE_SBOX_OPS = {

};

const struct uncore_box_ops HSWEP_UNCORE_CBOX_OPS = {

};

struct uncore_box_type HSWEP_UNCORE_UBOX = {
	.name		= "U-BOX";
	.num_counters	= 2,
	.num_boxes	= 1,
	.perf_ctr_bits	= 48,
	.perf_ctr	= HSWEP_MSR_U_PMON_CTR0,
	.perf_ctl	= HSWEP_MSR_U_PMON_EVNTSEL0,
	.event_mask	= 0,
	.fixed_ctr_bits	= 48,
	.fixed_ctr	= HSWEP_MSR_U_PMON_UCLK_FIXED_CTR,
	.fixed_ctl	= HSWEP_MSR_U_PMON_UCLK_FIXED_CTL,
	.ops		= HSWEP_UNCORE_UBOX_OPS
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
	.ops		= HSWEP_UNCORE_PCUBOX_OPS
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
	.ops		= HSWEP_UNCORE_SBOX_OPS
};

struct uncore_box_type HSWEP_UNCORE_CBOX = {
	.name		= "C-BOX",
	.num_counters	= 4,
	.num_boxes,	= 18,
	.perf_ctr_bits	= 48,
	.perf_ctr	= HSWEP_MSR_C_PMON_CTR0,
	.perf_ctl	= HSWEP_MSR_C_PMON_EVNTSEL0,
	.event_mask	= 0,
	.box_ctl	= HSWEP_MSR_C_PMON_BOX_CTL,
	.box_status	= HSWEP_MSR_C_PMON_BOX_STATUS,
	.msr_offset	= HSWEP_MSR_C_MSR_OFFSET,
	.ops		= HSWEP_UNCORE_CBOX_OPS
};

/* Boxes manipulated via MSR registers */
struct uncore_box_type *HSWEP_UNCORE_MSR_BOXES[] = {
	&HSWEP_UNCORE_UBOX,
	&HSWEP_UNCORE_PCUBOX,
	&HSWEP_UNCORE_SBOX,
	&HSWEP_UNCORE_CBOX,
	NULL
};

/* Boxes manipulated via PCI config area */
struct uncore_box_type *HSWEP_UNCORE_PCI_BOXES[] = {
	
};
