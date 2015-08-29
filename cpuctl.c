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

#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/cpumask.h>

void _enable_node(void)
{
	int i, j;
	/* Take Node0 down. CPU 6-11, 18-23 */
	for (i = 6, j = 18; i < 12; ) {
		cpu_up(i++);
		cpu_up(j++);
	}
}

void _disable_node(void)
{
	int i, j;
	/* Take Node0 down. CPU 6-11, 18-23 */
	for (i = 6, j = 18; i < 12; ) {
		cpu_down(i++);
		cpu_down(j++);
	}
}

void _no_affinity(void)
{
	struct task_struct *p;
	printk(KERN_INFO"NO_SET_AFFINITY TASKS\N");
	for_each_process(p) {
		if (p->flags && PF_NO_SETAFFINITY)
			printk(KERN_INFO"\tPID: %d\n", p->pid);
	}
}

void __main(void)
{
	struct task_struct *task;
	struct cpumask new_mask;
	int i, cpu = 0;
	
	for_each_process(task) {
		//do_set_cpus_allowed(task, &new_mask);
		if (task->nr_cpus_allowed != 24) {
			printk(KERN_INFO"\nPID: %d CPU_ALLOWED: %d MASK: %llx %lld",
				task->pid, task->nr_cpus_allowed,
				task->cpus_allowed.bits[0], task->cpus_allowed.bits[0]);
			for_each_cpu(cpu, &(task->cpus_allowed)) {
				printk(KERN_INFO" %d ", cpu);
			}
			i++;
		}
	}
	printk(KERN_INFO"TOTAL: %d\n", i);
}

void print_mask(void)
{
	printk(KERN_INFO"%llx  %llx\n", cpu_online_mask->bits[0], cpu_online_mask->bits[1]);
	printk(KERN_INFO"%llx  %llx\n", cpu_possible_mask->bits[0], cpu_possible_mask->bits[1]);
	printk(KERN_INFO"%llx  %llx\n", cpu_active_mask->bits[0], cpu_active_mask->bits[1]);
	printk(KERN_INFO"%llx  %llx\n", cpu_present_mask->bits[0], cpu_present_mask->bits[1]);
}

int affinity_init(void)
{
	printk(KERN_INFO "affinity init\n");

	//_no_affinity();
	//__main();
	_disable_node();
	cpu_up(6);

	return 0;
}

void affinity_exit(void)
{
	printk(KERN_INFO "affinity exit\n\n");
}

module_init(affinity_init);
module_exit(affinity_exit);
MODULE_LICENSE("GPL");
