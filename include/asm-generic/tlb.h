/* asm-generic/tlb.h
 *
 *	Generic TLB shootdown code
 *
 * Copyright 2001 Red Hat, Inc.
 * Based on code from mm/memory.c Copyright Linus Torvalds and others.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_GENERIC__TLB_H
#define _ASM_GENERIC__TLB_H

#include <linux/config.h>
#include <asm/tlbflush.h>

/* aim for something that fits in the L1 cache */
#define FREE_PTE_NR	508

/* mmu_gather_t is an opaque type used by the mm code for passing around any
 * data needed by arch specific code for tlb_remove_page.  This structure can
 * be per-CPU or per-MM as the page table lock is held for the duration of TLB
 * shootdown.
 */
typedef struct free_pte_ctx {
	struct mm_struct	*mm;
	unsigned long		nr;	/* set to ~0UL means fast mode */
	unsigned long		freed;
	unsigned long		start_addr, end_addr;
	pte_t	ptes[FREE_PTE_NR];
} mmu_gather_t;

/* Users of the generic TLB shootdown code must declare this storage space. */
extern mmu_gather_t	mmu_gathers[NR_CPUS];

/* Do me later */
#define tlb_start_vma(tlb, vma) do { } while (0)
#define tlb_end_vma(tlb, vma) do { } while (0)

/* tlb_gather_mmu
 *	Return a pointer to an initialized mmu_gather_t.
 */
static inline mmu_gather_t *tlb_gather_mmu(struct mm_struct *mm)
{
	mmu_gather_t *tlb = &mmu_gathers[smp_processor_id()];

	tlb->mm = mm;
	tlb->freed = 0;
	/* Use fast mode if there is only one user of this mm (this process) */
	tlb->nr = (atomic_read(&(mm)->mm_users) == 1) ? ~0UL : 0UL;
	return tlb;
}

static inline void tlb_flush_mmu(mmu_gather_t *tlb, unsigned long start, unsigned long end)
{
	unsigned long i, nr;

	/* Handle the fast case first. */
	if (tlb->nr == ~0UL) {
		flush_tlb_mm(tlb->mm);
		return;
	}
	nr = tlb->nr;
	tlb->nr = 0;
	if (nr)
		flush_tlb_mm(tlb->mm);
	for (i=0; i < nr; i++) {
		pte_t pte = tlb->ptes[i];
		__free_pte(pte);
	}
}

/* tlb_finish_mmu
 *	Called at the end of the shootdown operation to free up any resources
 *	that were required.  The page table lock is still held at this point.
 */
static inline void tlb_finish_mmu(mmu_gather_t *tlb, unsigned long start, unsigned long end)
{
	int freed = tlb->freed;
	struct mm_struct *mm = tlb->mm;
	int rss = mm->rss;

	if (rss < freed)
		freed = rss;
	mm->rss = rss - freed;

	tlb_flush_mmu(tlb, start, end);
}


/* void tlb_remove_page(mmu_gather_t *tlb, pte_t *ptep, unsigned long addr)
 *	Must perform the equivalent to __free_pte(pte_get_and_clear(ptep)), while
 *	handling the additional races in SMP caused by other CPUs caching valid
 *	mappings in their TLBs.
 */
static inline void tlb_remove_page(mmu_gather_t *tlb, pte_t *pte, unsigned long addr)
{
	struct page *page;
	unsigned long pfn = pte_pfn(*pte);

	if (pfn_valid(pfn)) {
		page = pfn_to_page(pfn);
		if (!PageReserved(page))
			tlb->freed++;
	}

	/* Handle the common case fast, first. */\
	if (tlb->nr == ~0UL) {
		__free_pte(*pte);
		pte_clear(pte);
		return;
	}
	if (!tlb->nr)
		tlb->start_addr = addr;
	tlb->ptes[tlb->nr++] = ptep_get_and_clear(pte);
	tlb->end_addr = addr + PAGE_SIZE;
	if (tlb->nr >= FREE_PTE_NR)
		tlb_finish_mmu(tlb, 0, 0);
}

#endif /* _ASM_GENERIC__TLB_H */

