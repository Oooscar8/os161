#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <vm.h>
#include <vfs.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <spinlock.h>
#include <swap.h>
#include <pagetable.h>
#include <pmm.h>
#include <kern/fcntl.h>

/* Global instance */
struct swap_manager swap_manager;

int swap_init(void)
{
    int result;

    /* Initialize swap lock */
    spinlock_init(&swap_manager.swap_lock);

    /* Initialize bitmap to 0 (all slots free) */
    memset(swap_manager.bitmap, 0, sizeof(unsigned long) * SWAP_BITMAP_WORDS);

    swap_manager.count = 0;

    swap_manager.swap_in_progress = false;

    swap_manager.swap_sem = sem_create("swap_sem", 0);

    /* Open swap device */
    result = vfs_open((char *)SWAP_DEVICE, O_RDWR, 0, &swap_manager.swap_dev);
    if (result)
    {
        return SWAP_IO_ERROR;
    }

    return SWAP_SUCCESS;
}

void swap_shutdown(void)
{
    if (swap_manager.swap_dev != NULL)
    {
        vfs_close(swap_manager.swap_dev);
    }

    sem_destroy(swap_manager.swap_sem);
    spinlock_cleanup(&swap_manager.swap_lock);
}

static int find_victim_page(struct page_table **pt_out, vaddr_t *vaddr_out)
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

    /* First pass: look for a page with accessed = 0 */
    for (i = 2; pt_list[i] != NULL; i++)
    {
        pt = pt_list[i];

        /* First check page table entries without lock */
        for (j = 0; j < PD_ENTRIES; j++)
        {
            pde = &pt->pgdir[j];

            if (!pde->valid)
            {
                continue;
            }

            pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);

            for (unsigned int k = 0; k < PT_ENTRIES_PER_PAGE; k++)
            {
                pte = &pte_page[k];
                vaddr = (j << PDE_SHIFT) | (k << PTE_SHIFT);

                /* Check if this is a potential victim page */
                if (pte->valid && !pte->swap && pte->pfn_or_swap_slot != 0 && !pte->accessed)
                {
                    /* Found a candidate, acquire lock to verify */
                    spinlock_acquire(&pt->pt_lock);

                    /* Check if state is still valid after acquiring lock */
                    if (pte->valid && !pte->swap && pte->pfn_or_swap_slot != 0 && !pte->accessed)
                    {
                        *pt_out = pt;
                        *vaddr_out = vaddr;
                        spinlock_release(&pt->pt_lock);
                        return 0;
                    }

                    spinlock_release(&pt->pt_lock);
                }
            }
        }
    }

    /* Second pass: accept any valid page */
    for (i = 0; pt_list[i] != NULL; i++)
    {
        pt = pt_list[i];

        for (j = 0; j < PD_ENTRIES; j++)
        {
            pde = &pt->pgdir[j];

            if (!pde->valid)
            {
                continue;
            }

            pte_page = (struct pte *)(pde->pt_pfn << PAGE_SHIFT);

            for (unsigned int k = 0; k < PT_ENTRIES_PER_PAGE; k++)
            {
                pte = &pte_page[k];
                vaddr = (j << PDE_SHIFT) | (k << PTE_SHIFT);

                /* Check if page is valid and has physical frame */
                if (pte->valid && !pte->swap && pte->pfn_or_swap_slot != 0)
                {
                    spinlock_acquire(&pt->pt_lock);

                    /* Verify state under lock */
                    if (pte->valid && !pte->swap && pte->pfn_or_swap_slot != 0)
                    {
                        *pt_out = pt;
                        *vaddr_out = vaddr;
                        spinlock_release(&pt->pt_lock);
                        return 0;
                    }

                    spinlock_release(&pt->pt_lock);
                }
            }
        }
    }

    return -1;
}

/*
 * Try to obtain swap permission
 * Returns true if permission granted, false if needs to wait
 */
bool need_swap(void)
{
    spinlock_acquire(&swap_manager.swap_lock);

    // No process is swapping, obtain permission
    if (!swap_manager.swap_in_progress)
    {
        swap_manager.swap_in_progress = true;
        spinlock_release(&swap_manager.swap_lock);
        return true;
    }

    // Another process is swapping, wait for notification
    spinlock_release(&swap_manager.swap_lock);
    P(swap_manager.swap_sem); // Wait
    return false;             // Woken up - retry memory allocation
}

/*
 * Execute page swapping
 * Returns result code from swap operation
 */
void do_swap(paddr_t *victim_pa, bool emergency)
{
    struct page_table *victim_pt;
    vaddr_t victim_vaddr;
    int result;

    /* Find a victim page */
    result = find_victim_page(&victim_pt, &victim_vaddr);
    if (result != 0)
    {
        panic("evict_page: no victim found\n");
    }

    *victim_pa = pagetable_translate(victim_pt, victim_vaddr, NULL);
    if (*victim_pa == 0)
    {
        panic("evict_page: failed to translate victim page\n");
    }

    /* Swap out the victim page */
    result = swap_out_page(victim_pt, victim_vaddr, emergency);
    if (result != SWAP_SUCCESS)
    {
        panic("swap_out_page: swap out failed\n");
    }

    // Clear swap state and notify waiting processes
    spinlock_acquire(&swap_manager.swap_lock);
    swap_manager.swap_in_progress = false;
    V(swap_manager.swap_sem); // Wake up one waiting process
    spinlock_release(&swap_manager.swap_lock);
}

