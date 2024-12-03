#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <vm.h>
#include <pr.h>
#include <pagetable.h>
#include <swap.h>

/*
 * find_victim_page
 * Helper function to find a page to evict
 * Returns: 0 on success, -1 if no victim page found
 * Output parameters: pt_out, vaddr_out for the chosen victim page
 */
static int
find_victim_page(struct page_table **pt_out, vaddr_t *vaddr_out)
{
    KASSERT(pt_list != NULL);
    KASSERT(pt_out != NULL);
    KASSERT(vaddr_out != NULL);

    unsigned int i, j;
    struct page_table *pt;
    struct pde *pde;
    struct pte *pte;
    struct pte *pte_page;
    vaddr_t vaddr;

    /* Save first valid page with accessed = 1 as backup */
    struct page_table *backup_pt = NULL;
    vaddr_t backup_vaddr = 0;
    bool found_backup = false;

    /* Iterate through all page tables */
    for (i = 0; pt_list[i] != NULL; i++) {
        pt = pt_list[i];
        
        spinlock_acquire(&pt->pt_lock);
        
        /* Iterate through page directory entries */
        for (j = 0; j < PD_ENTRIES; j++) {
            pde = &pt->pgdir[j];
            
            if (!pde->valid) {
                continue;
            }
            
            /* Get page table for this directory entry */
            pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT)
            
            /* Iterate through page table entries */
            for (unsigned int k = 0; k < PT_ENTRIES_PER_PAGE; k++) {
                pte = &pte_page[k];
                vaddr = (j << PDE_SHIFT) | (k << PTE_SHIFT); /* Construct virtual address */
            
                /* First check for valid, not swapped, has physical frame */
                if (pte->valid && !pte->swap && pte->pfn_or_swap_slot != 0) {
                    if (!pte->accessed) {
                        /* Found ideal page (accessed = 0) */
                        *pt_out = pt;
                        *vaddr_out = vaddr;
                        spinlock_release(&pt->pt_lock);
                        return 0;
                    } else if (!found_backup) {
                        /* Save first valid page with accessed = 1 as backup */
                        backup_pt = pt;
                        backup_vaddr = vaddr;
                        found_backup = true;
                    }
                }
            }
        }
        
        spinlock_release(&pt->pt_lock);
    }

    /* If we found a backup page (accessed = 1), use it */
    if (found_backup) {
        *pt_out = backup_pt;
        *vaddr_out = backup_vaddr;
        return 0;
    }

    return -1;
}

/*
 * evict_page
 * Finds and evicts a page from memory
 * Returns: 0 on success, negative value on error
 */
int
evict_page(bool emergency)
{
    struct page_table *victim_pt;
    vaddr_t victim_vaddr;
    int result;

    /* Find a victim page */
    result = find_victim_page(&victim_pt, &victim_vaddr);
    if (result != 0) {
        panic("evict_page: no victim found\n");
    }

    /* Swap out the victim page */
    result = swap_out_page(victim_pt, victim_vaddr, emergency);

    return (result == SWAP_SUCCESS) ? PR_SUCCESS : PR_ERROR;
}
