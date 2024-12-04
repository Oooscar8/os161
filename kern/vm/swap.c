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
#include <pr.h>

/* Global instance */
struct swap_manager swap_manager;

int swap_init(void) {
    int result;

    /* Initialize swap lock */
    spinlock_init(&swap_manager.swap_lock);

    /* Initialize bitmap to 0 (all slots free) */
    memset(swap_manager.bitmap, 0, sizeof(unsigned long) * SWAP_BITMAP_WORDS);
    
    swap_manager.count = 0;

    /* Open swap device */
    result = vfs_open((char *)SWAP_DEVICE, O_RDWR, 0, &swap_manager.swap_dev);
    if (result) {
        return SWAP_IO_ERROR;
    }

    return SWAP_SUCCESS;
}

void swap_shutdown(void) {
    if (swap_manager.swap_dev != NULL) {
        vfs_close(swap_manager.swap_dev);
    }

    spinlock_cleanup(&swap_manager.swap_lock);
}

static int find_free_slot(void) {
    unsigned int slot;
    
    for (slot = 0; slot < SWAP_MAX_PAGES; slot++) {
        if (!SWAP_BITMAP_TEST(swap_manager.bitmap, slot)) {
            return slot;
        }
    }
    
    return -1;
}

int swap_out_page(struct page_table *pt, vaddr_t vaddr, bool emergency) {
    struct iovec iov;
    struct uio ku;
    int result;
    int slot;
    struct pte *pte;

    KASSERT(pt != NULL);
    /* Verify alignment */
    if (vaddr & PAGE_MASK) {
        return SWAP_ALIGN;
    }

    spinlock_acquire(&swap_manager.swap_lock);

    /* Find a free swap slot */
    if (swap_manager.count >= SWAP_MAX_PAGES - 1 && !emergency) {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_FULL;
    }

    slot = find_free_slot();
    if (slot < 0) {
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

    result = VOP_WRITE(swap_manager.swap_dev, &ku);
    if (result) {
        /* Clear swap entry if I/O error occurred */
        spinlock_acquire(&swap_manager.swap_lock);
        SWAP_BITMAP_CLEAR(swap_manager.bitmap, slot);
        swap_manager.count--;
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_IO_ERROR;
    }

    /* Get the PTE for this virtual address */
    pte = pte_get(pt, vaddr);
    if (pte == NULL || !pte->valid) {
        if (pte != NULL) {
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

int swap_in_page(struct page_table *pt, vaddr_t vaddr) {
    struct iovec iov;
    struct uio ku;
    int result;
    struct pte *pte;
    unsigned int slot;
    paddr_t pa;

    KASSERT(pt != NULL);
    /* Verify alignment */
    if (vaddr & PAGE_MASK) {
        return SWAP_ALIGN;
    }
    
    /* Get a physical page */
    pa = getppages(1);
    /* If physical memory is full, evict a page before swapping in */
    if (pa == 0) {
        result = evict_page(true);
        if (result != PR_SUCCESS) {
            return SWAP_NOMEM;
        }
        
        /* Retry getting a physical page */
        pa = getppages(1);
        if (pa == 0) {
            return SWAP_NOMEM;
        }
    }

    /* Get PTE and verify page is on swap */
    pte = pte_get(pt, vaddr);
    if (pte == NULL || !PTE_ONSWAP(pte)) {
        if (pte != NULL) {
            spinlock_release(&pt->pt_lock);
        }
        kfree_pages(pa);
        return SWAP_INVALID;
    }

    /* Get swap slot number */
    slot = PTE_GET_SWAP_SLOT(pte);

    /* Set pfn and valid bit, clear swap bit, keep other flags unchanged */
    pte->pfn_or_swap_slot = pa >> PAGE_SHIFT;
    pte->valid = 1;
    pte->swap = 0;

    /* Release page table lock before I/O */
    spinlock_release(&pt->pt_lock);

    /* Read page from swap directly to mapped page */
    uio_kinit(&iov, &ku, (void *)vaddr, PAGE_SIZE,
              SWAP_PAGE_TO_OFFSET(slot), UIO_READ);

    result = VOP_READ(swap_manager.swap_dev, &ku);
    if (result) {
        spinlock_acquire(&pt->pt_lock);
        pte->pfn_or_swap_slot = slot;
        pte->valid = 0;
        pte->swap = 1;
        spinlock_release(&pt->pt_lock);
        free_kpages(pa);
        return SWAP_IO_ERROR;
    }

    spinlock_acquire(&swap_manager.swap_lock);

    /* Mark swap slot as free */
    SWAP_BITMAP_CLEAR(swap_manager.bitmap, slot);
    swap_manager.count--;

    spinlock_release(&swap_manager.swap_lock);

    return SWAP_SUCCESS;
}