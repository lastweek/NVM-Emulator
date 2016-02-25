/*
 *	Copyright (C) 2015-2016 Yizhou Shan <shanyizhou@ict.ac.cn>
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

#define pr_fmt(fmt) fmt

#include "uncore_pmu.h"
#include "emulate_nvm.h"

#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

/* TODO more general. */
extern struct uncore_event ha_requests_local_reads;
extern struct uncore_event ha_requests_remote_reads;

/* Latency model */
u64 dram_read_latency_ns;
u64 nvm_read_latency_ns;
u64 read_latency_delta_ns;

unsigned int polling_cpu;
unsigned int polling_node;
unsigned int emulate_nvm_cpu;
unsigned int emulate_nvm_node;

u64 emulate_nvm_hrtimer_duration_ns;

u64 hrtimer_jiffies;

static bool emulation_started = false;
static bool latency_started = false;
static struct uncore_box *HA_Box_0, *HA_Box_1;
static struct uncore_event *event;

/*
 * Hmm, this is the 'ultimate' emulating function. It is executed in the
 * emulating cpu core. The parameter is the nanoseconds to _waste_. You can do
 * anything in this interval... A delay function is the most lighweight one.
 * 
 * We are talking about interrupting a normal running program. But, no flush
 * overhead of cache/tlb/register are considered. It is hard to evaluate how
 * these overhead impact whole system throughput. Anyway, whatever la.
 */
static void emulate_nvm_func(void *info)
{
	u64 delay_ns = *(u64 *)info;
	udelay(delay_ns / 1000);
}

/*
 * Hmm, this depends on the emulating model. Anyone who even knows a little
 * about computer architecture should know this model sucks. No modern processor
 * would wait the entire memory read transaction, even read is on the critical
 * path. Why I still do this? I can not tell you why. Sigh.
 */
static inline u64 counts_to_delay_ns(u64 counts)
{
	return (counts * read_latency_delta_ns);
}

extern u64 proc_counts;

static enum hrtimer_restart emulate_nvm_hrtimer(struct hrtimer *hrtimer)
{
	struct uncore_box *box;
	u64 counts, delay_ns;
	
	box = container_of(hrtimer, struct uncore_box, hrtimer);
	
	/*
	 * Step I:
	 * a) Freeze counter
	 * b) Read counter
	 */
	uncore_disable_box(box);
	uncore_read_counter(box, &counts);
	proc_counts = counts;

	/*
	 * Step II:
	 * a) Translate counts to real additional delay
	 * b) Send delay function to remote emulating cpu
	 */
	delay_ns = counts_to_delay_ns(counts);
	smp_call_function_single(emulate_nvm_cpu, emulate_nvm_func, &delay_ns, 1);

	#ifdef verbose
	pr_info("on cpu %d, delay_ns=%llu, udelay=%llu", smp_processor_id(), delay_ns,
			delay_ns/1000);
	uncore_show_box(box);
	#endif

	/*
	 * Step III:
	 * a) Clear counter
	 * b) Enable counting
	 */
	uncore_write_counter(box, 0);
	uncore_enable_box(box);

	hrtimer_jiffies++;

	hrtimer_forward_now(hrtimer, ns_to_ktime(box->hrtimer_duration));
	return HRTIMER_RESTART;
}

static int start_emulate_latency(void)
{
	/*
	 * Home Agent: (Box0, Node0), (Box0, Node1)
	 */
	HA_Box_0 = uncore_get_first_box(uncore_pci_type[UNCORE_PCI_HA_ID], 0);
	HA_Box_1 = uncore_get_first_box(uncore_pci_type[UNCORE_PCI_HA_ID], 1);
	if (!HA_Box_0 || !HA_Box_1) {
		pr_err("Get HA Box Failed");
		return -ENXIO;
	}
	
	event = &ha_requests_remote_reads;
	uncore_box_bind_event(HA_Box_1, event);

	/*
	 * a) Init and reset box
	 * b) Freeze counter
	 * c) Set and enable event
	 * d) Un-Freeze, start counting
	 */
	uncore_init_box(HA_Box_1);
	uncore_disable_box(HA_Box_1);
	uncore_enable_event(HA_Box_1, event);
	uncore_enable_box(HA_Box_1);
	
	/*
	 * In emulating latency part, the most important thing
	 * is replacing the original hrtimer function. The original 
	 * one just collect counts and in case counter overflows.
	 * But here, we rely on our hrtimer function to send IPI
	 * to the emulating core, to emulate the slow read latency
	 * of NVM. Not so hard, huh?
	 */
	uncore_box_change_hrtimer(HA_Box_1, emulate_nvm_hrtimer);
	uncore_box_change_duration(HA_Box_1, emulate_nvm_hrtimer_duration_ns);
	uncore_box_start_hrtimer(HA_Box_1);

	latency_started = true;

	return 0;
}

static void finish_emulate_latency(void)
{
	if (latency_started) {
		/* cancel hrtimer */
		uncore_box_cancel_hrtimer(HA_Box_0);
		uncore_box_cancel_hrtimer(HA_Box_1);

		/* show some information, if you wanna */
		uncore_disable_box(HA_Box_0);
		uncore_show_box(HA_Box_0);
		uncore_disable_box(HA_Box_1);
		uncore_show_box(HA_Box_1);
		uncore_print_global_pmu(&uncore_pmu);

		/* clear these boxes and exit */
		uncore_clear_box(HA_Box_0);
		uncore_clear_box(HA_Box_1);

		latency_started = false;
	}
}

