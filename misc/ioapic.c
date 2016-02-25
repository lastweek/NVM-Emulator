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
 * WARNING: Dangerous code, doublecheck before use it.
 */

#include <linux/smp.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/io.h>
#include <asm/page.h>
#include <asm/fixmap.h>

/*
 * The structure of the IO-APIC:
 */
union __IO_APIC_reg_00 {
	u32	raw;
	struct {
		u32	__reserved_2	: 14,
			LTS		:  1,
			delivery_type	:  1,
			__reserved_1	:  8,
			ID		:  8;
	} __attribute__ ((packed)) bits;
};

union __IO_APIC_reg_01 {
	u32	raw;
	struct {
		u32	version		:  8,
			__reserved_2	:  7,
			PRQ		:  1,
			entries		:  8,
			__reserved_1	:  8;
	} __attribute__ ((packed)) bits;
};

union __IO_APIC_reg_02 {
	u32	raw;
	struct {
		u32	__reserved_2	: 24,
			arbitration	:  4,
			__reserved_1	:  4;
	} __attribute__ ((packed)) bits;
};

union __IO_APIC_reg_03 {
	u32	raw;
	struct {
		u32	boot_DT		:  1,
			__reserved_1	: 31;
	} __attribute__ ((packed)) bits;
};

struct __IO_APIC_route_entry {
	__u32	vector		:  8,
		delivery_mode	:  3,	/* 000: FIXED
					 * 001: lowest prio
					 * 111: ExtINT
					 */
		dest_mode	:  1,	/* 0: physical, 1: logical */
		delivery_status	:  1,
		polarity	:  1,
		irr		:  1,
		trigger		:  1,	/* 0: edge, 1: level */
		mask		:  1,	/* 0: enabled, 1: disabled */
		__reserved_2	: 15;

	__u32	__reserved_3	: 24,
		dest		:  8;
} __attribute__ ((packed));


union route_entry_union {
	struct {
		__u32	w1;
		__u32	w2;
	};
	struct __IO_APIC_route_entry entry;
};

/* Mapping area of IO APIC */
struct ioapic {
	__u32	index;
	__u32	__pad1[3];
	__u32	data;
	__u32	__pad2[11];
	__u32	eoi;
};

/*
 * BASE --> FIX_IO_APIC_BASE_0
 * MAX  --> FIX_IO_APIC_BASE_END
 */
static void *
IO_APIC_BASE(int idx)
{
	return (void *) __fix_to_virt(FIX_IO_APIC_BASE_0+idx)
		+ (0xfec00000U & ~PAGE_MASK);
}

static unsigned int
IO_APIC_READ(int apic, unsigned int reg)
{
	struct ioapic *ioapic = IO_APIC_BASE(apic);
	writel(reg, &ioapic->index);
	return readl(&ioapic->data);
}

static void
IO_APIC_WRITE(int apic, unsigned int reg, unsigned int value)
{
	struct ioapic *ioapic = IO_APIC_BASE(apic);
	writel(reg, &ioapic->index);
	writel(value, &ioapic->data);
}

static void
IO_APIC_READ_ENTRY(int apic, int pin, struct __IO_APIC_route_entry *entry)
{
	union route_entry_union reu;
	
	reu.w1 = IO_APIC_READ(apic, 0x10+2*pin);
	reu.w2 = IO_APIC_READ(apic, 0x11+2*pin);
	*entry = reu.entry;
}

/*
 * When we wtite a new IO APIC routing entry, we need to write the
 * high word frist. Because if the mask bit int the low word is clear,
 * the interrupt is enabled, and we need to make sure the entry is
 * fully populated before that happens.
 */
static void
IO_APIC_WRITE_ENTRY(int apic, int pin, struct __IO_APIC_route_entry e)
{
	union route_entry_union reu = {{0, 0}};

	reu.entry = e;
	IO_APIC_WRITE(apic, 0x11+2*pin, reu.w2);
	IO_APIC_WRITE(apic, 0x10+2*pin, reu.w1);
}

