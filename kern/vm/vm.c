/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <types.h>
#include <spinlock.h>
#include <pmm.h>
#include <pagetable.h>
#include <vaa.h>
#include <swap.h>

bool vm_initialized = false;
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct spinlock alloc_lock = SPINLOCK_INITIALIZER;

static bool as_valid_region(struct addrspace *as, vaddr_t vaddr);


void vm_bootstrap(void)
{
	pagetable_bootstrap();
	pagetable_init();
	swap_init();
	pmm_init();
	vm_initialized = true;
}

paddr_t
getppages(unsigned long npages)
{
	if (!vm_initialized)
	{
		paddr_t addr;
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(npages);
		spinlock_release(&stealmem_lock);
		return addr;
	}
	else
	{
		if (npages == 1) {
			spinlock_acquire(&alloc_lock);
			paddr_t addr = pmm_alloc_page();
			spinlock_release(&alloc_lock);
		
			return addr;
		}
		else {
			spinlock_acquire(&alloc_lock);
			paddr_t addr = pmm_alloc_npages(npages);
			spinlock_release(&alloc_lock);
		
			return addr;
		}
	}
}

vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa == 0)
	{
		return 0;
	}
	if (!vm_initialized)
	{
		return PADDR_TO_KVADDR(pa);
	}
	else
	{
		if (npages == 1) {
			spinlock_acquire(&alloc_lock);
		    vaddr_t va = vaa_alloc_kpage();
		
		    pte_map(kernel_pt, va, pa, PTE_WRITE);

		    // zero out the page
		    memset((void *)va, 0, PAGE_SIZE);
		    spinlock_release(&alloc_lock);
		    KASSERT(va >= MIPS_KSEG2);
		    return va;
		}
		else {
			spinlock_acquire(&alloc_lock);
		    vaddr_t va = vaa_alloc_npages(npages);
		
		    pagetable_map_region(kernel_pt, va, pa, npages, PTE_WRITE);

		    // zero out the page
		    memset((void *)va, 0, PAGE_SIZE * npages);
		    spinlock_release(&alloc_lock);
		    KASSERT(va >= MIPS_KSEG2);
		    return va;
		}
	}
}

void free_kpages(vaddr_t addr)
{
	// not free pages allocated by ram_stealmem
	if (!vm_initialized)
	{
		return;
	}
	//free direct map pages
	else if (addr >= MIPS_KSEG0 && addr < MIPS_KSEG2)
	{
		spinlock_acquire(&alloc_lock);
		paddr_t paddr = addr - MIPS_KSEG0;
		pmm_free_page(paddr);
		spinlock_release(&alloc_lock);
		return;
	}
	else if (addr >= MIPS_KSEG2)
	{
		spinlock_acquire(&alloc_lock);
		vaa_free_kpage(addr);
		paddr_t paddr = pagetable_translate(kernel_pt, addr, NULL);
		pte_unmap(kernel_pt, addr);
		pmm_free_page(paddr);
		spinlock_release(&alloc_lock);
		return;
	}
	//user space pages
	else {
		return;
	}
}

int vm_fault(int faulttype, vaddr_t faultaddress)
{
	paddr_t paddr = 0;

	if (faultaddress >= MIPS_KSEG2)
	{
		uint32_t flags;
		paddr = pagetable_translate(kernel_pt, faultaddress, &flags);
		if (paddr == 0)
		{
			return EFAULT;
		}

		if (faulttype == VM_FAULT_READONLY)
		{
			if (flags & PTE_WRITE)
			{
				return EFAULT;
			}
		}

		uint32_t entryhi, entrylo;
		entryhi = faultaddress & TLBHI_VPAGE;
		entrylo = (paddr & TLBLO_PPAGE) | TLBLO_VALID | TLBLO_DIRTY | TLBLO_GLOBAL;
		return tlb_write_entry(entryhi, entrylo);
	} else if (faultaddress < USERSTACK) {
		struct addrspace *as;
    	uint32_t flags;

    	/* Get current address space */
    	as = proc_getas();
    	if (as == NULL) {
        	return EFAULT;
    	}
    
        if (!as_valid_region(as, faultaddress)) {
        	return EFAULT;
    	}

    	paddr = pagetable_translate(as->pt, faultaddress, &flags);
    
    	if (paddr == 0) {
        	paddr = getppages(1);
        	if (paddr == 0) {
            	return ENOMEM;
        	}	

        	int result = pte_map(as->pt, faultaddress & PAGE_FRAME, 
                           paddr, PTE_USER | PTE_WRITE);
        	if (result != PT_OK) {
            	pmm_free_page(paddr);
            	return EFAULT;
        	}
			KASSERT(paddr == pagetable_translate(as->pt, faultaddress & PAGE_FRAME, NULL));
    	}

		/* Write TLB entry */
        uint32_t entryhi = (faultaddress & TLBHI_VPAGE);
        uint32_t entrylo = (paddr & TLBLO_PPAGE) | TLBLO_VALID | TLBLO_DIRTY;
                              
        return tlb_write_entry(entryhi, entrylo);
	}
	
	return EFAULT;
}

static bool
as_valid_region(struct addrspace *as, vaddr_t vaddr)
{
    KASSERT(as != NULL);
    
    if (vaddr >= as->as_vbase1 && vaddr < as->as_vbase1 + as->as_npages1 * PAGE_SIZE) {
        return true;
    }
    
    if (vaddr >= as->as_vbase2 && vaddr < as->as_vbase2 + as->as_npages2 * PAGE_SIZE) {
        return true;
    }
    
	if (vaddr >= USERSTACK - PAGE_SIZE && vaddr < USERSTACK) {
		return true;
	}

    if (vaddr >= as->heap_start && vaddr < as->heap_end) {
        return true;
    }
    
    return false;
}

void 
vm_activate(struct page_table *pt)
{
    int spl = splhigh();

    KASSERT(pt != NULL);

    uint32_t entryhi, entrylo;
    spinlock_acquire(&pt->pt_lock);

    /* Invalidate all TLB entries first */
    tlb_invalidate_all();

    /* Load kernel mappings into TLB */
    for (uint32_t i = 0; i < PD_ENTRIES; i++) {
        if (pt->pgdir[i].valid) {
            struct pte *pte = (struct pte *)(pt->pgdir[i].pt_pfn << PAGE_SHIFT);
            for (uint32_t j = 0; j < PT_ENTRIES_PER_PAGE; j++) {
                if (pte[j].valid) {
                    vaddr_t vaddr = (i << PDE_SHIFT) | (j << PTE_SHIFT);
                    paddr_t paddr = pte[j].pfn_or_swap_slot << PAGE_SHIFT;
                    
                    /* Create TLB entry with ASID */
                    entryhi = (vaddr & TLBHI_VPAGE) | 
                             ((pt->pid & 0x3f) << 6);
                    entrylo = (paddr & TLBLO_PPAGE) | TLBLO_VALID | TLBLO_DIRTY;

                    tlb_write_entry(entryhi, entrylo);
                }
            }
        }
    }

    spinlock_release(&pt->pt_lock);
    splx(spl);
}