static int find_free_slot(void)
{
    unsigned int slot;

    for (slot = 0; slot < SWAP_MAX_PAGES; slot++)
    {
        if (!SWAP_BITMAP_TEST(swap_manager.bitmap, slot))
        {
            return slot;
        }
    }

    return -1;
}

int swap_out_page(struct page_table *pt, vaddr_t vaddr, bool emergency)
{
    struct iovec iov;
    struct uio ku;
    int result;
    int slot;
    struct pte *pte;

    KASSERT(pt != NULL);
    /* Verify alignment */
    if (vaddr & PAGE_MASK)
    {
        return SWAP_ALIGN;
    }

    spinlock_acquire(&swap_manager.swap_lock);

    /* Find a free swap slot */
    if (swap_manager.count >= SWAP_MAX_PAGES - 1 && !emergency)
    {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_FULL;
    }

    slot = find_free_slot();
    if (slot < 0)
    {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_FULL;
    }

    /* Mark slot as used in bitmap */
    SWAP_BITMAP_SET(swap_manager.bitmap, slot);
    swap_manager.count++;

    spinlock_release(&swap_manager.swap_lock);

    /* Write page to swap space */
    uio_kinit(&iov, &ku, (void *)vaddr, PAGE_SIZE,
              SWAP_PAGE_TO_OFFSET(slot), UIO_WRITE);
    // if (vaddr < MIPS_KSEG0) {
    //     // User space address (kuseg)
    //     ku.uio_segflg = UIO_USERSPACE;
    //     ku.uio_space = curproc->p_addrspace;
    // }

    result = VOP_WRITE(swap_manager.swap_dev, &ku);
    if (result)
    {
        kprintf("swap_out: VOP_WRITE failed with error %d\n", result);
        /* Clear swap entry if I/O error occurred */
        spinlock_acquire(&swap_manager.swap_lock);
        SWAP_BITMAP_CLEAR(swap_manager.bitmap, slot);
        swap_manager.count--;
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_IO_ERROR;
    }

    /* Get the PTE for this virtual address */
    pte = pte_get(pt, vaddr);
    if (pte == NULL || !pte->valid)
    {
        if (pte != NULL)
        {
            spinlock_release(&pt->pt_lock);
        }
        return SWAP_INVALID;
    }

    /* Update PTE */
    PTE_SET_SWAP_SLOT(pte, slot);

    spinlock_release(&pt->pt_lock);

    /* Invalidate TLB entry on current CPU */
    tlb_invalidate_entry(vaddr);

    /* Broadcast TLB shootdown to other CPUs */
    tlbshootdown_broadcast(vaddr, pt->pid);

    return SWAP_SUCCESS;
}

int swap_in_page(struct page_table *pt, vaddr_t vaddr)
{
    struct iovec iov;
    struct uio ku;
    int result;
    struct pte *pte;
    unsigned int slot;
    paddr_t pa;
    bool was_waiting = false;

    KASSERT(pt != NULL);
    /* Verify alignment */
    if (vaddr & PAGE_MASK)
    {
        return SWAP_ALIGN;
    }

    // /* Get a physical page */
    // pa = getppages(1);
    // /* If physical memory is full, evict a page before swapping in */
    // if (pa == 0) {
    //     result = evict_page(&pa, true);
    //     if (result != PR_SUCCESS) {
    //         return SWAP_NOMEM;
    //     }
    // }

    while (1)
    {
        /* Try to get a physical page */
        pa = getppages(1);
        if (pa != 0)
        {
            if (was_waiting)
            {
                V(swap_manager.swap_sem);
            }
            break; // Success
        }

        /* Need to swap out to get free page */
        if (need_swap())
        {
            do_swap(&pa, true);
        }
        else
        {
            /* Another process is swapping, wait and retry */
            was_waiting = true;
            continue;
        }
    }

    /* Get PTE and verify page is on swap */
    pte = pte_get(pt, vaddr);
    if (pte == NULL || !PTE_ONSWAP(pte))
    {
        if (pte != NULL)
        {
            spinlock_release(&pt->pt_lock);
        }
        pmm_free_page(pa);
        return SWAP_INVALID;
    }

    /* Get swap slot number */
    slot = PTE_GET_SWAP_SLOT(pte);

    /* Set pfn, clear swap bit, keep other flags unchanged */
    pte->pfn_or_swap_slot = pa >> PAGE_SHIFT;
    pte->swap = 0;

    /* Release page table lock before I/O */
    spinlock_release(&pt->pt_lock);

    /* Read page from swap directly to mapped page */
    uio_kinit(&iov, &ku, (void *)vaddr, PAGE_SIZE,
              SWAP_PAGE_TO_OFFSET(slot), UIO_READ);
    if (vaddr < MIPS_KSEG0)
    {
        // User space address (kuseg)
        ku.uio_segflg = UIO_USERSPACE;
        ku.uio_space = curproc->p_addrspace;
    }

    result = VOP_READ(swap_manager.swap_dev, &ku);
    if (result)
    {
        spinlock_acquire(&pt->pt_lock);
        pte->pfn_or_swap_slot = slot;
        pte->swap = 1;
        spinlock_release(&pt->pt_lock);
        pmm_free_page(pa);
        return SWAP_IO_ERROR;
    }

    spinlock_acquire(&swap_manager.swap_lock);

    /* Mark swap slot as free */
    SWAP_BITMAP_CLEAR(swap_manager.bitmap, slot);
    swap_manager.count--;

    spinlock_release(&swap_manager.swap_lock);

    return SWAP_SUCCESS;
}