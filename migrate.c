/*
 *	Copyright (C) 2015-2016 Yizhou Shan <shanyizhou@ict.ac.cn>
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

#include <asm/pgtable.h>

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/hrtimer.h>

static pid_t pid;
static unsigned long timer_interval_ns;
static struct hrtimer migrate_hrtimer;

#define pte_accessed	pte_young

#ifdef debug
#define DEBUG_INFO(format...) do { pr_info(format); } while (0)
#else
#define DEBUG_INFO(format...) do { } while (0)
#endif

/*
 * Each page should have a corresponding counter. The page
 * should be migrated once the counter reaches threshold.
 */
static void collect_statistics(unsigned long pfn)
{
	/*
	 * inc_pfn_counter(pfn);
	 * if (read_pfn_counter(pfn) > threshold)
	 * 	do_hybrid_memory_page_migration();
	 *
	 */
}

static unsigned long clear_pte_range(struct vm_area_struct *vma, pmd_t *pmd,
				     unsigned long addr, unsigned long end)
{
	pte_t *pte;
	pte_t ptecont;

	pte = pte_offset_map(pmd, addr);
	ptecont = *pte;

	do {
		if (pte_none(ptecont))
			continue;

		if (pte_present(ptecont) && pte_accessed(ptecont)) {
			/*
			 * The physical page, which this pte points to, has
			 * been read or written to during this time period.
			 */
			MIGRATE_DEBUG("clear at pfn: %#lx", pte_pfn(ptecont));
			collect_statistics(pte_pfn(ptecont));
			pte_clear_flags(ptecont, _PAGE_ACCESSED)
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);

	return addr;
}

static unsigned long clear_pmd_range(struct vm_area_struct *vma, pud_t *pud,
				     unsigned long addr, unsigned long end)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		next = clear_pte_range(vma, pmd, addr, next);
	} while (pmd++, addr = next, addr != end);

	return addr;
}

static unsigned long clear_pud_range(struct vm_area_struct *vma, pgd_t *pgd,
				     unsigned long addr, unsigned long end)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		next = clear_pmd_range(vma, pud, addr, end);
	} while (pud++, addr = next, addr != end);

	return addr;
}

/*
 * Walking through page table. It's expansive, absolutely.
 * I do hope you can evaluate how expansive it is at your
 * machine and tell me. :-)
 */
static void clear_page_range(struct vm_area_struct *vma)
{
	pgd_t *pgd;
	unsigned long next, addr, end;

	addr = vma->vm_start;
	end = vma->vm_end;

	pgd = pgd_offset(vma->vm_mm);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		next = clear_pud_range(vma, pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
}

static void clear_single_vma(struct vm_area_struct *vma)
{
	clear_page_range(vma);
}

static enum hrtimer_restart hrtimer_def(struct hrtimer *hrtimer)
{
	struct vm_area_struct *vma;
	struct task_struct *task;
	struct mm_struct *mm;

	/*
	 * Currently, this demo assumes the userspace process: pid
	 * is being monitored. You should make it more general and
	 * be able to catch running process.
	 */
	task = find_task_by_vpid(pid);
	if (unlikely(!task))
		return HRTIMER_NORESTART;

	mm = task->mm;
	vma = mm->mmap;

	/*
	 * Maybe we should not hold the semaphore, since
	 * the only thing we touch is the accessed bit.
	 *
	 * There is no need to flush TLB, since the accessed
	 * bit will not cause inconsistency. On the other,
	 * TLB flush is very expansive if we do it frequently.
	 */
	down_write(&mm->mmap_sem);
	while (vma) {
		clear_single_vma(vma);
		vma = vma->next;
	}
	up_write(&mm->mmap_sem);

	hrtimer_forward_now(hrtimer, ns_to_ktime(timer_interval_ns));
	return HRTIMER_RESTART
}

static int migrate_init(void)
{
	/* timer restart interval */
	timer_interval_ns = 2000000000;

	hrtimer_init(&migrate_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	migrate_hrtimer.function = hrtimer_def;
	hrtimer_start(&migrate_hrtimer, ns_to_ktime(timer_interval_ns), HRTIMER_MODE_REL);

	return 0;
}

static void migrate_exit(void)
{
	hrtimer_cancel(&migrate_hrtimer);
}

module_init(migrate_init);
module_exit(migrate_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("shanyizhou@ict.ac.cn");
