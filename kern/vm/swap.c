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

/* Global instance */
struct swap_manager swap_manager;

int swap_init(void) {
    int result;

    /* Initialize swap lock */
    spinlock_init(&swap_manager.swap_lock);

    /* Allocate swap entries array */
    swap_manager.entries = kmalloc(sizeof(struct swap_entry) * SWAP_MAX_PAGES);
    if (swap_manager.entries == NULL) {
        return SWAP_NOMEM;
    }

    /* Initialize all entries as unused */
    memset(swap_manager.entries, 0, sizeof(struct swap_entry) * SWAP_MAX_PAGES);
    
    swap_manager.count = 0;

    /* Open swap device */
    result = vfs_open((char *)SWAP_DEVICE, O_RDWR, 0, &swap_manager.swap_dev);
    if (result) {
        kfree(swap_manager.entries);
        return SWAP_IO_ERROR;
    }

    return SWAP_SUCCESS;
}

void swap_shutdown(void) {
    if (swap_manager.swap_dev != NULL) {
        vfs_close(swap_manager.swap_dev);
    }
    
    if (swap_manager.entries != NULL) {
        kfree(swap_manager.entries);
    }

    spinlock_cleanup(&swap_manager.swap_lock);
}

static int find_free_slot(void) {
    for (unsigned int i = 0; i < SWAP_MAX_PAGES; i++) {
        if (!swap_manager.entries[i].used) {
            return i;
        }
    }
    return SWAP_FULL;
}

int swap_out_page(struct page_table *pt, vaddr_t vaddr) {
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
    
    /* Get the PTE for this virtual address */
    pte = pte_get(pt, vaddr);
    if (pte == NULL || !PTE_ONSWAP(pte)) {
        return SWAP_INVALID;
    }

    spinlock_acquire(&swap_manager.swap_lock);

    /* Find a free swap slot */
    if (swap_manager.count >= SWAP_MAX_PAGES) {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_FULL;
    }

    slot = find_free_slot();
    if (slot < 0) {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_FULL;
    }

    /* Write page to swap space */
    uio_kinit(&iov, &ku, (void *)vaddr, PAGE_SIZE,
              SWAP_PAGE_TO_OFFSET(slot), UIO_WRITE);

    result = VOP_WRITE(swap_manager.swap_dev, &ku);
    if (result) {
        spinlock_release(&swap_manager.swap_lock);
        return SWAP_IO_ERROR;
    }

    /* Update swap entry */
    swap_manager.entries[slot].pid = curproc->p_pid;
    swap_manager.entries[slot].used = true;
    swap_manager.count++;

    /* Update PTE */
    PTE_SET_SWAP_SLOT(pte, slot);

    spinlock_release(&swap_manager.swap_lock);
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

    /* Get PTE and verify page is on swap */
    pte = pte_get(pt, vaddr);
    if (pte == NULL || !PTE_ONSWAP(pte)) {
        return SWAP_INVALID;
    }

    /* Get swap slot number */
    slot = PTE_GET_SWAP_SLOT(pte);
    
    /* Allocate physical page */
    pa = alloc_kpages(1);
    if (pa == 0) {
        return SWAP_NOMEM;
    }

    /* Map the physical page */
    result = pte_map(pt, vaddr, pa, 0);
    if (result) {
        free_kpages(pa);
        return result;
    }

    /* Read page from swap directly to mapped page */
    uio_kinit(&iov, &ku, (void *)vaddr, PAGE_SIZE,
              SWAP_PAGE_TO_OFFSET(slot), UIO_READ);

    result = VOP_READ(swap_manager.swap_dev, &ku);
    if (result) {
        pte_unmap(pt, vaddr);
        free_kpages(pa);
        return SWAP_IO_ERROR;
    }

    spinlock_acquire(&swap_manager.swap_lock);

    /* Mark swap entry as free */
    swap_manager.entries[slot].used = false;
    swap_manager.count--;

    spinlock_release(&swap_manager.swap_lock);
    return SWAP_SUCCESS;
}