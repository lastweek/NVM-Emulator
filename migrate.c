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

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hrtimer.h>

static unsigned long migrate_threshold;
static unsigned long timer_interval_ns;

void set_migrate_threshold(unsigned long new)
{
	migrate_threshold = new;
}

void set_timer_interval_ns(unsigned long new)
{
	timer_interval_ns = new;
}

/*
 * restart function, do things here.. 
 */
static enum hrtimer_restart hrtimer_def(struct hrtimer *hrtimer)
{
	pr_info("Hello");

	hrtimer_forward_now(hrtimer, ns_to_ktime(timer_interval_ns));
	return HRTIMER_RESTART;
}

static void migrate_init_hrtimer(struct hrtimer *timer)
{
	hrtimer_init(timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	timer->function = hrtimer_def;
}

static void migrate_start_hrtimer(struct hrtimer *timer)
{
	/* time is relative to now, and is bound to cpu */
	hrtimer_start(timer, ns_to_ktime(timer_interval_ns), HRTIMER_MODE_REL);
}

static void migrate_cancel_hrtimer(struct hrtimer *timer)
{
	hrtimer_cancel(timer);
}

struct hrtimer migrate_hrtimer;

static int pm_init(void)
{
	timer_interval_ns = 1000000;
	migrate_threshold = 1000;

	migrate_init_hrtimer(&migrate_hrtimer);
	migrate_start_hrtimer(&migrate_hrtimer);

	return 0;
}

static void pm_exit(void)
{
	migrate_cancel_hrtimer(&migrate_hrtimer);
}

module_init(pm_init);
module_exit(pm_exit);
