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

/* CPU Independent Data */
struct uncore_box_type *dummy_xxx_type[] = { NULL, };
struct uncore_box_type **uncore_msr_type = dummy_xxx_type;
struct uncore_box_type **uncore_pci_type = dummy_xxx_type;

/* Not Used */
struct pci_driver *uncore_pci_driver;
static bool pci_driver_registered = false;

static int __always_unused uncore_pci_probe(struct pci_dev *dev,
					    const struct pci_device_id *id)
{
	return -EINVAL;
}

static void __always_unused  uncore_pci_remove(struct pci_dev *dev)
{ }

static void uncore_event_show(struct uncore_event *event)
{
	unsigned long long v1, v2;

	if (!event | !event->ctl | !event->ctr)
		return;
	
	rdmsrl(event->ctl, v1);
	rdmsrl(event->ctr, v2);
	printk(KERN_INFO "SEL=%llx CNT=%llx", v1, v2);
}

/**
 * uncore_types_init
 * @types:	box_type to init
 *
 * Init the array of uncore_box_type. Specially, the list_head.
 * This function should be called *after* CPU-specific init
 * function.
 */
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
 * Return 0 on success, otherwise return error number.
 *
 * Malloc a new box of PCI type, and then insert it into the tail
 * of box_list of its uncore_box_type.
 */
static int __must_check uncore_pci_new_box(struct pci_dev *pdev,
					   const struct pci_device_id *id)
{
	struct uncore_box_type *type;
	struct uncore_box *box, *last;

	type = uncore_pci_type[UNCORE_PCI_DEV_TYPE(id->driver_data)];
	if (!type)
		return -EFAULT;

	box = kzalloc(sizeof(struct uncore_box), GFP_KERNEL);
	if (!box)
		return -ENOMEM;
	
	if (list_empty(&type->box_list)) {
		box->idx = 0;
		type->num_boxes = 1;
	} else {
		last = list_last_entry(&type->box_list, struct uncore_box, next);
		box->idx = last->idx + 1;
		type->num_boxes++;
	}
	
	box->box_type = type;
	box->pdev = pdev;
	list_add_tail(&box->next, &type->box_list);
	
	return 0;
}

/* Free PCI type boxes */
static void uncore_pci_exit(void)
{
	struct uncore_box_type *type;
	struct uncore_box *box;
	struct list_head *head;
	int i;

	for (i = 0; uncore_pci_type[i]; i++) {
		type = uncore_pci_type[i];
		head = &type->box_list;

		while (!list_empty(head)) {
			box = list_first_entry(head, struct uncore_box, next);
			list_del(&box->next);
			
			/* Put PCI device */
			pci_dev_put(box->pdev);
			
			/* Let it go */
			kfree(box);
		}
	}
}

/* Malloc PCI type boxes */
static int __must_check uncore_pci_init(void)
{
	const struct pci_device_id *ids;
	struct pci_dev *pdev;
	int ret;

	hswep_pci_init();
	uncore_types_init(uncore_pci_type);

	ids = uncore_pci_driver->id_table;
	if (!ids)
		return -EFAULT;

	while (ids->vendor || ids->device) {
		pdev = pci_get_device(ids->vendor, ids->device, NULL);
		if (!pdev) {
			/*
			 * DO NOT ABORT!
			 * Not all SKUs support all PCI devices.
			 */
			ids++;
			continue;
		}

		ret = uncore_pci_new_box(pdev, ids);
		if (ret)
			goto error;

		ids++;
	}

	/* Driver methods, never being called */
	uncore_pci_driver->probe = uncore_pci_probe;
	uncore_pci_driver->remove = uncore_pci_remove;
	ret = pci_register_driver(uncore_pci_driver);
	if (ret)
		goto error;
	pci_driver_registered = true;

	return 0;

error:
	uncore_pci_exit();
	return ret;
}

/**
 * uncore_msr_new_box
 * @type:	the MSR box_type
 * @idx:	the idx of the new box
 * Return 0 on success, otherwise return error number
 *
 * Malloc a new box of MSR type, and then insert it into the tail
 * of box_list of its uncore_box_type.
 */
