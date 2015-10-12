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

#include <asm/setup.h>
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

/* PCI Bus Number <---> NUMA Node ID */
int uncore_pcibus_to_nodeid[256] = { [0 ... 255] = -1, };

/*
 * Since kernel has a uncore PMU module which has claimed all the PCI boxes
 * at kernel startup, so this uncore_pci_probe method will never get called.
 * I leave pci driver and these two methods here for future usage.
 */
struct pci_driver *uncore_pci_driver;
static bool pci_driver_registered = false;
static void __always_unused uncore_pci_remove(struct pci_dev *dev) {}
static int __always_unused uncore_pci_probe(struct pci_dev *dev,
					    const struct pci_device_id *id)
{return -EIO;}

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
 * uncore_get_box	-	Get a uncore PMU box
 * @type:	Pointer to box_type
 * @idx:	idx of the box in this box_type
 * @nodeid:	which NUMA node to get this box
 * Return:	%NULL on failure
 *
 * Get a uncore PMU box to perform tasks. Note that each box of its type has
 * its dedicated idx number, and belongs to a specific NUMA node. Therefore, to
 * get a PMU box you have to offer all these three parameters.
 */
struct uncore_box *uncore_get_box(struct uncore_box_type *type,
				  int idx, int nodeid)
{
	struct uncore_box *box;

	if (!type)
		return NULL;

	if (idx < 0 || idx >= type->num_boxes ||
	    nodeid < 0 || nodeid >= UNCORE_MAX_SOCKET)
		return NULL;
	
	list_for_each_entry(box, &type->box_list, next) {
		if (box->idx == idx && box->nodeid == nodeid)
			return box;
	}

	return NULL;
}

/**
 * uncore_types_init
 * @types:	box_type to init
 * Return:	Non-zero on failure
 *
 * Init the array of **uncore_box_type. Specially, the list_head.
 * This function should be called *after* CPU-specific init function.
 */
static int uncore_types_init(struct uncore_box_type **types)
{
	int i;

	for (i = 0; types[i]; i++) {
		INIT_LIST_HEAD(&types[i]->box_list);
	}

	return 0;
}

/**
 * uncore_pci_new_box
 * @pdev:	the pci device of this box
 * @id:		the device id of this box
 * Return:	Non-zero on failure
 *
 * Malloc a new box of PCI type, initilize all the fields. And then insert it
 * into the tail of box_list of its uncore_box_type.
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
	
	box->nodeid = uncore_pcibus_to_nodeid[pdev->bus->number];
	box->box_type = type;
	box->pdev = pdev;
	list_add_tail(&box->next, &type->box_list);
	
	return 0;
}

/* Free all PCI type boxes */
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
			pci_dev_put(box->pdev);
			kfree(box);
		}
	}
}

/* Malloc all PCI type boxes */
static int __must_check uncore_pci_init(void)
{
	const struct pci_device_id *ids;
	struct pci_dev *pdev;
	int ret;

	ret = -ENXIO;
	switch (boot_cpu_data.x86_model) {
		case 45: /* Sandy Bridge-EP*/
			break;
		case 62: /* Ivy Bridge-EP */
			break;
		case 63: /* Haswell-EP */
			ret = hswep_pci_init();
			break;
		default:
			pr_err("Buy an E5-v3");
	};

	if (ret)
		return ret;

	ret = uncore_types_init(uncore_pci_type);
	if (ret)
		return ret;

	ids = uncore_pci_driver->id_table;
	if (!ids)
		return -EFAULT;

	for (; ids->vendor || ids->device; ids++) {
		/* Iterate over all PCI buses */
		pdev = NULL;
		while (1) {
			pdev = pci_get_device(ids->vendor, ids->device, pdev);
			if (!pdev)
				break;
			
			ret = uncore_pci_new_box(pdev, ids);
			if (ret)
				goto error;
		}
	}

	/* Register PCI driver*/
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

	if (!type)
		return -EINVAL;

	box = kzalloc(sizeof(struct uncore_box), GFP_KERNEL);
	if (!box)
		return -ENOMEM;

	box->nodeid = 0;
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

	ret = -ENXIO;
	switch (boot_cpu_data.x86_model) {
		case 45: /* Sandy Bridge-EP*/
			break;
		case 62: /* Ivy Bridge-EP */
			break;
		case 63: /* Haswell-EP */
			ret = hswep_cpu_init();
			break;
		default:
			pr_err("Not an E5-v3");
	};

	if (ret)
		return ret;

	ret = uncore_types_init(uncore_msr_type);
	if (ret)
		return ret;
	
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
		pr_info("PCI Type: %s Boxes: %d",
			type->name,
			list_empty(&type->box_list)? 0: type->num_boxes);

		list_for_each_entry(box, &type->box_list, next) {
			pr_info("......Box%d, in node%d, %x:%x:%x",
			box->idx,
			box->nodeid,
			box->pdev->bus->number,
			box->pdev->vendor,
			box->pdev->device);
		}
	}
}

static void uncore_pci_print_mapping(void)
{
	int bus;

	pr_info("\n");
	for (bus = 0; bus < 256; bus++) {
		if (uncore_pcibus_to_nodeid[bus] != -1) {
			pr_info("PCI BUS %d(0x%x) <---> NODE %d",
				bus, bus, uncore_pcibus_to_nodeid[bus]);
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
			pr_info("......Box%d, in node%d",
			box->idx,
			box->nodeid);
		}
	}
}

static void test(void)
{
	struct uncore_box *box;

	box = uncore_get_box(uncore_pci_type[1], 0, 1);
	if (!box)
		return;
	
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

	ret = uncore_proc_create();
	if (ret)
		goto cpuerr;

	pr_info("INIT ON CPU %2d", smp_processor_id());
	uncore_msr_print_boxes();
	uncore_pci_print_boxes();
	uncore_pci_print_mapping();
	
	test();

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
	uncore_init_box(&cbox);
	uncore_enable_box(&cbox);
	uncore_enable_event(&cbox, &event);
	uncore_disable_event(&cbox, &event);
	uncore_disable_box(&cbox);
*/	

static void uncore_exit(void)
{
	uncore_pci_exit();
	uncore_cpu_exit();
	uncore_proc_remove();

	if (pci_driver_registered)
		pci_unregister_driver(uncore_pci_driver);
	
	pr_info("EXIT ON CPU %2d", smp_processor_id());
}

module_init(uncore_init);
module_exit(uncore_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("shanyizhou@ict.ac.cn");
