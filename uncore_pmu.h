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

#include <linux/pci.h>
#include <linux/types.h>
#include <linux/compiler.h>

#define UNCORE_MAX_SOCKET		8

/* PCI Driver Data <--> Box Type and IDX */
#define UNCORE_PCI_DEV_DATA(type, idx)	(((type) << 8) | (idx))
#define UNCORE_PCI_DEV_TYPE(data)	(((data) >> 8) & 0xFF)
#define UNCORE_PCI_DEV_IDX(data)	((data) & 0xFF)

struct uncore_box_type;

/**
 * struct uncore_event_desc
 * Describe an uncore monitoring event
 */
struct uncore_event_desc {

};

/**
 * struct uncore_event
 * @enable:	Bit mask to enable this event
 * @disable:	Bis mask to disable this event
 * @desc:	Description about this event
 */
struct uncore_event {
	unsigned long long enable, disable;
	struct uncore_event_desc *desc;
};

/**
 * struct uncore_box
 * @idx:	Index of this box
 * @nodeid	NUMA node id of this box
 * @box_type:	Pointer to the type of this box
 * @pdev:	PCI device of this box (For PCI type box)
 * @next:	List of the same type boxes
 *
 * Describe a single uncore pmu box instance. All boxes of the same type
 * are linked together. Since all boxes of all nodes all mixed together,
 * hence node_id is needed to distinguish two boxes with the same idx but
 * lay in different nodes.
 */
struct uncore_box {
	int			idx;
	int			nodeid;
	struct uncore_box_type	*box_type;
	struct pci_dev		*pdev;
	struct list_head	next;
};

/**
 * struct uncore_box_ops
 * @init_box:
 * @enable_box:
 * @disable_box:
 * @enable_event:
 * @disable_event:
 * @show_event:
 *
 * Describe methods for manipulating a uncore pmu box
 */
struct uncore_box_ops {
	void (*show_box)(struct uncore_box *box);
	void (*init_box)(struct uncore_box *box);
	void (*enable_box)(struct uncore_box *box);
	void (*disable_box)(struct uncore_box *box);
	void (*enable_event)(struct uncore_box *box, struct uncore_event *event);
	void (*disable_event)(struct uncore_box *box, struct uncore_event *event);
};

/**
 * struct uncore_box_type
 * @name:		Name of this type box
 * @num_counters:	Counters this type box has
 * @num_boxes:		Boxes this type box has
 * @perf_ctr_bits:	Bit width of PMC
 * @perf_ctr:		PMC address
 * @perf_ctl:		EventSel address
 * @event_mask:		perf_ctl writable bits mask
 * @fixed_ctr_bits:	Bit width of fixed counter
 * @fixed_ctr:		Fixed counter address
 * @fixed_ctl:		Fixed EventSel address
 * @box_ctl:		Box-level Control address
 * @box_status:		Box-level Status address
 * @msr_offset:		MSR address offset of next box
 * @box_list:		List of all avaliable boxes of this type
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
	
	struct list_head box_list;
	const struct uncore_box_ops *ops;
	const struct uncore_event_desc *desc;
};

extern struct uncore_box_type **uncore_msr_type;
extern struct uncore_box_type **uncore_pci_type;
extern struct pci_driver *uncore_pci_driver;
extern int uncore_pcibus_to_nodeid[256];

static inline unsigned int uncore_pci_box_status(struct uncore_box *box)
{
	return box->box_type->box_status;
}

static inline unsigned int uncore_pci_box_ctl(struct uncore_box *box)
{
	return box->box_type->box_ctl;
}

static inline unsigned int uncore_pci_perf_ctl(struct uncore_box *box)
{
	return box->box_type->perf_ctl;
}

static inline unsigned int uncore_pci_perf_ctr(struct uncore_box *box)
{
	return box->box_type->perf_ctr;
}

/**
 * uncore_msr_box_offset
 * @box:	the box in question
 *
 * Return the control MSR's address offset of this box
 */
static inline unsigned int uncore_msr_box_offset(struct uncore_box *box)
{
	return box->idx * box->box_type->msr_offset;
}

static inline unsigned int uncore_msr_box_status(struct uncore_box *box)
{
	return box->box_type->box_status + uncore_msr_box_offset(box);
}

static inline unsigned int uncore_msr_box_ctl(struct uncore_box *box)
{
	return box->box_type->box_ctl + uncore_msr_box_offset(box);
}

static inline unsigned int uncore_msr_perf_ctl(struct uncore_box *box)
{
	return box->box_type->perf_ctl + uncore_msr_box_offset(box);
}

static inline unsigned int uncore_msr_perf_ctr(struct uncore_box *box)
{
	return box->box_type->perf_ctr + uncore_msr_box_offset(box);
}

/**
 * uncore_show_event
 * @box:	the box to show
 *
 * Show control and conter status of the box
 */
static inline void uncore_show_box(struct uncore_box *box)
{
	box->box_type->ops->show_box(box);
}

/**
 * uncore_init_box
 * @box:	the box to init
 *
 * Initialize a uncore box for a new event.
 * This method will clear the control and the counter registers.
 * Always call this if you want to begin a new event to count or sample.
 */
static inline void uncore_init_box(struct uncore_box *box)
{
	box->box_type->ops->init_box(box);
}

/**
 * uncore_enable_box
 * @box:	the box to enable
 *
 * Enable the box to count or sample.
 * This method will enable counting at the box-level. Note that this method
 * will *NOT* clear counters, use uncore_init_box to clear all registers.
 */
static inline void uncore_enable_box(struct uncore_box *box)
{
	box->box_type->ops->enable_box(box);
}

/**
 * uncore_disable_box
 * @box:	the box to disable
 *
 * Disable the box to count or sample.
 * This method will disbale counting at the box-level. Just freeze the counter.
 */
static inline void uncore_disable_box(struct uncore_box *box)
{
	box->box_type->ops->disable_box(box);
}

/**
 * uncore_enable_event
 * @box:	the box to enable
 * @event:	the event to count or sample
 *
 * Assign a specific event to box.
 * This method will *NOT* start counting, call uncore_enable_box to start.
 */
static inline void uncore_enable_event(struct uncore_box *box,
				       struct uncore_event *event)
{
	box->box_type->ops->enable_event(box, event);
}

/**
 * uncore_disable_event
 * @box:	the box to disable
 * @event:	the event to disable
 *
 * Remove a specific event from box
 * This method will *NOT* disable counting, call uncore_disable_box to stop.
 */
static inline void uncore_disable_event(struct uncore_box *box,
					struct uncore_event *event)
{
	box->box_type->ops->disable_event(box, event);
}

/* Haswell-EP */
int hswep_cpu_init(void);
int hswep_pci_init(void);

/* User-Space Interface /proc */
int uncore_proc_create(void);
void uncore_proc_remove(void);