static int start_emulate_bandwidth(void)
{
	/* default to full bandwidth */
	uncore_imc_set_threshold_all(1);

	/* enable throttling at all nodes */
	uncore_imc_enable_throttle_all();

	return 0;
}

static void finish_emulate_bandwidth(void)
{
	/* disable throttling at all nodes */
	uncore_imc_disable_throttle_all();
}

void show_emulate_parameter(void)
{
	pr_info("------------------------ Emulation Parameters ----------------------");
	pr_info("Hrtimer Duration: %llu ns (%llu ms)\n", emulate_nvm_hrtimer_duration_ns,
		emulate_nvm_hrtimer_duration_ns/1000000);
	pr_info("Polling CPU:  CPU%2d (Node %2d)", polling_cpu, polling_node);
	pr_info("Emulated CPU: CPU%2d (Node %2d)", emulate_nvm_cpu, emulate_nvm_node);
	
	pr_info("Latency Model:");
	pr_info("\t---------------------");
	pr_info("\t|_______| Read (ns) |");
	pr_info("\t| NVM   |    %3llu    |", nvm_read_latency_ns);
	pr_info("\t| DRAM  |    %3llu    |", dram_read_latency_ns);
	pr_info("\t| Delta |    %3llu    |", read_latency_delta_ns);
	pr_info("\t---------------------");
	pr_info("------------------------ Emulation Parameters ----------------------");
}

static int prepare_platform_configuration(void)
{
	int cpu;
	const struct cpumask *mask;
	
	cpu = smp_processor_id();
	if (cpu != polling_cpu) {
		pr_info("\033[31mERROR: current cpu is %2d, not polling cpu %2d\033[0m",
			cpu, polling_cpu);
		return -1;
	}

	/*
 	 * In hybrid-memory configuration model, CPUs except the
	 * emulating one must be offlined. We have to do this because
	 * the 'ha_requests_remote_reads' event can _not_ distinguish
	 * requests from different cpus. To gain a 'best' emulation model,
	 * only the emulating cpu can alive!
 	 */
	mask = cpumask_of_node(emulate_nvm_node);
	for_each_cpu(cpu, mask) {
		if (cpu != emulate_nvm_cpu)
			cpu_down(cpu);
	}

	/*
	 * Hmm, this could be a little strict. All CPUs of polling node
	 * except the polling cpu should be offlined, too. It is OK if
	 * they are still online, however...
	 */
	mask = cpumask_of_node(polling_node);
	for_each_cpu(cpu, mask) {
		if (cpu != polling_cpu)
			cpu_down(cpu);
	}

	return 0;
}

static void restore_platform_configuration(void)
{
	int cpu;
	const struct cpumask *mask;

	mask = cpumask_of_node(emulate_nvm_node);
	for_each_cpu_not(cpu, mask) {
		if (cpu != emulate_nvm_cpu)
			cpu_up(cpu);
	}
	
	mask = cpumask_of_node(polling_node);
	for_each_cpu(cpu, mask) {
		if (cpu != polling_cpu)
			cpu_up(cpu);
	}
}

#define pr_fail()	printk(KERN_CONT "\033[31m fail \033[0m")
#define pr_okay()	printk(KERN_CONT "\033[32m okay \033[0m")

#define pr_result(ret)		\
do {				\
	if (ret) {		\
		pr_fail();	\
		return;		\
	}			\
	pr_okay();		\
} while (0)

void start_emulate_nvm(void)
{
	int ret;

	/*
	 * Memory Latency Model
	 */
	dram_read_latency_ns  = 100;
	nvm_read_latency_ns   = 300;
	read_latency_delta_ns = 200;

	/*
	 * Polling CPU is the one always polling uncore pmu
	 * and sending IPI delay function to emulate_nvm_cpu.
	 *
	 * Emulate NVM CPU is the one used to emulate NVM,
	 * also the receiver of IPI sent from polling cpu.
	 */
	polling_cpu = 6;
	emulate_nvm_cpu = 0;

	polling_node = cpu_to_node(polling_cpu);
	emulate_nvm_node = cpu_to_node(emulate_nvm_cpu);

	/*
	 * Hrtimer Forward Duration (ns)
	 * Default: 100 ms
	 */
	emulate_nvm_hrtimer_duration_ns = 1000000 * 100;

	show_emulate_parameter();

	pr_info("creating /proc/emulate_nvm... ");
	ret = emulate_nvm_proc_create();
	pr_result(ret);

	pr_info("preparing platform... ");
	ret = prepare_platform_configuration();
	pr_result(ret);
	
	pr_info("emulating bandwidth... ");
	ret = start_emulate_bandwidth();
	pr_result(ret);
	
	pr_info("emulating latency... ");
	ret = start_emulate_latency();
	if (ret)
		finish_emulate_bandwidth();
	pr_result(ret);

	emulation_started = true;
}

void finish_emulate_nvm(void)
{
	if (emulation_started) {
		emulate_nvm_proc_remove();
		restore_platform_configuration();
		finish_emulate_bandwidth();
		finish_emulate_latency();
		pr_info("finish emulating nvm... ");
		emulation_started = false;
	}
}
