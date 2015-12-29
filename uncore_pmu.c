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
 *	BASIC INFORMATION ABOUT INTEL UNCORE PMU:
 *	For more information, please consult Intel PMU manuals.
 *
 *	Uncore performance monitors represent a per-socket resource that is not
 *	meant to be affected by context switches and thread migration performed
 *	by the OS, it is recommended that the monitoring software agent
 *	establish a fixed affinity binding to prevent cross-talk of event count
 *	from different uncore PMU.
 *
 *	The programming interface of the counter registers and control regiters
 *	fall into two address spaces:
 *	* Accessed by MSR are PMON registers within the CBo, SBo, PCU, U-Box.
 *	* Accessed by PCI device configuration space are PMON registers within
 *	  the HA, IMC, Intel QPI, R2PCIe and R3QPI units.
 */

#define pr_fmt(fmt) "UNCORE PMU: " fmt

#include "uncore_pmu.h"

#include <asm/setup.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

/* TOP description of the whole system uncore PMU resources */
struct uncore_pmu uncore_pmu;

unsigned int uncore_pcibus_to_nodeid[256] = { [0 ... 255] = -1, };

struct uncore_box_type *dummy_xxx_type[] = { NULL, };
struct uncore_box_type **uncore_msr_type = dummy_xxx_type;
struct uncore_box_type **uncore_pci_type = dummy_xxx_type;

/*
 * Since kernel has a uncore PMU module which has claimed all the PCI boxes
 * at kernel startup, so this uncore_pci_probe method will never get called.
 * I leave pci driver and these two methods here for future usage.
 */
struct pci_driver *uncore_pci_driver;
static void __always_unused uncore_pci_remove(struct pci_dev *dev) {}
static int __always_unused uncore_pci_probe(struct pci_dev *dev,
					    const struct pci_device_id *id)
{return -EIO;}

/**
 * uncore_get_box
 * @type:	pointer to box_type
 * @idx:	idx of the box in this box_type
 * @nodeid:	which NUMA node to get this box
 * Return:	%NULL on failure
 *
 * Get a uncore PMU box to perform tasks. Note that each box of its type has
 * its dedicated idx number, and belongs to a specific NUMA node. Therefore, to
 * get a PMU box you have to offer all these three parameters. Besides, you can
 * see the idx information after print_boxes.
 */
struct uncore_box *uncore_get_box(struct uncore_box_type *type,
				  unsigned int idx, unsigned int nodeid)
{
	struct uncore_box *box;

	if (!type || idx < 0 || nodeid > UNCORE_MAX_SOCKET)
		return NULL;

	list_for_each_entry(box, &type->box_list, next) {
		if (box->idx == idx && box->nodeid == nodeid)
			return box;
	}

	return NULL;
}

/**
 * uncore_get_first_box
 * @type:	pointer to box_type
 * @nodeid:	which NUMA node to get this box
 * Return:	%NULL on failure
 *
 * Get the first box in the box_type list of @nodeid node. We have this
 * function because some box types only have one avaliable box within a node.
 * It is more convenient to get box without an idx. (I know...)
 */
struct uncore_box *uncore_get_first_box(struct uncore_box_type *type,
					unsigned int nodeid)
{
	struct uncore_box *box;

	if (!type || nodeid > UNCORE_MAX_SOCKET)
		return NULL;

	list_for_each_entry(box, &type->box_list, next) {
		if (box->nodeid == nodeid)
			return box;
	}

	return NULL;
}

/**
 * uncore_show_global_pmu
 * @pmu:	the uncore_pmu in question
 *
 * Display the contents of the three global registers. Normally, every
 * microarchitecture has global registers including: CONTROL/STATUS/CONFIG.
 */
static void uncore_show_global_pmu(struct uncore_pmu *pmu)
{
	unsigned int config;

	pr_info("Name: %s", pmu->name);
	
	/* Careful, invalid address would cause #GP */
	if (pmu->global_ctl) {
		rdmsrl(pmu->global_ctl, config);
		pr_info("Uncore PMU Global Control: 0x%x", config);
	}

	if (pmu->global_status) {
		rdmsrl(pmu->global_status, config);
		pr_info("Uncore PMU Global Status:  0x%x", config);
	}

	if (pmu->global_config) {
		rdmsrl(pmu->global_config, config);
		pr_info("Uncore PMU Global Config:  0x%x", config);
	}
}

