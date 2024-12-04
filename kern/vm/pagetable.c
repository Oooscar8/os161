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

static void pte_free(struct pte *pte)
{
    kfree(pte);
}

/* Initialize page table system */
void pagetable_bootstrap(void)
{
    /* Initialize VAA first */
    vaa_init();
    // max 10 processes
    pt_list = kmalloc(sizeof(struct pde *) * 10);
}

// create page table for the kernel
void pagetable_init(void)
{
    struct page_table *pt = pagetable_create();
    if (pt == NULL)
    {
        panic("Failed to create kernel page table\n");
    }
    pt->pid = 1;
    kernel_pt = pt;
}

/* Create a new page table */
struct page_table *pagetable_create(void)
{
    struct page_table *pt;

    pt = kmalloc(sizeof(struct page_table));
    if (pt == NULL)
    {
        return NULL;
    }

    /* Allocate page directory */
    pt->pgdir = (struct pde *)kmalloc(PAGE_SIZE);
    if (pt->pgdir == NULL)
    {
        kfree(pt);
        return NULL;
    }

    /* Initialize page directory */
    bzero(pt->pgdir, PAGE_SIZE);

    /* Initialize spinlock */
    spinlock_init(&pt->pt_lock);

    /* Initialize other fields */
    pt->pid = 1; /* Will be set by process creation code */
    pt->heap_start = 0;
    pt->heap_end = 0;

    for (int i = 0; i < 10; i++)
    {
        if (pt_list[i] == NULL)
        {
            pt_list[i] = pt->pgdir;
            break;
        }
    }

    return pt;
}

