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

/*
 * (The same PCI devices with UNOCRE IMC PMON)
 *
 * IMC0, Channel 0-1 --> 20:0 20:1 (2fb4 2fb5)
 * IMC1, Channel 2-3 --> 21:0 21:1 (2fb0 2fb1)
 *
 * IMC1, Channel 2-3 --> 23:0 23:1 (2fd0 2fd1)
 */

const struct pci_device_id *uncore_imc_device_ids;
LIST_HEAD(uncore_imc_devices);

void uncore_imc_exit(void)
{
	struct list_head *imc, *head;

	head = &uncore_imc_devices;
	while (!list_empty(head)) {
		imc = list_first_entry(head, struct uncore_imc, next);
		list_del(&imc->next);
		pci_dev_put(imc->pdev);
		kfree(imc);
	}
}

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
	WARN_ON((nodeid < 0) || (nodeid > UNCORE_MAX_SOCKET));

	imc->nodeid = nodeid;
	imc->pdev = pdev;
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
			
			/* Increase kref manually */
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

void uncore_imc_print_devices(void)
{
	struct uncore_imc *imc;

	for_each_list_entry(imc, &uncore_imc_devices, next) {
	
	}

}