static void
IO_APIC_PRINT_ENTRIES(int apic, int nr_entries)
{
	struct __IO_APIC_route_entry entry;
	int i;

	for (i = 0; i <= nr_entries; i++) {
		IO_APIC_READ_ENTRY(apic, i, &entry);
		printk(KERN_INFO " %02x %02X  ", i, entry.dest);
		printk(KERN_INFO "MASK = %1d    %1d    %1d   %1d   %1d    "
			"%1d    %1d    %02X\n",
			entry.mask, entry.trigger, entry.irr,
			entry.polarity, entry.delivery_status,
			entry.dest_mode, entry.delivery_mode,
			entry.vector);
	}
}

static void
IO_APIC_PRINT_APIC(int apic)
{
	union __IO_APIC_reg_00 reg_00;
	union __IO_APIC_reg_01 reg_01;
	union __IO_APIC_reg_02 reg_02;
	union __IO_APIC_reg_03 reg_03;

	reg_00.raw = IO_APIC_READ(apic, 0);
	reg_01.raw = IO_APIC_READ(apic, 1);
	if (reg_01.bits.version >= 0x10)
		reg_02.raw = IO_APIC_READ(apic, 2);
	if (reg_01.bits.version >= 0x20)
		reg_03.raw = IO_APIC_READ(apic, 3);

	printk(KERN_DEBUG "IO APIC #%d......\n", apic);
	printk(KERN_DEBUG ".... register #00: %08X\n", reg_00.raw);
	printk(KERN_DEBUG ".......    : physical APIC id: %02X\n", reg_00.bits.ID);
	printk(KERN_DEBUG ".......    : Delivery Type: %X\n", reg_00.bits.delivery_type);
	printk(KERN_DEBUG ".......    : LTS          : %X\n", reg_00.bits.LTS);
	printk(KERN_DEBUG ".... register #01: %08X\n", *(int *)&reg_01);
	printk(KERN_DEBUG ".......     : max redirection entries: %02X\n", reg_01.bits.entries);
	printk(KERN_DEBUG ".......     : PRQ implemented: %X\n", reg_01.bits.PRQ);
	printk(KERN_DEBUG ".......     : IO APIC version: %02X\n", reg_01.bits.version);

	/*
	 * Some Intel chipsets with IO APIC VERSION of 0x1? don't have reg_02,
	 * but the value of reg_02 is read as the previous read register
	 * value, so ignore it if reg_02 == reg_01.
	 */
	if (reg_01.bits.version >= 0x10 && reg_02.raw != reg_01.raw) {
		printk(KERN_DEBUG ".... register #02: %08X\n", reg_02.raw);
		printk(KERN_DEBUG ".......     : arbitration: %02X\n", reg_02.bits.arbitration);
	}

	/*
	 * Some Intel chipsets with IO APIC VERSION of 0x2? don't have reg_02
	 * or reg_03, but the value of reg_0[23] is read as the previous read
	 * register value, so ignore it if reg_03 == reg_0[12].
	 */
	if (reg_01.bits.version >= 0x20 && reg_03.raw != reg_02.raw &&
	    reg_03.raw != reg_01.raw) {
		printk(KERN_DEBUG ".... register #03: %08X\n", reg_03.raw);
		printk(KERN_DEBUG ".......     : Boot DT    : %X\n", reg_03.bits.boot_DT);
	}

	printk(KERN_DEBUG ".... IRQ redirection table:\n");
	IO_APIC_PRINT_ENTRIES(apic, reg_01.bits.entries);
}

static int __init apicinit(void)
{
	IO_APIC_PRINT_APIC(0);
	IO_APIC_PRINT_APIC(1);
	return 0;
}

static void apicexit(void)
{
	
}

module_init(apicinit);
module_exit(apicexit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("shanyizhou@ict.ac.cn");
