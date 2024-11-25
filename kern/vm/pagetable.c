/* 
 * pagetable.c - Two-level page table implementation
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <vm.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <vaa.h>
#include <pagetable.h>
#include <current.h>
#include <mips/tlb.h>
#include <pmm.h>

/* Static helper functions */
static struct pte *pte_alloc(void) {
    /* Allocate a page for PTEs */
    struct pte *pte_page = (struct pte *)kmalloc(PAGE_SIZE);
    if (pte_page == NULL) {
        return NULL;
    }
    /* Zero out the new page table */
    bzero(pte_page, PAGE_SIZE);
    return pte_page;
}

static void pte_free(struct pte *pte) {
    kfree(pte);
}

/* Initialize page table system */
void pagetable_bootstrap(void) {
    /* Initialize VAA first */
    vaa_init();
}


// create page table for the kernel
void pagetable_init(void) {
    struct page_table *pt = pagetable_create();
    if (pt == NULL) {
        panic("Failed to create kernel page table\n");
    }
    pt->pid = 0;
    pt->asid = 0;
    kernel_pt = pt;
}

/* Create a new page table */
struct page_table *pagetable_create(void) {
    struct page_table *pt;
    
    pt = kmalloc(sizeof(struct page_table));
    if (pt == NULL) {
        return NULL;
    }

    /* Allocate page directory */
    pt->pgdir = (struct pde *)kmalloc(PAGE_SIZE);
    if (pt->pgdir == NULL) {
        kfree(pt);
        return NULL;
    }

    /* Initialize page directory */
    bzero(pt->pgdir, PAGE_SIZE);
    
    /* Initialize spinlock */
    spinlock_init(&pt->pt_lock);
    
    /* Initialize other fields */
    pt->pid = 0;  /* Will be set by process creation code */
    pt->heap_start = 0;
    pt->heap_end = 0;
    pt->asid = 0; /* Will be assigned by VM system */
    
    return pt;
}

/* Destroy a page table */
void pagetable_destroy(struct page_table *pt) {
    int i, j;
    struct pde *pde;
    struct pte *pte;

    KASSERT(pt != NULL);

    spinlock_acquire(&pt->pt_lock);

    /* Free all page tables */
    for (i = 0; i < PD_ENTRIES; i++) {
        pde = &pt->pgdir[i];
        if (pde->valid) {
            pte = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
            /* Free all pages in this table */
            for (j = 0; j < PT_ENTRIES_PER_PAGE; j++) {
                if (pte[j].valid) {
                    /* Free physical page if it exists */
                    pte_unmap(pt, (i << PDE_SHIFT) | (j << PTE_SHIFT));
                }
            }
            /* Free the page table itself */
            pte_free(pte);
        }
    }

    spinlock_release(&pt->pt_lock);
    
    /* Free page directory */
    kfree(pt->pgdir);
    
    /* Free page table structure */
    kfree(pt);
}

/* Map a virtual page to a physical page */
int pte_map(struct page_table *pt, vaddr_t vaddr, paddr_t paddr, uint32_t flags) {
    struct pde *pde;
    struct pte *pte_page;
    struct pte *pte;
    unsigned pde_index = PDE_INDEX(vaddr);
    unsigned pte_index = PTE_INDEX(vaddr);

    /* Verify alignment */
    if ((vaddr & PAGE_MASK) || (paddr & PAGE_MASK)) {
        return PT_ALIGN;
    }

    //spinlock_acquire(&pt->pt_lock);

    /* Get the page directory entry */
    pde = &pt->pgdir[pde_index];

    /* If page table doesn't exist, create it */
    if (!pde->valid) {
        if (vaddr >= MIPS_KSEG2) {
            paddr_t pde_paddr = pmm_alloc_page();
            vaddr_t pde_vaddr = vaa_alloc_kpage();
            if (pde_paddr == 0 || pde_vaddr == 0) {
                return PT_NOMEM;
            }

            pde->pt_pfn = pde_vaddr >> PAGE_SHIFT;
            pde->valid = 1;
            pde->write = 1;
            pde->user = 0;
            
            uint32_t ehi, elo;
            int i, spl;

            spl = splhigh();

            int flag = 0;
            for (i = 0; i < NUM_TLB; i++)
            {
                tlb_read(&ehi, &elo, i);
                if (elo & TLBLO_VALID)
                {
                    continue;
                }
                ehi = pde_vaddr;
                elo = pde_paddr | TLBLO_DIRTY | TLBLO_VALID;
                tlb_write(ehi, elo, i);
                splx(spl);
                pte_map(pt, vaddr, paddr, flags);
                flag = 1;
                break;
            }

            if (flag == 0)
            {
                panic("No TLB entry available\n");
            }
            
        } else {
            pte_page = pte_alloc();
            if (pte_page == NULL) {
                //spinlock_release(&pt->pt_lock);
                return PT_NOMEM;
            }
        
            pde->pt_pfn = (vaddr_t)pte_page >> PAGE_SHIFT;
            pde->valid = 1;
            pde->write = !!(flags & PTE_WRITE);
            pde->user = !!(flags & PTE_USER);
        }
        
    }

    /* Get the page table */
    pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
    pte = &pte_page[pte_index];

    /* Check if already mapped */
    if (pte->valid) {
        //spinlock_release(&pt->pt_lock);
        return PT_PRESENT;
    }

    /* Create the mapping */
    pte->pfn = paddr >> PAGE_SHIFT;
    pte->valid = 1;
    pte->write = !!(flags & PTE_WRITE);
    pte->user = !!(flags & PTE_USER);
    pte->nocache = !!(flags & PTE_NOCACHE);
    pte->dirty = 0;
    pte->accessed = 0;

    /* Flush TLB */
    //tlb_invalidate(pt, vaddr);

    //spinlock_release(&pt->pt_lock);
    return PT_OK;
}

