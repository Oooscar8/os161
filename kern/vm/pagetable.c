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

    spinlock_acquire(&pt->pt_lock);

    /* Get the page directory entry */
    pde = &pt->pgdir[pde_index];

    /* If page table doesn't exist, create it */
    if (!pde->valid) {
        if (vaddr >= MIPS_KSEG2) {
            paddr_t pde_paddr = pmm_alloc_page();
            if (pde_paddr == 0) {
                return PT_NOMEM;
            }

            vaddr_t pde_vaddr = PADDR_TO_KVADDR(pde_paddr);
            pde->pt_pfn = pde_vaddr >> PAGE_SHIFT;
            pde->valid = 1;
            pde->write = 1;
            pde->user = 0;
        }
    }

    /* Get the page table */
    pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
    pte = &pte_page[pte_index];

    /* Check if already mapped */
    if (pte->valid) {
        spinlock_release(&pt->pt_lock);
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
    pte->swap = 0;

    spinlock_release(&pt->pt_lock);
    return PT_OK;
}

/* Remove mapping for a virtual page */
int pte_unmap(struct page_table *pt, vaddr_t vaddr) {
    struct pde *pde;
    struct pte *pte_page;
    struct pte *pte;
    
    /* Get the page directory entry */
    spinlock_acquire(&pt->pt_lock);
    tlbshootdown_broadcast(vaddr, curproc->p_pid);
    pde = &pt->pgdir[PDE_INDEX(vaddr)];
    if (!pde->valid) {
        return PT_NOTPRESENT;
    }

    /* Get the page table */
    pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
    pte = &pte_page[PTE_INDEX(vaddr)];

    /* Check if page is present */
    if (!pte->valid) {
        spinlock_release(&pt->pt_lock);
        panic("pte_unmap: page not present\n");
        return PT_NOTPRESENT;
    }

    /* Remove mapping */
    pte->valid = 0;
    spinlock_release(&pt->pt_lock);
    
    /* Flush TLB */
    tlb_invalidate_entry(vaddr);
    
    return PT_OK;
}

/* Translate virtual to physical address */
paddr_t pagetable_translate(struct page_table *pt, vaddr_t vaddr, uint32_t *flags) {
    struct pte *pte;
    struct pde *pde;
    paddr_t ret;
    
    spinlock_acquire(&pt->pt_lock);
    
    /* Get page directory entry */
    pde = &pt->pgdir[PDE_INDEX(vaddr)];
    if (!pde->valid) {
        spinlock_release(&pt->pt_lock);
        return PT_NOTPRESENT;
    }

    /* Get page table */
    pte = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
    if (pte == NULL) {
        spinlock_release(&pt->pt_lock);
        panic("pte is NULL\n");
        return PT_NOTPRESENT;
    }
    
    pte = &pte[PTE_INDEX(vaddr)];
    if (!pte->valid) {
        panic("pte is not valid\n");
        if (flags) *flags = 0;
        spinlock_release(&pt->pt_lock);
        return 0;
    }

    if (flags) {
        *flags = 0;
        if (pte->write) *flags |= PTE_WRITE;
        if (pte->user) *flags |= PTE_USER;
        if (pte->nocache) *flags |= PTE_NOCACHE;
    }

    ret = (pte->pfn << PAGE_SHIFT) | (vaddr & PAGE_MASK);
    spinlock_release(&pt->pt_lock);
    return ret;
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

struct pte *pte_get(struct page_table *pt, vaddr_t vaddr) {
    struct pde *pde;
    struct pte *pte_page;

    KASSERT(pt != NULL);
    
    spinlock_acquire(&pt->pt_lock);
    
    /* Get PDE */
    pde = &pt->pgdir[PDE_INDEX(vaddr)];
    if (!pde->valid) {
        spinlock_release(&pt->pt_lock);
        return NULL;
    }
    
    /* Get the page table */
    pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
    
    /* Return PTE */
    return &pt_page[PTE_INDEX(vaddr)];
}