static int __must_check uncore_msr_new_box(struct uncore_box_type *type,
					   unsigned int idx)
{
	struct uncore_box *box;
	int ret;

	if (!type)
		return -EINVAL;

	box = kzalloc(sizeof(struct uncore_box), GFP_KERNEL);
	if (!box)
		return -ENOMEM;

	box->idx = idx;
	box->box_type = type;
	list_add_tail(&box->next, &type->box_list);

	return 0;
}

/* Free MSR type boxes */
static void uncore_cpu_exit(void)
{
	struct uncore_box_type *type;
	struct uncore_box *box;
	struct list_head *head;
	int i;

	for (i = 0; uncore_msr_type[i]; i++) {
		type = uncore_msr_type[i];
		head = &type->box_list;
		while (!list_empty(head)) {
			box = list_first_entry(head, struct uncore_box, next);
			list_del(&box->next);
			kfree(box);
		}
	}
}

/* Malloc MSR type boxes */
static int __must_check uncore_cpu_init(void)
{
	struct uncore_box_type *type;
	int n, idx, ret;

	hswep_cpu_init();
	uncore_types_init(uncore_msr_type);
	
	for (n = 0; uncore_msr_type[n]; n++) {
		type = uncore_msr_type[n];
		for (idx = 0; idx < type->num_boxes; idx++) {
			ret = uncore_msr_new_box(type, idx);
			if (ret)
				goto error;
		}
	}

	return 0;

error:
	uncore_cpu_exit();
	return ret;
}

/**
 * uncore_pci_print_boxes
 * 
 * Print information about all avaliable PCI type boxes.
 * Read this to make sure your CPU has the capacity you need
 * before sampling uncore PMU.
 */
static void uncore_pci_print_boxes(void)
{
	struct uncore_box_type *type;
	struct uncore_box *box;
	int i;

	for (i = 0; uncore_pci_type[i]; i++) {
		type = uncore_pci_type[i];
		pr_info("\n");
		pr_info("PCI Type: %s Boxes: %d", type->name,
			list_empty(&type->box_list)?0:type->num_boxes);

		list_for_each_entry(box, &type->box_list, next) {
			pr_info("......Box%d  %x:%x:%x",
			box->idx,
			box->pdev->bus->number,
			box->pdev->vendor,
			box->pdev->device);
		}
	}
}

/**
 * uncore_msr_print_boxes
 * 
 * Print information about all avaliable MSR type boxes.
 * Read this to make sure your CPU has the capacity you need
 * before sampling uncore PMU.
 */
static void uncore_msr_print_boxes(void)
{
	struct uncore_box_type *type;
	struct uncore_box *box;
	int i;
	
	for (i = 0; uncore_msr_type[i]; i++) {
		type = uncore_msr_type[i];
		pr_info("\n");
		pr_info("MSR Type: %s Boxes: %d", type->name,
			list_empty(&type->box_list)?0:type->num_boxes);

		list_for_each_entry(box, &type->box_list, next) {
			pr_info("......Box%d", box->idx);
		}
	}
}

static int uncore_init(void)
{
	int ret;
	
	ret = uncore_pci_init();
	if (ret)
		goto pcierr;

	ret = uncore_cpu_init();
	if (ret)
		goto cpuerr;
	
	uncore_pci_print_boxes();
	uncore_msr_print_boxes();
	pr_info("INIT ON CPU %2d", smp_processor_id());

	return 0;

cpuerr:
	uncore_cpu_exit();
pcierr:
	uncore_pci_exit();
	return ret;
}
/*
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

static void uncore_exit(void)
{
	uncore_pci_exit();
	uncore_cpu_exit();

	if (pci_driver_registered)
		pci_unregister_driver(uncore_pci_driver);
	
	pr_info("EXIT ON CPU %2d", smp_processor_id());
}

module_init(uncore_init);
module_exit(uncore_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("shanyizhou@ict.ac.cn");