/* Remove mapping for a virtual page */
int pte_unmap(struct page_table *pt, vaddr_t vaddr) {
    struct pde *pde;
    struct pte *pte_page;
    struct pte *pte;
    
    /* Get the page directory entry */
    pde = &pt->pgdir[PDE_INDEX(vaddr)];
    if (!pde->valid) {
        return PT_NOTPRESENT;
    }

    /* Get the page table */
    pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
    pte = &pte_page[PTE_INDEX(vaddr)];

    /* Check if page is present */
    if (!pte->valid) {
        return PT_NOTPRESENT;
    }

    /* Remove mapping */
    pte->valid = 0;
    
    /* Flush TLB */
    tlb_invalidate(pt, vaddr);

    return PT_OK;
}

/* Get PTE entry for a virtual address */
struct pte *pte_get(struct page_table *pt, vaddr_t vaddr) {
    struct pde *pde;
    struct pte *pte_page;
    
    //spinlock_acquire(&pt->pt_lock);

    /* Get page directory entry */
    pde = &pt->pgdir[PDE_INDEX(vaddr)];
    if (!pde->valid) {
        //spinlock_release(&pt->pt_lock);
        return NULL;
    }

    /* Get page table */
    pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
    
    /* Return PTE */
    return &pte_page[PTE_INDEX(vaddr)];
}

/* Translate virtual to physical address */
paddr_t pagetable_translate(struct page_table *pt, vaddr_t vaddr, uint32_t *flags) {
    struct pte *pte;
    struct pde *pde;
    
    /* Get page directory entry */
    pde = &pt->pgdir[PDE_INDEX(vaddr)];
    if (!pde->valid) {
        return PT_NOTPRESENT;
    }

    /* Get page table */
    pte = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
    
    if (pte == NULL || !pte->valid) {
        if (flags) *flags = 0;
        return 0;
    }

    if (flags) {
        *flags = 0;
        if (pte->write) *flags |= PTE_WRITE;
        if (pte->user) *flags |= PTE_USER;
        if (pte->nocache) *flags |= PTE_NOCACHE;
    }

    return (pte->pfn << PAGE_SHIFT) | (vaddr & PAGE_MASK);
}

/* Map a contiguous region */
int pagetable_map_region(struct page_table *pt, vaddr_t vaddr, 
                        paddr_t paddr, size_t npages, uint32_t flags) {
    size_t i;
    int result;

    for (i = 0; i < npages; i++) {
        result = pte_map(pt, vaddr + i*PAGE_SIZE, 
                        paddr + i*PAGE_SIZE, flags);
        if (result != PT_OK) {
            /* Unmap previously mapped pages on failure */
            while (i > 0) {
                i--;
                pte_unmap(pt, vaddr + i*PAGE_SIZE);
            }
            return result;
        }
    }
    return PT_OK;
}

void tlb_invalidate(struct page_table *pt, vaddr_t vaddr) 
{
    (void)pt; 
    
    uint32_t entryhi, entrylo;
    int index;
    
    entryhi = vaddr & TLBHI_VPAGE;
    entrylo = 0;  
    
    index = tlb_probe(entryhi, entrylo);
    
    if (index >= 0) {
        tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
    }
}

/* TLB management functions */
// void tlb_invalidate(struct page_table *pt, vaddr_t vaddr) {
//     int spl = splhigh();
    
//     if (pt == curproc->p_addrspace->pt && curproc->p_addrspace->pt->asid == curcpu->c_tlbinfo.asid) {
//         tlb_invalidate_one(vaddr & PAGE_FRAME);
//     }
    
//     splx(spl);
// }

// void tlb_invalidate_all(void) {
//     int spl = splhigh();
//     tlb_clear();
//     splx(spl);
// }