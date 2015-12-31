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

#include <linux/delay.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

/******************************************************************************
 * Uncore PMU Specific Application: [NVM Emulation]
 * There are some restrictions on the system if you want to emulate NVM.
 * 	1)	Only one CPU core should be enabled at the emulation node.
 *	2)	Distribute the memory among different nodes manually.
 *	3)	I have to say, this emulation is not perfect.
 *****************************************************************************/

extern struct uncore_event ha_requests_local_reads;

struct uncore_box *HA_Box_0, *HA_Box_1;
struct uncore_box *U_Box_0, *U_Box_1;
struct uncore_event *event;

void start_emulate_nvm(void)
{
	/*
	 * Throttle bandwidth of all nodes
	 * Default to full bandwidth
	 */
	uncore_imc_set_threshold_all(1);
	uncore_imc_enable_throttle_all();

	/* Home Agent: (Box0, Node0), (Box0, Node1) */
	HA_Box_0 = uncore_get_first_box(uncore_pci_type[UNCORE_PCI_HA_ID], 0);
	HA_Box_1 = uncore_get_first_box(uncore_pci_type[UNCORE_PCI_HA_ID], 1);
	if (!HA_Box_0 || !HA_Box_1) {
		pr_err("Get HA Box Failed");
		return;
	}

	event = &ha_requests_local_reads;
	uncore_box_bind_event(HA_Box_0, event);
	uncore_box_bind_event(HA_Box_1, event);

	uncore_init_box(HA_Box_0);
	uncore_init_box(HA_Box_1);
	uncore_enable_box(HA_Box_0);
	uncore_enable_box(HA_Box_1);
/*
	uncore_enable_event(HA_Box_0, event);
	uncore_enable_event(HA_Box_1, event);
	mdelay(100);

	uncore_show_global_pmu(&uncore_pmu);
	uncore_show_box(HA_Box_0);
	uncore_show_box(HA_Box_1);
*/
	uncore_box_start_hrtimer(HA_Box_0);
	uncore_box_start_hrtimer(HA_Box_1);

	mdelay(1000);

	uncore_box_cancel_hrtimer(HA_Box_0);
	uncore_box_cancel_hrtimer(HA_Box_1);

	uncore_disable_box(HA_Box_0);
	uncore_disable_box(HA_Box_1);
}

void finish_emulate_nvm(void)
{
	uncore_imc_disable_throttle_all();
}
