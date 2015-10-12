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
#include <linux/types.h>
#include <linux/proc_fs.h>

static int pmu_proc_show(struct seq_file *file, void *v)
{
	seq_printf(file, "UNCORE PMU %d", 2333);
	
	return 0;
}

static int uncore_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmu_proc_show, NULL);
}

const struct file_operations uncore_proc_fops = {
	.open		= uncore_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release
};

static bool is_proc_registed = false;

void uncore_proc_create(void)
{
	if (proc_create("uncore", 0, NULL, &uncore_proc_fops))
		is_proc_registed = true;
}

void uncore_proc_remove(void)
{
	if (is_proc_registed)
		remove_proc_entry("uncore", NULL);
}