/* Destroy a page table */
void pagetable_destroy(struct page_table *pt)
{
    int i, j;
    struct pde *pde;
    struct pte *pte;

    KASSERT(pt != NULL);

    spinlock_acquire(&pt->pt_lock);

    /* Free all page tables */
    for (i = 0; i < PD_ENTRIES; i++)
    {
        pde = &pt->pgdir[i];
        if (pde->valid)
        {
            pte = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
            /* Free all pages in this table */
            for (j = 0; j < PT_ENTRIES_PER_PAGE; j++)
            {
                if (pte[j].valid)
                {
                    /* Free physical page if it exists */
                    spinlock_release(&pt->pt_lock);
                    pte_unmap(pt, (i << PDE_SHIFT) | (j << PTE_SHIFT));
                    spinlock_acquire(&pt->pt_lock);
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
int pte_map(struct page_table *pt, vaddr_t vaddr, paddr_t paddr, uint32_t flags)
{
    struct pde *pde;
    struct pte *pte_page;
    struct pte *pte;
    unsigned pde_index = PDE_INDEX(vaddr);
    unsigned pte_index = PTE_INDEX(vaddr);

    /* Verify alignment */
    if ((vaddr & PAGE_MASK) || (paddr & PAGE_MASK))
    {
        return PT_ALIGN;
    }

    spinlock_acquire(&pt->pt_lock);

    /* Get the page directory entry */
    pde = &pt->pgdir[pde_index];

    /* If page table doesn't exist, create it */
    if (!pde->valid)
    {
        paddr_t pde_paddr = pmm_alloc_page();
        if (pde_paddr == 0)
        {
            return PT_NOMEM;
        }

        vaddr_t pde_vaddr = PADDR_TO_KVADDR(pde_paddr);
        pde->pt_pfn = pde_vaddr >> PAGE_SHIFT;
        pde->valid = 1;
        pde->write = 1;
        if (vaddr >= MIPS_KSEG2)
        {

            pde->user = 0;
        }
        else
        {
            pde->user = 1;
        }
    }

    /* Get the page table */
    pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
    pte = &pte_page[pte_index];

    /* Check if already mapped */
    if (pte->valid)
    {
        spinlock_release(&pt->pt_lock);
        return PT_PRESENT;
    }

    /* Create the mapping */
    pte->pfn_or_swap_slot = paddr >> PAGE_SHIFT;
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
int pte_unmap(struct page_table *pt, vaddr_t vaddr)
{
    struct pde *pde;
    struct pte *pte_page;
    struct pte *pte;

    /* Get the page directory entry */
    spinlock_acquire(&pt->pt_lock);
    //tlbshootdown_broadcast(vaddr, curproc->p_pid);
    pde = &pt->pgdir[PDE_INDEX(vaddr)];
    if (!pde->valid)
    {
        return PT_NOTPRESENT;
    }

    /* Get the page table */
    pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
    pte = &pte_page[PTE_INDEX(vaddr)];

    /* Check if page is present */
    if (!pte->valid)
    {
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
paddr_t pagetable_translate(struct page_table *pt, vaddr_t vaddr, uint32_t *flags)
{
    struct pte *pte;
    struct pde *pde;
    paddr_t ret;

    spinlock_acquire(&pt->pt_lock);

    /* Get page directory entry */
    pde = &pt->pgdir[PDE_INDEX(vaddr)];
    if (!pde->valid)
    {
        spinlock_release(&pt->pt_lock);
        return 0;
        // return PT_NOTPRESENT;
    }

    /* Get page table */
    pte = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);
    if (pte == NULL)
    {
        spinlock_release(&pt->pt_lock);
        if (vaddr >= MIPS_KSEG2)
        {
            panic("pte is NULL\n");
        }
        return 0;
        // return PT_NOTPRESENT;
    }

    pte = &pte[PTE_INDEX(vaddr)];
    pte->accessed = 1;
    if (!pte->valid)
    {   
        if (vaddr >= MIPS_KSEG2)
        {   
            spinlock_release(&pt->pt_lock);
            panic("pte is not valid\n");
            return 0;
        }
        
        if (flags)
            *flags = 0;
        spinlock_release(&pt->pt_lock);
        return 0;
    }

    if (flags)
    {
        *flags = 0;
        if (pte->write)
            *flags |= PTE_WRITE;
        if (pte->user)
            *flags |= PTE_USER;
        if (pte->nocache)
            *flags |= PTE_NOCACHE;
    }

    ret = (pte->pfn_or_swap_slot << PAGE_SHIFT) | (vaddr & PAGE_MASK);
    spinlock_release(&pt->pt_lock);
    return ret;
}

/* Map a contiguous region */
int pagetable_map_region(struct page_table *pt, vaddr_t vaddr,
                         paddr_t paddr, size_t npages, uint32_t flags)
{
    size_t i;
    int result;

    for (i = 0; i < npages; i++)
    {
        result = pte_map(pt, vaddr + i * PAGE_SIZE,
                         paddr + i * PAGE_SIZE, flags);
        if (result != PT_OK)
        {
            /* Unmap previously mapped pages on failure */
            while (i > 0)
            {
                i--;
                pte_unmap(pt, vaddr + i * PAGE_SIZE);
            }
            return result;
        }
    }
    return PT_OK;
}

struct pte *pte_get(struct page_table *pt, vaddr_t vaddr)
{
    struct pde *pde;
    struct pte *pte_page;

    KASSERT(pt != NULL);

    spinlock_acquire(&pt->pt_lock);

    /* Get PDE */
    pde = &pt->pgdir[PDE_INDEX(vaddr)];
    if (!pde->valid)
    {
        spinlock_release(&pt->pt_lock);
        return NULL;
    }

    /* Get the page table */
    pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);

    /* Return PTE */
    return &pte_page[PTE_INDEX(vaddr)];
}

int pagetable_copy(struct page_table *src_pt, struct page_table *dst_pt)
{
    int i, j;
    struct pde *src_pde, *dst_pde;
    struct pte *src_pte, *dst_pte;
    paddr_t new_pt_paddr;

    /* Verify parameters */
    if (src_pt == NULL || dst_pt == NULL)
    {
        return PT_NOTPRESENT;
    }

    /* Acquire locks for both page tables */
    spinlock_acquire(&src_pt->pt_lock);
    spinlock_acquire(&dst_pt->pt_lock);

    /* Copy page directory entries */
    for (i = 0; i < PD_ENTRIES; i++)
    {
        src_pde = &src_pt->pgdir[i];
        dst_pde = &dst_pt->pgdir[i];

        if (src_pde->valid)
        {
            /* Allocate new page table if entry is valid */
            new_pt_paddr = pmm_alloc_page();
            if (new_pt_paddr == 0)
            {
                /* Failed to allocate - cleanup and return */
                spinlock_release(&dst_pt->pt_lock);
                spinlock_release(&src_pt->pt_lock);
                pagetable_destroy(dst_pt);
                return PT_NOMEM;
            }

            // directly map the kernel page table, to avoid the deadlock (call pte_map)
            vaddr_t new_pt_vaddr = PADDR_TO_KVADDR(new_pt_paddr);

            /* Setup page directory entry */
            dst_pde->pt_pfn = new_pt_vaddr >> PAGE_SHIFT;
            dst_pde->valid = 1;
            dst_pde->write = src_pde->write;
            dst_pde->user = src_pde->user;

            /* Get source and destination page tables */
            src_pte = (struct pte *)(src_pde->pt_pfn << PAGE_SHIFT);
            dst_pte = (struct pte *)(dst_pde->pt_pfn << PAGE_SHIFT);

            /* Copy all page table entries */
            for (j = 0; j < PT_ENTRIES_PER_PAGE; j++)
            {
                if (src_pte[j].valid)
                {   
                    paddr_t new_paddr = pmm_alloc_page();
                    if (new_paddr == 0) {
                    return PT_NOMEM;
                    }
                
                    memcpy((void *)PADDR_TO_KVADDR(new_paddr),
                       (void *)PADDR_TO_KVADDR(src_pte[j].pfn_or_swap_slot << PAGE_SHIFT),
                       PAGE_SIZE);
                
                    dst_pte[j].pfn_or_swap_slot = new_paddr >> PAGE_SHIFT;
                    dst_pte[j].valid = 1;
                    dst_pte[j].write = src_pte[j].write;
                    dst_pte[j].user = src_pte[j].user;
                    dst_pte[j].nocache = src_pte[j].nocache;
                    dst_pte[j].dirty = src_pte[j].dirty;
                    dst_pte[j].accessed = src_pte[j].accessed;
                }
            }
        }
    }

    /* Copy other page table attributes */
    dst_pt->heap_start = src_pt->heap_start;
    dst_pt->heap_end = src_pt->heap_end;
    /* Don't copy ASID - it should be assigned by the VM system */

    /* Release locks */
    spinlock_release(&dst_pt->pt_lock);
    spinlock_release(&src_pt->pt_lock);

    return PT_OK;
}
