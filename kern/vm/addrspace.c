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
#include <addrspace.h>
#include <vm.h>
#include <pagetable.h>
#include <current.h>
#include <proc.h>
#include <pmm.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *as_create(void) {
    struct addrspace *as;

    as = kmalloc(sizeof(struct addrspace));
    if (as == NULL) {
        return NULL;
    }

    as->as_vbase1 = 0;
    as->as_npages1 = 0;
    as->as_vbase2 = 0;
    as->as_npages2 = 0;

    as->heap_start = 0;
    as->heap_end = 0;
    as->as_stackptr = USERSTACK;

    as->as_text_prot = 0; 
    as->as_data_prot = 0;

    as->pt = pagetable_create();
    if (as->pt == NULL) {
        kfree(as);
        return NULL;
    }
    
    return as;
}


int as_copy(struct addrspace *old, struct addrspace **ret) {
    struct addrspace *new;

    /* Create a new empty address space */
    new = as_create();
    if (new == NULL) {
        return ENOMEM;
    }

    /* Copy segments info */
    new->as_vbase1 = old->as_vbase1;
    new->as_npages1 = old->as_npages1;
    new->as_vbase2 = old->as_vbase2;
    new->as_npages2 = old->as_npages2;
    
    /* Copy heap info */
    new->heap_start = old->heap_start;
    new->heap_end = old->heap_end;
    
    /* Copy protection bits */
    new->as_text_prot = old->as_text_prot;
    new->as_data_prot = old->as_data_prot;
    
    /* Copy stack pointer */
    new->as_stackptr = old->as_stackptr;

    /* Copy page table entries */
    new->pt = pagetable_create();
    int result = pagetable_copy(old->pt, new->pt);
    if (result) {
        as_destroy(new);
        return result;
    }

    *ret = new;
    return 0;
}


void as_destroy(struct addrspace *as) {
    if (as == NULL) {
        return;
    }


    /* Destroy page table and free all physical pages */
    if (as->pt != NULL) {
        pagetable_destroy(as->pt);
    }
    
    /* Free the address space structure itself */
    kfree(as);
}


void as_activate(void) {
    struct addrspace *as = proc_getas();
    
    if (as == NULL) {
        /* 
         * Kernel thread without an address space; leave the
         * prior address space in place.
         */
        return;
    }
    
    /* 
     * Turn on address translation 
     * Load TLB with current address space's page table
     */
    vm_activate(as->pt);
}


int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
                int readable, int writeable, int executable)
{
    if (as == NULL) {
        return EFAULT;
    }

    size_t npages;
    sz += vaddr & ~(vaddr_t)PAGE_FRAME;
    vaddr &= PAGE_FRAME;

    sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;
    npages = sz / PAGE_SIZE;

    if (as->as_vbase1 == 0) {
        as->as_vbase1 = vaddr;
        as->as_npages1 = npages;

        as->as_text_prot = 0;
        if (readable) as->as_text_prot |= PF_R;
        if (writeable) as->as_text_prot |= PF_W;
        if (executable) as->as_text_prot |= PF_X;
    }
    else if (as->as_vbase2 == 0) {
        as->as_vbase2 = vaddr;
        as->as_npages2 = npages;
        
        as->as_data_prot = 0;
        if (readable) as->as_data_prot |= PF_R;
        if (writeable) as->as_data_prot |= PF_W;
        if (executable) as->as_data_prot |= PF_X;

        as->heap_start = as->as_vbase2 + (as->as_npages2 * PAGE_SIZE);
        as->heap_end = as->heap_start;
    }
    else {
        return EINVAL;
    }

    return 0;
}

int
as_prepare_load(struct addrspace *as)
{
    KASSERT(as != NULL);
    
    struct page_table *pt = as->pt;
    spinlock_acquire(&pt->pt_lock);

    /* Go through all page directory entries */
    for (int i = 0; i < PD_ENTRIES; i++) {
        struct pde *pde = &pt->pgdir[i];
        if (!pde->valid) {
            continue;
        }

        /* Get the page table */
        struct pte *pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
        
        /* Go through all page table entries */
        for (int j = 0; j < PT_ENTRIES_PER_PAGE; j++) {
            struct pte *pte = &pte_page[j];
            if (!pte->valid) {
                continue;
            }

            pte->write = 1;
            
            /* Invalidate any TLB entries for this page */
            vaddr_t vaddr = (i << PDE_SHIFT) | (j << PTE_SHIFT);
            tlb_invalidate_entry(vaddr);
        }
    }

    spinlock_release(&pt->pt_lock);
    return 0;
}


int
as_complete_load(struct addrspace *as)
{
    KASSERT(as != NULL);
    
    struct page_table *pt = as->pt;
    spinlock_acquire(&pt->pt_lock);

    /* Go through all page directory entries */
    for (int i = 0; i < PD_ENTRIES; i++) {
        struct pde *pde = &pt->pgdir[i];
        if (!pde->valid) {
            continue;
        }

        /* Get the page table */
        struct pte *pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
        
        /* Go through all page table entries */
        for (int j = 0; j < PT_ENTRIES_PER_PAGE; j++) {
            struct pte *pte = &pte_page[j];
            if (!pte->valid) {
                continue;
            }

            vaddr_t vaddr = (i << PDE_SHIFT) | (j << PTE_SHIFT);
            
            /* Set permissions based on segment type */
            if (vaddr >= as->as_vbase1 && vaddr < as->as_vbase2) {
                pte->write = 0;
            }
            else if (vaddr >= as->as_vbase2 && vaddr < as->as_stackptr) {
                pte->write = 1;
            }
            else if (vaddr >= USERSTACK - PAGE_SIZE && vaddr < USERSTACK) {
                pte->write = 1;
            }
            else if (vaddr >= as->heap_start && vaddr < as->heap_end) {
                pte->write = 1;
            }
            pte->user = 1;
            
            tlb_invalidate_entry(vaddr);
        }
    }

    spinlock_release(&pt->pt_lock);
    return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *initstackptr)
{
    KASSERT(as != NULL);
    KASSERT(initstackptr != NULL);

    *initstackptr = USERSTACK;
    
    return 0;
}

void
as_deactivate(void)
{
	tlb_invalidate_all();
}