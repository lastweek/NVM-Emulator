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

#define pr_fmt(fmt) "UNCORE PMU: " fmt

#include "uncore_pmu.h"

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

/* CPU-Independent Data */
struct uncore_box_type *dummy_xxx_type[] = { NULL, };
struct uncore_box_type **uncore_msr_type = dummy_xxx_type;
struct uncore_box_type **uncore_pci_type = dummy_xxx_type;

struct pci_driver *uncore_pci_driver;
static bool pci_driver_registered = false;

static void uncore_event_show(struct uncore_event *event)
{
	unsigned long long v1, v2;

	if (!event | !event->ctl | !event->ctr)
		return;
	
	rdmsrl(event->ctl, v1);
	rdmsrl(event->ctr, v2);
	printk(KERN_INFO "SEL=%llx CNT=%llx", v1, v2);
}

__unused static int
uncore_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct uncore_box_type *type;
	type = uncore_pci_type[UNCORE_PCI_DEV_TYPE(id->driver_data)];
	if (!type)
		pr_info("pci_probe %s", type->name);
	return -1;
}

__unused static void
uncore_pci_remove(struct pci_dev *dev)
{}

static void uncore_types_init(struct uncore_box_type **types)
{
	int i;

	for (i = 0; types[i] != NULL; i++) {
		INIT_LIST_HEAD(&types[i]->box_list);
	}
}

/**
 * uncore_pci_new_box
 * @pdev:	the pci device of this box
 * @id:		the device id of this box
 *
 * Malloc a new box, and then insert into box_list of its box type.
 */
static int uncore_pci_new_box(struct pci_dev *pdev, struct pci_dev_id *id)
{
	struct uncore_box_type *type;
	struct uncore_box *box, *last;

	box = kzalloc(sizeof(struct uncore_box), GFP_KERNEL);
	if (!box)
		return -ENOMEM;
	
	type = uncore_pci_type[UNCORE_PCI_DEV_TYPE(id->driver_data)];
	if (!type) {
		kfree(box);
		return -EFAULT;
	}

	if (list_empty(&type->box_list))
		box->idx = 0;
	else {
		last = list_last_entry(&type->box_list, struct uncore_box, next);
		box->idx = last->idx + 1;
	}
	
	box->box_type = type;
	box->pdev = pdev;
	list_add_tail(&box->next, &type->box_list);
	
	return 0;
}

static void uncore_pci_init(void)
{
	int ret;
	struct pci_device_id *ids;
	struct pci_dev *pdev;

	hswep_pci_init();
	uncore_types_init(uncore_pci_type);

	ids = uncore_pci_driver->id_table;
	if (!ids)
		goto error;

	while (ids->vendor && ids->device) {
		pdev = pci_get_device(ids->vendor, ids->device, NULL);
		if (pdev)
			uncore_pci_new_box(pdev, ids);
		ids++;
	}

	/* Driver methods, never being called */
	uncore_pci_driver->probe = uncore_pci_probe;
	uncore_pci_driver->remove = uncore_pci_remove;

error:
	
}

static void uncore_cpu_init(void)
{
	hswep_cpu_init();
}

static int uncore_init(void)
{
	pr_info("init");
	uncore_cpu_init();
	uncore_pci_init();
/*
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
*/	
	return 0;
}

static void uncore_exit(void)
{
	pr_info("exit");

	if (pci_driver_registered)
		pci_unregister_driver(uncore_pci_driver);
}

module_init(uncore_init);
module_exit(uncore_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("shanyizhou@ict.ac.cn");
