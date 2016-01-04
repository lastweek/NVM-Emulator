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

#include "uncore_pmu.h"

#include <linux/delay.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

extern struct uncore_event ha_requests_local_reads;

u64 emulate_nvm_hrtimer_duration;
unsigned int emulate_nvm_cpu;
static bool latency_started = false;

struct uncore_box *HA_Box_0, *HA_Box_1;
struct uncore_box *U_Box_0, *U_Box_1;
struct uncore_event *event;

/*
 *
 */
static void emulate_nvm_func(void *info)
{
	u64 delay_ns = *(u64 *)info;

	pr_info("on cpu %d, delay_ns=%llu", smp_processor_id(), delay_ns);
}

/*
 *
 */
static u64 counts_to_delay_ns(u64 counts)
{
	u64 delay_ns;

	delay_ns = counts;

	return delay_ns;
}

static enum hrtimer_restart emulate_nvm_hrtimer(struct hrtimer *hrtimer)
{
	struct uncore_box *box;
	u64 counts, delay_ns;
	
	box = container_of(hrtimer, struct uncore_box, hrtimer);
	
	/* Step I:
	 * a) Freeze counter
	 * b) Read counter
	 */
	uncore_disable_box(box);
	uncore_read_counter(box, &counts);

	/* Step II:
	 * a) Translate counts to real additional delay
	 * b) Send delay function to remote emulating cpu
	 */
	delay_ns = counts_to_delay_ns(counts);
	smp_call_function_single(emulate_nvm_cpu, emulate_nvm_func, &delay_ns, 1);

	/* Step III:
	 * a) Clear counter
	 * b) Enable counting
	 */
	uncore_write_counter(box, 0);
	uncore_enable_box(box);
	
	uncore_show_box(box);

	hrtimer_forward_now(hrtimer, ns_to_ktime(box->hrtimer_duration));

	return HRTIMER_RESTART;
}

static void start_emulate_latency(void)
{
	/*
	 * Home Agent: (Box0, Node0), (Box0, Node1)
	 */
	HA_Box_0 = uncore_get_first_box(uncore_pci_type[UNCORE_PCI_HA_ID], 0);
	HA_Box_1 = uncore_get_first_box(uncore_pci_type[UNCORE_PCI_HA_ID], 1);
	if (!HA_Box_0 || !HA_Box_1) {
		pr_err("Get HA Box Failed");
		return;
	}
	
	/* XXX
	 * U-Box: (Box0, Node0), (Box0, Node1)
	 */
	U_Box_0 = uncore_get_first_box(uncore_msr_type[UNCORE_MSR_UBOX_ID], 0);
	if (!U_Box_0) {
		pr_err("Get UBox Failed");
		return;
	}

	event = &ha_requests_local_reads;
	uncore_box_bind_event(HA_Box_0, event);
	uncore_box_bind_event(HA_Box_1, event);

	/*
	 * a) Init and reset box
	 */
	uncore_init_box(HA_Box_0);
	uncore_init_box(HA_Box_1);
	
	/*
	 * b) Freeze counter
	 */
	uncore_disable_box(HA_Box_0);
	uncore_disable_box(HA_Box_1);

	/*
	 * c) Set and enable event
	 */
	uncore_enable_event(HA_Box_0, event);
	uncore_enable_event(HA_Box_1, event);
	
	/*
	 * d) Un-Freeze, start counting
	 */
	uncore_enable_box(HA_Box_0);
	uncore_enable_box(HA_Box_1);
	
	/*
	 * In emulating latency part, the most important thing
	 * is replacing the original hrtimer function. The original 
	 * one just collect counts and in case counter overflows.
	 * But here, we rely on our hrtimer function to send IPI
	 * to the emulating core, to emulate the slow read latency
	 * of NVM. Not so hard, huh?
	 */
	uncore_box_change_hrtimer(HA_Box_0, emulate_nvm_hrtimer);
	uncore_box_change_hrtimer(HA_Box_1, emulate_nvm_hrtimer);
	uncore_box_change_duration(HA_Box_0, emulate_nvm_hrtimer_duration);
	uncore_box_change_duration(HA_Box_1, emulate_nvm_hrtimer_duration);
	uncore_box_start_hrtimer(HA_Box_0);
	uncore_box_start_hrtimer(HA_Box_1);

	latency_started = true;
}

static void finish_emulate_latency(void)
{
	if (latency_started) {
		/*
		 * Cancel hrtimer
		 */
		uncore_box_cancel_hrtimer(HA_Box_0);
		uncore_box_cancel_hrtimer(HA_Box_1);

		/*
		 * Show some information, if you wanna
		 */
		uncore_disable_box(HA_Box_0);
		uncore_show_box(HA_Box_0);
		uncore_disable_box(HA_Box_1);
		uncore_show_box(HA_Box_1);
		uncore_print_global_pmu(&uncore_pmu);

		/*
		 * Clear these boxes and exit
		 */
		uncore_clear_box(HA_Box_0);
		uncore_clear_box(HA_Box_1);

		latency_started = false;
	}
}

static void start_emulate_bandwidth(void)
{
	/*
	 * Default to full bandwidth
	 */
	uncore_imc_set_threshold_all(1);

	/*
	 * Enable throttling at all nodes
	 */
	uncore_imc_enable_throttle_all();
}

static void finish_emulate_bandwidth(void)
{
	/*
	 * Disable throttling at all nodes
	 */
	uncore_imc_disable_throttle_all();
}

void start_emulate_nvm(void)
{
	emulate_nvm_hrtimer_duration	= 1000000 * 500;
	emulate_nvm_cpu			= 0;

	start_emulate_bandwidth();
	start_emulate_latency();
}

void finish_emulate_nvm(void)
{
	finish_emulate_bandwidth();
	finish_emulate_latency();
}
