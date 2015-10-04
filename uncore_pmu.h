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

/**
 * struct uncore_event_desc
 * Describe an uncore monitoring event
 */
struct uncore_event_desc {

};

/**
 * struct uncore_box_ops
 * Describe methods for a uncore pmu box
 */
struct uncore_box_ops {
	void (*init_box)(struct uncore_box *box);
	void (*enable_box)(struct uncore_box *box);
	void (*disable_box)(struct uncore_box *box);
	void (*enable_event)(struct uncore_box *box);
	void (*disable_event)(struct uncore_box *box);
};

/**
 * struct uncore_box
 * @idx:	IDX of this box
 * @name:	Name of this box
 * @box_list:	List of the name type boxes
 * @box_type:	Pointer to the type of this box
 *
 * Describe a single uncore pmu box. IDX is the suffix
 * of the box described in SDM. IDX is used to address
 * each box's base MSR adddress.
 */
struct uncore_box {
	int			idx;
	const char		*name;
	struct list_head	box_list;
	struct uncore_box_type	*box_type;
};

/**
 * struct uncore_box_type
 * @name:		Name of this type box
 * @num_counters:	Counters this type box has
 * @num_boxes:		Boxes this type box has
 * @perf_ctr_bits:	Bit width of PMC
 * @perf_ctr:		PMC MSR address
 * @perf_ctl:		EventSel MSR address
 * @event_mask:		perf_ctl event mask
 * @fixed_ctr_bits:	Bit width of fixed counter
 * @fixed_ctr:		Fixed counter MSR address
 * @fixed_ctl:		Fixed EventSel MSR address
 * @box_ctl:		Box-level Control MSR address
 * @box_status:		Box-level Status MSR address
 * @msr_offset:		MSR address offset of next box
 * @boxes:		List of all avaliable boxes of this type
 * @ops:		Box manipulation functions
 * @desc:		Performance Monitoring Event Description
 *
 * This struct describes a specific type of box. Not all box types
 * support all fields, it is up to the @ops to manipulate each
 * box properly.
 */
struct uncore_box_type {
	const char	*name;
	unsigned int	num_counters;
	unsigned int	num_boxes;
	unsigned int	perf_ctr_bits;
	unsigned int	perf_ctr;
	unsigned int	perf_ctl;
	unsigned int	event_mask;
	unsigned int	fixed_ctr_bits;
	unsigned int	fixed_ctr;
	unsigned int	fixed_ctl;
	unsigned int	box_ctl;
	unsigned int	box_status;
	unsigned int	msr_offset;
	
	struct uncore_box *boxes;
	struct uncore_box_ops *ops;
	struct uncore_event_desc *desc;
};
