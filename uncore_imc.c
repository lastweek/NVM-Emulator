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
 * This file describes methods to manipulate Integrated Memory Controller (IMC)
 */

#define pr_fmt(fmt) "UNCORE IMC: " fmt

#include "uncore_pmu.h"

#include <asm/setup.h>
#include <linux/bug.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/kernel.h>

const struct pci_device_id *uncore_imc_device_ids;
const struct uncore_imc_ops *uncore_imc_ops;
LIST_HEAD(uncore_imc_devices);

void uncore_imc_exit(void)
{
	struct list_head *head;
	struct uncore_imc *imc;

	head = &uncore_imc_devices;
	while (!list_empty(head)) {
		imc = list_first_entry(head, struct uncore_imc, next);
		list_del(&imc->next);
		pci_dev_put(imc->pdev);
		kfree(imc);
	}
}

/**
 * uncore_imc_new_device
 * @pdev:		the pci device instance
 * Return:		Non-zero on failure
 *
 * Add a new IMC struct to the list.
 */
static int __must_check uncore_imc_new_device(struct pci_dev *pdev)
{
	struct uncore_imc *imc;
	int nodeid;

	if (!pdev)
		return -EINVAL;
	
	imc = kzalloc(sizeof(struct uncore_imc), GFP_KERNEL);
	if (!imc)
		return -ENOMEM;
	
	nodeid = uncore_pcibus_to_nodeid[pdev->bus->number];
	WARN_ONCE((nodeid < 0) || (nodeid > UNCORE_MAX_SOCKET), 
		"Invalid Node ID: %d, check pci-node mapping", nodeid);

	imc->nodeid = nodeid;
	imc->pdev = pdev;
	imc->ops = uncore_imc_ops;
	list_add_tail(&imc->next, &uncore_imc_devices);

	return 0;
}

int __must_check uncore_imc_init(void)
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
			ret = hswep_imc_init();
			break;
		default:
			pr_err("Buy an E5-v3");
	};

	if (ret)
		return ret;
	
	ids = uncore_imc_device_ids;
	for (; ids->vendor; ids++) {
		pdev = NULL;
		while (1) {
			pdev = pci_get_device(ids->vendor, ids->device, pdev);
			if (!pdev)
				break;
			
			/* See uncore_pmu.c for why */
			get_device(&pdev->dev);
			ret = uncore_imc_new_device(pdev);
			if (ret)
				goto out;
		}
	}
	return 0;

out:
	uncore_imc_exit();
	return ret;
}

int uncore_imc_set_threshold(int nodeid, int threshold)
{
	struct uncore_imc *imc;
	int ret = -ENXIO;

	if (nodeid < 0 || nodeid > UNCORE_MAX_SOCKET)
		return -EINVAL;

	list_for_each_entry(imc, &uncore_imc_devices, next) {
		if (imc->nodeid == nodeid) {
			ret = imc->ops->set_threshold(imc->pdev, threshold);
			if (ret)
				break;
		}
	}
	return ret;
}

void uncore_imc_disable_throttle(int nodeid)
{
	struct uncore_imc *imc;

	if (nodeid < 0 || nodeid > UNCORE_MAX_SOCKET)
		return;

	list_for_each_entry(imc, &uncore_imc_devices, next) {
		if (imc->nodeid == nodeid)
			imc->ops->disable_throttle(imc->pdev);
	}
}

int uncore_imc_enable_throttle(int nodeid, int threshold)
{
	struct uncore_imc *imc;
	int ret = -ENXIO;

	if (nodeid < 0 || nodeid > UNCORE_MAX_SOCKET)
		return -EINVAL;

	list_for_each_entry(imc, &uncore_imc_devices, next) {
		if (imc->nodeid == nodeid) {
			ret = imc->ops->enable_throttle(imc->pdev, threshold);
			if (ret) {
				uncore_imc_disable_throttle(nodeid);
				break;
			}
		}
	}
	return ret;
}

void uncore_imc_print_devices(void)
{
	struct uncore_imc *imc;

	pr_info("\n");
	pr_info("IMC Devices:");
	list_for_each_entry(imc, &uncore_imc_devices, next) {
		pr_info("......Node %d, %x:%x:%x, %d:%d:%d, Kref = %d",
		imc->nodeid,
		imc->pdev->bus->number,
		imc->pdev->vendor,
		imc->pdev->device,
		imc->pdev->bus->number,
		(imc->pdev->devfn >> 3) & 0x1f,
		(imc->pdev->devfn) & 0x7,
		imc->pdev->dev.kobj.kref.refcount.counter);
	}
}