/******************************************************************************
 * Uncore PMU Specific Application: [NVM Emulation]
 * There are some restrictions on the system if you want to emulate NVM.
 * 	1)	Only one CPU core should be enabled at the emulation node.
 *	2)	Distribute the memory among different nodes manually.
 *	3)	I have to say, this emulation is not perfect.
 *****************************************************************************/

static void emulate_nvm(void)
{
	struct uncore_box *HA_Box_0, *HA_Box_1;
	struct uncore_box *U_Box_0, *U_Box_1;

	/* Home Agent Box, Box0, Node0 */
	HA_Box_0 = uncore_get_first_box(uncore_pci_type[UNCORE_PCI_HA_ID], 0);
	if (!HA_Box_0) {
		pr_err("Get HA_Box_0 Failed");
		return;
	}

	/* Home Agent Box, Box0, Node1 */
	HA_Box_1 = uncore_get_first_box(uncore_pci_type[UNCORE_PCI_HA_ID], 1);
	if (!HA_Box_0) {
		pr_err("Get HA_Box_1 Failed");
		return;
	}

	uncore_show_global(&uncore_pmu);
	uncore_show_box(HA_Box_0);
	uncore_show_box(HA_Box_1);
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
			pr_info("......Box%d, in Node%d, %x:%x:%x, %d:%d:%d, Kref = %d",
			box->idx,
			box->nodeid,
			box->pdev->bus->number,
			box->pdev->vendor,
			box->pdev->device,
			box->pdev->bus->number,
			(box->pdev->devfn >> 3) & 0x1f,
			(box->pdev->devfn) & 0x7,
			box->pdev->dev.kobj.kref.refcount.counter);
		}
	}
}

static void uncore_pci_print_mapping(void)
{
	int bus;

	pr_info("\n");
	pr_info("PCI BUS Number to NodeID Mapping:");
	for (bus = 0; bus < 256; bus++) {
		if (uncore_pcibus_to_nodeid[bus] != -1) {
			pr_info("......BUS %d (0x%x) <---> NODE %d",
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
			pr_info("......Box%d, in Node%d",
			box->idx,
			box->nodeid);
		}
	}
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
			/* Since we have get_device manually */
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
			
			/* BIG FAT NOTE
			 * pci_get_device will call pci_dev_put to put pdev.
			 * As a consequence, every pdev actually does *NOT*
			 * increase its kref at all! So we have to manually
			 * increase pdev's kref counter in case these pdevs
			 * are released! (we will put pdev when pci_exit)
			 */
			get_device(&pdev->dev);

			ret = uncore_pci_new_box(pdev, ids);
			if (ret)
				goto error;
		}
	}

	return 0;

error:
	uncore_pci_exit();
	return ret;
}

/**
 * uncore_msr_new_box
 * @type:	the MSR box_type
 * @idx:	the idx of the new box
 * Return:	Non-zero on failure
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
	unsigned int idx;
	int n, ret;

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
			pr_err("Buy an E5-v3");
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

static int uncore_init(void)
{
	int ret;

	pr_info("INIT ON CPU %2d (NODE %2d)",
		smp_processor_id(), numa_node_id());

	ret = uncore_pci_init();
	if (ret)
		goto pcierr;

	ret = uncore_cpu_init();
	if (ret)
		goto cpuerr;

	ret = uncore_imc_init();
	if (ret)
		goto out;

	ret = uncore_proc_create();
	if (ret)
		goto out;

	/*
	 * Pay attention to these messages
	 * Check if everything goes as expected
	 */
	uncore_msr_print_boxes();
	uncore_pci_print_boxes();
	uncore_pci_print_mapping();
	uncore_imc_print_devices();
	
	/*
	 * Throttle bandwidth for all nodes.
	 * Default to full bandwidth.
	 */
	uncore_imc_set_threshold_all(1);
	uncore_imc_enable_throttle_all();

	/*
	 * ...Actually, the emulator is just a tiny application of PMU module.
	 * We set two special sampling sessions for the emulator. We also
	 * register some functions to do emulating stuff.
	 */
	emulate_nvm();

	return 0;

out:
	uncore_imc_exit();
cpuerr:
	uncore_cpu_exit();
pcierr:
	uncore_pci_exit();
	return ret;
}

static void uncore_exit(void)
{
	pr_info("EXIT ON CPU %2d (NODE %2d)",
		smp_processor_id(), numa_node_id());

	uncore_imc_disable_throttle_all();
	uncore_proc_remove();
	uncore_imc_exit();
	uncore_cpu_exit();
	uncore_pci_exit();
}

module_init(uncore_init);
module_exit(uncore_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("shanyizhou@ict.ac.cn");
